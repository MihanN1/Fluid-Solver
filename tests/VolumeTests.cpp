#include "TestHarness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace testing;

namespace {

Config volumeConfig(const std::filesystem::path& out) {
    Config cfg = baseConfig(out);
    cfg.Lx = 1.0f;
    cfg.Ly = 1.0f;
    cfg.Lz = 1.0f;
    cfg.nx = 16;
    cfg.ny = 16;
    cfg.nz = 16;
    cfg.totalTime = 0.2;
    return cfg;
}

float peak(const std::vector<float>& values) {
    float worst = 0.0f;
    for (float value : values)
        worst = std::max(worst, std::fabs(value));
    return worst;
}

int checkPlaneStillWorks() {
    const auto dir = scratchDir("volume-plane");
    Config flat = baseConfig(dir);
    flat.nx = 32;
    flat.ny = 16;
    flat.totalTime = 0.08;

    RestartData frame;
    std::string error;
    if (!runCase(flat, frame, error))
        return fail("the plane case did not run: " + error);
    if (frame.nz != 1)
        return fail("a run with nz = 1 came back as nz = " +
                    std::to_string(frame.nz));
    if (peak(frame.w) != 0.0f)
        return fail("a plane run put something in w");
    if (!allFinite(frame.u) || !allFinite(frame.v) || !allFinite(frame.p))
        return fail("the plane case stopped being a number");
    report("plane nz=1        w is exactly zero, frame reads back as nz = 1");
    removeDir(dir);
    return 0;
}

int checkCubeCavity() {
    const auto dir = scratchDir("volume-cavity");
    Config cfg = volumeConfig(dir);
    cfg.nx = cfg.ny = cfg.nz = 20;
    cfg.caseType = CaseType::Cavity;
    cfg.lidSpeed = 1.0f;
    cfg.boundaries = cavityBoundaries(cfg.lidSpeed);
    cfg.nu = 0.01f;
    cfg.totalTime = 2.0;
    cfg.mgIterations = 6;

    RestartData frame;
    std::string error;
    if (!runCase(cfg, frame, error))
        return fail("the cube cavity did not run: " + error);
    if (!allFinite(frame.u) || !allFinite(frame.v) || !allFinite(frame.w))
        return fail("the cube cavity stopped being a number");

    const float divergence = maxDivergence(frame);
    const float lid = peak(frame.u);
    const float span = peak(frame.w);
    if (divergence > 5e-3f)
        return fail("the cube cavity left a divergence of " +
                    std::to_string(divergence));
    if (!(lid > 0.3f))
        return fail("the lid did not drag the fluid: |u|max = " +
                    std::to_string(lid));
    if (!(span > 1e-3f * lid))
        return fail("the cube cavity produced no motion across the depth, "
                    "which a cube must and a plane cannot");
    std::printf("  cube cavity       |u|max %.4f, |w|max %.4f, divergence "
                "%.2e\n",
                lid, span, divergence);
    removeDir(dir);
    return 0;
}

// Gravity is turned to each of the three axes in turn. The same drop has to
// rise the same distance every time, or one of the strides is wrong.
int checkGravityIsotropy() {
    struct Case {
        const char* label;
        float angle;
        float tilt;
        int axis;
        float sign;
    };
    const Case cases[4] = {{"down -y", 0.0f, 0.0f, 1, 1.0f},
                           {"along -x", 90.0f, 0.0f, 0, 1.0f},
                           {"along -z", 0.0f, -90.0f, 2, 1.0f},
                           {"along +z", 0.0f, 90.0f, 2, -1.0f}};

    double travelled[4] = {0.0, 0.0, 0.0, 0.0};
    double volume[4] = {0.0, 0.0, 0.0, 0.0};

    for (int which = 0; which < 4; ++which) {
        const auto dir = scratchDir("volume-gravity");
        Config cfg = volumeConfig(dir);
        cfg.nx = cfg.ny = cfg.nz = 20;
        cfg.boundaries = closedBoundaries();
        cfg.phases = 2;
        cfg.rho1 = 1.0f;
        cfg.rho2 = 1000.0f;
        cfg.nu1 = 1e-5f;
        cfg.nu2 = 1e-6f;
        cfg.phaseInit = PhaseInit::Drop;
        cfg.phaseLevel = 0.4f;
        cfg.phaseX = 0.5f;
        cfg.phaseY = 0.5f;
        cfg.phaseZ = 0.5f;
        cfg.gravityEnabled = true;
        cfg.gravityMode = GravityMode::Body;
        cfg.gravityAccel = 9.81f;
        cfg.gravityAngle = cases[which].angle;
        cfg.gravityTilt = cases[which].tilt;
        cfg.surfaceTension = 0.0f;
        cfg.U0 = 0.0f;
        cfg.totalTime = 0.1;
        cfg.mgIterations = 6;

        RestartData frame;
        std::string error;
        if (!runCase(cfg, frame, error))
            return fail(std::string("gravity ") + cases[which].label +
                        " did not run: " + error);
        if (frame.phase.empty())
            return fail("the two phase run wrote no phase field");
        if (!allFinite(frame.phase))
            return fail(std::string("gravity ") + cases[which].label +
                        " lost the interface");

        const int nx = frame.nx, ny = frame.ny, nz = frame.nz;
        double mass = 0.0, centre = 0.0;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    const double part =
                        frame.phase[(static_cast<std::size_t>(k) * ny + j) *
                                    nx + i];
                    if (part <= 0.0)
                        continue;
                    const double along =
                        cases[which].axis == 0
                            ? (i + 0.5) * frame.dx
                            : (cases[which].axis == 1 ? (j + 0.5) * frame.dy
                                                      : (k + 0.5) * frame.dz);
                    mass += part;
                    centre += part * along;
                }
        if (!(mass > 0.0))
            return fail("the drop vanished");
        volume[which] = mass * frame.dx * frame.dy * frame.dz;
        travelled[which] =
            cases[which].sign * (centre / mass - 0.5 * 1.0);
    }

