#include "TestHarness.hpp"

#include <cstdio>

using namespace testing;

namespace {

struct Balance {
    double in = 0.0;
    double out = 0.0;
    double imbalance = 0.0;
};

Balance balanceOf(const RestartData& frame) {
    Balance b;
    const int nx = frame.nx, ny = frame.ny;
    const auto count = [&](double normal, double area) {
        if (normal > 0.0)
            b.in += normal * area;
        else
            b.out -= normal * area;
    };
    for (int j = 0; j < ny; ++j) {
        count(frame.u[j * (nx + 1)], frame.dy);
        count(-frame.u[j * (nx + 1) + nx], frame.dy);
    }
    for (int i = 0; i < nx; ++i) {
        count(frame.v[i], frame.dx);
        count(-frame.v[ny * nx + i], frame.dx);
    }
    b.imbalance = std::fabs(b.in - b.out) / std::max(b.in, 1e-12);
    return b;
}

Config bandConfig(const std::filesystem::path& out) {
    Config cfg = baseConfig(out);
    cfg.geometryFile = "empty";
    cfg.nu = 0.02f;
    cfg.totalTime = 4.0;
    cfg.mgIterations = 30;
    cfg.mgTolerance = 1e-6f;
    cfg.boundaries[BoundarySide::Bottom].kind = BoundaryKind::Wall;
    cfg.boundaries[BoundarySide::Top].kind = BoundaryKind::Wall;
    return cfg;
}

int checkInletColumn(const RestartData& frame, const Config& cfg,
                     const char* label) {
    const BoundarySpec& spec = cfg.boundaries[BoundarySide::Left];
    BoundarySpec resolved = spec;
    if (!resolved.speedSet)
        resolved.speed = cfg.U0;

    double worst = 0.0;
    int inside = 0;
    for (int j = 0; j < frame.ny; ++j) {
        const float want =
            inletVelocityAt(resolved, (j + 0.5f) / static_cast<float>(frame.ny));
        worst = std::max(worst, static_cast<double>(std::fabs(
                                    frame.u[j * (frame.nx + 1)] - want)));
        inside += want != 0.0f ? 1 : 0;
    }
    std::printf("  %-18s %2d of %d rows open, worst face error %.2e\n",
                label, inside, frame.ny, worst);
    if (inside == 0)
        return fail(std::string(label) + ": the band covers no cell at all");
    if (!(worst < 1e-5))
        return fail(std::string(label) +
                    ": the inlet faces are not what the side asked for");
    return 0;
}

int checkBalance(const RestartData& frame, const char* label) {
    const Balance b = balanceOf(frame);
    std::printf("  %-18s in %.6f out %.6f imbalance %.2e, "
                "max divergence %.2e\n",
                label, b.in, b.out, b.imbalance, maxDivergence(frame));
    if (!allFinite(frame.u) || !allFinite(frame.v))
        return fail(std::string(label) + ": the field stopped being a number");
    if (!(b.imbalance < 2e-3))
        return fail(std::string(label) +
                    ": what went in did not come back out");
    if (!(maxDivergence(frame) < 1e-3f))
        return fail(std::string(label) + ": the projection left a divergence");
    return 0;
}

int expectRefused(const Config& cfg, const char* label) {
    std::string error;
    std::vector<int> solid(static_cast<size_t>(cfg.nx) * cfg.ny * cfg.nz, 0);
    if (checkBoundaryMassBalance(cfg.boundaries, cfg.U0,
                                 DomainExtent{cfg.Lx, cfg.Ly, cfg.Lz, cfg.nx, cfg.ny, cfg.nz},
                                 solid, error))
        return fail(std::string(label) + " was accepted and should not have "
                                         "been");
    std::printf("  %-18s refused: %.70s...\n", label, error.c_str());
    return 0;
}

int expectAccepted(const Config& cfg, const char* label) {
    std::string error;
    std::vector<int> solid(static_cast<size_t>(cfg.nx) * cfg.ny * cfg.nz, 0);
    if (!checkBoundaryMassBalance(cfg.boundaries, cfg.U0,
                                  DomainExtent{cfg.Lx, cfg.Ly, cfg.Lz, cfg.nx, cfg.ny, cfg.nz},
                                  solid, error))
        return fail(std::string(label) + " was refused: " + error);
    std::printf("  %-18s accepted\n", label);
    return 0;
}

}

