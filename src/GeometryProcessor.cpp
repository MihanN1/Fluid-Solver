#include "GeometryProcessor.hpp"

#include <tiny_obj_loader.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <utility>

namespace maskui {
namespace {

constexpr double PI = 3.14159265358979323846;
constexpr std::size_t MAX_MASK_CELLS = 10'000'000;

double toRadians(double degrees) {
    return degrees * PI / 180.0;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

Vec3 subtract(const Vec3& first, const Vec3& second) {
    return {
        first.x - second.x,
        first.y - second.y,
        first.z - second.z
    };
}

double dot(const Vec3& first, const Vec3& second) {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

double squaredDistance(const Vec3& first, const Vec3& second) {
    const Vec3 delta = subtract(first, second);
    return dot(delta, delta);
}

double squaredDistance(const Vec2& first, const Vec2& second) {
    const double deltaX = first.x - second.x;
    const double deltaY = first.y - second.y;
    return deltaX * deltaX + deltaY * deltaY;
}

double distanceToSegment(double pointX,
                         double pointY,
                         const Vec2& first,
                         const Vec2& second) {
    const double segmentX = second.x - first.x;
    const double segmentY = second.y - first.y;
    const double lengthSquared =
        segmentX * segmentX + segmentY * segmentY;
    if (lengthSquared <= 0.0) {
        return std::hypot(pointX - first.x, pointY - first.y);
    }

    const double projection = std::clamp(
        ((pointX - first.x) * segmentX +
         (pointY - first.y) * segmentY) /
            lengthSquared,
        0.0,
        1.0);
    const double closestX = first.x + projection * segmentX;
    const double closestY = first.y + projection * segmentY;
    return std::hypot(pointX - closestX, pointY - closestY);
}

bool readLittleEndianUInt32(std::istream& input, std::uint32_t& value) {
    std::array<unsigned char, 4> bytes{};
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[3]) << 24u);
    return true;
}

bool readLittleEndianFloat(std::istream& input, float& value) {
    std::uint32_t bits = 0;
    if (!readLittleEndianUInt32(input, bits)) {
        return false;
    }
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool pointInsidePolygon(double pointX, double pointY, const std::vector<Vec2>& polygon) {
    bool inside = false;
    for (std::size_t current = 0, previous = polygon.size() - 1;
         current < polygon.size();
         previous = current++) {
        const Vec2& first = polygon[current];
        const Vec2& second = polygon[previous];
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
    return inside;
}

bool finiteParameters(const MaskParameters& parameters) {
    return std::isfinite(parameters.Lx) &&
           std::isfinite(parameters.Ly) &&
           std::isfinite(parameters.sliceAngleX) &&
           std::isfinite(parameters.sliceAngleY) &&
           std::isfinite(parameters.sliceAngleZ) &&
           std::isfinite(parameters.sliceRotation);
}

} // namespace

bool GeometryProcessor::load(
    const std::filesystem::path& filename,
    std::string& error) {
    clear();
    error.clear();

    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(filename, filesystemError) ||
        filesystemError) {
        error = "Geometry file does not exist or is not a regular file.";
        return false;
    }

    const std::string extension = lowercase(filename.extension().string());
    bool loaded = false;
    if (extension == ".obj") {
        loaded = loadOBJ(filename, error);
    } else if (extension == ".stl") {
        loaded = loadSTL(filename, error);
    } else {
        error = "Unsupported geometry extension. Use .stl or .obj.";
        return false;
    }

    if (!loaded || triangles_.empty()) {
        if (error.empty()) {
            error = "Geometry contains no triangles.";
        }
        clear();
        return false;
    }

    sourcePath_ = std::filesystem::absolute(filename, filesystemError);
    if (filesystemError) {
        sourcePath_ = filename;
    }
    updateBounds();
    if (!bounds_.valid) {
        error = "Geometry bounds are degenerate.";
        clear();
        return false;
    }
    return true;
}

void GeometryProcessor::clear() {
    sourcePath_.clear();
    triangles_.clear();
    bounds_ = {};
}

bool GeometryProcessor::empty() const {
    return triangles_.empty();
}

const std::filesystem::path& GeometryProcessor::sourcePath() const {
    return sourcePath_;
}

const std::vector<Triangle3>& GeometryProcessor::triangles() const {
    return triangles_;
}

const GeometryBounds& GeometryProcessor::bounds() const {
    return bounds_;
}

bool GeometryProcessor::loadOBJ(
    const std::filesystem::path& filename,
    std::string& error) {
    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string loaderError;

    std::ifstream input(filename);
    if (!input.is_open()) {
        error = "Cannot open OBJ file.";
        return false;
    }

    const bool loaded = tinyobj::LoadObj(
        &attributes,
        &shapes,
        &materials,
        &warning,
        &loaderError,
        &input,
        nullptr,
        true);

    if (!loaded) {
        error = loaderError.empty() ? "OBJ loader failed." : loaderError;
        return false;
    }

    for (const tinyobj::shape_t& shape : shapes) {
        std::size_t indexOffset = 0;
        for (unsigned int faceVertexCount : shape.mesh.num_face_vertices) {
            std::vector<Vec3> faceVertices;
            faceVertices.reserve(faceVertexCount);

            for (unsigned int vertex = 0; vertex < faceVertexCount; ++vertex) {
                const tinyobj::index_t index =
                    shape.mesh.indices[indexOffset + vertex];
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

            for (std::size_t vertex = 1;
                 vertex + 1 < faceVertices.size();
                 ++vertex) {
                triangles_.push_back({
                    faceVertices[0],
                    faceVertices[vertex],
                    faceVertices[vertex + 1]
                });
            }
        }
    }

    if (!warning.empty() && error.empty()) {
        error = "OBJ warning: " + warning;
    }
    return !triangles_.empty();
}

bool GeometryProcessor::loadSTL(
    const std::filesystem::path& filename,
    std::string& error) {
    std::error_code filesystemError;
    const std::uintmax_t fileSize =
        std::filesystem::file_size(filename, filesystemError);
    if (filesystemError) {
        error = "Cannot determine STL file size.";
        return false;
    }

    std::ifstream binaryInput(filename, std::ios::binary);
    if (!binaryInput.is_open()) {
        error = "Cannot open STL file.";
        return false;
    }

    bool isBinary = false;
    std::uint32_t triangleCount = 0;
    if (fileSize >= 84u) {
        binaryInput.seekg(80, std::ios::beg);
        if (readLittleEndianUInt32(binaryInput, triangleCount) &&
            triangleCount <=
                (std::numeric_limits<std::uintmax_t>::max() - 84u) / 50u &&
            84u + static_cast<std::uintmax_t>(triangleCount) * 50u ==
                fileSize) {
            isBinary = true;
        }
    }

    if (isBinary) {
        triangles_.reserve(triangleCount);
        binaryInput.clear();
        binaryInput.seekg(84, std::ios::beg);
        for (std::uint32_t triangle = 0;
             triangle < triangleCount;
             ++triangle) {
            binaryInput.ignore(12);
            std::array<Vec3, 3> vertices{};
            for (Vec3& vertex : vertices) {
                std::array<float, 3> coordinates{};
                for (float& coordinate : coordinates) {
                    if (!readLittleEndianFloat(binaryInput, coordinate)) {
                        error = "Binary STL ended inside triangle data.";
                        return false;
                    }
                }
                vertex = {
                    static_cast<double>(coordinates[0]),
                    static_cast<double>(coordinates[1]),
                    static_cast<double>(coordinates[2])
                };
                if (!std::isfinite(vertex.x) ||
                    !std::isfinite(vertex.y) ||
                    !std::isfinite(vertex.z)) {
                    error = "Binary STL contains a non-finite vertex.";
                    return false;
                }
            }
            binaryInput.ignore(2);
            if (!binaryInput) {
                error = "Binary STL ended inside triangle data.";
                return false;
            }
            triangles_.push_back({vertices[0], vertices[1], vertices[2]});
        }
        return !triangles_.empty();
    }

    binaryInput.close();
    std::ifstream textInput(filename);
    if (!textInput.is_open()) {
        error = "Cannot reopen ASCII STL file.";
        return false;
    }

    std::vector<Vec3> facetVertices;
    std::string token;
    while (textInput >> token) {
        if (lowercase(token) != "vertex") {
            continue;
        }
        Vec3 vertex;
        if (!(textInput >> vertex.x >> vertex.y >> vertex.z)) {
            error = "ASCII STL contains an invalid vertex.";
            return false;
        }
        if (!std::isfinite(vertex.x) ||
            !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z)) {
            error = "ASCII STL contains a non-finite vertex.";
            return false;
        }
        facetVertices.push_back(vertex);
        if (facetVertices.size() == 3) {
            triangles_.push_back(
                {facetVertices[0], facetVertices[1], facetVertices[2]});
            facetVertices.clear();
        }
    }
    if (!textInput.eof() || !facetVertices.empty()) {
        error = "ASCII STL is truncated.";
        return false;
    }
    return !triangles_.empty();
}

void GeometryProcessor::updateBounds() {
    if (triangles_.empty()) {
        bounds_ = {};
        return;
    }

    const double largest = std::numeric_limits<double>::max();
    GeometryBounds updated;
    updated.minimum = {largest, largest, largest};
    updated.maximum = {-largest, -largest, -largest};

    for (const Triangle3& triangle : triangles_) {
        for (const Vec3& vertex : {triangle.v0, triangle.v1, triangle.v2}) {
            updated.minimum.x = std::min(updated.minimum.x, vertex.x);
            updated.minimum.y = std::min(updated.minimum.y, vertex.y);
            updated.minimum.z = std::min(updated.minimum.z, vertex.z);
            updated.maximum.x = std::max(updated.maximum.x, vertex.x);
            updated.maximum.y = std::max(updated.maximum.y, vertex.y);
            updated.maximum.z = std::max(updated.maximum.z, vertex.z);
        }
    }

    updated.centre = {
        0.5 * (updated.minimum.x + updated.maximum.x),
        0.5 * (updated.minimum.y + updated.maximum.y),
        0.5 * (updated.minimum.z + updated.maximum.z)
    };
    updated.characteristicLength = std::max({
        updated.maximum.x - updated.minimum.x,
        updated.maximum.y - updated.minimum.y,
        updated.maximum.z - updated.minimum.z
    });
    updated.valid =
        std::isfinite(updated.characteristicLength) &&
        updated.characteristicLength > 0.0;
    bounds_ = updated;
}

SectionFrame GeometryProcessor::sectionFrame(
    const MaskParameters& parameters) const {
    SectionFrame frame;
    if (!bounds_.valid ||
        !std::isfinite(parameters.sliceAngleX) ||
        !std::isfinite(parameters.sliceAngleY) ||
        !std::isfinite(parameters.sliceAngleZ)) {
        return frame;
    }

    const double angleX = toRadians(parameters.sliceAngleX);
    const double angleY = toRadians(parameters.sliceAngleY);
    const double angleZ = toRadians(parameters.sliceAngleZ);
    const double cosineX = std::cos(angleX);
    const double sineX = std::sin(angleX);
    const double cosineY = std::cos(angleY);
    const double sineY = std::sin(angleY);
    const double cosineZ = std::cos(angleZ);
    const double sineZ = std::sin(angleZ);

    frame.centre = bounds_.centre;
    frame.axisX = {cosineZ * cosineY, sineZ * cosineY, -sineY};
    frame.axisY = {
        cosineZ * sineY * sineX - sineZ * cosineX,
        sineZ * sineY * sineX + cosineZ * cosineX,
        cosineY * sineX
    };
    frame.normal = {
        cosineZ * sineY * cosineX + sineZ * sineX,
        sineZ * sineY * cosineX - cosineZ * sineX,
        cosineY * cosineX
    };
    frame.extent = 0.65 * bounds_.characteristicLength;
    frame.tolerance =
        std::max(1e-12, bounds_.characteristicLength * 1e-9);
    frame.valid = true;
    return frame;
}

std::vector<SectionSegment> GeometryProcessor::sectionSegments(
    const MaskParameters& parameters) const {
    const SectionFrame frame = sectionFrame(parameters);
    if (!frame.valid) {
        return {};
    }
    return buildSectionSegments(frame);
}

std::vector<SectionSegment> GeometryProcessor::buildSectionSegments(
    const SectionFrame& frame) const {
    std::vector<SectionSegment> segments;
    const double toleranceSquared = frame.tolerance * frame.tolerance;

    for (const Triangle3& triangle : triangles_) {
        const std::array<Vec3, 3> vertices{
            triangle.v0,
            triangle.v1,
            triangle.v2
        };
        std::array<double, 3> distances{};
        for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
            distances[vertex] =
                dot(subtract(vertices[vertex], frame.centre), frame.normal);
        }

        std::array<Vec3, 3> intersections{};
        std::size_t intersectionCount = 0;
        const auto addUniqueIntersection = [&](const Vec3& intersection) {
            for (std::size_t index = 0;
                 index < intersectionCount;
                 ++index) {
                if (squaredDistance(intersections[index], intersection) <=
                    toleranceSquared) {
                    return;
                }
            }
            if (intersectionCount < intersections.size()) {
                intersections[intersectionCount++] = intersection;
            }
        };

        for (std::size_t edge = 0; edge < vertices.size(); ++edge) {
            const std::size_t next = (edge + 1) % vertices.size();
            const Vec3& first = vertices[edge];
            const Vec3& second = vertices[next];
            const double firstDistance = distances[edge];
            const double secondDistance = distances[next];
            const bool firstOnPlane =
                std::abs(firstDistance) <= frame.tolerance;
            const bool secondOnPlane =
                std::abs(secondDistance) <= frame.tolerance;

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

        if (intersectionCount == 2 &&
            squaredDistance(intersections[0], intersections[1]) >
                toleranceSquared) {
            segments.push_back({intersections[0], intersections[1]});
        }
    }

    return segments;
}

std::vector<std::vector<Vec2>> GeometryProcessor::buildContours(
    const MaskParameters& parameters,
    const SectionFrame& frame,
    std::string& error) const {
    using Segment = std::pair<Vec2, Vec2>;
    std::vector<Segment> segments;
    const double toleranceSquared = frame.tolerance * frame.tolerance;

    const auto project = [&frame](const Vec3& vertex) {
        const Vec3 relative = subtract(vertex, frame.centre);
        return Vec2{
            dot(relative, frame.axisX),
            dot(relative, frame.axisY)
        };
    };

    for (const SectionSegment& spatialSegment :
         buildSectionSegments(frame)) {
        const Vec2 first = project(spatialSegment.first);
        const Vec2 second = project(spatialSegment.second);
        if (squaredDistance(first, second) > toleranceSquared) {
            segments.emplace_back(first, second);
        }
    }

    if (segments.empty()) {
        error = "The selected plane does not produce section segments.";
        return {};
    }

    std::vector<Vec2> nodes;
    const auto findOrAddNode = [&](const Vec2& point) {
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            if (squaredDistance(nodes[index], point) <= toleranceSquared) {
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
    for (std::size_t node = 0; node < adjacency.size(); ++node) {
        std::sort(
            adjacency[node].begin(),
            adjacency[node].end(),
            [&nodes, node](int first, int second) {
                const double firstAngle = std::atan2(
                    nodes[first].y - nodes[node].y,
                    nodes[first].x - nodes[node].x);
                const double secondAngle = std::atan2(
                    nodes[second].y - nodes[node].y,
                    nodes[second].x - nodes[node].x);
                return firstAngle < secondAngle;
            });
    }

    std::vector<std::vector<int>> acceptedLoops;
    std::set<std::vector<int>> acceptedLoopKeys;
    const double orientationTolerance =
        frame.tolerance * std::max(1.0, frame.extent);

    const auto orientation = [](const Vec2& first,
                                const Vec2& second,
                                const Vec2& third) {
        return (second.x - first.x) * (third.y - first.y) -
               (second.y - first.y) * (third.x - first.x);
    };
    const auto pointOnSegment =
        [orientation, orientationTolerance](
            const Vec2& point,
            const Vec2& first,
            const Vec2& second) {
            return std::abs(orientation(first, second, point)) <=
                       orientationTolerance &&
                   point.x >=
                       std::min(first.x, second.x) - orientationTolerance &&
                   point.x <=
                       std::max(first.x, second.x) + orientationTolerance &&
                   point.y >=
                       std::min(first.y, second.y) - orientationTolerance &&
                   point.y <=
                       std::max(first.y, second.y) + orientationTolerance;
        };
    const auto segmentsIntersect =
        [orientation, pointOnSegment, orientationTolerance](
            const Vec2& firstStart,
            const Vec2& firstEnd,
            const Vec2& secondStart,
            const Vec2& secondEnd) {
            const double firstSideStart =
                orientation(firstStart, firstEnd, secondStart);
            const double firstSideEnd =
                orientation(firstStart, firstEnd, secondEnd);
            const double secondSideStart =
                orientation(secondStart, secondEnd, firstStart);
            const double secondSideEnd =
                orientation(secondStart, secondEnd, firstEnd);
            const bool properIntersection =
                ((firstSideStart > orientationTolerance &&
                  firstSideEnd < -orientationTolerance) ||
                 (firstSideStart < -orientationTolerance &&
                  firstSideEnd > orientationTolerance)) &&
                ((secondSideStart > orientationTolerance &&
                  secondSideEnd < -orientationTolerance) ||
                 (secondSideStart < -orientationTolerance &&
                  secondSideEnd > orientationTolerance));
            return properIntersection ||
                   pointOnSegment(secondStart, firstStart, firstEnd) ||
                   pointOnSegment(secondEnd, firstStart, firstEnd) ||
                   pointOnSegment(firstStart, secondStart, secondEnd) ||
                   pointOnSegment(firstEnd, secondStart, secondEnd);
        };
    const auto isSimpleLoop =
        [&nodes, segmentsIntersect](const std::vector<int>& loop) {
            for (std::size_t firstEdge = 0;
                 firstEdge < loop.size();
                 ++firstEdge) {
                const std::size_t firstNext =
                    (firstEdge + 1) % loop.size();
                for (std::size_t secondEdge = firstEdge + 1;
                     secondEdge < loop.size();
                     ++secondEdge) {
                    const std::size_t secondNext =
                        (secondEdge + 1) % loop.size();
                    const bool adjacent =
                        firstNext == secondEdge ||
                        secondNext == firstEdge;
                    if (adjacent) {
                        continue;
                    }
                    if (segmentsIntersect(
                            nodes[loop[firstEdge]],
                            nodes[loop[firstNext]],
                            nodes[loop[secondEdge]],
                            nodes[loop[secondNext]])) {
                        return false;
                    }
                }
            }
            return true;
        };
    const auto considerLoop =
        [&nodes, &acceptedLoops, &acceptedLoopKeys, &isSimpleLoop](
            const std::vector<int>& loop) {
            const std::set<int> uniqueNodes(loop.begin(), loop.end());
            if (loop.size() < 3 ||
                uniqueNodes.size() != loop.size() ||
                !isSimpleLoop(loop)) {
                return false;
            }

            std::vector<int> key = loop;
            std::sort(key.begin(), key.end());
            if (!acceptedLoopKeys.insert(key).second) {
                return true;
            }

            double signedAreaTwice = 0.0;
            for (std::size_t index = 0; index < loop.size(); ++index) {
                const Vec2& first = nodes[loop[index]];
                const Vec2& second =
                    nodes[loop[(index + 1) % loop.size()]];
                signedAreaTwice +=
                    first.x * second.y - second.x * first.y;
            }
            if (std::abs(signedAreaTwice) <=
                std::numeric_limits<double>::epsilon()) {
                return false;
            }
            acceptedLoops.push_back(loop);
            return true;
        };
    const double characteristicLength = frame.extent / 0.65;
    const auto closureGuard =
        [&nodes, characteristicLength](
            const std::vector<int>& path,
            double& closedPerimeter) {
            double openLength = 0.0;
            for (std::size_t index = 1; index < path.size(); ++index) {
                openLength += std::sqrt(
                    squaredDistance(
                        nodes[path[index - 1]],
                        nodes[path[index]]));
            }
            const double gap = std::sqrt(
                squaredDistance(nodes[path.front()], nodes[path.back()]));
            closedPerimeter = openLength + gap;
            return closedPerimeter > 0.0 &&
                   characteristicLength > 0.0 &&
                   gap / closedPerimeter <= 0.01 &&
                   gap / characteristicLength <= 0.02;
        };

    std::vector<bool> visitedNodes(nodes.size(), false);
    for (std::size_t seed = 0; seed < nodes.size(); ++seed) {
        if (visitedNodes[seed] || adjacency[seed].empty()) {
            continue;
        }

        std::vector<int> component;
        std::vector<int> pending{static_cast<int>(seed)};
        visitedNodes[seed] = true;
        while (!pending.empty()) {
            const int node = pending.back();
            pending.pop_back();
            component.push_back(node);
            for (int neighbour : adjacency[node]) {
                if (!visitedNodes[neighbour]) {
                    visitedNodes[neighbour] = true;
                    pending.push_back(neighbour);
                }
            }
        }

        std::vector<int> endpoints;
        bool isPath = true;
        for (int node : component) {
            if (adjacency[node].size() == 1) {
                endpoints.push_back(node);
            } else if (adjacency[node].size() != 2) {
                isPath = false;
                break;
            }
        }
        if (!isPath || endpoints.size() != 2) {
            continue;
        }

        std::vector<int> path;
        int previous = -1;
        int current = endpoints.front();
        while (path.size() <= component.size()) {
            path.push_back(current);
            if (current == endpoints.back()) {
                break;
            }
            int next = -1;
            for (int neighbour : adjacency[current]) {
                if (neighbour != previous) {
                    next = neighbour;
                    break;
                }
            }
            if (next < 0) {
                break;
            }
            previous = current;
            current = next;
        }
        if (path.size() != component.size() ||
            path.back() != endpoints.back()) {
            continue;
        }

        double closedPerimeter = 0.0;
        if (closureGuard(path, closedPerimeter) &&
            !considerLoop(path) &&
            path.size() > 3) {
            const double frontTerminalLength = std::sqrt(
                squaredDistance(nodes[path[0]], nodes[path[1]]));
            if (frontTerminalLength / closedPerimeter <= 0.001) {
                std::vector<int> trimmedFront(path.begin() + 1, path.end());
                double trimmedPerimeter = 0.0;
                if (closureGuard(trimmedFront, trimmedPerimeter)) {
                    considerLoop(trimmedFront);
                }
            }

            const double backTerminalLength = std::sqrt(
                squaredDistance(
                    nodes[path[path.size() - 2]],
                    nodes[path.back()]));
            if (backTerminalLength / closedPerimeter <= 0.001) {
                std::vector<int> trimmedBack(path.begin(), path.end() - 1);
                double trimmedPerimeter = 0.0;
                if (closureGuard(trimmedBack, trimmedPerimeter)) {
                    considerLoop(trimmedBack);
                }
            }
        }
    }

    std::set<std::pair<int, int>> visitedDirectedEdges;
    for (const auto& edge : edges) {
        const std::array<std::pair<int, int>, 2> directions{{
            {edge.first, edge.second},
            {edge.second, edge.first}
        }};
        for (const auto& start : directions) {
            if (visitedDirectedEdges.count(start) != 0) {
                continue;
            }

            std::vector<int> loop;
            int from = start.first;
            int to = start.second;
            bool closed = false;

            while (loop.size() <= edges.size() * 2u) {
                const std::pair<int, int> directedEdge{from, to};
                if (visitedDirectedEdges.count(directedEdge) != 0) {
                    closed = directedEdge == start;
                    break;
                }
                visitedDirectedEdges.insert(directedEdge);
                loop.push_back(from);

                const auto& neighbours = adjacency[to];
                const auto incoming =
                    std::find(neighbours.begin(), neighbours.end(), from);
                if (incoming == neighbours.end() || neighbours.empty()) {
                    break;
                }
                const std::size_t incomingIndex =
                    static_cast<std::size_t>(
                        std::distance(neighbours.begin(), incoming));
                const std::size_t nextIndex =
                    (incomingIndex + neighbours.size() - 1u) %
                    neighbours.size();
                from = to;
                to = neighbours[nextIndex];
            }

            if (closed) {
                considerLoop(loop);
            }
        }
    }

    if (acceptedLoops.empty()) {
        error = "Section segments do not contain simple closed contours.";
        return {};
    }

    const double rotation = toRadians(parameters.sliceRotation);
    const double cosineRotation = std::cos(rotation);
    const double sineRotation = std::sin(rotation);

    std::vector<std::vector<Vec2>> contours;
    contours.reserve(acceptedLoops.size());
    for (const std::vector<int>& loop : acceptedLoops) {
        std::vector<Vec2> contour;
        contour.reserve(loop.size());
        for (int nodeIndex : loop) {
            Vec2 point = nodes[nodeIndex];
            if (parameters.invertSection) {
                point.x = -point.x;
            }
            contour.push_back({
                cosineRotation * point.x - sineRotation * point.y,
                sineRotation * point.x + cosineRotation * point.y
            });
        }
        contours.push_back(std::move(contour));
    }

    double minimumX = std::numeric_limits<double>::max();
    double minimumY = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    double maximumY = std::numeric_limits<double>::lowest();
    for (const std::vector<Vec2>& contour : contours) {
        for (const Vec2& point : contour) {
            minimumX = std::min(minimumX, point.x);
            minimumY = std::min(minimumY, point.y);
            maximumX = std::max(maximumX, point.x);
            maximumY = std::max(maximumY, point.y);
        }
    }

    const double sectionSpan =
        std::max(maximumX - minimumX, maximumY - minimumY);
    if (sectionSpan <= frame.tolerance) {
        error = "Section contour is degenerate.";
        return {};
    }

    constexpr double TARGET_DOMAIN_FRACTION = 0.2;
    const double targetSpan =
        TARGET_DOMAIN_FRACTION * std::min(parameters.Lx, parameters.Ly);
    const double scale = targetSpan / sectionSpan;
    const double sectionCentreX = 0.5 * (minimumX + maximumX);
    const double sectionCentreY = 0.5 * (minimumY + maximumY);

    for (std::vector<Vec2>& contour : contours) {
        for (Vec2& point : contour) {
            point.x =
                parameters.Lx / 2.0 + scale * (point.x - sectionCentreX);
            point.y =
                parameters.Ly / 2.0 + scale * (point.y - sectionCentreY);
        }
    }
    return contours;
}

MaskResult GeometryProcessor::generateMask(
    const MaskParameters& parameters) const {
    MaskResult result;

    if (triangles_.empty() || !bounds_.valid) {
        result.error = "Load an STL or OBJ model first.";
        return result;
    }
    if (!finiteParameters(parameters) ||
        parameters.Lx <= 0.0 ||
        parameters.Ly <= 0.0 ||
        parameters.nx < 2 ||
        parameters.ny < 2) {
        result.error = "Mask parameters are invalid.";
        return result;
    }

    const std::size_t width = static_cast<std::size_t>(parameters.nx);
    const std::size_t height = static_cast<std::size_t>(parameters.ny);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        result.error = "Mask dimensions overflow addressable memory.";
        return result;
    }
    if (width * height > MAX_MASK_CELLS) {
        result.error = "Mask exceeds the 10000000-cell allocation limit.";
        return result;
    }

    const SectionFrame frame = sectionFrame(parameters);
    if (!frame.valid) {
        result.error = "Cannot construct the section plane.";
        return result;
    }

    result.contours = buildContours(parameters, frame, result.error);
    if (result.contours.empty()) {
        return result;
    }

    return rasterizeContours(parameters, std::move(result.contours));
}

MaskResult GeometryProcessor::rasterizeContours(
    const MaskParameters& parameters,
    std::vector<std::vector<Vec2>> contours) const {
    MaskResult result;
    if (!finiteParameters(parameters) ||
        parameters.Lx <= 0.0 || parameters.Ly <= 0.0 ||
        parameters.nx < 2 || parameters.ny < 2) {
        result.error = "Mask parameters are invalid.";
        return result;
    }
    if (contours.empty()) {
        result.error = "At least one contour is required.";
        return result;
    }
    const std::size_t width = static_cast<std::size_t>(parameters.nx);
    const std::size_t height = static_cast<std::size_t>(parameters.ny);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        result.error = "Mask dimensions overflow addressable memory.";
        return result;
    }
    if (width * height > MAX_MASK_CELLS) {
        result.error = "Mask exceeds the 10000000-cell allocation limit.";
        return result;
    }
    result.contours = std::move(contours);
    result.cells.assign(width * height, 0);

    const double dx = static_cast<float>(parameters.Lx) /
                      static_cast<float>(parameters.nx);
    const double dy = static_cast<float>(parameters.Ly) /
                      static_cast<float>(parameters.ny);
    const double boundaryRadius = 0.5 * std::hypot(dx, dy);

    for (int j = 0; j < parameters.ny; ++j) {
        for (int i = 0; i < parameters.nx; ++i) {
            const double cellX = (i + 0.5) * dx;
            const double cellY = (j + 0.5) * dy;
            const std::size_t cell =
                static_cast<std::size_t>(j) * width +
                static_cast<std::size_t>(i);

            for (const std::vector<Vec2>& contour : result.contours) {
                for (std::size_t point = 0;
                     point < contour.size();
                     ++point) {
                    const Vec2& first = contour[point];
                    const Vec2& second =
                        contour[(point + 1) % contour.size()];
                    if (distanceToSegment(
                            cellX,
                            cellY,
                            first,
                            second) <= boundaryRadius) {
                        result.cells[cell] = 1;
                        break;
                    }
                }
                if (result.cells[cell] != 0) {
                    break;
                }
            }
        }
    }

    for (int j = 0; j < parameters.ny; ++j) {
        for (int i = 0; i < parameters.nx; ++i) {
            const std::size_t cell =
                static_cast<std::size_t>(j) * width +
                static_cast<std::size_t>(i);
            if (result.cells[cell] != 0) {
                continue;
            }

            const double cellX = (i + 0.5) * dx;
            const double cellY = (j + 0.5) * dy;
            for (const std::vector<Vec2>& contour : result.contours) {
                if (pointInsidePolygon(cellX, cellY, contour)) {
                    result.cells[cell] = 1;
                    break;
                }
            }
        }
    }

    result.solidCellCount = static_cast<int>(
        std::count(result.cells.begin(), result.cells.end(), 1));
    result.success = result.solidCellCount > 0;
    if (!result.success) {
        result.error = "The generated section contains no solid cells.";
    }
    return result;
}

} // namespace maskui
