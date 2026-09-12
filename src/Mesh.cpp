#include "Mesh.hpp"
#include "AppPaths.hpp"
#include "Restart.hpp"   // narrowToPath / pathToConsole, the Windows path encoding fix
#include <stl_reader/stl_reader.h>
#include <tiny_obj_loader.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <utility>

#ifndef CFD_MODELS_DIR
#define CFD_MODELS_DIR "models"
#endif

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double OBSTACLE_DOMAIN_FRACTION = 0.2;

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool pathExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

std::filesystem::path resolveGeometryPath(const std::string& filename) {
    // Straight from the console or from argv, so it is not necessarily in the
    // code page a path is built from. Same trap as restartFile.
    const std::filesystem::path requested = narrowToPath(filename);
    if (pathExists(requested)) {
        return requested;
    }

    // Beside the executable first: that is where an installed copy keeps its
    // models, and CFD_MODELS_DIR is baked in at compile time and points at the
    // machine the binary was built on, which is not the machine running it.
    const std::filesystem::path installed =
        executableDir() / "models" / requested;
    if (pathExists(installed)) {
        return installed;
    }

    const std::filesystem::path modelPath =
        std::filesystem::path(CFD_MODELS_DIR) / requested;
    if (pathExists(modelPath)) {
        return modelPath;
    }

    return {};
}

double squaredDistance(const Mesh::Vertex& first, const Mesh::Vertex& second) {
    const double deltaX = first.x - second.x;
    const double deltaY = first.y - second.y;
    const double deltaZ = first.z - second.z;
    return deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
}

void poseRotation(const Mesh::BodyPose& pose, double rotation[9]) {
    const double xx = pose.qx * pose.qx;
    const double yy = pose.qy * pose.qy;
    const double zz = pose.qz * pose.qz;
    const double xy = pose.qx * pose.qy;
    const double xz = pose.qx * pose.qz;
    const double yz = pose.qy * pose.qz;
    const double wx = pose.qw * pose.qx;
    const double wy = pose.qw * pose.qy;
    const double wz = pose.qw * pose.qz;

    rotation[0] = 1.0 - 2.0 * (yy + zz);
    rotation[1] = 2.0 * (xy - wz);
    rotation[2] = 2.0 * (xz + wy);
    rotation[3] = 2.0 * (xy + wz);
    rotation[4] = 1.0 - 2.0 * (xx + zz);
    rotation[5] = 2.0 * (yz - wx);
    rotation[6] = 2.0 * (xz - wy);
    rotation[7] = 2.0 * (yz + wx);
    rotation[8] = 1.0 - 2.0 * (xx + yy);
}

int openEdgeCount(const std::vector<Mesh::Triangle>& triangles, double snap) {
    std::vector<std::array<long long, 3>> corners;
    corners.reserve(triangles.size() * 3);
    for (const Mesh::Triangle& triangle : triangles) {
        for (const Mesh::Vertex& vertex :
             {triangle.v0, triangle.v1, triangle.v2}) {
            corners.push_back({std::llround(vertex.x / snap),
                               std::llround(vertex.y / snap),
                               std::llround(vertex.z / snap)});
        }
    }

    std::vector<std::array<long long, 3>> distinct(corners);
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()),
                   distinct.end());

    std::vector<std::pair<int, int>> edges;
    edges.reserve(corners.size());
    for (std::size_t triangle = 0; triangle < triangles.size(); ++triangle) {
        std::array<int, 3> corner{};
        for (std::size_t which = 0; which < corner.size(); ++which) {
            corner[which] = static_cast<int>(
                std::lower_bound(distinct.begin(),
                                 distinct.end(),
                                 corners[triangle * 3 + which]) -
                distinct.begin());
        }
        for (std::size_t which = 0; which < corner.size(); ++which) {
            int first = corner[which];
            int second = corner[(which + 1) % corner.size()];
            if (first == second) {
                continue;
            }
            if (first > second) {
                std::swap(first, second);
            }
            edges.emplace_back(first, second);
        }
    }

    std::sort(edges.begin(), edges.end());
    int open = 0;
    for (std::size_t index = 0; index < edges.size();) {
        std::size_t run = index;
        while (run < edges.size() && edges[run] == edges[index]) {
            ++run;
        }
        if (run - index != 2) {
            ++open;
        }
        index = run;
    }
    return open;
}
}
Mesh::Mesh(const Config& cfg, const std::vector<uint8_t>* presetSolid)
    : nx(cfg.nx), ny(cfg.ny), nz(cfg.nz), cfg(cfg)
{
    solid.resize(nx * ny * nz, 0);
    createGrid();

    if (presetSolid) {
        // Restart: the mask came out of the frame, so no model is loaded, no
        // section is cut and no fallback circle is generated
        const int cells = nx * ny * nz;
        for (int id = 0; id < cells; ++id)
            solid[id] = (*presetSolid)[id] ? 1 : 0;
        labelObjects();
        return;
    }

    const std::vector<Profile> profiles = cfg.resolvedProfiles();
    bool geometryLoaded = false;
    std::vector<std::vector<SectionPoint>> placed;
    std::vector<Triangle> placedTriangles;

    for (const Profile& profile : profiles) {
        if (!loadGeometry(profile.file)) {
            std::cerr << "Warning: geometry '" << profile.file
                      << "' could not be read.\n";
            continue;
        }
        geometryLoaded = true;
        if (nz > 1) {
            buildVolume(profile);
            if (volumeTriangles.empty()) {
                if (placementError.empty())
                    std::cerr << "Warning: geometry '" << profile.file
                              << "' produced no volume.\n";
                continue;
            }
            for (Triangle& triangle : volumeTriangles)
                placedTriangles.push_back(triangle);
            continue;
        }
        buildSection(profile);
        if (sectionContours.empty()) {
            std::cerr << "Warning: geometry '" << profile.file
                      << "' produced no section.\n";
            continue;
        }
        for (std::vector<SectionPoint>& contour : sectionContours)
            placed.push_back(std::move(contour));
    }

    sectionContours = std::move(placed);
    volumeTriangles = std::move(placedTriangles);
    if (!placementError.empty())
        return;

    if (nz > 1) {
        voxelize();
    } else {
        rasterizeSection();
        buildSolid();
    }

    const bool built = nz > 1 ? !volumeTriangles.empty() : hasSection();
    if ((!geometryLoaded || !built) && !cfg.emptyDomain()) {
        if (!profiles.empty())
            std::cerr << "Warning: no model produced a section, falling back "
                         "to the verification circle.\n";
        const double cx = cfg.Lx / 2.0;
        const double cy = cfg.Ly / 2.0;
        const double cz = cfg.Lz / 2.0;
        const double radius =
            0.1 * (nz > 1 ? std::min({cfg.Lx, cfg.Ly, cfg.Lz})
                          : std::min(cfg.Lx, cfg.Ly));
        initCircle(cx, cy, cz, radius);
    }

    labelObjects();
}

void Mesh::createGrid() {
    dx = cfg.Lx / nx;
    dy = cfg.Ly / ny;
    dz = cfg.Lz / nz;

    const int nodes = (nx + 1) * (ny + 1) * (nz + 1);
    x.resize(nodes);
    y.resize(nodes);
    z.resize(nodes);
    for (int k = 0; k <= nz; ++k) {
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                const int node = (k * (ny + 1) + j) * (nx + 1) + i;
                x[node] = i * dx;
                y[node] = j * dy;
                z[node] = k * dz;
            }
        }
    }
}

void Mesh::clearSolid() {
    std::fill(solid.begin(), solid.end(), 0);
}

