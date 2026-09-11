#include "Viewport3D.hpp"
#include "VtkFrame.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void writeWord(std::ostream& output, std::uint32_t value) {
    output.put(static_cast<char>((value >> 24u) & 0xffu));
    output.put(static_cast<char>((value >> 16u) & 0xffu));
    output.put(static_cast<char>((value >> 8u) & 0xffu));
    output.put(static_cast<char>(value & 0xffu));
}

void writeFloat(std::ostream& output, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    writeWord(output, bits);
}

float pressureOf(std::size_t i, std::size_t j, std::size_t k) {
    return static_cast<float>(i) + 10.0f * static_cast<float>(j) +
           100.0f * static_cast<float>(k);
}

void writeVolumeFrame(
    const std::filesystem::path& path,
    std::size_t nx,
    std::size_t ny,
    std::size_t nz,
    bool legacyFlatHeader) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "# vtk DataFile Version 3.0\n"
           << "CFD-Solver-2D output, step 12\n"
           << "BINARY\n"
           << "DATASET STRUCTURED_POINTS\n"
           << "DIMENSIONS " << nx + 1 << ' ' << ny + 1 << ' '
           << (legacyFlatHeader ? 1 : nz + 1) << "\n"
           << "ORIGIN 0 0 0\n"
           << "SPACING 0.5 0.25 2\n"
           << "CELL_DATA " << nx * ny * nz << "\n"
           << "SCALARS pressure float 1\n"
           << "LOOKUP_TABLE default\n";
    for (std::size_t k = 0; k < nz; ++k)
        for (std::size_t j = 0; j < ny; ++j)
            for (std::size_t i = 0; i < nx; ++i)
                writeFloat(output, pressureOf(i, j, k));

    output << "\nSCALARS solid int 1\nLOOKUP_TABLE default\n";
    for (std::size_t k = 0; k < nz; ++k)
        for (std::size_t j = 0; j < ny; ++j)
            for (std::size_t i = 0; i < nx; ++i)
                writeWord(output, (i == 2 && j == 1 && k == 1) ? 1u : 0u);

    output << "\nVECTORS velocity float\n";
    for (std::size_t k = 0; k < nz; ++k)
        for (std::size_t j = 0; j < ny; ++j)
            for (std::size_t i = 0; i < nx; ++i) {
                writeFloat(output, static_cast<float>(i));
                writeFloat(output, static_cast<float>(j));
                writeFloat(output, static_cast<float>(k));
            }
    output << '\n';
}

void writeRectilinearVolume(
    const std::filesystem::path& path,
    bool flat) {
    const float xs[4] = {0.0f, 0.1f, 0.5f, 1.5f};
    const float ys[3] = {0.0f, 0.25f, 1.0f};
    const float zs[3] = {0.0f, 0.5f, 2.0f};
    const std::size_t nx = 3;
    const std::size_t ny = 2;
    const std::size_t nz = flat ? 1 : 2;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "# vtk DataFile Version 3.0\n"
           << "CFD-Solver-2D output, step 9\n"
           << "BINARY\n"
           << "DATASET RECTILINEAR_GRID\n"
           << "DIMENSIONS 4 3 " << (flat ? 1 : 3) << "\n"
           << "X_COORDINATES 4 float\n";
    for (float value : xs)
        writeFloat(output, value);
    output << "\nY_COORDINATES 3 float\n";
    for (float value : ys)
        writeFloat(output, value);
    output << "\nZ_COORDINATES " << (flat ? 1 : 3) << " float\n";
    for (std::size_t index = 0; index < (flat ? 1u : 3u); ++index)
        writeFloat(output, zs[index]);
    output << "\nCELL_DATA " << nx * ny * nz << "\n"
           << "SCALARS pressure float 1\n"
           << "LOOKUP_TABLE default\n";
    for (std::size_t k = 0; k < nz; ++k)
        for (std::size_t j = 0; j < ny; ++j)
            for (std::size_t i = 0; i < nx; ++i)
                writeFloat(output, pressureOf(i, j, k));
    output << "\nSCALARS solid int 1\nLOOKUP_TABLE default\n";
    for (std::size_t index = 0; index < nx * ny * nz; ++index)
        writeWord(output, 0u);
    output << "\nVECTORS velocity float\n";
    for (std::size_t index = 0; index < 3u * nx * ny * nz; ++index)
        writeFloat(output, 0.0f);
    output << '\n';
}