    double low = travelled[0], high = travelled[0];
    for (int which = 1; which < 4; ++which) {
        low = std::min(low, travelled[which]);
        high = std::max(high, travelled[which]);
    }
    const double mean = 0.25 * (travelled[0] + travelled[1] + travelled[2] +
                                travelled[3]);
    if (!(mean > 0.0))
        return fail("the light drop did not rise against gravity");
    if (high - low > 0.02 * std::fabs(mean))
        return fail("gravity is not isotropic: the drop rose " +
                    std::to_string(low) + " .. " + std::to_string(high) +
                    " m depending on which axis gravity points along");

    double volumeLow = volume[0], volumeHigh = volume[0];
    for (int which = 1; which < 4; ++which) {
        volumeLow = std::min(volumeLow, volume[which]);
        volumeHigh = std::max(volumeHigh, volume[which]);
    }
    std::printf("  gravity isotropy  rise %.6f m, spread %.2e m (%.4f%%), "
                "drop volume %.6f m^3\n",
                mean, high - low, 100.0 * (high - low) / std::fabs(mean),
                0.25 * (volume[0] + volume[1] + volume[2] + volume[3]));
    if (volumeHigh - volumeLow > 1e-3 * volumeHigh)
        return fail("the drop had a different volume depending on the axis");
    return 0;
}

int checkClosedBoxMass() {
    const auto dir = scratchDir("volume-mass");
    Config cfg = volumeConfig(dir);
    cfg.boundaries = closedBoundaries();
    cfg.phases = 2;
    cfg.rho1 = 1000.0f;
    cfg.rho2 = 1.0f;
    cfg.phaseInit = PhaseInit::Layer;
    cfg.phaseLevel = 0.5f;
    cfg.gravityEnabled = true;
    cfg.gravityMode = GravityMode::Body;
    cfg.surfaceTension = 0.05f;
    cfg.U0 = 0.0f;
    cfg.totalTime = 0.05;
    cfg.mgIterations = 6;

    RestartData frame;
    std::string error;
    if (!runCase(cfg, frame, error))
        return fail("the closed box did not run: " + error);

    double total = 0.0;
    for (float part : frame.phase)
        total += part;
    const double cell = static_cast<double>(frame.dx) * frame.dy * frame.dz;
    const double held = total * cell;
    const double expected = 0.5 * cfg.Lx * cfg.Ly * cfg.Lz;
    const double drift = std::fabs(held - expected) / expected;
    if (drift > 1e-4)
        return fail("the closed box lost " + std::to_string(100.0 * drift) +
                    "% of its heavy fluid");
    std::printf("  closed box        phase volume %.10f m^3, drift %.2e\n",
                held, drift);
    removeDir(dir);
    return 0;
}

