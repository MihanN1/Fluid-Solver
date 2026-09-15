#include "VtkFrame.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

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

void writeFrame(const std::filesystem::path& path,
                std::size_t pRawCount,
                bool early = false) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "# vtk DataFile Version 3.0\n"
           << "CFD-Solver-2D output, step 20\n"
           << "BINARY\n"
           << "DATASET STRUCTURED_POINTS\n"
           << "DIMENSIONS 3 3 1\n"
           << "ORIGIN 0 0 0\n"
           << "SPACING 0.5 0.5 1\n"
           << "CELL_DATA 4\n"
           << "SCALARS pressure float 1\n"
           << "LOOKUP_TABLE default\n";
    for (int value = 0; value < 4; ++value) {
        writeFloat(output, static_cast<float>(value));
    }

    if (early) {
        output << "\nSCALARS phase float 1\nLOOKUP_TABLE default\n";
        for (int value = 0; value < 4; ++value) {
            writeFloat(output, 0.25f * static_cast<float>(value));
        }
    }
    output << "\nSCALARS solid int 1\nLOOKUP_TABLE default\n";
    for (int value = 0; value < 4; ++value) {
        writeWord(output, value == 0 ? 1u : 0u);
    }
    output << "\nVECTORS velocity float\n";
    for (int value = 0; value < 12; ++value) {
        writeFloat(output, 0.25f * static_cast<float>(value));
    }

    const std::string config =
        "formatVersion=1\n"
        "Lx=1\nLy=1\nnx=2\nny=2\n"
        "totalTime=10\n"
        "ro=1.225\n"
        "dtSafety=0.9\n"
        "useCuda=1\n"
        "restartTime=4.5\n"
        "restartStep=20\n"
        "restartDt=0.01\n";
    output << "\nFIELD RestartData 4\n"
           << "configText 1 " << config.size() << " char\n";
    output.write(config.data(), static_cast<std::streamsize>(config.size()));
    output << "\nuFace 1 6 float\n";
    for (int value = 0; value < 6; ++value) {
        writeFloat(output, 0.0f);
    }
    output << "\nvFace 1 6 float\n";
    for (int value = 0; value < 6; ++value) {
        writeFloat(output, 0.0f);
    }
    output << "\npRaw 1 " << pRawCount << " float\n";
    for (std::size_t value = 0; value < pRawCount; ++value) {
        writeFloat(output, 0.0f);
    }
    output << '\n';
}

void writeRectilinearFrame(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const float xs[4] = {0.0f, 0.1f, 0.5f, 1.5f};
    const float ys[3] = {0.0f, 0.25f, 1.0f};
    output << "# vtk DataFile Version 3.0\n"
           << "CFD-Solver-2D output, step 7\n"
           << "BINARY\n"
           << "DATASET RECTILINEAR_GRID\n"
           << "DIMENSIONS 4 3 1\n"
           << "X_COORDINATES 4 float\n";
    for (float value : xs)
        writeFloat(output, value);
    output << "\nY_COORDINATES 3 float\n";
    for (float value : ys)
        writeFloat(output, value);
    output << "\nZ_COORDINATES 1 float\n";
    writeFloat(output, 0.0f);
    output << "\nCELL_DATA 6\n"
           << "SCALARS pressure float 1\n"
           << "LOOKUP_TABLE default\n";
    for (int value = 0; value < 6; ++value)
        writeFloat(output, static_cast<float>(value));
    output << "\nSCALARS solid int 1\nLOOKUP_TABLE default\n";
    for (int value = 0; value < 6; ++value)
        writeWord(output, 0u);
    output << "\nVECTORS velocity float\n";
    for (int value = 0; value < 18; ++value)
        writeFloat(output, 0.0f);
    output << '\n';
}