bool sameFrame(const maskui::VtkFrame& first, const maskui::VtkFrame& second) {
    if (first.sourcePath != second.sourcePath || first.title != second.title ||
        first.frameNumber != second.frameNumber ||
        first.association != second.association) {
        return false;
    }
    if (first.nx != second.nx || first.ny != second.ny ||
        first.nz != second.nz) {
        return false;
    }
    if (first.originX != second.originX || first.originY != second.originY ||
        first.originZ != second.originZ ||
        first.spacingX != second.spacingX ||
        first.spacingY != second.spacingY ||
        first.spacingZ != second.spacingZ) {
        return false;
    }
    if (first.faceX != second.faceX || first.faceY != second.faceY ||
        first.faceZ != second.faceZ) {
        return false;
    }
    if (first.pressure != second.pressure || first.solid != second.solid ||
        first.pressureFinite != second.pressureFinite ||
        first.velocityFinite != second.velocityFinite) {
        return false;
    }
    if (first.velocityMagnitude.size() != second.velocityMagnitude.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.velocityMagnitude.size(); ++index) {
        const float a = first.velocityMagnitude[index];
        const float b = second.velocityMagnitude[index];
        if (std::isnan(a) != std::isnan(b)) {
            return false;
        }
        if (!std::isnan(a) && a != b) {
            return false;
        }
    }
    if (first.velocity.size() != second.velocity.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.velocity.size(); ++index) {
        if (first.velocity[index].x != second.velocity[index].x ||
            first.velocity[index].y != second.velocity[index].y ||
            first.velocity[index].z != second.velocity[index].z) {
            return false;
        }
    }
    const auto sameRange = [](const maskui::DataRange& a,
                              const maskui::DataRange& b) {
        return a.available == b.available && a.minimum == b.minimum &&
               a.maximum == b.maximum;
    };
    if (!sameRange(first.pressureRange, second.pressureRange) ||
        !sameRange(first.velocityXRange, second.velocityXRange) ||
        !sameRange(first.velocityYRange, second.velocityYRange) ||
        !sameRange(first.velocityMagnitudeRange, second.velocityMagnitudeRange) ||
        !sameRange(first.pressureTrimmedRange, second.pressureTrimmedRange) ||
        !sameRange(
            first.velocityMagnitudeTrimmedRange,
            second.velocityMagnitudeTrimmedRange)) {
        return false;
    }
    if (first.scalarNames != second.scalarNames) {
        return false;
    }
    for (const std::string& name : first.scalarNames) {
        if (first.scalars.at(name) != second.scalars.at(name)) {
            return false;
        }
        if (!sameRange(first.scalarRanges.at(name),
                       second.scalarRanges.at(name)) ||
            !sameRange(first.scalarTrimmedRanges.at(name),
                       second.scalarTrimmedRanges.at(name))) {
            return false;
        }
    }
    return first.warnings == second.warnings &&
           first.restart.hasConfigText == second.restart.hasConfigText &&
           first.restart.restartCapable == second.restart.restartCapable &&
           first.restart.config == second.restart.config;
}

maskui::VtkFrame buildVolume(
    std::size_t nx,
    std::size_t ny,
    std::size_t nz,
    double spacing) {
    maskui::VtkFrame frame;
    frame.association = maskui::VtkDataAssociation::Cell;
    frame.nx = nx;
    frame.ny = ny;
    frame.nz = nz;
    frame.spacingX = spacing;
    frame.spacingY = spacing;
    frame.spacingZ = spacing;
    const std::size_t count = nx * ny * nz;
    frame.pressure.assign(count, 0.0f);
    frame.solid.assign(count, 0u);
    frame.velocity.assign(count, maskui::Velocity{});
    frame.velocityMagnitude.assign(count, 0.0f);
    frame.pressureFinite.assign(count, 1u);
    frame.velocityFinite.assign(count, 1u);
    return frame;
}

void finishVolume(maskui::VtkFrame& frame) {
    frame.velocityMagnitudeRange = maskui::DataRange{};
    for (std::size_t index = 0; index < frame.velocity.size(); ++index) {
        const maskui::Velocity value = frame.velocity[index];
        const float speed = std::sqrt(
            value.x * value.x + value.y * value.y + value.z * value.z);
        frame.velocityMagnitude[index] = speed;
        if (!frame.velocityMagnitudeRange.available) {
            frame.velocityMagnitudeRange.available = true;
            frame.velocityMagnitudeRange.minimum = speed;
            frame.velocityMagnitudeRange.maximum = speed;
        } else {
            frame.velocityMagnitudeRange.minimum =
                std::min(frame.velocityMagnitudeRange.minimum,
                         static_cast<double>(speed));
            frame.velocityMagnitudeRange.maximum =
                std::max(frame.velocityMagnitudeRange.maximum,
                         static_cast<double>(speed));
        }
    }
}