std::filesystem::path newestFrame(const std::filesystem::path& dir) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".vtk")
            continue;
        const auto stamp = std::filesystem::last_write_time(entry.path(), ec);
        if (newest.empty() || stamp >= newestTime) {
            newest = entry.path();
            newestTime = stamp;
        }
    }
    return newest;
}

// A volume stopped halfway and continued has to land on the same field as one
// that never stopped, or the third component is not being carried through the
// frame.
int checkVolumetricRestart() {
    const auto root = scratchDir("volume-restart");
    std::string error;

    Config whole = volumeConfig(root / "whole");
    whole.totalTime = 0.12;
    whole.mgIterations = 40;
    whole.mgTolerance = 1e-6f;
    whole.dtUpdateInterval = 1;
    RestartData wholeFrame;
    if (!runCase(whole, wholeFrame, error))
        return fail("the volumetric whole run did not start: " + error);
    if (wholeFrame.nz != whole.nz)
        return fail("the frame came back with nz = " +
                    std::to_string(wholeFrame.nz));
    if (wholeFrame.w.size() != static_cast<std::size_t>(whole.nx) * whole.ny *
                                   (whole.nz + 1))
        return fail("w came back the wrong size");

    Config half = volumeConfig(root / "half");
    half.totalTime = 0.06;
    half.mgIterations = 40;
    half.mgTolerance = 1e-6f;
    half.dtUpdateInterval = 1;
    RestartData halfFrame;
    if (!runCase(half, halfFrame, error))
        return fail("the volumetric first half did not run: " + error);
    if (!halfFrame.exactState)
        return fail("the volumetric frame did not carry its faces exactly");

    const std::filesystem::path source = newestFrame(root / "half");
    const std::filesystem::path restDir = root / "rest";
    {
        Quiet quiet;
        RestartData loaded;
        if (!loadRestart(source, loaded, error))
            return fail("loading the volumetric frame back: " + error);
        if (loaded.nz != whole.nz)
            return fail("the reader lost the depth of the frame");

        Config merged = loaded.cfg;
        merged.restart = true;
        merged.restartFile = source.string();
        merged.outputDir = restDir.string();
        merged.totalTime = whole.totalTime;
        merged.mgIterations = 40;
        merged.mgTolerance = 1e-6f;
        merged.dtUpdateInterval = 1;

        std::filesystem::create_directories(restDir);
        Mesh mesh(merged, &loaded.solid);
        Solver solver(merged, mesh);
        if (!solver.setInitialState(std::move(loaded), "cont"))
            return fail("the solver refused the volumetric frame");
        solver.run();
    }

    RestartData restFrame;
    const std::filesystem::path continued = newestFrame(restDir);
    if (continued.empty() || !loadRestart(continued, restFrame, error))
        return fail("the continued volumetric frame: " + error);
    if (restFrame.nz != whole.nz)
        return fail("the continued run changed nz");
    if (!allFinite(restFrame.u) || !allFinite(restFrame.v) ||
        !allFinite(restFrame.w))
        return fail("the continued run stopped being a number");

    const float du = maxDifference(wholeFrame.u, restFrame.u) /
                     magnitude(wholeFrame.u);
    const float dv = maxDifference(wholeFrame.v, restFrame.v) /
                     magnitude(wholeFrame.v);
    const float dw = maxDifference(wholeFrame.w, restFrame.w) /
                     magnitude(wholeFrame.w);
    if (du > 1.5e-2f || dv > 1.5e-2f || dw > 1.5e-2f)
        return fail("the continued volume drifted from the whole one: du " +
                    std::to_string(du) + ", dv " + std::to_string(dv) +
                    ", dw " + std::to_string(dw));
    std::printf("  restart           %d x %d x %d, continued run differs by "
                "%.2e / %.2e / %.2e in u / v / w\n",
                wholeFrame.nx, wholeFrame.ny, wholeFrame.nz, du, dv, dw);
    removeDir(root);
    return 0;
}