bool Mesh::loadGeometry(const std::string& filename) {
    triangles.clear();
    sectionContours.clear();
    volumeTriangles.clear();

    if (filename.empty() || lowercase(filename) == "none") {
        return false;
    }

    const std::filesystem::path path = resolveGeometryPath(filename);
    if (path.empty()) {
        std::cerr << "Geometry file not found: " << filename << "\n";
        return false;
    }

    const std::string extension = lowercase(path.extension().string());
    if (extension == ".stl") {
        geometryType = GeometryType::STL;
    } else if (extension == ".obj") {
        geometryType = GeometryType::OBJ;
    } else {
        std::cerr << "Unsupported geometry format: "
                  << pathToConsole(path) << "\n";
        return false;
    }

    // Both loaders take a narrow file name and open it themselves, so the
    // path has to be handed over in the encoding the C runtime reads back,
    // which is exactly what path::string() produces. It throws on a path the
    // runtime cannot express at all, hence the catch.
    try {
        switch (geometryType) {
            case GeometryType::STL:
                return loadSTL(path.string());
            case GeometryType::OBJ:
                return loadOBJ(path.string());
        }
    } catch (const std::exception& exception) {
        std::cerr << "Cannot hand this path to the model loader: "
                  << pathToConsole(path) << "\n"
                  << "  (" << exception.what()
                  << ") Put the model somewhere with a simpler path.\n";
        return false;
    }

    return false;
}

bool Mesh::loadOBJ(const std::string& filename) {
    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string error;

    const std::filesystem::path path(filename);
    std::string materialDirectory = path.parent_path().string();
    if (!materialDirectory.empty()) {
        materialDirectory += std::filesystem::path::preferred_separator;
    }

    const bool loaded = tinyobj::LoadObj(
        &attributes,
        &shapes,
        &materials,
        &warning,
        &error,
        filename.c_str(),
        materialDirectory.empty() ? nullptr : materialDirectory.c_str(),
        true);

    if (!warning.empty()) {
        std::cerr << "OBJ warning: " << warning << "\n";
    }
    if (!loaded) {
        std::cerr << "OBJ load failed: " << error << "\n";
        return false;
    }

    for (const tinyobj::shape_t& shape : shapes) {
        std::size_t indexOffset = 0;
        for (unsigned int faceVertexCount : shape.mesh.num_face_vertices) {
            std::vector<Vertex> faceVertices;
            faceVertices.reserve(faceVertexCount);

            for (unsigned int vertex = 0; vertex < faceVertexCount; ++vertex) {
                const tinyobj::index_t index = shape.mesh.indices[indexOffset + vertex];
                if (index.vertex_index < 0) {
                    continue;
                }

                const std::size_t coordinateIndex =
                    3 * static_cast<std::size_t>(index.vertex_index);
                if (coordinateIndex + 2 >= attributes.vertices.size()) {
                    continue;
                }

                faceVertices.push_back({
                    static_cast<double>(attributes.vertices[coordinateIndex]),
                    static_cast<double>(attributes.vertices[coordinateIndex + 1]),
                    static_cast<double>(attributes.vertices[coordinateIndex + 2])
                });
            }
            indexOffset += faceVertexCount;

            for (std::size_t vertex = 1; vertex + 1 < faceVertices.size(); ++vertex) {
                triangles.push_back({
                    faceVertices[0],
                    faceVertices[vertex],
                    faceVertices[vertex + 1]
                });
            }
        }
    }

    return !triangles.empty();
}

bool Mesh::loadSTL(const std::string& filename) {
    try {
        const stl_reader::StlMesh<float, unsigned int> mesh(filename);
        triangles.reserve(mesh.num_tris());

        for (std::size_t triangleIndex = 0;
             triangleIndex < mesh.num_tris();
             ++triangleIndex) {
            std::array<Vertex, 3> vertices{};
            for (std::size_t corner = 0; corner < vertices.size(); ++corner) {
                const float* coordinates = mesh.tri_corner_coords(triangleIndex, corner);
                vertices[corner] = {
                    static_cast<double>(coordinates[0]),
                    static_cast<double>(coordinates[1]),
                    static_cast<double>(coordinates[2])
                };
            }

            triangles.push_back({vertices[0], vertices[1], vertices[2]});
        }
    } catch (const std::exception& exception) {
        std::cerr << "STL load failed: " << exception.what() << "\n";
        return false;
    }

    return !triangles.empty();
}