void testVolumeParsing(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "solution_12.vtk";
    writeVolumeFrame(path, 8, 6, 4, false);
    const maskui::VtkFrame frame = maskui::VtkFrameParser::parse(path);

    check(frame.nx == 8 && frame.ny == 6 && frame.nz == 4,
          "a volume frame did not come back with nx=8 ny=6 nz=4");
    check(frame.volumetric(), "a four-plane frame did not report as a volume");
    check(std::abs(frame.spacingX - 0.5) < 1e-12 &&
              std::abs(frame.spacingY - 0.25) < 1e-12 &&
              std::abs(frame.spacingZ - 2.0) < 1e-12,
          "the volume spacings are not what SPACING says");
    check(std::abs(frame.spanZ() - 8.0) < 1e-12,
          "the depth of a four-plane frame with dz=2 is not 8");
    check(frame.pressure.size() == 8u * 6u * 4u &&
              frame.velocity.size() == 8u * 6u * 4u &&
              frame.solid.size() == 8u * 6u * 4u &&
              frame.velocityMagnitude.size() == 8u * 6u * 4u &&
              frame.pressureFinite.size() == 8u * 6u * 4u &&
              frame.velocityFinite.size() == 8u * 6u * 4u,
          "the volume arrays are not nx*ny*nz long");

    check(frame.cellIndex(5, 3, 2) == (2u * 6u + 3u) * 8u + 5u,
          "cellIndex(i, j, k) is not (k*ny + j)*nx + i");
    check(frame.cellIndex(5, 3) == frame.cellIndex(5, 3, 0),
          "the two-argument cellIndex stopped meaning the k = 0 plane");

    bool valuesMatch = true;
    for (std::size_t k = 0; k < frame.nz; ++k)
        for (std::size_t j = 0; j < frame.ny; ++j)
            for (std::size_t i = 0; i < frame.nx; ++i) {
                const std::size_t index = frame.cellIndex(i, j, k);
                if (frame.pressure[index] != pressureOf(i, j, k) ||
                    frame.velocity[index].x != static_cast<float>(i) ||
                    frame.velocity[index].y != static_cast<float>(j) ||
                    frame.velocity[index].z != static_cast<float>(k)) {
                    valuesMatch = false;
                }
            }
    check(valuesMatch, "a sampled cell of the volume holds the wrong values");
    check(frame.solid[frame.cellIndex(2, 1, 1)] == 1u &&
              frame.solid[frame.cellIndex(2, 1, 0)] == 0u,
          "the solid mask of the volume is in the wrong order");

    check(std::abs(frame.cellFront(2) - 4.0) < 1e-12 &&
              std::abs(frame.cellBack(2) - 6.0) < 1e-12 &&
              std::abs(frame.cellCentreZ(2) - 5.0) < 1e-12,
          "the z faces of a uniform volume are wrong");
    check(frame.planeAt(5.0) == 2 && frame.planeAt(-3.0) == 0 &&
              frame.planeAt(99.0) == 3,
          "planeAt did not land in the plane that contains z");

    const float speed = frame.velocityMagnitude[frame.cellIndex(3, 2, 1)];
    check(std::abs(speed - std::sqrt(9.0f + 4.0f + 1.0f)) < 1e-5f,
          "the speed of a volume cell does not include the third component");
}

void testFlatFrameUnchanged(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "solution_13.vtk";
    writeVolumeFrame(path, 4, 3, 1, true);
    const maskui::VtkFrame frame = maskui::VtkFrameParser::parse(path);
    check(frame.nx == 4 && frame.ny == 3 && frame.nz == 1,
          "a frame with the old flat header did not load as one plane deep");
    check(!frame.volumetric(), "a flat frame reported as a volume");
    check(frame.pressure.size() == 12u,
          "a flat frame changed size");
    check(frame.faceZ.empty(),
          "a flat structured frame invented a z face array");
    check(std::abs(frame.spacingZ - 2.0) < 1e-12,
          "a flat frame lost the SPACING z the file carries");
    check(frame.cellIndex(3, 2) == 11u,
          "the flat cell index changed");
    check(frame.pressure[frame.cellIndex(3, 2)] == pressureOf(3, 2, 0),
          "a flat frame came back with the wrong values");
}

void testRectilinearVolume(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "solution_14.vtk";
    writeRectilinearVolume(path, false);
    const maskui::VtkFrame frame = maskui::VtkFrameParser::parse(path);
    check(frame.rectilinear() && frame.nx == 3 && frame.ny == 2 &&
              frame.nz == 2,
          "a RECTILINEAR_GRID volume came back the wrong size");
    check(frame.faceZ.size() == 3u,
          "Z_COORDINATES did not become the z face array");
    check(std::abs(frame.cellFront(1) - 0.5) < 1e-6 &&
              std::abs(frame.cellBack(1) - 2.0) < 1e-6,
          "the z faces of a stretched volume are wrong");
    check(std::abs(frame.spanZ() - 2.0) < 1e-6,
          "the depth of a stretched volume is not what its coordinates say");
    check(frame.planeAt(0.25) == 0 && frame.planeAt(1.0) == 1,
          "planeAt does not work on a stretched volume");

    const std::filesystem::path flatPath = root / "solution_15.vtk";
    writeRectilinearVolume(flatPath, true);
    const maskui::VtkFrame flat = maskui::VtkFrameParser::parse(flatPath);
    check(flat.rectilinear() && flat.nx == 3 && flat.ny == 2 && flat.nz == 1,
          "a flat RECTILINEAR_GRID frame stopped loading");
    check(flat.faceZ.empty() && std::abs(flat.spacingZ - 1.0) < 1e-12,
          "a flat RECTILINEAR_GRID frame did not fall back to a unit depth");
    check(std::abs(flat.spanX() - 1.5) < 1e-6,
          "a flat stretched frame lost its x extent");
}

