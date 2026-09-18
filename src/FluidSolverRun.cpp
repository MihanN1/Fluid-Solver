#include "FluidSolverRun.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace maskui {
namespace {

constexpr int kMinimumGridDimension = 8;
constexpr int kMaximumGridDimension = 1'000'000;
constexpr std::size_t kMaximumGridCells = 100'000'000;

bool fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

bool requireFinite(const char* name, double value, std::string& error) {
    if (!std::isfinite(value)) {
        return fail(error, std::string(name) + " must be finite");
    }
    return true;
}

bool requirePositive(const char* name, double value, std::string& error) {
    if (!requireFinite(name, value, error)) {
        return false;
    }
    if (value <= 0.0) {
        return fail(error, std::string(name) + " must be positive");
    }
    return true;
}

bool requireNonNegative(const char* name,
                        double value,
                        std::string& error) {
    if (!requireFinite(name, value, error)) {
        return false;
    }
    if (value < 0.0) {
        return fail(error, std::string(name) + " must be non-negative");
    }
    return true;
}

bool validateGrid(const FluidSolverRunConfig& config, std::string& error) {
    if (config.nx < kMinimumGridDimension ||
        config.nx > kMaximumGridDimension) {
        return fail(error, "nx must be in [8, 1000000]");
    }
    if (config.ny < kMinimumGridDimension ||
        config.ny > kMaximumGridDimension) {
        return fail(error, "ny must be in [8, 1000000]");
    }

    if (config.nz < 1 || config.nz > kMaximumGridDimension) {
        return fail(error, "nz must be in [1, 1000000]");
    }

    const std::size_t width = static_cast<std::size_t>(config.nx);
    const std::size_t height = static_cast<std::size_t>(config.ny);
    const std::size_t depth = static_cast<std::size_t>(config.nz);
    if (width > std::numeric_limits<std::size_t>::max() / height ||
        width * height > kMaximumGridCells) {
        return fail(error, "nx * ny exceeds the 100000000-cell limit");
    }
    if (width * height > kMaximumGridCells / depth) {
        return fail(error, "nx * ny * nz exceeds the 100000000-cell limit");
    }
    return true;
}

bool validateGeometry(const std::filesystem::path& filename,
                      std::string& error) {
    const std::string pathText = filename.string();
    if (pathText.find_first_of("\r\n") != std::string::npos) {
        return fail(error, "geometryFile must not contain CR or LF");
    }

    {
        std::string lowered = pathText;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        if (lowered == "empty" || lowered == "none")
            return true;
    }
    if (filename.empty()) {
        return fail(error, "geometryFile must not be empty");
    }

    std::error_code filesystemError;
    const bool isFile =
        std::filesystem::is_regular_file(filename, filesystemError);
    if (filesystemError || !isFile) {
        return fail(
            error,
            "geometryFile must name an existing regular file: " + pathText);
    }

    std::string extension = filename.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (extension != ".obj" && extension != ".stl") {
        return fail(error, "geometryFile extension must be .obj or .stl");
    }
    return true;
}

bool isRegularFile(const std::filesystem::path& filename) {
    std::error_code error;
    const bool regular =
        std::filesystem::is_regular_file(filename, error);
    return !error && regular;
}

bool validateArgumentPath(const char* name,
                          const std::filesystem::path& path,
                          std::string& error);

std::string serializeDouble(double value) {
    std::ostringstream output;
    output << std::setprecision(
        std::numeric_limits<double>::max_digits10) << value;
    return output.str();
}

bool validateArgumentPath(const char* name,
                          const std::filesystem::path& path,
                          std::string& error) {
    const std::string text = path.u8string();
    if (text.empty()) {
        return fail(error, std::string(name) + " must not be empty");
    }
    if (text.find_first_of("\r\n") != std::string::npos) {
        return fail(error, std::string(name) + " must not contain CR or LF");
    }
    return true;
}

} // namespace