void Mesh::buildSection(const Profile& profile) {
    sectionContours.clear();
    if (triangles.empty()) {
        return;
    }

    Vertex minimum{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()
    };
    Vertex maximum{
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()
    };

    for (const Triangle& triangle : triangles) {
        for (const Vertex& vertex : {triangle.v0, triangle.v1, triangle.v2}) {
            minimum.x = std::min(minimum.x, vertex.x);
            minimum.y = std::min(minimum.y, vertex.y);
            minimum.z = std::min(minimum.z, vertex.z);
            maximum.x = std::max(maximum.x, vertex.x);
            maximum.y = std::max(maximum.y, vertex.y);
            maximum.z = std::max(maximum.z, vertex.z);
        }
    }

    const Vertex centre{
        0.5 * (minimum.x + maximum.x),
        0.5 * (minimum.y + maximum.y),
        0.5 * (minimum.z + maximum.z)
    };
    const double characteristicLength = std::max({
        maximum.x - minimum.x,
        maximum.y - minimum.y,
        maximum.z - minimum.z
    });
    if (characteristicLength <= 0.0) {
        return;
    }

    const double angleX = profile.angleX * PI / 180.0;
    const double angleZ = profile.angleZ * PI / 180.0;
    const double cosineX = std::cos(angleX);
    const double sineX = std::sin(angleX);
    const double cosineZ = std::cos(angleZ);
    const double sineZ = std::sin(angleZ);

    // Rz(angleZ) * Rx(angleX) gives an orthonormal basis for the section plane.
    const Vertex sectionAxisX{cosineZ, sineZ, 0.0};
    const Vertex sectionAxisY{
        -sineZ * cosineX,
        cosineZ * cosineX,
        sineX
    };
    const Vertex sectionNormal{
        sineZ * sineX,
        -cosineZ * sineX,
        cosineX
    };

    const double tolerance = std::max(1e-12, characteristicLength * 1e-9);
    const double toleranceSquared = tolerance * tolerance;
    using Segment = std::pair<SectionPoint, SectionPoint>;
    std::vector<Segment> segments;

    const auto relativeToCentre = [&centre](const Vertex& vertex) {
        return Vertex{
            vertex.x - centre.x,
            vertex.y - centre.y,
            vertex.z - centre.z
        };
    };
    const auto dot = [](const Vertex& first, const Vertex& second) {
        return first.x * second.x + first.y * second.y + first.z * second.z;
    };
    const auto project = [&](const Vertex& vertex) {
        const Vertex relative = relativeToCentre(vertex);
        return SectionPoint{
            dot(relative, sectionAxisX),
            dot(relative, sectionAxisY)
        };
    };

    // Each non-coplanar triangle contributes at most one plane-intersection segment.
    for (const Triangle& triangle : triangles) {
        const std::array<Vertex, 3> vertices{
            triangle.v0,
            triangle.v1,
            triangle.v2
        };
        std::array<double, 3> distances{};
        for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
            distances[vertex] = dot(relativeToCentre(vertices[vertex]), sectionNormal);
        }

        std::vector<Vertex> intersections;
        const auto addUniqueIntersection = [&](const Vertex& intersection) {
            const bool duplicate = std::any_of(
                intersections.begin(),
                intersections.end(),
                [&](const Vertex& existing) {
                    return squaredDistance(existing, intersection) <= toleranceSquared;
                });
            if (!duplicate) {
                intersections.push_back(intersection);
            }
        };

        for (std::size_t edge = 0; edge < vertices.size(); ++edge) {
            const std::size_t next = (edge + 1) % vertices.size();
            const Vertex& first = vertices[edge];
            const Vertex& second = vertices[next];
            const double firstDistance = distances[edge];
            const double secondDistance = distances[next];
            const bool firstOnPlane = std::abs(firstDistance) <= tolerance;
            const bool secondOnPlane = std::abs(secondDistance) <= tolerance;

            if (firstOnPlane) {
                addUniqueIntersection(first);
            }
            if (secondOnPlane) {
                addUniqueIntersection(second);
            }
            if (!firstOnPlane &&
                !secondOnPlane &&
                ((firstDistance < 0.0) != (secondDistance < 0.0))) {
                const double interpolation =
                    firstDistance / (firstDistance - secondDistance);
                addUniqueIntersection({
                    first.x + interpolation * (second.x - first.x),
                    first.y + interpolation * (second.y - first.y),
                    first.z + interpolation * (second.z - first.z)
                });
            }
        }

        if (intersections.size() == 2) {
            const SectionPoint first = project(intersections[0]);
            const SectionPoint second = project(intersections[1]);
            const double deltaX = first.x - second.x;
            const double deltaY = first.y - second.y;
            if (deltaX * deltaX + deltaY * deltaY > toleranceSquared) {
                segments.emplace_back(first, second);
            }
        }
    }

    if (segments.empty()) {
        return;
    }

    // Merge coincident segment endpoints into a graph of section edges.
    std::vector<SectionPoint> nodes;
    const auto findOrAddNode = [&](const SectionPoint& point) {
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            const double deltaX = nodes[index].x - point.x;
            const double deltaY = nodes[index].y - point.y;
            if (deltaX * deltaX + deltaY * deltaY <= toleranceSquared) {
                return static_cast<int>(index);
            }
        }
        nodes.push_back(point);
        return static_cast<int>(nodes.size() - 1);
    };

    std::set<std::pair<int, int>> edges;
    for (const Segment& segment : segments) {
        int first = findOrAddNode(segment.first);
        int second = findOrAddNode(segment.second);
        if (first == second) {
            continue;
        }
        if (first > second) {
            std::swap(first, second);
        }
        edges.emplace(first, second);
    }

    std::vector<std::vector<int>> adjacency(nodes.size());
    for (const auto& edge : edges) {
        adjacency[edge.first].push_back(edge.second);
        adjacency[edge.second].push_back(edge.first);
    }

    std::set<std::pair<int, int>> visitedEdges;
    // Every closed loop, with the area it encloses. The largest used to be the
    // only one kept; the rest are what a pair of aerofoils, a ring or a body
    // with a hole in it are made of.
    struct Loop {
        std::vector<int> nodes;
        double area = 0.0;
    };
    std::vector<Loop> loops;
    double largestArea = 0.0;
    const auto normalizedEdge = [](int first, int second) {
        return std::pair<int, int>{std::min(first, second), std::max(first, second)};
    };

    // A watertight manifold section has degree two at every contour node.
    for (const auto& edge : edges) {
        if (visitedEdges.count(edge) != 0) {
            continue;
        }

        std::vector<int> loop{edge.first};
        int previous = edge.first;
        int current = edge.second;

        while (loop.size() <= edges.size() + 1) {
            visitedEdges.insert(normalizedEdge(previous, current));
            loop.push_back(current);
            if (current == loop.front()) {
                break;
            }

            int next = -1;
            for (int candidate : adjacency[current]) {
                if (visitedEdges.count(normalizedEdge(current, candidate)) == 0) {
                    next = candidate;
                    break;
                }
            }
            if (next < 0) {
                break;
            }

            previous = current;
            current = next;
        }

        if (loop.size() < 4 || loop.back() != loop.front()) {
            continue;
        }
        loop.pop_back();

        double signedAreaTwice = 0.0;
        for (std::size_t index = 0; index < loop.size(); ++index) {
            const SectionPoint& first = nodes[loop[index]];
            const SectionPoint& second = nodes[loop[(index + 1) % loop.size()]];
            signedAreaTwice += first.x * second.y - second.x * first.y;
        }

        const double area = 0.5 * std::abs(signedAreaTwice);
        largestArea = std::max(largestArea, area);
        loops.push_back({std::move(loop), area});
    }

    if (loops.empty() || largestArea <= 0.0) {
        return;
    }

    // A watertight mesh cut near a tangency throws off slivers - loops of three
    // or four nodes enclosing almost nothing. Rasterizing one of those puts a
    // stray solid cell in the middle of the flow, so anything under a
    // ten-thousandth of the biggest loop is treated as noise rather than as
    // geometry. A genuine second body is never that small next to the first.
    const double areaFloor = 1e-4 * largestArea;

    const double rotation = profile.rotation * PI / 180.0;
    const double cosineRotation = std::cos(rotation);
    const double sineRotation = std::sin(rotation);

    sectionContours.reserve(loops.size());
    for (const Loop& loop : loops) {
        if (loop.nodes.size() < 3 || loop.area < areaFloor) {
            continue;
        }
        std::vector<SectionPoint> contour;
        contour.reserve(loop.nodes.size());
        for (int nodeIndex : loop.nodes) {
            SectionPoint point = nodes[nodeIndex];
            if (profile.invert) {
                point.x = -point.x;
            }
            contour.push_back({
                cosineRotation * point.x - sineRotation * point.y,
                sineRotation * point.x + cosineRotation * point.y
            });
        }
        sectionContours.push_back(std::move(contour));
    }

    if (sectionContours.empty()) {
        return;
    }

    // One bounding box over all of them, so the loops keep their positions
    // relative to each other. Fitting each contour to the domain on its own
    // would stack two aerofoils on top of one another.
    double minimumX = std::numeric_limits<double>::max();
    double minimumY = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    double maximumY = std::numeric_limits<double>::lowest();
    for (const std::vector<SectionPoint>& contour : sectionContours) {
        for (const SectionPoint& point : contour) {
            minimumX = std::min(minimumX, point.x);
            minimumY = std::min(minimumY, point.y);
            maximumX = std::max(maximumX, point.x);
            maximumY = std::max(maximumY, point.y);
        }
    }

    const double sectionSpan = std::max(maximumX - minimumX, maximumY - minimumY);
    if (sectionSpan <= tolerance) {
        sectionContours.clear();
        return;
    }

    const double targetSpan =
        (profile.size > 0.0f)
            ? static_cast<double>(profile.size)
            : OBSTACLE_DOMAIN_FRACTION * std::min(cfg.Lx, cfg.Ly);
    const double scale = targetSpan / sectionSpan;
    const double sectionCentreX = 0.5 * (minimumX + maximumX);
    const double sectionCentreY = 0.5 * (minimumY + maximumY);

    const double placeX = profile.placed ? profile.x : cfg.Lx / 2.0;
    const double placeY = profile.placed ? profile.y : cfg.Ly / 2.0;

    // Preserve the former circle diameter while centring imported geometry.
    for (std::vector<SectionPoint>& contour : sectionContours) {
        for (SectionPoint& point : contour) {
            point.x = placeX + scale * (point.x - sectionCentreX);
            point.y = placeY + scale * (point.y - sectionCentreY);
        }
    }

    checkPlacement(profile);
}