void testSlices(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "solution_16.vtk";
    writeVolumeFrame(path, 8, 6, 4, false);
    const maskui::VtkFrame volume = maskui::VtkFrameParser::parse(path);

    const maskui::VtkFrame zSlice =
        maskui::extractSlice(volume, maskui::SliceAxis::Z, 2);
    check(zSlice.nx == 8 && zSlice.ny == 6 && zSlice.nz == 1,
          "a z slice has the wrong in-plane size");
    check(std::abs(zSlice.originZ - 4.0) < 1e-12 &&
              std::abs(zSlice.spacingZ - 2.0) < 1e-12,
          "a z slice does not sit where its plane is");
    bool zMatches = true;
    for (std::size_t j = 0; j < 6; ++j)
        for (std::size_t i = 0; i < 8; ++i) {
            const std::size_t target = zSlice.cellIndex(i, j);
            const std::size_t source = volume.cellIndex(i, j, 2);
            if (zSlice.pressure[target] != volume.pressure[source] ||
                zSlice.velocity[target].x != volume.velocity[source].x ||
                zSlice.velocity[target].y != volume.velocity[source].y ||
                zSlice.velocity[target].z != volume.velocity[source].z) {
                zMatches = false;
            }
        }
    check(zMatches, "a z slice does not hold the plane it was asked for");

    const maskui::VtkFrame ySlice =
        maskui::extractSlice(volume, maskui::SliceAxis::Y, 3);
    check(ySlice.nx == 8 && ySlice.ny == 4 && ySlice.nz == 1,
          "a y slice is not (x, z)");
    check(std::abs(ySlice.spacingX - 0.5) < 1e-12 &&
              std::abs(ySlice.spacingY - 2.0) < 1e-12,
          "a y slice did not take its spacings from x and z");
    bool yMatches = true;
    for (std::size_t k = 0; k < 4; ++k)
        for (std::size_t i = 0; i < 8; ++i) {
            const std::size_t target = ySlice.cellIndex(i, k);
            const std::size_t source = volume.cellIndex(i, 3, k);
            if (ySlice.pressure[target] != volume.pressure[source] ||
                ySlice.velocity[target].x != volume.velocity[source].x ||
                ySlice.velocity[target].y != volume.velocity[source].z ||
                ySlice.velocity[target].z != volume.velocity[source].y) {
                yMatches = false;
            }
        }
    check(yMatches, "a y slice did not permute (u, v, w) into (u, w, v)");

    const maskui::VtkFrame xSlice =
        maskui::extractSlice(volume, maskui::SliceAxis::X, 5);
    check(xSlice.nx == 6 && xSlice.ny == 4 && xSlice.nz == 1,
          "an x slice is not (y, z)");
    check(std::abs(xSlice.spacingX - 0.25) < 1e-12 &&
              std::abs(xSlice.spacingY - 2.0) < 1e-12,
          "an x slice did not take its spacings from y and z");
    bool xMatches = true;
    for (std::size_t k = 0; k < 4; ++k)
        for (std::size_t j = 0; j < 6; ++j) {
            const std::size_t target = xSlice.cellIndex(j, k);
            const std::size_t source = volume.cellIndex(5, j, k);
            if (xSlice.pressure[target] != volume.pressure[source] ||
                xSlice.velocity[target].x != volume.velocity[source].y ||
                xSlice.velocity[target].y != volume.velocity[source].z ||
                xSlice.velocity[target].z != volume.velocity[source].x) {
                xMatches = false;
            }
        }
    check(xMatches, "an x slice did not permute (u, v, w) into (v, w, u)");
    check(xSlice.solid[xSlice.cellIndex(1, 1)] == 0u,
          "an x slice picked up the wrong solid column");

    const maskui::VtkFrame zeroSlice =
        maskui::extractSlice(volume, maskui::SliceAxis::Z, 1);
    check(zeroSlice.solid[zeroSlice.cellIndex(2, 1)] == 1u,
          "a z slice lost the solid cell in its plane");

    const std::filesystem::path flatPath = root / "solution_17.vtk";
    writeVolumeFrame(flatPath, 5, 4, 1, true);
    const maskui::VtkFrame flat = maskui::VtkFrameParser::parse(flatPath);
    check(sameFrame(flat, maskui::extractSlice(flat, maskui::SliceAxis::Z, 0)),
          "slicing a flat frame on z did not return the frame itself");

    const std::filesystem::path stretched = root / "solution_18.vtk";
    writeRectilinearVolume(stretched, false);
    const maskui::VtkFrame uneven = maskui::VtkFrameParser::parse(stretched);
    const maskui::VtkFrame unevenSlice =
        maskui::extractSlice(uneven, maskui::SliceAxis::Y, 1);
    check(unevenSlice.rectilinear(),
          "a slice of a stretched volume stopped being stretched");
    check(std::abs(unevenSlice.cellRight(2) - unevenSlice.cellLeft(2) - 1.0) <
              1e-6,
          "a slice of a stretched volume lost its x face positions");
    check(std::abs(unevenSlice.cellTop(1) - unevenSlice.cellBottom(1) - 1.5) <
              1e-6,
          "a y slice did not map the z faces onto its own y axis");
    check(sameFrame(
              maskui::extractSlice(uneven, maskui::SliceAxis::Z, 0),
              maskui::extractSlice(
                  maskui::extractSlice(uneven, maskui::SliceAxis::Z, 0),
                  maskui::SliceAxis::Z,
                  0)),
          "slicing a slice on z is not the identity");
}

