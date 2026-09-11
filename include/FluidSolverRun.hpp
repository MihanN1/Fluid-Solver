#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace maskui {

struct FluidSolverRunConfig {
    double Lx = 1.0;
    double Ly = 1.0;
    double Lz = 1.0;
    int nx = 50;
    int ny = 50;
    int nz = 1;

    double U0 = 1.0;
    double nu = 0.01;
    double ro = 1.225;

    double CFL = 0.5;
    double totalTime = 10.0;
    int dtUpdateInterval = 5;
    double dtSafety = 0.9;

    double omega = 1.85;
    double smootherOmega = 1.15;
    int mgIterations = 2;
    double mgTolerance = 1e-4;
    int mgMinCoarseSize = 8;

    int saveInterval = 20;
    bool useCuda = true;

    std::filesystem::path geometryFile;
    double sliceAngleX = 0.0;
    double sliceAngleY = 0.0;
    double sliceAngleZ = 0.0;
    double sliceRotation = 0.0;
    bool invertSection = false;

    bool supportsVolume = false;

    // ---- continuing an earlier run ----------------------------------------
    // restartFile is a solution_*.vtk, or the folder holding them, in which
    // case the solver takes the newest. The grid and the geometry come out of
    // the frame, so neither is asked for again - and the model file does not
    // even have to still exist.
    bool restart = false;
    std::filesystem::path restartFile;
    // Seconds to add to the time the frame stopped at. Zero means "use
    // totalTime as given", which is the older way of saying the same thing and
    // easier to get wrong.
    double addTime = 0.0;

    // ---- the 0.2 runtime switches -----------------------------------------
    // A 0.1 solver rejects any argument it does not know and exits, so these
    // are only put on the command line when the solver is new enough to have
    // them. supportsRuntimeSwitches is what the UI sets after reading the
    // version marker out of the executable.
    bool supportsRuntimeSwitches = false;
    bool useAvx2 = true;
    bool useOpenMp = true;
    int threads = 0;          // 0 = every core
    bool solverTray = true;   // the solver's own tray icon and progress

    // ---- gravity and wall behaviour ---------------------------------------
    // Same rule again: a solver without these keys exits on the first one it
    // does not know, so the two supports* flags are what the UI sets after
    // finding the key names in the executable.
    bool supportsGravity = false;
    bool gravityEnabled = false;
    double gravityAccel = 9.81;   // m/s^2
    double gravityAngle = 0.0;    // degrees, clockwise, 0 = down
    double gravityTilt = 0.0;

    bool supportsWallMotion = false;
    // "1:rot=90,slideX=0.5;2:slip=1". Empty means every wall is static no-slip,
    // which is what the solver does when the key is absent.
    std::string wallMotion;

    bool supportsBodyMotion = false;

    std::string bodyMotion;
    std::string bodyCoupling = "added";
    bool bodyCollisions = false;
    double bodyRestitution = 0.2;
    bool bodyForceReport = false;

    bool supportsCompressible = false;
    std::string regime = "incompressible";
    double gamma = 1.4;
    double gasConstant = 287.05;
    double gamma2 = 1.667;
    double gasConstant2 = 2077.0;
    double T0 = 288.15;
    double pInf = 101325.0;
    double machInlet = 0.5;
    std::string speciesMode = "active";
    int amrLevels = 0;
    std::string amrCriterion = "everything";
    double amrThreshold = 0.2;
    int amrEvery = 8;

    std::string gridStretch = "off";
    double stretchRatio = 1.05;
    double refineNear = 0.25;

    bool acousticFields = false;
    double acousticWindow = 0.02;
    double acousticRef = 2e-5;
    std::string microphones;
    int micInterval = 1;
    bool micAudio = false;
    int micAudioRate = 44100;
    double micAudioSpeed = 1.0;

    bool supportsTurbulence = false;
    std::string turbulence = "none";
    double turbulenceCs = 0.17;
    double turbIntensity = 0.05;
    double turbLengthScale = 0.0;

    bool supportsProfiles = false;
    std::string profiles;

    bool supportsExtraFields = false;
    std::string extraFields;

    bool supportsSchemes = false;
    std::string convection = "upwind";
    std::string limiter = "vanLeer";
    std::string timeScheme = "euler";
    std::string gravityMode = "reduced";

    bool supportsCase = false;
    std::string caseType = "channel";
    double lidSpeed = 1.0;
    double steadyTolerance = 0.0;

    bool supportsPhases = false;
    int phases = 1;
    double rho1 = 1000.0;
    double rho2 = 1.225;
    double nu1 = 1e-6;
    double nu2 = 1.5e-5;
    std::string phaseInit = "layer";
    double phaseLevel = 0.5;
    double phaseX = 0.5;
    double phaseY = 0.5;
    double phaseZ = 0.5;
    std::string vofScheme = "hric";

    bool supportsTension = false;
    std::string mixing = "immiscible";
    double diffusivity = 1e-6;
    double surfaceTension = 0.0;
    double contactAngle = 90.0;
    std::filesystem::path initialPhaseFile;

    std::string sources;

    bool supportsBoundaries = false;

    std::string boundaryKind[6] = {
        "inlet", "outlet", "slip", "slip", "slip", "slip"};
    double boundarySpeed[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double inletFrom = 0.0;
    double inletTo = 1.0;
    double inletFrom2 = 0.0;
    double inletTo2 = 1.0;
    std::string inletProfile = "uniform";
};

bool validateFluidSolverRunConfig(const FluidSolverRunConfig& config,
                             std::string& error);

bool buildFluidSolverArguments(
    const FluidSolverRunConfig& config,
    const std::filesystem::path& outputDirectory,
    std::vector<std::string>& arguments,
    std::string& error);

bool writeFluidSolverArguments(
    const std::filesystem::path& filename,
    const std::vector<std::string>& arguments,
    std::string& error);

std::filesystem::path resolveFluidSolverExecutable(
    const std::filesystem::path& uiExecutable,
    const std::filesystem::path& configuredExecutable);

bool validateFluidSolverExecutable(
    const std::filesystem::path& executable,
    std::string& error);

bool writeFluidSolverSelection(
    const std::filesystem::path& selectionFile,
    const std::filesystem::path& executable,
    std::string& error);

std::optional<std::filesystem::path> readFluidSolverSelection(
    const std::filesystem::path& selectionFile,
    std::string& error);

} // namespace maskui