void Mesh::buildVolume(const Profile& profile) {
    volumeTriangles.clear();
    if (triangles.empty()) {
        return;
    }

    Vertex minimum{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()
    };
    Vertex maximum{
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()
    };

    for (const Triangle& triangle : triangles) {
        for (const Vertex& vertex : {triangle.v0, triangle.v1, triangle.v2}) {
            minimum.x = std::min(minimum.x, vertex.x);
            minimum.y = std::min(minimum.y, vertex.y);
            minimum.z = std::min(minimum.z, vertex.z);
            maximum.x = std::max(maximum.x, vertex.x);
            maximum.y = std::max(maximum.y, vertex.y);
            maximum.z = std::max(maximum.z, vertex.z);
        }
    }

    const Vertex centre{
        0.5 * (minimum.x + maximum.x),
        0.5 * (minimum.y + maximum.y),
        0.5 * (minimum.z + maximum.z)
    };
    const double characteristicLength = std::max({
        maximum.x - minimum.x,
        maximum.y - minimum.y,
        maximum.z - minimum.z
    });
    if (characteristicLength <= 0.0) {
        return;
    }

    const double tolerance = std::max(1e-12, characteristicLength * 1e-9);
    const double weld = std::max(1e-12, characteristicLength * 1e-7);

    const int openEdges = openEdgeCount(triangles, weld);
    if (openEdges > 0) {
        std::ostringstream message;
        message << "profile '" << profile.file << "' is not a closed surface: "
                << openEdges << " of its edges are shared by something other "
                   "than two triangles."
                << "\n    A run of nz > 1 fills a model by asking which side "
                   "of its surface every cell centre is on, and a"
                << "\n    surface with a hole in it has no inside. Mend the "
                   "model, or set nz=1 to cut a section through it.";
        placementError = message.str();
        return;
    }

    const double angleX = profile.angleX * PI / 180.0;
    const double angleY = profile.angleY * PI / 180.0;
    const double angleZ = profile.angleZ * PI / 180.0;
    const double cosineX = std::cos(angleX);
    const double sineX = std::sin(angleX);
    const double cosineY = std::cos(angleY);
    const double sineY = std::sin(angleY);
    const double cosineZ = std::cos(angleZ);
    const double sineZ = std::sin(angleZ);

    const Vertex modelAxisX{cosineZ * cosineY, sineZ * cosineY, -sineY};
    const Vertex modelAxisY{
        cosineZ * sineY * sineX - sineZ * cosineX,
        sineZ * sineY * sineX + cosineZ * cosineX,
        cosineY * sineX
    };
    const Vertex modelAxisZ{
        cosineZ * sineY * cosineX + sineZ * sineX,
        sineZ * sineY * cosineX - cosineZ * sineX,
        cosineY * cosineX
    };

    const double rotation = profile.rotation * PI / 180.0;
    const double cosineRotation = std::cos(rotation);
    const double sineRotation = std::sin(rotation);

    const auto dot = [](const Vertex& first, const Vertex& second) {
        return first.x * second.x + first.y * second.y + first.z * second.z;
    };
    const auto place = [&](const Vertex& vertex) {
        const Vertex relative{
            vertex.x - centre.x,
            vertex.y - centre.y,
            vertex.z - centre.z
        };
        double along = dot(relative, modelAxisX);
        const double across = dot(relative, modelAxisY);
        const double depth = dot(relative, modelAxisZ);
        if (profile.invert) {
            along = -along;
        }
        return Vertex{
            cosineRotation * along - sineRotation * across,
            sineRotation * along + cosineRotation * across,
            depth
        };
    };

    volumeTriangles.reserve(triangles.size());
    for (const Triangle& triangle : triangles) {
        volumeTriangles.push_back({
            place(triangle.v0),
            place(triangle.v1),
            place(triangle.v2)
        });
    }

    double minimumX = std::numeric_limits<double>::max();
    double minimumY = std::numeric_limits<double>::max();
    double minimumZ = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    double maximumY = std::numeric_limits<double>::lowest();
    double maximumZ = std::numeric_limits<double>::lowest();
    for (const Triangle& triangle : volumeTriangles) {
        for (const Vertex& vertex : {triangle.v0, triangle.v1, triangle.v2}) {
            minimumX = std::min(minimumX, vertex.x);
            minimumY = std::min(minimumY, vertex.y);
            minimumZ = std::min(minimumZ, vertex.z);
            maximumX = std::max(maximumX, vertex.x);
            maximumY = std::max(maximumY, vertex.y);
            maximumZ = std::max(maximumZ, vertex.z);
        }
    }

    const double modelSpan = std::max({
        maximumX - minimumX,
        maximumY - minimumY,
        maximumZ - minimumZ
    });
    if (modelSpan <= tolerance) {
        volumeTriangles.clear();
        return;
    }

    const double targetSpan =
        (profile.size > 0.0f)
            ? static_cast<double>(profile.size)
            : OBSTACLE_DOMAIN_FRACTION * std::min({cfg.Lx, cfg.Ly, cfg.Lz});
    const double scale = targetSpan / modelSpan;
    const double modelCentreX = 0.5 * (minimumX + maximumX);
    const double modelCentreY = 0.5 * (minimumY + maximumY);
    const double modelCentreZ = 0.5 * (minimumZ + maximumZ);

    const double placeX = profile.placed ? profile.x : cfg.Lx / 2.0;
    const double placeY = profile.placed ? profile.y : cfg.Ly / 2.0;
    const double placeZ = profile.placed ? profile.z : cfg.Lz / 2.0;

    for (Triangle& triangle : volumeTriangles) {
        for (Vertex* vertex : {&triangle.v0, &triangle.v1, &triangle.v2}) {
            vertex->x = placeX + scale * (vertex->x - modelCentreX);
            vertex->y = placeY + scale * (vertex->y - modelCentreY);
            vertex->z = placeZ + scale * (vertex->z - modelCentreZ);
        }
    }

    checkPlacement(profile);
}

void Mesh::checkPlacement(const Profile& profile) {
    if (!placementError.empty())
        return;
    if (nz > 1 ? volumeTriangles.empty() : sectionContours.empty())
        return;

    if (profile.attach)
        return;

    double lowX = std::numeric_limits<double>::max();
    double lowY = std::numeric_limits<double>::max();
    double lowZ = std::numeric_limits<double>::max();
    double highX = std::numeric_limits<double>::lowest();
    double highY = std::numeric_limits<double>::lowest();
    double highZ = std::numeric_limits<double>::lowest();
    if (nz > 1) {
        for (const Triangle& triangle : volumeTriangles) {
            for (const Vertex& vertex :
                 {triangle.v0, triangle.v1, triangle.v2}) {
                lowX = std::min(lowX, vertex.x);
                lowY = std::min(lowY, vertex.y);
                lowZ = std::min(lowZ, vertex.z);
                highX = std::max(highX, vertex.x);
                highY = std::max(highY, vertex.y);
                highZ = std::max(highZ, vertex.z);
            }
        }
    } else {
        for (const std::vector<SectionPoint>& contour : sectionContours) {
            for (const SectionPoint& point : contour) {
                lowX = std::min(lowX, point.x);
                lowY = std::min(lowY, point.y);
                highX = std::max(highX, point.x);
                highY = std::max(highY, point.y);
            }
        }
    }

    const double marginX = dx;
    const double marginY = dy;
    const double marginZ = dz;
    std::ostringstream out;
    const auto miss = [&out](const char* side, double by) {
        out << "\n    " << side << " by " << by << " m";
    };

    if (lowX < marginX)          miss("past the left edge", marginX - lowX);
    if (highX > cfg.Lx - marginX) miss("past the right edge", highX - (cfg.Lx - marginX));
    if (lowY < marginY)          miss("past the bottom edge", marginY - lowY);
    if (highY > cfg.Ly - marginY) miss("past the top edge", highY - (cfg.Ly - marginY));
    if (nz > 1) {
        if (lowZ < marginZ)      miss("past the front edge", marginZ - lowZ);
        if (highZ > cfg.Lz - marginZ) miss("past the back edge", highZ - (cfg.Lz - marginZ));
    }

    const std::string misses = out.str();
    if (misses.empty())
        return;

    std::ostringstream message;
    message << "profile '" << profile.file << "' does not fit the domain:"
            << misses
            << "\n    it spans x " << lowX << ".." << highX
            << " and y " << lowY << ".." << highY;
    if (nz > 1)
        message << " and z " << lowZ << ".." << highZ;
    message << " in a domain of " << cfg.Lx << " x " << cfg.Ly;
    if (nz > 1)
        message << " x " << cfg.Lz;
    message << " m.";
    if (nz > 1)
        message << "\n    Move it with x=, y= and z=, or shrink it with size=.";
    else
        message << "\n    Move it with x= and y=, or shrink it with size=.";
    placementError = message.str();
}