void testMarchingCubes() {
    const std::size_t size = 48;
    const float low = -1.0f;
    const float step = 2.0f / static_cast<float>(size);
    std::vector<float> axis(size);
    for (std::size_t index = 0; index < size; ++index) {
        axis[index] = low + step * (static_cast<float>(index) + 0.5f);
    }

    maskui::ScalarVolume volume;
    volume.nx = size;
    volume.ny = size;
    volume.nz = size;
    volume.values.assign(size * size * size, 0.0f);
    for (std::size_t k = 0; k < size; ++k)
        for (std::size_t j = 0; j < size; ++j)
            for (std::size_t i = 0; i < size; ++i) {
                const float x = axis[i];
                const float y = axis[j];
                const float z = axis[k];
                volume.values[volume.index(i, j, k)] =
                    std::sqrt(x * x + y * y + z * z);
            }

    const float radius = 0.7f;
    const maskui::SurfaceMesh mesh =
        maskui::marchingCubes(volume, axis, axis, axis, radius);
    check(mesh.triangleCount() > 1000u,
          "marching cubes on a sphere produced almost no triangles");
    check(maskui::surfaceIsClosed(mesh, 1.0e-5f),
          "the marching cubes sphere is not a closed surface");

    const double area = maskui::surfaceArea(mesh);
    const double exact = 4.0 * 3.14159265358979323846 * radius * radius;
    check(std::abs(area - exact) / exact < 0.03,
          "the marching cubes sphere area is more than 3% off 4 pi r^2: " +
              std::to_string(area) + " against " + std::to_string(exact));

    double worst = 0.0;
    for (std::size_t vertex = 0; vertex < mesh.triangleCount() * 3u; ++vertex) {
        const std::size_t base = vertex * 3u;
        const float x = mesh.positions[base];
        const float y = mesh.positions[base + 1u];
        const float z = mesh.positions[base + 2u];
        worst = std::max<double>(
            worst,
            std::abs(std::sqrt(x * x + y * y + z * z) - radius));
    }
    check(worst < 1.0e-3,
          "a marching cubes vertex is not on the isosurface, worst " +
              std::to_string(worst));

    maskui::ScalarVolume flat = volume;
    flat.nz = 1;
    flat.values.resize(size * size);
    check(maskui::marchingCubes(flat, axis, axis, axis, radius)
              .triangleCount() == 0u,
          "marching cubes produced triangles from a single plane");
}

void testCriteria() {
    const std::size_t size = 12;
    const double spacing = 0.1;
    const double omega = 2.0;

    maskui::VtkFrame rotation = buildVolume(size, size, size, spacing);
    for (std::size_t k = 0; k < size; ++k)
        for (std::size_t j = 0; j < size; ++j)
            for (std::size_t i = 0; i < size; ++i) {
                const float x = static_cast<float>(rotation.cellCentreX(i));
                const float y = static_cast<float>(rotation.cellCentreY(j));
                rotation.velocity[rotation.cellIndex(i, j, k)] = {
                    static_cast<float>(-omega * y),
                    static_cast<float>(omega * x),
                    0.0f
                };
            }
    finishVolume(rotation);

    const maskui::ScalarVolume solidBody = maskui::sampleVolumeField(
        rotation, maskui::VolumeField::QCriterion, std::string());
    double smallest = 1.0e30;
    for (std::size_t k = 0; k < size; ++k)
        for (std::size_t j = 0; j < size; ++j)
            for (std::size_t i = 0; i < size; ++i) {
                smallest = std::min<double>(smallest, solidBody.at(i, j, k));
            }
    check(smallest > 0.0,
          "the Q criterion of a solid-body rotation is not positive");
    check(std::abs(solidBody.at(6, 6, 6) - omega * omega) < 1e-4,
          "the Q criterion of a solid-body rotation is not omega squared: " +
              std::to_string(solidBody.at(6, 6, 6)));

    const maskui::ScalarVolume spin = maskui::sampleVolumeField(
        rotation, maskui::VolumeField::Vorticity, std::string());
    check(std::abs(spin.at(6, 6, 6) - 2.0 * omega) < 1e-4,
          "the vorticity of a solid-body rotation is not twice omega");

    maskui::VtkFrame shear = buildVolume(size, size, size, spacing);
    for (std::size_t k = 0; k < size; ++k)
        for (std::size_t j = 0; j < size; ++j)
            for (std::size_t i = 0; i < size; ++i) {
                const float y = static_cast<float>(shear.cellCentreY(j));
                shear.velocity[shear.cellIndex(i, j, k)] = {
                    static_cast<float>(3.0 * y), 0.0f, 0.0f};
            }
    finishVolume(shear);

    const maskui::ScalarVolume pure = maskui::sampleVolumeField(
        shear, maskui::VolumeField::QCriterion, std::string());
    double largest = 0.0;
    for (std::size_t k = 0; k < size; ++k)
        for (std::size_t j = 0; j < size; ++j)
            for (std::size_t i = 0; i < size; ++i) {
                largest = std::max<double>(largest, std::abs(pure.at(i, j, k)));
            }
    check(largest < 1e-4,
          "the Q criterion of a pure shear is not about zero: " +
              std::to_string(largest));
}