bool validateFluidSolverRunConfig(const FluidSolverRunConfig& config,
                             std::string& error) {
    error.clear();

    const bool gas = config.supportsCompressible &&
                     config.regime == "compressible";

    if (!validateGrid(config, error) ||
        !requirePositive("Lx", config.Lx, error) ||
        !requirePositive("Ly", config.Ly, error) ||
        !requirePositive("Lz", config.Lz, error) ||
        !requireNonNegative("U0", config.U0, error) ||
        !requirePositive("nu", config.nu, error) ||
        !requirePositive("ro", config.ro, error) ||
        !requirePositive("CFL", config.CFL, error) ||
        !requirePositive("totalTime", config.totalTime, error) ||
        !requirePositive("dtSafety", config.dtSafety, error) ||
        !requirePositive("omega", config.omega, error) ||
        !requirePositive("smootherOmega", config.smootherOmega, error) ||
        !requirePositive("mgTolerance", config.mgTolerance, error) ||
        !requireFinite("sliceAngleX", config.sliceAngleX, error) ||
        !requireFinite("sliceAngleY", config.sliceAngleY, error) ||
        !requireFinite("sliceAngleZ", config.sliceAngleZ, error) ||
        !requireFinite("sliceRotation", config.sliceRotation, error)) {
        return false;
    }

    if (config.dtUpdateInterval <= 0) {
        return fail(error, "dtUpdateInterval must be positive");
    }
    if (config.mgIterations <= 0) {
        return fail(error, "mgIterations must be positive");
    }
    if (config.mgMinCoarseSize <= 0) {
        return fail(error, "mgMinCoarseSize must be positive");
    }
    if (config.saveInterval <= 0) {
        return fail(error, "saveInterval must be positive");
    }
    if (config.CFL > 1.0) {
        return fail(error, "CFL must be in (0, 1]");
    }
    if (config.dtSafety > 1.0) {
        return fail(error, "dtSafety must be in (0, 1]");
    }
    if (config.omega >= 2.0) {
        return fail(error, "omega must be in (0, 2)");
    }
    if (config.smootherOmega >= 2.0) {
        return fail(error, "smootherOmega must be in (0, 2)");
    }
    if (!requireNonNegative("addTime", config.addTime, error)) {
        return false;
    }
    if (config.threads < 0) {
        return fail(error, "threads must be zero (all cores) or positive");
    }
    if (!requireNonNegative("gravityAccel", config.gravityAccel, error) ||
        !requireFinite("gravityAngle", config.gravityAngle, error) ||
        !requireFinite("gravityTilt", config.gravityTilt, error)) {
        return false;
    }
    if (config.wallMotion.find_first_of("\r\n") != std::string::npos) {
        return fail(error, "wallMotion must not contain CR or LF");
    }
    if (config.bodyMotion.find_first_of("\r\n") != std::string::npos) {
        return fail(error, "bodyMotion must not contain CR or LF");
    }
    if (config.supportsBodyMotion && !config.bodyMotion.empty()) {
        if (config.bodyCoupling != "weak" && config.bodyCoupling != "added" &&
            config.bodyCoupling != "strong") {
            return fail(error, "bodyCoupling is weak, added or strong");
        }
        if (!(config.bodyRestitution >= 0.0) ||
            !(config.bodyRestitution <= 1.0)) {
            return fail(error, "bodyRestitution is how much of the closing "
                               "speed survives a bounce, so it lives between "
                               "0 and 1");
        }

        if (config.geometryFile == "empty" && config.profiles.empty()) {
            return fail(error, "bodyMotion needs a model to move: an empty "
                               "domain has no body in it, and a body has to "
                               "have its outline cut again wherever it goes");
        }
    }
    if (config.supportsCompressible) {
        if (config.regime != "incompressible" &&
            config.regime != "compressible") {
            return fail(error, "regime is incompressible or compressible");
        }
    }
    if (gas) {
        if (!(config.gamma > 1.0) || !(config.gamma <= 3.0))
            return fail(error, "gamma is the ratio of specific heats and lives "
                               "above 1: 1.4 for air, 1.667 for helium");
        if (!(config.gasConstant > 0.0) || !std::isfinite(config.gasConstant))
            return fail(error, "R has to be a positive number of J/(kg K)");
        if (!(config.T0 > 0.0) || !std::isfinite(config.T0))
            return fail(error, "T0 is in kelvin, so it cannot be zero or "
                               "below");
        if (!(config.pInf > 0.0) || !std::isfinite(config.pInf))
            return fail(error, "pInf has to be a positive pressure");
        if (!(config.machInlet >= 0.0) || !(config.machInlet <= 20.0))
            return fail(error, "machInlet is a Mach number between 0 and 20");
        if (config.speciesMode != "active" && config.speciesMode != "passive")
            return fail(error, "speciesMode is active or passive");
        if (!(config.acousticWindow > 0.0) ||
            !std::isfinite(config.acousticWindow))
            return fail(error, "acousticWindow is a positive number of "
                               "seconds");
        if (!(config.acousticRef > 0.0) || !std::isfinite(config.acousticRef))
            return fail(error, "the 0 dB reference has to be a positive "
                               "pressure");
        if (config.amrLevels < 0 || config.amrLevels > 4)
            return fail(error, "refinement is between 0 and 4 levels; four is "
                               "already sixteen times finer");
        if (config.amrLevels > 0) {
            if (config.amrCriterion != "density" &&
                config.amrCriterion != "vorticity" &&
                config.amrCriterion != "species" &&
                config.amrCriterion != "body" &&
                config.amrCriterion != "everything")
                return fail(error, "amrCriterion is density, vorticity, "
                                   "species, body or everything");
            if (!(config.amrThreshold > 0.0) || !(config.amrThreshold <= 1.0))
                return fail(error, "amrThreshold is a fraction of the "
                                   "steepest feature in the domain, so it "
                                   "lives between 0 and 1");
            if (config.amrEvery < 1)
                return fail(error, "the patches have to be rebuilt at least "
                                   "every step");
        }
        if (config.gridStretch != "off" && config.gridStretch != "body" &&
            config.gridStretch != "wake" && config.gridStretch != "edges")
            return fail(error, "gridStretch is off, body, wake or edges");
        if (config.gridStretch != "off") {
            if (!(config.stretchRatio > 1.0) || !(config.stretchRatio <= 1.5))
                return fail(error, "stretchRatio is above 1 and at most 1.5. "
                                   "1 is no stretching at all, and past about "
                                   "1.2 the scheme quietly drops an order");
            if (!(config.refineNear > 0.0) || !(config.refineNear <= 1.0))
                return fail(error, "refineNear is the width of the fine band "
                                   "as a fraction of the domain, so it lives "
                                   "between 0 and 1");
        }
        if (config.micInterval < 1)
            return fail(error, "micInterval is at least one step");
        if (config.micAudioRate < 1000 || config.micAudioRate > 384000)
            return fail(error, "micAudioRate is between 1000 and 384000 Hz");
        if (!(config.micAudioSpeed > 0.0) ||
            !std::isfinite(config.micAudioSpeed))
            return fail(error, "micAudioSpeed is a positive multiple of real "
                               "time: 1 is real time, 0.05 is twenty times "
                               "slower");
        if (config.micAudio && config.microphones.empty())
            return fail(error, "micAudio writes what the microphones heard, "
                               "so it needs at least one microphone");
        if (config.microphones.find_first_of("\r\n") != std::string::npos)
            return fail(error, "microphones must not contain CR or LF");
        if (config.turbulence != "none")
            return fail(error, "the compressible solver is inviscid Euler and "
                               "has no viscous term for an eddy viscosity to "
                               "be added to. Set the turbulence model to none, "
                               "or the regime back to incompressible");
        if (config.gravityEnabled)
            return fail(error, "gravity is not written for the compressible "
                               "solver: it would be a source term in both the "
                               "momentum and the energy, and neither is there");
        if (config.surfaceTension > 0.0)
            return fail(error, "two gases share a composition rather than an "
                               "interface, so there is no surface for tension "
                               "to pull on");
        if (!config.sources.empty())
            return fail(error, "a source needs a pressure solve to make room "
                               "for what it pushes out, and the compressible "
                               "solver has not got one");
        if (config.caseType == "cavity")
            return fail(error, "the cavity is driven by a sliding lid, which "
                               "is a low speed case by definition. Use channel "
                               "or shockTube");
    } else if (config.acousticFields || !config.microphones.empty()) {
        return fail(error, "acoustics need the compressible solver: the "
                           "projection method makes the speed of sound "
                           "infinite, so there is nothing travelling to "
                           "listen to");
    }
    if (config.supportsTurbulence && config.turbulence != "none") {
        if (config.turbulence != "smagorinsky" &&
            config.turbulence != "kOmegaSST") {
            return fail(error, "turbulence is none, smagorinsky or kOmegaSST");
        }
        if (!(config.turbulenceCs > 0.0) || !(config.turbulenceCs <= 1.0)) {
            return fail(error, "Cs is the Smagorinsky constant and lives "
                               "between 0 and 1; 0.17 is the value it was "
                               "derived at and 0.1 is what channels want");
        }
        if (!(config.turbIntensity >= 0.0) ||
            !(config.turbIntensity <= 1.0)) {
            return fail(error, "turbIntensity is a fraction of the inlet "
                               "speed, so it lives between 0 and 1: 0.05 is "
                               "five percent");
        }
        if (!(config.turbLengthScale >= 0.0) ||
            !std::isfinite(config.turbLengthScale)) {
            return fail(error, "turbLengthScale cannot be negative; zero lets "
                               "the solver take a tenth of the domain height");
        }
    }
    if (config.profiles.find_first_of("\r\n") != std::string::npos) {
        return fail(error, "profiles must not contain CR or LF");
    }
    if (config.extraFields.find_first_of("\r\n") != std::string::npos) {
        return fail(error, "extraFields must not contain CR or LF");
    }
    if (config.runName.find_first_of("\r\n") != std::string::npos) {
        return fail(error, "runName must not contain CR or LF");
    }
    if (config.phases != 1 && config.phases != 2) {
        return fail(error, "phases is 1 or 2: a second phase field would need "
                           "a rule for what happens where three fluids meet, "
                           "and there is not one");
    }
    if (config.phases > 1) {
        if (!(config.rho1 > 0.0) || !(config.rho2 > 0.0) ||
            !std::isfinite(config.rho1) || !std::isfinite(config.rho2)) {
            return fail(error, "both densities have to be positive numbers");
        }
        if (!(config.nu1 >= 0.0) || !(config.nu2 >= 0.0) ||
            !std::isfinite(config.nu1) || !std::isfinite(config.nu2)) {
            return fail(error, "neither viscosity can be negative");
        }
        if (config.vofScheme != "upwind" && config.vofScheme != "hric" &&
            config.vofScheme != "cicsam") {
            return fail(error, "vofScheme is upwind, hric or cicsam");
        }
        if (config.phaseInit != "layer" && config.phaseInit != "drop" &&
            config.phaseInit != "column" && config.phaseInit != "file") {
            return fail(error, "phaseInit is layer, drop, column or file");
        }
        for (double fraction : {config.phaseLevel, config.phaseX,
                                config.phaseY, config.phaseZ}) {
            if (!(fraction >= 0.0 && fraction <= 1.0))
                return fail(error, "phaseLevel, phaseX, phaseY and phaseZ are "
                                   "fractions of the domain, so they live "
                                   "between 0 and 1");
        }
        if (config.supportsTension && !gas) {
            if (config.mixing != "immiscible" && config.mixing != "miscible")
                return fail(error, "mixing is immiscible or miscible");
            if (!(config.diffusivity >= 0.0) ||
                !std::isfinite(config.diffusivity))
                return fail(error, "diffusivity cannot be negative");
            if (!(config.surfaceTension >= 0.0) ||
                !std::isfinite(config.surfaceTension))
                return fail(error, "surfaceTension cannot be negative");
            if (!(config.contactAngle >= 0.0) ||
                !(config.contactAngle <= 180.0))
                return fail(error, "contactAngle is measured inside fluid 1 "
                                   "and lives between 0 and 180 degrees");
            if (config.mixing == "miscible" && config.surfaceTension > 0.0)
                return fail(error, "fluids that mix have no surface for "
                                   "tension to pull on: set surfaceTension to "
                                   "zero, or make them immiscible");
        }
        if (!config.gravityEnabled && !gas)
            return fail(error, "two fluids with gravity off never separate: "
                               "the density difference is the only thing that "
                               "would move them, and nothing here reads it. "
                               "Turn gravity on, or drop back to one phase");
    }
    if (config.sources.find_first_of("\r\n") != std::string::npos) {
        return fail(error, "sources must not contain CR or LF");
    }
    if (!std::isfinite(config.lidSpeed)) {
        return fail(error, "lidSpeed must be finite");
    }
    if (!std::isfinite(config.steadyTolerance) ||
        config.steadyTolerance < 0.0) {
        return fail(error, "steadyTolerance is a fraction and cannot be "
                           "negative; zero means the run goes the whole way");
    }
    if (config.caseType != "channel" && config.caseType != "cavity") {
        return fail(error, "caseType must be channel or cavity");
    }
    if (!(config.inletFrom >= 0.0 && config.inletFrom <= 1.0) ||
        !(config.inletTo >= 0.0 && config.inletTo <= 1.0)) {
        return fail(error, "inletFrom and inletTo are fractions of a side, so "
                           "they live between 0 and 1");
    }
    if (config.inletFrom >= config.inletTo) {
        return fail(error, "inletFrom must be below inletTo, or the band the "
                           "inlet occupies is empty");
    }
    if (!(config.inletFrom2 >= 0.0 && config.inletFrom2 <= 1.0) ||
        !(config.inletTo2 >= 0.0 && config.inletTo2 <= 1.0)) {
        return fail(error, "inletFrom2 and inletTo2 are fractions of a side, "
                           "so they live between 0 and 1");
    }
    if (config.inletFrom2 >= config.inletTo2) {
        return fail(error, "inletFrom2 must be below inletTo2, or the window "
                           "the inlet occupies is empty");
    }
    {
        bool anyOutlet = false;
        bool anyInlet = false;
        const int sides = config.supportsVolume && config.nz > 1 ? 6 : 4;
        for (int side = 0; side < sides; ++side) {
            if (config.boundaryKind[side] == "outlet")
                anyOutlet = true;
            if (config.boundaryKind[side] == "inlet")
                anyInlet = true;
            if (!std::isfinite(config.boundarySpeed[side]))
                return fail(error, "boundary speeds must be finite");
        }

        if (!config.sources.empty())
            anyInlet = true;
        if (config.supportsBoundaries && config.caseType != "cavity" &&
            anyInlet && !anyOutlet && !config.restart)
            return fail(error,
                        "there is an inlet but no outlet, so fluid is pushed "
                        "into a box it cannot leave. Give one side outlet, or "
                        "make every side a wall for a closed case");
    }

    if (config.restart) {
        // The mask, the grid and the geometry all come out of the frame, so
        // the model file is not needed and is not asked for. What has to be
        // there is the frame.
        if (config.restartFile.empty()) {
            return fail(error, "restartFile must name a frame or its folder");
        }
        std::error_code filesystemError;
        if (!std::filesystem::exists(config.restartFile, filesystemError) ||
            filesystemError) {
            return fail(
                error,
                "restartFile does not exist: " + config.restartFile.string());
        }
        return validateArgumentPath("restartFile", config.restartFile, error);
    }

    return validateGeometry(config.geometryFile, error);
}