bool Mesh::hasSection() const {
    for (const std::vector<SectionPoint>& contour : sectionContours) {
        if (contour.size() >= 3) {
            return true;
        }
    }
    return false;
}

std::size_t Mesh::sectionPointCount() const {
    std::size_t total = 0;
    for (const std::vector<SectionPoint>& contour : sectionContours) {
        total += contour.size();
    }
    return total;
}

void Mesh::rasterizeSection() {
    clearSolid();
    if (!hasSection()) {
        return;
    }

    const double boundaryRadius = 0.5 * std::hypot(dx, dy);
    const double radiusSquared = boundaryRadius * boundaryRadius;
    const double innerSquared = radiusSquared * (1.0 - 1e-12);
    const double outerSquared = radiusSquared * (1.0 + 1e-12);

    const auto distanceToSegment = [](double pointX,
                                      double pointY,
                                      const SectionPoint& first,
                                      const SectionPoint& second) {
        const double segmentX = second.x - first.x;
        const double segmentY = second.y - first.y;
        const double lengthSquared = segmentX * segmentX + segmentY * segmentY;
        if (lengthSquared <= 0.0) {
            return std::hypot(pointX - first.x, pointY - first.y);
        }

        const double projection = std::clamp(
            ((pointX - first.x) * segmentX + (pointY - first.y) * segmentY) /
                lengthSquared,
            0.0,
            1.0);
        const double closestX = first.x + projection * segmentX;
        const double closestY = first.y + projection * segmentY;
        return std::hypot(pointX - closestX, pointY - closestY);
    };

    const auto distanceSquared = [](double pointX,
                                    double pointY,
                                    const SectionPoint& first,
                                    const SectionPoint& second) {
        const double segmentX = second.x - first.x;
        const double segmentY = second.y - first.y;
        const double lengthSquared = segmentX * segmentX + segmentY * segmentY;
        double closestX = first.x;
        double closestY = first.y;
        if (lengthSquared > 0.0) {
            const double projection = std::clamp(
                ((pointX - first.x) * segmentX +
                 (pointY - first.y) * segmentY) / lengthSquared,
                0.0,
                1.0);
            closestX += projection * segmentX;
            closestY += projection * segmentY;
        }
        const double offsetX = pointX - closestX;
        const double offsetY = pointY - closestY;
        return offsetX * offsetX + offsetY * offsetY;
    };

    const auto touches = [&](double pointX,
                             double pointY,
                             const SectionPoint& first,
                             const SectionPoint& second) {
        const double squared = distanceSquared(pointX, pointY, first, second);
        if (squared > outerSquared)
            return false;
        if (squared < innerSquared)
            return true;
        return distanceToSegment(pointX, pointY, first, second) <=
               boundaryRadius;
    };

    // Mark cells touched by any contour's boundary before filling the
    // interiors. "any" is the whole change here: the loop below used to see a
    // single polygon.

    for (const std::vector<SectionPoint>& contour : sectionContours) {
        if (contour.size() < 3)
            continue;

        double minX = contour[0].x, maxX = contour[0].x;
        double minY = contour[0].y, maxY = contour[0].y;
        for (const SectionPoint& point : contour) {
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }

        const int i0 = std::max(0, static_cast<int>(
                                       (minX - boundaryRadius) / dx) - 1);
        const int i1 = std::min(nx - 1, static_cast<int>(
                                            (maxX + boundaryRadius) / dx) + 1);
        const int j0 = std::max(0, static_cast<int>(
                                       (minY - boundaryRadius) / dy) - 1);
        const int j1 = std::min(ny - 1, static_cast<int>(
                                            (maxY + boundaryRadius) / dy) + 1);
        if (i0 > i1 || j0 > j1)
            continue;

        #pragma omp parallel for schedule(static) if (j1 - j0 >= 16)
        for (int j = j0; j <= j1; ++j) {
            const double cellY = (j + 0.5) * dy;
            std::vector<std::size_t> near;
            near.reserve(contour.size());
            for (std::size_t point = 0; point < contour.size(); ++point) {
                const SectionPoint& first = contour[point];
                const SectionPoint& second =
                    contour[(point + 1) % contour.size()];
                const double low = std::min(first.y, second.y) - boundaryRadius;
                const double high = std::max(first.y, second.y) + boundaryRadius;
                if (cellY >= low && cellY <= high)
                    near.push_back(point);
            }
            if (near.empty())
                continue;

            for (int i = i0; i <= i1; ++i) {
                if (solid[j * nx + i])
                    continue;
                const double cellX = (i + 0.5) * dx;
                for (std::size_t point : near) {
                    const SectionPoint& first = contour[point];
                    const SectionPoint& second =
                        contour[(point + 1) % contour.size()];
                    if (std::min(first.x, second.x) - boundaryRadius > cellX ||
                        std::max(first.x, second.x) + boundaryRadius < cellX)
                        continue;
                    if (touches(cellX, cellY, first, second)) {
                        solid[j * nx + i] = 1;
                        break;
                    }
                }
            }
        }
    }
}