void testVortices() {
    const std::size_t size = 40;
    const double spacing = 1.0 / static_cast<double>(size);
    const double sigma = 0.12;
    auto frame = std::make_shared<maskui::VtkFrame>(
        buildVolume(size, size, size, spacing));
    for (std::size_t k = 0; k < size; ++k)
        for (std::size_t j = 0; j < size; ++j)
            for (std::size_t i = 0; i < size; ++i) {
                const double x = frame->cellCentreX(i) - 0.5;
                const double y = frame->cellCentreY(j) - 0.5;
                const double decay =
                    std::exp(-(x * x + y * y) / (2.0 * sigma * sigma));
                frame->velocity[frame->cellIndex(i, j, k)] = {
                    static_cast<float>(-y * decay),
                    static_cast<float>(x * decay),
                    0.0f
                };
            }
    finishVolume(*frame);

    const maskui::ScalarVolume criterion = maskui::sampleVolumeField(
        *frame, maskui::VolumeField::QCriterion, std::string());
    check(criterion.range.available && criterion.range.maximum > 0.0,
          "a concentrated vortex produced no positive Q criterion");
    check(criterion.at(size / 2, size / 2, size / 2) >
              criterion.at(2, 2, size / 2),
          "the Q criterion of a concentrated vortex does not peak on its axis");

    const maskui::VortexCore core = maskui::vortexCoreLines(
        *frame,
        criterion,
        static_cast<float>(0.25 * criterion.range.maximum));
    check(core.segments.size() >= 3u * 2u * 10u,
          "the vortex core of a straight vortex was not found");
    double offAxis = 0.0;
    for (std::size_t index = 0; index + 2u < core.segments.size(); index += 3u) {
        offAxis = std::max<double>(
            offAxis, std::abs(core.segments[index] - 0.5));
        offAxis = std::max<double>(
            offAxis, std::abs(core.segments[index + 1u] - 0.5));
    }
    check(offAxis < 2.0 * spacing,
          "the vortex core lines are not on the axis of the vortex: " +
              std::to_string(offAxis));

    maskui::Viewport3D viewport;
    viewport.setFrame(frame);
    maskui::Viewport3DSettings settings = viewport.settings();
    settings.showSlices = false;
    settings.showSolid = false;
    settings.showVortices = true;
    settings.vortexLevel = 0.25f;
    viewport.setSettings(settings);
    check(viewport.triangleCount() > 100u,
          "the Q criterion isosurface of a concentrated vortex is empty");
}