int main() {
    const std::filesystem::path root = scratchDir("inlet");
    std::string error;
    int rc = 0;

    {
        Config cfg = bandConfig(root / "uniform");
        cfg.boundaries[BoundarySide::Left].from = 0.4f;
        cfg.boundaries[BoundarySide::Left].to = 0.6f;

        RestartData frame;
        if (!runCase(cfg, frame, error))
            return fail("uniform band: " + error);
        rc |= checkInletColumn(frame, cfg, "uniform band");
        rc |= checkBalance(frame, "uniform band");
    }

    {
        Config cfg = bandConfig(root / "parabolic");
        cfg.boundaries[BoundarySide::Left].from = 0.25f;
        cfg.boundaries[BoundarySide::Left].to = 0.75f;
        cfg.boundaries[BoundarySide::Left].profile = InletProfile::Parabolic;

        RestartData frame;
        if (!runCase(cfg, frame, error))
            return fail("parabolic band: " + error);
        rc |= checkInletColumn(frame, cfg, "parabolic band");
        rc |= checkBalance(frame, "parabolic band");

        double peak = 0.0, flux = 0.0;
        for (int j = 0; j < frame.ny; ++j) {
            peak = std::max(peak,
                            static_cast<double>(frame.u[j * (frame.nx + 1)]));
            flux += frame.u[j * (frame.nx + 1)] * frame.dy;
        }
        const double want = 0.5 * cfg.Ly * cfg.U0;
        std::printf("  parabolic peak %.4f (1.5 * U0 = %.4f), "
                    "flux %.4f (want %.4f)\n",
                    peak, 1.5 * cfg.U0, flux, want);
        if (!(std::fabs(peak - 1.5 * cfg.U0) < 0.05))
            return fail("the parabolic inlet does not peak at 1.5 * speed");
        if (!(std::fabs(flux - want) < 0.02 * want))
            return fail("the parabolic inlet does not carry the same flow "
                        "rate as the flat one");
    }

    {
        Config cfg = baseConfig(root / "vertical");
        cfg.geometryFile = "empty";
        cfg.Lx = 1.0f;
        cfg.Ly = 2.0f;
        cfg.nx = 32;
        cfg.ny = 64;
        cfg.nu = 0.02f;
        cfg.totalTime = 4.0;
        cfg.mgIterations = 30;
        cfg.mgTolerance = 1e-6f;
        cfg.boundaries[BoundarySide::Left].kind = BoundaryKind::Wall;
        cfg.boundaries[BoundarySide::Right].kind = BoundaryKind::Wall;
        cfg.boundaries[BoundarySide::Bottom].kind = BoundaryKind::Inlet;
        cfg.boundaries[BoundarySide::Bottom].from = 0.3f;
        cfg.boundaries[BoundarySide::Bottom].to = 0.7f;
        cfg.boundaries[BoundarySide::Top].kind = BoundaryKind::Outlet;

        RestartData frame;
        if (!runCase(cfg, frame, error))
            return fail("vertical band: " + error);
        rc |= checkBalance(frame, "vertical band");

        double up = 0.0;
        for (int i = 0; i < frame.nx; ++i)
            up += frame.v[frame.ny * frame.nx + i] * frame.dx;
        if (!(up > 0.1))
            return fail("the flow driven from the bottom is not leaving "
                        "through the top");
    }

    {
        Config cfg = bandConfig(root / "refused");
        cfg.boundaries[BoundarySide::Right].kind = BoundaryKind::Wall;
        rc |= expectRefused(cfg, "inlet, no outlet");
    }
    {
        Config cfg = bandConfig(root / "refused");
        cfg.boundaries[BoundarySide::Left].from = 0.5f;
        cfg.boundaries[BoundarySide::Left].to = 0.5f;
        rc |= expectRefused(cfg, "band of no width");
    }
    {
        Config cfg = bandConfig(root / "refused");
        cfg.boundaries[BoundarySide::Left].from = 0.500f;
        cfg.boundaries[BoundarySide::Left].to = 0.505f;
        rc |= expectRefused(cfg, "band under one cell");
    }
    {
        Config cfg = bandConfig(root / "accepted");
        rc |= expectAccepted(cfg, "plain channel");
    }
    {
        Config cfg = bandConfig(root / "accepted");
        cfg.boundaries = cavityBoundaries(1.0f);
        rc |= expectAccepted(cfg, "lid driven cavity");
    }
    {
        Config cfg = bandConfig(root / "accepted");
        cfg.boundaries[BoundarySide::Right].kind = BoundaryKind::Inlet;
        cfg.boundaries[BoundarySide::Right].speed = -cfg.U0;
        cfg.boundaries[BoundarySide::Right].speedSet = true;
        rc |= expectAccepted(cfg, "inlets cancelling");
    }

    if (rc == 0) {
        removeDir(root);
        std::printf("InletTests OK\n");
    }
    return rc;
}