int checkTurbulenceStaysFinite() {
    const auto dir = scratchDir("volume-turbulence");
    for (int model = 0; model < 2; ++model) {
        Config cfg = volumeConfig(dir);
        cfg.nx = 24;
        cfg.ny = 16;
        cfg.nz = 16;
        cfg.Lx = 1.5f;
        cfg.turbulence = model == 0 ? TurbulenceKind::Smagorinsky
                                    : TurbulenceKind::KOmegaSST;
        cfg.totalTime = 0.15;

        RestartData frame;
        std::string error;
        if (!runCase(cfg, frame, error))
            return fail(std::string("the volumetric ") +
                        turbulenceKindName(cfg.turbulence) +
                        " run did not start: " + error);
        if (!allFinite(frame.u) || !allFinite(frame.v) || !allFinite(frame.w))
            return fail(std::string("volumetric ") +
                        turbulenceKindName(cfg.turbulence) +
                        " stopped being a number");
        std::printf("  %-17s |u|max %.4f, divergence %.2e\n",
                    turbulenceKindName(cfg.turbulence), peak(frame.u),
                    maxDivergence(frame));
    }
    removeDir(dir);
    return 0;
}

int checkWallsTurnAboutEveryAxis() {
    const auto dir = scratchDir("volume-walls");
    const char* motions[3] = {"1:rotX=120", "1:rotY=120", "1:rot=120"};
    float speeds[3] = {0.0f, 0.0f, 0.0f};

    for (int axis = 0; axis < 3; ++axis) {
        Config cfg = volumeConfig(dir);
        cfg.nx = cfg.ny = cfg.nz = 16;
        cfg.geometryFile = "none";
        cfg.wallMotion = motions[axis];
        cfg.totalTime = 0.05;

        RestartData frame;
        std::string error;
        if (!runCase(cfg, frame, error))
            return fail(std::string("a wall turning about axis ") +
                        std::to_string(axis) + " did not run: " + error);
        if (!allFinite(frame.u) || !allFinite(frame.v) || !allFinite(frame.w))
            return fail("a turning wall broke the field");
        speeds[axis] = std::max(peak(frame.u),
                                std::max(peak(frame.v), peak(frame.w)));
    }

    for (int axis = 0; axis < 3; ++axis)
        if (!(speeds[axis] > 0.0f))
            return fail("a wall turning about one axis moved nothing");
    std::printf("  turning walls     |v|max %.4f / %.4f / %.4f about x / y / "
                "z\n",
                speeds[0], speeds[1], speeds[2]);
    removeDir(dir);
    return 0;
}

// A shock tube is a one dimensional solution living in a three dimensional
// box: every column along y and every plane along z has to hold exactly the
// same profile. A wrong stride or a stray z flux shows up here and nowhere
// else.
int checkShockTubeStaysOneDimensional() {
    const auto dir = scratchDir("volume-sod");
    Config cfg = volumeConfig(dir);
    cfg.regime = Regime::Compressible;
    cfg.caseType = CaseType::ShockTube;
    cfg.boundaries = closedBoundaries();
    cfg.boundaries[BoundarySide::Bottom].kind = BoundaryKind::Slip;
    cfg.boundaries[BoundarySide::Top].kind = BoundaryKind::Slip;
    cfg.boundaries[BoundarySide::Front].kind = BoundaryKind::Slip;
    cfg.boundaries[BoundarySide::Back].kind = BoundaryKind::Slip;
    cfg.geometryFile = "empty";
    cfg.nx = 64;
    cfg.ny = 12;
    cfg.nz = 12;
    cfg.Lx = cfg.Ly = cfg.Lz = 1.0f;
    cfg.totalTime = 0.0006;
    cfg.CFL = 0.4f;
    cfg.gravityEnabled = false;

    RestartData frame;
    std::string error;
    if (!runCase(cfg, frame, error))
        return fail("the volumetric shock tube did not run: " + error);
    if (frame.stateRho.empty())
        return fail("the compressible frame carried no density");
    if (!allFinite(frame.stateRho) || !allFinite(frame.stateRhoW))
        return fail("the shock tube stopped being a number");

    const int nx = frame.nx, ny = frame.ny, nz = frame.nz;
    const auto at = [&](const std::vector<float>& field, int i, int j, int k) {
        return field[(static_cast<std::size_t>(k) * ny + j) * nx + i];
    };

    float spread = 0.0f;
    float scale = 0.0f;
    for (int i = 0; i < nx; ++i) {
        const float reference = at(frame.stateRho, i, 0, 0);
        scale = std::max(scale, std::fabs(reference));
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                spread = std::max(
                    spread, std::fabs(at(frame.stateRho, i, j, k) - reference));
    }
    if (!(scale > 0.0f))
        return fail("the shock tube carried no density at all");
    if (spread > 1e-4f * scale)
        return fail("the shock tube is not the same in every column: spread " +
                    std::to_string(spread / scale));

    const float acrossZ = peak(frame.stateRhoW);
    if (acrossZ > 1e-4f * scale)
        return fail("a shock along x produced momentum along z");

    Config flat = cfg;
    flat.nz = 1;
    flat.outputDir = (dir / "flat").string();
    RestartData plane;
    if (!runCase(flat, plane, error))
        return fail("the plane shock tube did not run: " + error);

    float profileGap = 0.0f;
    for (int i = 0; i < nx; ++i)
        profileGap = std::max(
            profileGap,
            std::fabs(at(frame.stateRho, i, ny / 2, nz / 2) -
                      plane.stateRho[static_cast<std::size_t>(ny / 2) * nx +
                                     i]));
    if (profileGap > 1e-3f * scale)
        return fail("the volume does not reproduce the plane shock tube: gap " +
                    std::to_string(profileGap / scale));

    std::printf("  shock tube        1D in a volume: spread %.2e, |rho w|max "
                "%.2e, gap to the plane run %.2e\n",
                spread / scale, acrossZ, profileGap / scale);
    removeDir(dir);
    return 0;
}