void testStreamlines() {
    const std::size_t size = 16;
    const double spacing = 1.0 / static_cast<double>(size);

    maskui::VtkFrame uniform = buildVolume(size, size, size, spacing);
    for (maskui::Velocity& value : uniform.velocity) {
        value = {1.0f, 0.0f, 0.0f};
    }
    finishVolume(uniform);

    maskui::StreamlineOptions options;
    options.steps = 50;
    options.stepTime = 0.01f;
    const maskui::Streamline straight = maskui::traceStreamline(
        uniform, {0.2f, 0.5f, 0.5f}, options);
    check(straight.pointCount() == 51u,
          "a streamline in a uniform field stopped early");
    check(std::abs(straight.length() - 0.5) < 1e-4,
          "a streamline in a uniform field is the wrong length: " +
              std::to_string(straight.length()));
    double drift = 0.0;
    for (std::size_t point = 0; point < straight.pointCount(); ++point) {
        drift = std::max<double>(
            drift, std::abs(straight.points[point * 3u + 1u] - 0.5));
        drift = std::max<double>(
            drift, std::abs(straight.points[point * 3u + 2u] - 0.5));
    }
    check(drift < 1e-6,
          "a streamline in a uniform field is not straight");

    const double omega = 1.0;
    maskui::VtkFrame swirl = buildVolume(size, size, size, spacing);
    for (std::size_t k = 0; k < size; ++k)
        for (std::size_t j = 0; j < size; ++j)
            for (std::size_t i = 0; i < size; ++i) {
                const double x = swirl.cellCentreX(i) - 0.5;
                const double y = swirl.cellCentreY(j) - 0.5;
                swirl.velocity[swirl.cellIndex(i, j, k)] = {
                    static_cast<float>(-omega * y),
                    static_cast<float>(omega * x),
                    0.0f
                };
            }
    finishVolume(swirl);

    maskui::StreamlineOptions loop;
    loop.steps = 400;
    loop.stepTime = static_cast<float>(2.0 * 3.14159265358979323846 / 400.0);
    const maskui::Streamline circle = maskui::traceStreamline(
        swirl, {0.75f, 0.5f, 0.5f}, loop);
    check(circle.pointCount() == 401u,
          "a streamline in a solid-body rotation left the domain");
    const std::size_t last = (circle.pointCount() - 1u) * 3u;
    const double closeX = circle.points[last] - circle.points[0];
    const double closeY = circle.points[last + 1u] - circle.points[1];
    const double closeZ = circle.points[last + 2u] - circle.points[2];
    check(std::sqrt(closeX * closeX + closeY * closeY + closeZ * closeZ) < 1e-3,
          "a streamline in a solid-body rotation did not close on itself");
    double radiusError = 0.0;
    for (std::size_t point = 0; point < circle.pointCount(); ++point) {
        const double x = circle.points[point * 3u] - 0.5;
        const double y = circle.points[point * 3u + 1u] - 0.5;
        radiusError = std::max(
            radiusError, std::abs(std::sqrt(x * x + y * y) - 0.25));
    }
    check(radiusError < 1e-3,
          "a streamline in a solid-body rotation is not a circle");

    check(!maskui::streamlineSeedPoints(uniform, 64).empty(),
          "no streamline seeds were placed in an open volume");
}

void testPicking() {
    const std::size_t size = 16;
    const double spacing = 1.0 / static_cast<double>(size);
    auto frame = std::make_shared<maskui::VtkFrame>(
        buildVolume(size, size, size, spacing));
    const std::size_t targetI = 11;
    const std::size_t targetJ = 4;
    const std::size_t targetK = 9;
    frame->solid[frame->cellIndex(targetI, targetJ, targetK)] = 1u;
    finishVolume(*frame);

    maskui::Viewport3D viewport;
    viewport.setFrame(frame);
    const sf::FloatRect area({0.0f, 0.0f}, {800.0f, 600.0f});
    const float centreX = 400.0f;
    const float centreY = 300.0f;

    const float angles[6][2] = {
        {0.0f, 0.0f},
        {0.6f, 0.4f},
        {-1.2f, 0.9f},
        {2.4f, -0.7f},
        {3.0f, 0.15f},
        {-2.2f, -1.1f}
    };
    for (const auto& angle : angles) {
        viewport.camera().targetX =
            static_cast<float>(frame->cellCentreX(targetI));
        viewport.camera().targetY =
            static_cast<float>(frame->cellCentreY(targetJ));
        viewport.camera().targetZ =
            static_cast<float>(frame->cellCentreZ(targetK));
        viewport.camera().distance = 3.0f;
        viewport.camera().yaw = angle[0];
        viewport.camera().pitch = angle[1];
        viewport.camera().orthographic = false;
        const maskui::Viewport3D::Pick pick =
            viewport.pickAt(area, centreX, centreY);
        check(pick.hit && pick.solidHit && pick.i == targetI &&
                  pick.j == targetJ && pick.k == targetK,
              "a ray aimed at the solid cell missed it at yaw " +
                  std::to_string(angle[0]) + " pitch " +
                  std::to_string(angle[1]) + ", got " +
                  std::to_string(pick.i) + "," + std::to_string(pick.j) + "," +
                  std::to_string(pick.k) + " solid " +
                  std::to_string(static_cast<int>(pick.solidHit)));

        viewport.camera().orthographic = true;
        const maskui::Viewport3D::Pick orthographic =
            viewport.pickAt(area, centreX, centreY);
        check(orthographic.solidHit && orthographic.i == targetI &&
                  orthographic.j == targetJ && orthographic.k == targetK,
              "an orthographic ray aimed at the solid cell missed it");
    }

    viewport.camera().orthographic = false;
    viewport.camera().yaw = 0.0f;
    viewport.camera().pitch = 0.0f;
    viewport.camera().targetX = static_cast<float>(frame->cellCentreX(2));
    viewport.camera().targetY = static_cast<float>(frame->cellCentreY(2));
    viewport.camera().targetZ = static_cast<float>(frame->cellCentreZ(8));
    const maskui::Viewport3D::Pick wall =
        viewport.pickAt(area, centreX, centreY);
    check(wall.hit && !wall.solidHit && wall.face == 5,
          "a ray down the z axis did not report the back wall, face " +
              std::to_string(wall.face));
    check(wall.i == 2 && wall.j == 2,
          "the wall pick did not report the cell the ray entered");

    viewport.camera().yaw = 3.14159265358979323846f;
    const maskui::Viewport3D::Pick front =
        viewport.pickAt(area, centreX, centreY);
    check(front.hit && front.face == 4,
          "a ray from behind did not report the front wall, face " +
              std::to_string(front.face));

    const maskui::Viewport3D::Pick missed =
        viewport.pickAt(area, 5.0f, 5.0f);
    check(!missed.solidHit,
          "a ray into the corner of the viewport claimed a solid hit");
}