void Mesh::voxelize() {
    clearSolid();
    if (volumeTriangles.empty()) {
        return;
    }

    const int ny = this->ny;
    const int nz = this->nz;

    double minY = std::numeric_limits<double>::max();
    double minZ = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    double maxZ = std::numeric_limits<double>::lowest();
    for (const Triangle& triangle : volumeTriangles) {
        for (const Vertex& vertex : {triangle.v0, triangle.v1, triangle.v2}) {
            minY = std::min(minY, vertex.y);
            minZ = std::min(minZ, vertex.z);
            maxY = std::max(maxY, vertex.y);
            maxZ = std::max(maxZ, vertex.z);
        }
    }

    const double spanY = std::max(maxY - minY, 1e-12);
    const double spanZ = std::max(maxZ - minZ, 1e-12);
    const int wanted = std::max(1, static_cast<int>(std::sqrt(
        static_cast<double>(volumeTriangles.size()))));
    const int bucketsY = std::min(wanted, ny);
    const int bucketsZ = std::min(wanted, nz);
    const double scaleY = bucketsY / spanY;
    const double scaleZ = bucketsZ / spanZ;

    const auto bucketY = [&](double value) {
        return std::clamp(static_cast<int>((value - minY) * scaleY),
                          0, bucketsY - 1);
    };
    const auto bucketZ = [&](double value) {
        return std::clamp(static_cast<int>((value - minZ) * scaleZ),
                          0, bucketsZ - 1);
    };

    std::vector<int> bucketStart(
        static_cast<std::size_t>(bucketsY) * bucketsZ + 1, 0);
    for (const Triangle& triangle : volumeTriangles) {
        const int firstY = bucketY(std::min({triangle.v0.y, triangle.v1.y, triangle.v2.y}));
        const int lastY = bucketY(std::max({triangle.v0.y, triangle.v1.y, triangle.v2.y}));
        const int firstZ = bucketZ(std::min({triangle.v0.z, triangle.v1.z, triangle.v2.z}));
        const int lastZ = bucketZ(std::max({triangle.v0.z, triangle.v1.z, triangle.v2.z}));
        for (int bz = firstZ; bz <= lastZ; ++bz)
            for (int by = firstY; by <= lastY; ++by)
                ++bucketStart[static_cast<std::size_t>(bz) * bucketsY + by + 1];
    }
    for (std::size_t bucket = 1; bucket < bucketStart.size(); ++bucket)
        bucketStart[bucket] += bucketStart[bucket - 1];

    std::vector<int> bucketed(static_cast<std::size_t>(bucketStart.back()));
    std::vector<int> cursor(bucketStart.begin(), bucketStart.end() - 1);
    for (std::size_t index = 0; index < volumeTriangles.size(); ++index) {
        const Triangle& triangle = volumeTriangles[index];
        const int firstY = bucketY(std::min({triangle.v0.y, triangle.v1.y, triangle.v2.y}));
        const int lastY = bucketY(std::max({triangle.v0.y, triangle.v1.y, triangle.v2.y}));
        const int firstZ = bucketZ(std::min({triangle.v0.z, triangle.v1.z, triangle.v2.z}));
        const int lastZ = bucketZ(std::max({triangle.v0.z, triangle.v1.z, triangle.v2.z}));
        for (int bz = firstZ; bz <= lastZ; ++bz)
            for (int by = firstY; by <= lastY; ++by) {
                const std::size_t bucket =
                    static_cast<std::size_t>(bz) * bucketsY + by;
                bucketed[static_cast<std::size_t>(cursor[bucket]++)] =
                    static_cast<int>(index);
            }
    }

    const double nudgeY = dy * 1e-7;
    const double nudgeZ = dz * 3e-7;

    #pragma omp parallel
    {
        std::vector<double> hits;
        #pragma omp for schedule(static) collapse(2)
        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                const double rayY = (j + 0.5) * dy + nudgeY;
                const double rayZ = (k + 0.5) * dz + nudgeZ;
                if (rayY < minY || rayY > maxY || rayZ < minZ || rayZ > maxZ)
                    continue;

                const std::size_t bucket =
                    static_cast<std::size_t>(bucketZ(rayZ)) * bucketsY +
                    bucketY(rayY);
                hits.clear();
                for (int slot = bucketStart[bucket];
                     slot < bucketStart[bucket + 1];
                     ++slot) {
                    const Triangle& triangle =
                        volumeTriangles[static_cast<std::size_t>(bucketed[slot])];
                    const double y0 = triangle.v0.y - rayY;
                    const double z0 = triangle.v0.z - rayZ;
                    const double y1 = triangle.v1.y - rayY;
                    const double z1 = triangle.v1.z - rayZ;
                    const double y2 = triangle.v2.y - rayY;
                    const double z2 = triangle.v2.z - rayZ;
                    const double w0 = y1 * z2 - y2 * z1;
                    const double w1 = y2 * z0 - y0 * z2;
                    const double w2 = y0 * z1 - y1 * z0;
                    if (!((w0 > 0.0 && w1 > 0.0 && w2 > 0.0) ||
                          (w0 < 0.0 && w1 < 0.0 && w2 < 0.0)))
                        continue;
                    hits.push_back((w0 * triangle.v0.x + w1 * triangle.v1.x +
                                    w2 * triangle.v2.x) / (w0 + w1 + w2));
                }
                if (hits.empty())
                    continue;

                std::sort(hits.begin(), hits.end());
                std::size_t crossed = 0;
                const int row = (k * ny + j) * nx;
                for (int i = 0; i < nx; ++i) {
                    const double cellX = (i + 0.5) * dx;
                    while (crossed < hits.size() && hits[crossed] <= cellX)
                        ++crossed;
                    if (((hits.size() - crossed) & 1u) != 0)
                        solid[row + i] = 1;
                }
            }
        }
    }
}

bool Mesh::pointInsideSection(double pointX, double pointY) const {
    // Toggle once per horizontal-ray crossing (even-odd polygon rule), counted
    // across every contour at once rather than one polygon at a time. Two
    // separate bodies each toggle their own cells; a loop drawn inside another
    // loop toggles twice and comes out as a hole, which is what a ring or a
    // duct actually is.
    bool inside = false;
    for (const std::vector<SectionPoint>& contour : sectionContours) {
        if (contour.size() < 3) {
            continue;
        }
        for (std::size_t current = 0, previous = contour.size() - 1;
             current < contour.size();
             previous = current++) {
            const SectionPoint& first = contour[current];
            const SectionPoint& second = contour[previous];
            const bool crossesRay =
                ((first.y > pointY) != (second.y > pointY)) &&
                (pointX <
                 (second.x - first.x) * (pointY - first.y) /
                         (second.y - first.y) +
                     first.x);
            if (crossesRay) {
                inside = !inside;
            }
        }
    }
    return inside;
}

void Mesh::buildSolid() {
    if (!hasSection()) {
        return;
    }

    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    bool first = true;
    for (const std::vector<SectionPoint>& contour : sectionContours) {
        if (contour.size() < 3)
            continue;
        for (const SectionPoint& point : contour) {
            if (first) {
                minX = maxX = point.x;
                minY = maxY = point.y;
                first = false;
                continue;
            }
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }
    }
    if (first)
        return;

    const int i0 = std::max(0, static_cast<int>(minX / dx) - 1);
    const int i1 = std::min(nx - 1, static_cast<int>(maxX / dx) + 1);
    const int j0 = std::max(0, static_cast<int>(minY / dy) - 1);
    const int j1 = std::min(ny - 1, static_cast<int>(maxY / dy) + 1);

    #pragma omp parallel for schedule(static) if (j1 - j0 >= 16)
    for (int j = j0; j <= j1; ++j) {
        const double cellY = (j + 0.5) * dy;
        for (int i = i0; i <= i1; ++i) {
            if (solid[j * nx + i] != 0) {
                continue;
            }
            if (pointInsideSection((i + 0.5) * dx, cellY)) {
                solid[j * nx + i] = 1;
            }
        }
    }
}

void Mesh::labelObjects() {
    objectId.assign(static_cast<std::size_t>(nx) * ny * nz, 0);
    objects.clear();

    const int cells = nx * ny * nz;
    const int plane = nx * ny;
    std::vector<int> pending;

    for (int seed = 0; seed < cells; ++seed) {
        if (solid[seed] == 0 || objectId[seed] != 0) {
            continue;
        }

        const int label = static_cast<int>(objects.size()) + 1;
        SolidObject body;
        double sumX = 0.0;
        double sumY = 0.0;
        double sumZ = 0.0;

        objectId[seed] = label;
        pending.push_back(seed);
        while (!pending.empty()) {
            const int id = pending.back();
            pending.pop_back();
            const int i = id % nx;
            const int j = id / nx % ny;
            const int k = id / plane;

            ++body.cells;
            sumX += (i + 0.5) * dx;
            sumY += (j + 0.5) * dy;
            sumZ += (k + 0.5) * dz;

            for (int neighbourK = std::max(k - 1, 0);
                 neighbourK <= std::min(k + 1, nz - 1);
                 ++neighbourK) {
            for (int neighbourJ = std::max(j - 1, 0);
                 neighbourJ <= std::min(j + 1, ny - 1);
                 ++neighbourJ) {
                for (int neighbourI = std::max(i - 1, 0);
                     neighbourI <= std::min(i + 1, nx - 1);
                     ++neighbourI) {
                    const int neighbour =
                        (neighbourK * ny + neighbourJ) * nx + neighbourI;
                    if (solid[neighbour] == 0 || objectId[neighbour] != 0) {
                        continue;
                    }
                    objectId[neighbour] = label;
                    pending.push_back(neighbour);
                }
            }
            }
        }

        body.cx = sumX / body.cells;
        body.cy = sumY / body.cells;
        body.cz = sumZ / body.cells;
        objects.push_back(body);
    }

    // The rim radius needs the centroid, so it cannot be accumulated above.
    for (int id = 0; id < cells; ++id) {
        if (objectId[id] == 0) {
            continue;
        }
        SolidObject& body = objects[objectId[id] - 1];
        const double offsetX = (id % nx + 0.5) * dx - body.cx;
        const double offsetY = (id / nx % ny + 0.5) * dy - body.cy;
        const double offsetZ = (id / plane + 0.5) * dz - body.cz;
        body.radius = std::max(body.radius, offsetX * offsetX +
                                                offsetY * offsetY +
                                                offsetZ * offsetZ);
        body.inertia[0] += offsetY * offsetY + offsetZ * offsetZ;
        body.inertia[1] -= offsetX * offsetY;
        body.inertia[2] -= offsetX * offsetZ;
        body.inertia[3] -= offsetY * offsetX;
        body.inertia[4] += offsetX * offsetX + offsetZ * offsetZ;
        body.inertia[5] -= offsetY * offsetZ;
        body.inertia[6] -= offsetZ * offsetX;
        body.inertia[7] -= offsetZ * offsetY;
        body.inertia[8] += offsetX * offsetX + offsetY * offsetY;
    }
    for (SolidObject& body : objects)
        body.radius = std::sqrt(body.radius);

    const double sideX = dx;
    const double sideY = dy;
    const double sideZ = dz;
    const double cellVolume = sideX * sideY * sideZ;
    for (SolidObject& body : objects) {
        body.volume = body.cells * static_cast<double>(dx) * dy * dz;
        for (double& term : body.inertia)
            term *= cellVolume;
        body.inertia[0] += body.volume * (sideY * sideY + sideZ * sideZ) / 12.0;
        body.inertia[4] += body.volume * (sideX * sideX + sideZ * sideZ) / 12.0;
        body.inertia[8] += body.volume * (sideX * sideX + sideY * sideY) / 12.0;
        body.baseCx = body.cx;
        body.baseCy = body.cy;
        body.baseCz = body.cz;
    }
}