bool buildFluidSolverArguments(
    const FluidSolverRunConfig& config,
    const std::filesystem::path& outputDirectory,
    std::vector<std::string>& arguments,
    std::string& error) {
    error.clear();
    if (!validateFluidSolverRunConfig(config, error)) {
        return false;
    }
    if (!validateArgumentPath("outputDirectory", outputDirectory, error)) {
        return false;
    }
    if (!outputDirectory.is_absolute()) {
        return fail(
            error,
            "outputDirectory must be absolute for the current Fluid Solver");
    }

    arguments = {
        "Lx=" + serializeDouble(config.Lx),
        "Ly=" + serializeDouble(config.Ly),
        "nx=" + std::to_string(config.nx),
        "ny=" + std::to_string(config.ny),
        "U0=" + serializeDouble(config.U0),
        "nu=" + serializeDouble(config.nu),
        "ro=" + serializeDouble(config.ro),
        "CFL=" + serializeDouble(config.CFL),
        "totalTime=" + serializeDouble(config.totalTime),
        "dtUpdateInterval=" + std::to_string(config.dtUpdateInterval),
        "dtSafety=" + serializeDouble(config.dtSafety),
        "omega=" + serializeDouble(config.omega),
        "smootherOmega=" + serializeDouble(config.smootherOmega),
        "mgIterations=" + std::to_string(config.mgIterations),
        "mgTolerance=" + serializeDouble(config.mgTolerance),
        "mgMinCoarseSize=" + std::to_string(config.mgMinCoarseSize),
        "saveInterval=" + std::to_string(config.saveInterval),
        "useCuda=" + std::string(config.useCuda ? "1" : "0"),
        "outputDir=" + outputDirectory.u8string()
    };

    if (config.restart) {
        // The solver reads the frame first and then applies whatever follows,
        // so restart= and restartFile= go in before the overrides. The
        // geometry keys are left out entirely: the mask comes out of the frame
        // and the model file may not even exist any more.
        arguments.insert(
            arguments.begin(),
            {"restart=1", "restartFile=" + config.restartFile.u8string()});
        if (config.addTime > 0.0) {
            arguments.push_back("addTime=" + serializeDouble(config.addTime));
        }
    } else {
        arguments.push_back("geometryFile=" + config.geometryFile.u8string());
        arguments.push_back("sliceAngleX=" + serializeDouble(config.sliceAngleX));
        if (config.supportsVolume) {
            arguments.push_back(
                "sliceAngleY=" + serializeDouble(config.sliceAngleY));
        }
        arguments.push_back("sliceAngleZ=" + serializeDouble(config.sliceAngleZ));
        arguments.push_back(
            "sliceRotation=" + serializeDouble(config.sliceRotation));
        arguments.push_back(
            "invertSection=" + std::string(config.invertSection ? "1" : "0"));
    }

    if (config.supportsVolume && !config.restart) {
        arguments.push_back("Lz=" + serializeDouble(config.Lz));
        arguments.push_back("nz=" + std::to_string(config.nz));
    }

    if (config.supportsRuntimeSwitches) {
        // A 0.1 solver stops on the first argument it does not recognise, so
        // these only exist once the executable has been read and found to be
        // 0.2 or newer.
        arguments.push_back("avx2=" + std::string(config.useAvx2 ? "1" : "0"));
        arguments.push_back(
            "openmp=" + std::string(config.useOpenMp ? "1" : "0"));
        arguments.push_back("threads=" + std::to_string(config.threads));
        arguments.push_back(
            "tray=" + std::string(config.solverTray ? "1" : "0"));
    }
    if (config.supportsGravity) {
        arguments.push_back(
            "gravityEnabled=" + std::string(config.gravityEnabled ? "1" : "0"));
        arguments.push_back(
            "gravityAccel=" + serializeDouble(config.gravityAccel));
        arguments.push_back(
            "gravityAngle=" + serializeDouble(config.gravityAngle));
        if (config.supportsVolume) {
            arguments.push_back(
                "gravityTilt=" + serializeDouble(config.gravityTilt));
        }
    }
    if (config.supportsWallMotion) {
        arguments.push_back("wallMotion=" + config.wallMotion);
    }
    if (config.supportsBodyMotion) {
        arguments.push_back("bodyMotion=" + config.bodyMotion);
        if (!config.bodyMotion.empty()) {
            arguments.push_back("bodyCoupling=" + config.bodyCoupling);
            arguments.push_back(
                "bodyCollisions=" + std::string(config.bodyCollisions ? "1" : "0"));
            if (config.bodyCollisions)
                arguments.push_back("bodyRestitution=" +
                                    serializeDouble(config.bodyRestitution));
            arguments.push_back(
                "bodyForceReport=" +
                std::string(config.bodyForceReport ? "1" : "0"));
        }
    }
    const bool gasRun = config.supportsCompressible &&
                        config.regime == "compressible";
    if (config.supportsCompressible) {
        arguments.push_back("regime=" + config.regime);
        if (gasRun) {
            arguments.push_back("gamma=" + serializeDouble(config.gamma));
            arguments.push_back("R=" + serializeDouble(config.gasConstant));
            arguments.push_back("T0=" + serializeDouble(config.T0));
            arguments.push_back("pInf=" + serializeDouble(config.pInf));
            arguments.push_back("machInlet=" +
                                serializeDouble(config.machInlet));
            if (config.phases > 1) {
                arguments.push_back("gamma2=" + serializeDouble(config.gamma2));
                arguments.push_back("R2=" +
                                    serializeDouble(config.gasConstant2));
                arguments.push_back("speciesMode=" + config.speciesMode);
            }
            arguments.push_back("amrLevels=" +
                                std::to_string(config.amrLevels));
            if (config.amrLevels > 0) {
                arguments.push_back("amrCriterion=" + config.amrCriterion);
                arguments.push_back("amrThreshold=" +
                                    serializeDouble(config.amrThreshold));
                arguments.push_back("amrEvery=" +
                                    std::to_string(config.amrEvery));
            }
            arguments.push_back("gridStretch=" + config.gridStretch);
            if (config.gridStretch != "off") {
                arguments.push_back("stretchRatio=" +
                                    serializeDouble(config.stretchRatio));
                arguments.push_back("refineNear=" +
                                    serializeDouble(config.refineNear));
            }
            arguments.push_back(
                "acousticFields=" +
                std::string(config.acousticFields ? "1" : "0"));
            if (config.acousticFields || !config.microphones.empty()) {
                arguments.push_back("acousticWindow=" +
                                    serializeDouble(config.acousticWindow));
                arguments.push_back("acousticRef=" +
                                    serializeDouble(config.acousticRef));
            }
            arguments.push_back("microphones=" + config.microphones);
            if (!config.microphones.empty()) {
                arguments.push_back("micInterval=" +
                                    std::to_string(config.micInterval));
                arguments.push_back("micAudio=" +
                                    std::string(config.micAudio ? "1" : "0"));
                if (config.micAudio) {
                    arguments.push_back("micAudioRate=" +
                                        std::to_string(config.micAudioRate));
                    arguments.push_back("micAudioSpeed=" +
                                        serializeDouble(config.micAudioSpeed));
                }
            }
        }
    }
    if (config.supportsTurbulence && !gasRun) {
        arguments.push_back("turbulence=" + config.turbulence);
        if (config.turbulence == "smagorinsky")
            arguments.push_back("Cs=" + serializeDouble(config.turbulenceCs));
        if (config.turbulence == "kOmegaSST") {
            arguments.push_back("turbIntensity=" +
                                serializeDouble(config.turbIntensity));
            arguments.push_back("turbLengthScale=" +
                                serializeDouble(config.turbLengthScale));
        }
    }
    if (config.supportsProfiles && !config.profiles.empty()) {
        arguments.push_back("profiles=" + config.profiles);
    }
    if (config.supportsExtraFields) {
        arguments.push_back("extraFields=" + config.extraFields);
    }
    if (config.supportsRunName && !config.runName.empty()) {
        arguments.push_back("runName=" + config.runName);
    }
    if (config.supportsSchemes) {
        arguments.push_back("convection=" + config.convection);
        arguments.push_back("limiter=" + config.limiter);
        arguments.push_back("timeScheme=" + config.timeScheme);
        arguments.push_back("gravityMode=" + config.gravityMode);
    }
    if (config.supportsPhases) {
        arguments.push_back("phases=" + std::to_string(config.phases));
        if (config.phases > 1) {
            arguments.push_back("rho1=" + serializeDouble(config.rho1));
            arguments.push_back("rho2=" + serializeDouble(config.rho2));
            arguments.push_back("nu1=" + serializeDouble(config.nu1));
            arguments.push_back("nu2=" + serializeDouble(config.nu2));
            arguments.push_back("vofScheme=" + config.vofScheme);
            if (config.supportsTension) {
                arguments.push_back("mixing=" + config.mixing);
                arguments.push_back("diffusivity=" +
                                    serializeDouble(config.diffusivity));
                arguments.push_back("surfaceTension=" +
                                    serializeDouble(config.surfaceTension));
                arguments.push_back("contactAngle=" +
                                    serializeDouble(config.contactAngle));
            }

            if (!config.initialPhaseFile.empty()) {
                arguments.push_back("phaseInit=file");
                arguments.push_back("initialPhaseFile=" +
                                    config.initialPhaseFile.u8string());
            } else {
                arguments.push_back("phaseInit=" + config.phaseInit);
                arguments.push_back("phaseLevel=" +
                                    serializeDouble(config.phaseLevel));
                arguments.push_back("phaseX=" + serializeDouble(config.phaseX));
                arguments.push_back("phaseY=" + serializeDouble(config.phaseY));
                if (config.supportsVolume) {
                    arguments.push_back("phaseZ=" +
                                        serializeDouble(config.phaseZ));
                }
            }
        }
        arguments.push_back("sources=" + config.sources);
    }
    if (config.supportsCase) {
        arguments.push_back("caseType=" + config.caseType);
        if (config.caseType == "cavity")
            arguments.push_back("lidSpeed=" + serializeDouble(config.lidSpeed));
        arguments.push_back(
            "steadyTolerance=" + serializeDouble(config.steadyTolerance));
    }
    if (config.supportsBoundaries && config.caseType != "cavity") {
        static const char* const kKind[6] = {
            "bcLeft", "bcRight", "bcBottom", "bcTop", "bcFront", "bcBack"};
        static const char* const kSpeed[6] = {
            "bcLeftSpeed", "bcRightSpeed", "bcBottomSpeed", "bcTopSpeed",
            "bcFrontSpeed", "bcBackSpeed"};
        const int sides = config.supportsVolume ? 6 : 4;
        for (int side = 0; side < sides; ++side) {
            arguments.push_back(std::string(kKind[side]) + "=" +
                                config.boundaryKind[side]);

            const bool movingWall = config.boundaryKind[side] == "movingWall";
            if (movingWall || config.boundarySpeed[side] != 0.0)
                arguments.push_back(std::string(kSpeed[side]) + "=" +
                                    serializeDouble(config.boundarySpeed[side]));
        }
        arguments.push_back("inletFrom=" + serializeDouble(config.inletFrom));
        arguments.push_back("inletTo=" + serializeDouble(config.inletTo));
        if (config.supportsVolume) {
            arguments.push_back(
                "inletFrom2=" + serializeDouble(config.inletFrom2));
            arguments.push_back("inletTo2=" + serializeDouble(config.inletTo2));
        }
        arguments.push_back("inletProfile=" + config.inletProfile);
    }
    return true;
}