void testViewportBuilds() {
    const std::size_t size = 12;
    const double spacing = 1.0 / static_cast<double>(size);
    auto frame = std::make_shared<maskui::VtkFrame>(
        buildVolume(size, size, size, spacing));
    for (std::size_t k = 0; k < size; ++k)
        for (std::size_t j = 0; j < size; ++j)
            for (std::size_t i = 0; i < size; ++i) {
                const double x = frame->cellCentreX(i) - 0.5;
                const double y = frame->cellCentreY(j) - 0.5;
                frame->velocity[frame->cellIndex(i, j, k)] = {
                    static_cast<float>(-y),
                    static_cast<float>(x),
                    0.1f
                };
                frame->pressure[frame->cellIndex(i, j, k)] =
                    static_cast<float>(x * x + y * y);
                if (i >= 5 && i <= 6 && j >= 5 && j <= 6 && k >= 5 && k <= 6) {
                    frame->solid[frame->cellIndex(i, j, k)] = 1u;
                }
            }
    finishVolume(*frame);
    frame->pressureRange = maskui::DataRange{true, 0.0, 0.5};
    frame->pressureTrimmedRange = frame->pressureRange;

    maskui::Viewport3D viewport;
    viewport.setFrame(frame);
    viewport.frameAll();
    check(std::abs(viewport.camera().targetX - 0.5f) < 1e-5f &&
              std::abs(viewport.camera().targetZ - 0.5f) < 1e-5f,
          "frameAll did not centre the camera on the box");
    check(viewport.camera().distance > 0.8f,
          "frameAll did not back the camera away from the box");

    maskui::Viewport3DSettings settings = viewport.settings();
    settings.showSolid = true;
    settings.showSlices = false;
    settings.showBox = false;
    viewport.setSettings(settings);
    check(viewport.triangleCount() == 48u,
          "the outer faces of a two by two by two block are not 24 quads: " +
              std::to_string(viewport.triangleCount()));

    settings.showBox = true;
    settings.showSlices = true;
    settings.sliceX = true;
    settings.sliceY = true;
    settings.sliceZ = true;
    settings.sliceIndexX = 6;
    settings.sliceIndexY = 6;
    settings.sliceIndexZ = 6;
    settings.showIsosurface = true;
    settings.isoField = maskui::VolumeField::Pressure;
    settings.isoLevel = 0.4f;
    settings.showVortices = true;
    settings.vortexLevel = 0.05f;
    settings.showStreamlines = true;
    settings.streamlineSeeds = 40;
    settings.streamlineSteps = 30;
    settings.animateTracers = true;
    settings.showGrid = true;
    viewport.setSettings(settings);

    check(viewport.triangleCount() > 0u,
          "the viewport built no triangles for a volume with a body in it");
    check(viewport.lineCount() > 0u,
          "the viewport built no lines for a volume");

    const std::size_t before = viewport.triangleCount();
    viewport.advance(0.5f);
    check(viewport.triangleCount() == before,
          "advancing the tracers changed the triangle count");

    settings.showSolid = false;
    settings.showSlices = false;
    settings.showIsosurface = false;
    settings.showVortices = false;
    viewport.setSettings(settings);
    check(viewport.triangleCount() == 0u,
          "turning every surface off left triangles behind");

    viewport.setView(2, false);
    check(std::abs(viewport.camera().yaw) < 1e-6f &&
              std::abs(viewport.camera().pitch) < 1e-6f,
          "the front view is not down the z axis");
    viewport.setView(1, false);
    check(viewport.camera().pitch > 1.5f,
          "the top view does not look down");
    const float distance = viewport.camera().distance;
    viewport.zoom(1.0f);
    check(viewport.camera().distance < distance,
          "the wheel did not move the camera closer");
    viewport.orbit(10.0f, 0.0f);
    check(std::abs(viewport.camera().yaw) > 1e-6f,
          "dragging did not orbit the camera");
}

} // namespace

int main() {
    const auto unique = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mask-ui-volume-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);

    testVolumeParsing(root);
    testFlatFrameUnchanged(root);
    testRectilinearVolume(root);
    testSlices(root);
    testMarchingCubes();
    testCriteria();
    testVortices();
    testStreamlines();
    testPicking();
    testViewportBuilds();

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    if (failures != 0) {
        std::cerr << failures << " volume check(s) failed\n";
        return 1;
    }
    return 0;
}