bool Mesh::prepareMotion() {
    if (motionPrepared)
        return true;
    if (objects.empty())
        return false;

    if (nz > 1) {
        if (volumeTriangles.empty())
            return false;

        baseObjectId = objectId;
        baseCells.assign(objects.size(), std::vector<int>());
        const int cells = nx * ny * nz;
        for (int id = 0; id < cells; ++id)
            if (objectId[id] > 0)
                baseCells[objectId[id] - 1].push_back(id);

        poses.assign(objects.size() + 1, BodyPose());
        motionPrepared = true;
        return true;
    }

    if (sectionContours.empty())
        return false;

    baseContours = sectionContours;
    contourObject.assign(baseContours.size(), 0);

    for (std::size_t which = 0; which < baseContours.size(); ++which) {
        std::vector<int> votes(objects.size() + 1, 0);
        for (const SectionPoint& point : baseContours[which]) {
            const int i = static_cast<int>(point.x / dx);
            const int j = static_cast<int>(point.y / dy);
            if (i < 0 || j < 0 || i >= nx || j >= ny)
                continue;
            const int label = objectId[j * nx + i];
            if (label > 0)
                ++votes[label];
        }
        int best = 0;
        for (std::size_t label = 1; label < votes.size(); ++label)
            if (votes[label] > votes[best])
                best = static_cast<int>(label);
        contourObject[which] = best;
        if (best == 0)
            return false;
    }

    poses.assign(objects.size() + 1, BodyPose());
    motionPrepared = true;
    return true;
}

void Mesh::setPose(int object, const BodyPose& newPose) {
    if (object >= 1 && static_cast<std::size_t>(object) < poses.size())
        poses[object] = newPose;
}

void Mesh::updateSolid() {
    if (!motionPrepared)
        return;

    if (nz > 1) {
        voxelizeOwned();
        relabelStable();
        return;
    }

    sectionContours = baseContours;
    for (std::size_t which = 0; which < sectionContours.size(); ++which) {
        const int owner = contourObject[which];
        const BodyPose& current = poses[owner];
        if (current.x == 0.0 && current.y == 0.0 && current.qx == 0.0 &&
            current.qy == 0.0 && current.qz == 0.0)
            continue;

        const double originX = objects[owner - 1].baseCx;
        const double originY = objects[owner - 1].baseCy;
        double rotation[9];
        poseRotation(current, rotation);
        const double cosT = rotation[0];
        const double sinT = rotation[3];
        for (SectionPoint& point : sectionContours[which]) {
            const double localX = point.x - originX;
            const double localY = point.y - originY;
            point.x = originX + current.x + localX * cosT - localY * sinT;
            point.y = originY + current.y + localX * sinT + localY * cosT;
        }
    }

    rasterizeOwned();
    relabelStable();
}

void Mesh::rasterizeOwned() {
    const int cells = nx * ny;
    cellOwner.assign(static_cast<std::size_t>(cells), 0);
    contestedCells.clear();

    std::vector<std::vector<SectionPoint>> all = std::move(sectionContours);
    const std::size_t bodies = objects.size();
    for (std::size_t body = 1; body <= bodies; ++body) {
        sectionContours.clear();
        for (std::size_t which = 0; which < all.size(); ++which)
            if (contourObject[which] == static_cast<int>(body))
                sectionContours.push_back(all[which]);
        if (sectionContours.empty())
            continue;

        rasterizeSection();
        buildSolid();
        for (int id = 0; id < cells; ++id) {
            if (!solid[id])
                continue;
            if (cellOwner[id] == 0)
                cellOwner[id] = static_cast<int>(body);
            else if (cellOwner[id] != static_cast<int>(body))
                contestedCells.push_back(id);
        }
        if (body == 1)
            claimScratch = solid;
        else
            for (int id = 0; id < cells; ++id)
                if (solid[id])
                    claimScratch[id] = 1;
    }

    sectionContours = std::move(all);
    if (bodies == 0) {
        rasterizeSection();
        buildSolid();
        return;
    }
    solid = claimScratch;
}