// Exactly what the compressible writer lays down: pressure, then density, then
// the mask, then velocity, and a restart block of conserved variables rather
// than the face velocities an incompressible run leaves. Both halves of that
// used to be lost on the way in - density was in the file and unreachable from
// the window, and every frame of every compressible run was called
// uncontinuable, with a warning on it, while the solver itself restarted from
// the same file perfectly well.
void writeCompressibleFrame(
    const std::filesystem::path& path,
    std::size_t conservedCount = 4) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "# vtk DataFile Version 3.0\n"
           << "Fluid Solver output, step 30\n"
           << "BINARY\n"
           << "DATASET STRUCTURED_POINTS\n"
           << "DIMENSIONS 3 3 1\n"
           << "ORIGIN 0 0 0\n"
           << "SPACING 0.5 0.5 1\n"
           << "CELL_DATA 4\n"
           << "SCALARS pressure float 1\nLOOKUP_TABLE default\n";
    for (int value = 0; value < 4; ++value)
        writeFloat(output, 101325.0f + 100.0f * static_cast<float>(value));
    output << "\nSCALARS density float 1\nLOOKUP_TABLE default\n";
    for (int value = 0; value < 4; ++value)
        writeFloat(output, 1.225f + 0.1f * static_cast<float>(value));
    output << "\nSCALARS solid unsigned_char 1\nLOOKUP_TABLE default\n";
    for (int value = 0; value < 4; ++value)
        output.put('\0');
    output << "\nVECTORS velocity float\n";
    for (int value = 0; value < 12; ++value)
        writeFloat(output, 0.0f);

    const std::string config =
        "formatVersion=1\n"
        "regime=compressible\n"
        "Lx=1\nLy=1\nnx=2\nny=2\nnz=1\n"
        "totalTime=10\n"
        "restartTime=4.5\n"
        "restartStep=30\n"
        "restartDt=0.01\n";
    output << "\nFIELD RestartData 6\n"
           << "configText 1 " << config.size() << " char\n";
    output.write(config.data(), static_cast<std::streamsize>(config.size()));
    const char* names[5] = {
        "stateRho", "stateRhoU", "stateRhoV", "stateRhoW", "stateRhoE"};
    for (const char* name : names) {
        output << "\n" << name << " 1 " << conservedCount << " float\n";
        for (std::size_t value = 0; value < conservedCount; ++value)
            writeFloat(output, 1.0f);
    }
    output << '\n';
}

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main() {
    if (maskui::VtkFrameParser::frameNumberFromFilename(
            "solution_20.vtk") != 20 ||
        maskui::VtkFrameParser::frameNumberFromFilename(
            "solution_3_6.vtk") != 6) {
        return fail("new or continued solver filename was not recognized");
    }
    const auto unique = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mask-ui-vtk-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    const std::filesystem::path exact = root / "solution_20.vtk";
    const std::filesystem::path malformed = root / "solution_21.vtk";
    writeFrame(exact, 4);
    writeFrame(malformed, 3);

    const maskui::VtkFrame frame = maskui::VtkFrameParser::parse(exact);
    if (frame.nx != 2 || frame.ny != 2 ||
        !frame.restart.restartCapable ||
        !frame.restart.currentTime ||
        std::abs(*frame.restart.currentTime - 4.5) > 1e-12 ||
        frame.restart.totalTime != 10.0 ||
        frame.restart.restartStep != 20 ||
        frame.restart.config.at("dtSafety") != "0.9") {
        return fail("exact RestartData metadata was not parsed correctly");
    }

    const auto sample = maskui::sampleVtkPixel(
        frame,
        maskui::ResultImageTransform{0.0, 0.0, 10.0, 10.0},
        5.0,
        5.0);
    if (!sample || std::abs(sample->velocityX - 1.5f) > 1e-6f ||
        std::abs(sample->velocityY - 1.75f) > 1e-6f) {
        return fail("pixel sample did not expose velocity components u/v");
    }

    const maskui::VtkFrame bad = maskui::VtkFrameParser::parse(malformed);
    if (bad.restart.restartCapable) {
        return fail("mismatched RestartData sizes were accepted");
    }


    const std::filesystem::path compressible = root / "solution_30.vtk";
    writeCompressibleFrame(compressible);
    const maskui::VtkFrame gas = maskui::VtkFrameParser::parse(compressible);
    if (gas.scalars.find("density") == gas.scalars.end()) {
        return fail("the density a compressible run writes into every frame "
                    "did not survive parsing");
    }
    if (gas.scalarNames.empty() || gas.scalarNames.front() != "density") {
        return fail("density was not the first named field, which is what "
                    "puts it one press away on the Field button");
    }
    if (std::abs(gas.scalars.at("density").at(2) - 1.425f) > 1e-6f) {
        return fail("density came back with the wrong values");
    }
    if (!gas.restart.restartCapable) {
        return fail("a compressible frame was called uncontinuable; its "
                    "conserved arrays are exactly what the solver restarts "
                    "from");
    }
    for (const std::string& warning : gas.warnings) {
        if (warning.find("RestartData") != std::string::npos) {
            return fail("a compressible frame was warned about: " + warning);
        }
    }

    const std::filesystem::path stunted = root / "solution_31.vtk";
    writeCompressibleFrame(stunted, 3);
    if (maskui::VtkFrameParser::parse(stunted).restart.restartCapable) {
        return fail("conserved arrays shorter than the grid were accepted");
    }

    const std::filesystem::path phased = root / "solution_22.vtk";
    writeFrame(phased, 4, true);
    const maskui::VtkFrame withPhase = maskui::VtkFrameParser::parse(phased);
    if (withPhase.scalars.find("phase") == withPhase.scalars.end()) {
        return fail("a scalar written before the mask was dropped, and the "
                    "solver writes the phase fraction exactly there");
    }
    if (std::abs(withPhase.scalars.at("phase").at(3) - 0.75f) > 1e-6f) {
        return fail("the phase fraction came back with the wrong values");
    }

    const std::filesystem::path stretched = root / "solution_7.vtk";
    writeRectilinearFrame(stretched);
    const maskui::VtkFrame uneven = maskui::VtkFrameParser::parse(stretched);
    if (!uneven.rectilinear())
        return fail("a RECTILINEAR_GRID frame came back claiming to be evenly "
                    "spaced");
    if (uneven.nx != 3 || uneven.ny != 2)
        return fail("a RECTILINEAR_GRID frame came back the wrong size");
    if (std::abs(uneven.spanX() - 1.5) > 1e-6 ||
        std::abs(uneven.spanY() - 1.0) > 1e-6)
        return fail("the domain of a stretched frame is not what its "
                    "coordinates say");
    if (std::abs(uneven.cellRight(0) - uneven.cellLeft(0) - 0.1) > 1e-6 ||
        std::abs(uneven.cellRight(2) - uneven.cellLeft(2) - 1.0) > 1e-6)
        return fail("the cell widths of a stretched frame are wrong, and a "
                    "ten to one stretch is the whole point of the format");

    if (uneven.columnAt(0.05) != 0 || uneven.columnAt(0.3) != 1 ||
        uneven.columnAt(1.2) != 2)
        return fail("a physical x did not land in the cell that contains it");
    if (uneven.columnAt(-5.0) != 0 || uneven.columnAt(99.0) != 2)
        return fail("a physical x outside the domain did not clamp to an end "
                    "cell");
    if (uneven.rowAt(0.1) != 0 || uneven.rowAt(0.8) != 1)
        return fail("a physical y did not land in the row that contains it");
    if (std::abs(uneven.cellCentreX(1) - 0.3) > 1e-6)
        return fail("the centre of a stretched cell is not between its faces");

    const maskui::VtkFrame even = maskui::VtkFrameParser::parse(exact);
    if (even.rectilinear())
        return fail("an evenly spaced frame came back claiming to be "
                    "stretched");
    if (even.columnAt(0.75) != 1 || even.rowAt(0.25) != 0)
        return fail("the lookup stopped working on an evenly spaced frame, "
                    "where it is supposed to be the identity it always was");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    return 0;
}