bool writeFluidSolverArguments(
    const std::filesystem::path& filename,
    const std::vector<std::string>& arguments,
    std::string& error) {
    error.clear();

    try {
        std::ofstream output(filename, std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            return fail(
                error,
                "cannot open Fluid Solver argument file for writing: " +
                    filename.string());
        }

        for (const std::string& argument : arguments) {
            if (argument.empty() ||
                argument.find_first_of("\r\n") != std::string::npos) {
                return fail(
                    error,
                    "Fluid Solver arguments must be non-empty single lines");
            }
            output << argument << '\n';
        }
        output.flush();

        if (!output) {
            return fail(
                error,
                "failed while writing Fluid Solver argument file: " +
                    filename.string());
        }
        return true;
    } catch (const std::exception& exception) {
        return fail(
            error,
            "Fluid Solver argument write exception: " +
                std::string(exception.what()));
    }
}

// The solver ships as "Fluid Solver.exe" on Windows and as "Fluid Solver" with
// no extension everywhere else. Both names are tried on every platform, the
// native one first: the UI ships for Linux and macOS now, and it used to insist
// on a .exe that does not exist there.
const char* const FLUID_SOLVER_FILE_NAMES[] = {
#if defined(_WIN32)
    "Fluid Solver.exe",
    "Fluid Solver",
#else
    "Fluid Solver",
    "Fluid Solver.exe",
#endif
};