int checkAdaptiveRefinesInDepth() {
    const auto dir = scratchDir("volume-amr");
    Config cfg = volumeConfig(dir);
    cfg.regime = Regime::Compressible;
    cfg.caseType = CaseType::ShockTube;
    cfg.boundaries = closedBoundaries();
    cfg.geometryFile = "empty";
    cfg.nx = cfg.ny = cfg.nz = 24;
    cfg.amrLevels = 1;
    cfg.amrEvery = 4;
    cfg.amrMinPatch = 4;
    cfg.amrMaxPatch = 24;
    cfg.amrThreshold = 0.1f;
    cfg.totalTime = 0.03;
    cfg.CFL = 0.4f;

    RestartData frame;
    std::string error;
    if (!runCase(cfg, frame, error))
        return fail("the volumetric adaptive run did not start: " + error);
    if (!allFinite(frame.stateRho))
        return fail("the adaptive run stopped being a number");
    std::printf("  adaptive mesh     ran in three dimensions, density %.4f .. "
                "%.4f\n",
                *std::min_element(frame.stateRho.begin(),
                                  frame.stateRho.end()),
                *std::max_element(frame.stateRho.begin(),
                                  frame.stateRho.end()));
    removeDir(dir);
    return 0;
}

int checkSolidBodyInAVolume() {
    const auto dir = scratchDir("volume-body");
    Config cfg = volumeConfig(dir);
    cfg.nx = 24;
    cfg.ny = 16;
    cfg.nz = 16;
    cfg.Lx = 1.5f;
    cfg.totalTime = 0.1;

    RestartData frame;
    std::string error;
    if (!runCase(cfg, frame, error))
        return fail("the volumetric body case did not run: " + error);

    long long solidCells = 0;
    for (uint8_t flag : frame.solid)
        solidCells += flag ? 1 : 0;
    if (solidCells <= 0)
        return fail("the fallback sphere produced no solid cells");

    const int nx = frame.nx, ny = frame.ny, nz = frame.nz;
    long long asymmetric = 0;
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const std::size_t here =
                    (static_cast<std::size_t>(k) * ny + j) * nx + i;
                const std::size_t mirrored =
                    (static_cast<std::size_t>(nz - 1 - k) * ny + j) * nx + i;
                if (frame.solid[here] != frame.solid[mirrored])
                    ++asymmetric;
            }
    if (asymmetric != 0)
        return fail("the voxelised body is not symmetric about the mid depth");

    std::printf("  body in a volume  %lld solid cells, symmetric in z, "
                "divergence %.2e\n",
                solidCells, maxDivergence(frame));
    removeDir(dir);
    return 0;
}

}

int main() {
    int failures = 0;
    failures += checkPlaneStillWorks();
    failures += checkCubeCavity();
    failures += checkSolidBodyInAVolume();
    failures += checkVolumetricRestart();
    failures += checkGravityIsotropy();
    failures += checkClosedBoxMass();
    failures += checkTurbulenceStaysFinite();
    failures += checkWallsTurnAboutEveryAxis();
    failures += checkShockTubeStaysOneDimensional();
    failures += checkAdaptiveRefinesInDepth();

    if (failures != 0) {
        std::cerr << failures << " volumetric checks failed\n";
        return 1;
    }
    std::cout << "VolumeTests OK\n";
    return 0;
}