void Mesh::voxelizeOwned() {
    const int cells = nx * ny * nz;
    cellOwner.assign(static_cast<std::size_t>(cells), 0);
    contestedCells.clear();

    const std::size_t bodies = objects.size();
    if (bodies == 0) {
        voxelize();
        return;
    }
    claimScratch.assign(static_cast<std::size_t>(cells), 0);

    const int plane = nx * ny;
    for (std::size_t body = 1; body <= bodies; ++body) {
        if (body > baseCells.size() || baseCells[body - 1].empty())
            continue;

        const SolidObject& shape = objects[body - 1];
        const BodyPose& current = poses[body];
        double rotation[9];
        poseRotation(current, rotation);

        const double originX = shape.baseCx + current.x;
        const double originY = shape.baseCy + current.y;
        const double originZ = shape.baseCz + current.z;

        double lowX = std::numeric_limits<double>::max();
        double lowY = std::numeric_limits<double>::max();
        double lowZ = std::numeric_limits<double>::max();
        double highX = std::numeric_limits<double>::lowest();
        double highY = std::numeric_limits<double>::lowest();
        double highZ = std::numeric_limits<double>::lowest();
        for (int id : baseCells[body - 1]) {
            const double offsetX = (id % nx + 0.5) * dx - shape.baseCx;
            const double offsetY = (id / nx % ny + 0.5) * dy - shape.baseCy;
            const double offsetZ = (id / plane + 0.5) * dz - shape.baseCz;
            const double placedX = originX + rotation[0] * offsetX +
                                   rotation[1] * offsetY + rotation[2] * offsetZ;
            const double placedY = originY + rotation[3] * offsetX +
                                   rotation[4] * offsetY + rotation[5] * offsetZ;
            const double placedZ = originZ + rotation[6] * offsetX +
                                   rotation[7] * offsetY + rotation[8] * offsetZ;
            lowX = std::min(lowX, placedX);
            lowY = std::min(lowY, placedY);
            lowZ = std::min(lowZ, placedZ);
            highX = std::max(highX, placedX);
            highY = std::max(highY, placedY);
            highZ = std::max(highZ, placedZ);
        }

        const int i0 = std::max(0, static_cast<int>(lowX / dx) - 1);
        const int i1 = std::min(nx - 1, static_cast<int>(highX / dx) + 1);
        const int j0 = std::max(0, static_cast<int>(lowY / dy) - 1);
        const int j1 = std::min(ny - 1, static_cast<int>(highY / dy) + 1);
        const int k0 = std::max(0, static_cast<int>(lowZ / dz) - 1);
        const int k1 = std::min(nz - 1, static_cast<int>(highZ / dz) + 1);
        if (i0 > i1 || j0 > j1 || k0 > k1)
            continue;

        for (int k = k0; k <= k1; ++k) {
            for (int j = j0; j <= j1; ++j) {
                for (int i = i0; i <= i1; ++i) {
                    const double deltaX = (i + 0.5) * dx - originX;
                    const double deltaY = (j + 0.5) * dy - originY;
                    const double deltaZ = (k + 0.5) * dz - originZ;
                    const double baseX = shape.baseCx + rotation[0] * deltaX +
                                         rotation[3] * deltaY +
                                         rotation[6] * deltaZ;
                    const double baseY = shape.baseCy + rotation[1] * deltaX +
                                         rotation[4] * deltaY +
                                         rotation[7] * deltaZ;
                    const double baseZ = shape.baseCz + rotation[2] * deltaX +
                                         rotation[5] * deltaY +
                                         rotation[8] * deltaZ;
                    const int baseI = static_cast<int>(std::floor(baseX / dx));
                    const int baseJ = static_cast<int>(std::floor(baseY / dy));
                    const int baseK = static_cast<int>(std::floor(baseZ / dz));
                    if (baseI < 0 || baseI >= nx || baseJ < 0 || baseJ >= ny ||
                        baseK < 0 || baseK >= nz)
                        continue;
                    if (baseObjectId[(baseK * ny + baseJ) * nx + baseI] !=
                        static_cast<int>(body))
                        continue;

                    const int id = (k * ny + j) * nx + i;
                    if (cellOwner[id] == 0)
                        cellOwner[id] = static_cast<int>(body);
                    else if (cellOwner[id] != static_cast<int>(body))
                        contestedCells.push_back(id);
                    claimScratch[id] = 1;
                }
            }
        }
    }

    solid = claimScratch;
}

void Mesh::relabelStable() {
    const std::vector<SolidObject> previous = objects;
    const std::vector<BodyPose> keptPoses = poses;
    lastRenumbered.clear();

    labelObjects();

    const std::size_t wanted = previous.size();

    struct Candidate {
        double distance;
        std::size_t body;
        std::size_t blob;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(wanted * objects.size());
    for (std::size_t body = 0; body < wanted; ++body) {
        const double wantX = previous[body].baseCx + keptPoses[body + 1].x;
        const double wantY = previous[body].baseCy + keptPoses[body + 1].y;
        const double wantZ = previous[body].baseCz + keptPoses[body + 1].z;
        for (std::size_t blob = 0; blob < objects.size(); ++blob)
            candidates.push_back({std::hypot(std::hypot(objects[blob].cx - wantX,
                                                        objects[blob].cy - wantY),
                                             objects[blob].cz - wantZ),
                                  body, blob});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.distance < b.distance;
              });

    std::vector<int> bodyOf(objects.size(), 0);
    std::vector<bool> claimed(wanted, false);
    std::vector<bool> taken(objects.size(), false);
    for (const Candidate& candidate : candidates) {
        if (claimed[candidate.body] || taken[candidate.blob])
            continue;
        claimed[candidate.body] = true;
        taken[candidate.blob] = true;
        bodyOf[candidate.blob] = static_cast<int>(candidate.body) + 1;
    }

    std::vector<SolidObject> ordered(wanted);
    for (std::size_t body = 0; body < wanted; ++body) {
        ordered[body] = previous[body];
        ordered[body].cells = 0;
        ordered[body].volume = 0.0;
    }
    for (std::size_t blob = 0; blob < objects.size(); ++blob) {
        if (bodyOf[blob] == 0) {
            ordered.push_back(objects[blob]);
            bodyOf[blob] = static_cast<int>(ordered.size());
            lastRenumbered.push_back({0, bodyOf[blob]});
            continue;
        }
        SolidObject kept = objects[blob];
        kept.baseCx = previous[bodyOf[blob] - 1].baseCx;
        kept.baseCy = previous[bodyOf[blob] - 1].baseCy;
        kept.baseCz = previous[bodyOf[blob] - 1].baseCz;
        ordered[bodyOf[blob] - 1] = kept;
    }
    for (std::size_t body = 0; body < wanted; ++body)
        if (!claimed[body])
            lastRenumbered.push_back({static_cast<int>(body) + 1, 0});

    const int cells = nx * ny * nz;
    for (int id = 0; id < cells; ++id) {
        const int raw = objectId[id];
        objectId[id] = raw == 0 ? 0 : bodyOf[raw - 1];
    }

    objects = std::move(ordered);
    poses.resize(objects.size() + 1);
    for (std::size_t body = 0; body < wanted; ++body)
        poses[body + 1] = keptPoses[body + 1];
}

void Mesh::initCircle(double cx, double cy, double cz, double R) {
    clearSolid();
    if (nz > 1) {
        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    double xc = (i + 0.5) * dx;
                    double yc = (j + 0.5) * dy;
                    double zc = (k + 0.5) * dz;
                    double dist = std::sqrt((xc - cx) * (xc - cx) +
                                            (yc - cy) * (yc - cy) +
                                            (zc - cz) * (zc - cz));
                    if (dist <= R)
                        solid[(k * ny + j) * nx + i] = 1;
                    else
                        solid[(k * ny + j) * nx + i] = 0;
                }
            }
        }
        return;
    }

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            double xc = (i + 0.5) * dx;
            double yc = (j + 0.5) * dy;
            double dist = std::sqrt((xc - cx) * (xc - cx) + (yc - cy) * (yc - cy));
            if (dist <= R)
                solid[j * nx + i] = 1;
            else
                solid[j * nx + i] = 0;
        }
    }
}

void Mesh::printInfo() const {
    std::cout << "\n=== Mesh Information ===\n";
    if (nz > 1) {
        std::cout << "  nx = " << nx << ", ny = " << ny << ", nz = " << nz
                  << "\n";
        std::cout << "  dx = " << dx << ", dy = " << dy << ", dz = " << dz
                  << "\n";
    } else {
        std::cout << "  nx = " << nx << ", ny = " << ny << "\n";
        std::cout << "  dx = " << dx << ", dy = " << dy << "\n";
    }
    int count = 0;
    for (int v : solid) if (v) ++count;
    std::cout << "  Number of solid cells = " << count << "\n";
    std::cout << "  Number of objects = " << objects.size()
              << " (these numbers are what wallMotion takes)\n";
    constexpr std::size_t LISTED_OBJECTS = 10;
    for (std::size_t index = 0;
         index < std::min(objects.size(), LISTED_OBJECTS);
         ++index) {
        const SolidObject& body = objects[index];
        std::cout << "    object " << index + 1 << ": " << body.cells
                  << " cells, centre (" << body.cx << ", " << body.cy;
        if (nz > 1)
            std::cout << ", " << body.cz;
        std::cout << ") m, rim " << body.radius << " m\n";
    }
    if (objects.size() > LISTED_OBJECTS)
        std::cout << "    ... and " << objects.size() - LISTED_OBJECTS
                  << " more\n";
    std::cout << "  Number of geometry triangles = " << triangles.size() << "\n";
    if (nz > 1)
        std::cout << "  Number of placed triangles = " << volumeTriangles.size()
                  << "\n";
    else
        std::cout << "  Number of section contours = " << sectionContours.size()
                  << " (" << sectionPointCount() << " points)\n";
    std::cout << "=========================\n";
}