std::filesystem::path resolveFluidSolverExecutable(
    const std::filesystem::path& uiExecutable,
    const std::filesystem::path& configuredExecutable) {
    const std::filesystem::path directory = uiExecutable.parent_path();
    for (const char* const name : FLUID_SOLVER_FILE_NAMES) {
        const std::filesystem::path adjacent = directory / name;
        if (isRegularFile(adjacent)) {
            return adjacent;
        }
    }
    if (isRegularFile(configuredExecutable)) {
        return configuredExecutable;
    }
    // Nothing there. Name the file the UI looks for beside itself, so the
    // message the user gets points at the place it has to be put.
    return directory.empty()
        ? configuredExecutable
        : directory / FLUID_SOLVER_FILE_NAMES[0];
}

bool validateFluidSolverExecutable(
    const std::filesystem::path& executable,
    std::string& error) {
    if (!isRegularFile(executable)) {
        return fail(
            error,
            "Fluid Solver executable is not a regular file: " +
                executable.string());
    }
#if defined(_WIN32)
    std::string extension = executable.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (extension != ".exe") {
        return fail(error, "On Windows the Fluid Solver must be a .exe file");
    }
#else
    // Elsewhere the solver carries no extension, so the thing worth checking is
    // whether it can actually be run.
    std::error_code permissionError;
    const std::filesystem::perms permissions =
        std::filesystem::status(executable, permissionError).permissions();
    if (!permissionError &&
        (permissions & (std::filesystem::perms::owner_exec |
                        std::filesystem::perms::group_exec |
                        std::filesystem::perms::others_exec)) ==
            std::filesystem::perms::none) {
        return fail(
            error,
            "Fluid Solver is not executable: " + executable.string() +
                " - chmod +x it");
    }
#endif
    error.clear();
    return true;
}

bool writeFluidSolverSelection(
    const std::filesystem::path& selectionFile,
    const std::filesystem::path& executable,
    std::string& error) {
    if (!validateFluidSolverExecutable(executable, error)) {
        return false;
    }
    try {
        if (!selectionFile.parent_path().empty()) {
            std::filesystem::create_directories(
                selectionFile.parent_path());
        }
        std::ofstream output(
            selectionFile,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            return fail(
                error,
                "cannot write solver selection file: " +
                    selectionFile.string());
        }
        output << executable.u8string() << '\n';
        output.flush();
        if (!output) {
            return fail(
                error,
                "failed while writing solver selection file: " +
                    selectionFile.string());
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        return fail(
            error,
            "solver selection write exception: " +
                std::string(exception.what()));
    }
}

std::optional<std::filesystem::path> readFluidSolverSelection(
    const std::filesystem::path& selectionFile,
    std::string& error) {
    error.clear();
    std::error_code existenceError;
    if (!std::filesystem::exists(selectionFile, existenceError)) {
        if (existenceError) {
            error = "cannot inspect solver selection file: " +
                existenceError.message();
        }
        return std::nullopt;
    }

    try {
        std::ifstream input(selectionFile, std::ios::binary);
        std::string encodedPath;
        if (!input.is_open() || !std::getline(input, encodedPath)) {
            error = "cannot read solver selection file: " +
                selectionFile.string();
            return std::nullopt;
        }
        if (!encodedPath.empty() && encodedPath.back() == '\r') {
            encodedPath.pop_back();
        }
        if (encodedPath.empty()) {
            error = "solver selection file is empty";
            return std::nullopt;
        }
        const std::filesystem::path executable =
            std::filesystem::u8path(encodedPath);
        if (!validateFluidSolverExecutable(executable, error)) {
            return std::nullopt;
        }
        return executable;
    } catch (const std::exception& exception) {
        error = "solver selection read exception: " +
            std::string(exception.what());
        return std::nullopt;
    }
}

} // namespace maskui
