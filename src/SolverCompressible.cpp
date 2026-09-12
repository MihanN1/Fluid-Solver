#include "CompressibleKernels.hpp"
#include "SolverCompressible.hpp"

#include "AmrDriver.hpp"
#include "AmrHierarchy.hpp"

#include "Progress.hpp"
#include "Runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace {

using namespace cfd;

}

void Workspace::fit(const Block& block, int components) {
    const std::size_t faceX = static_cast<std::size_t>(block.nx + 1) *
                              block.ny * block.nz * components;
    const std::size_t faceY = static_cast<std::size_t>(block.nx) *
                              (block.ny + 1) * block.nz * components;
    const std::size_t faceZ =
        block.spans() ? static_cast<std::size_t>(block.nx) * block.ny *
                            (block.nz + 1) * components
                      : 0;
    if (fluxX.size() != faceX)
        fluxX.assign(faceX, 0.0f);
    if (fluxY.size() != faceY)
        fluxY.assign(faceY, 0.0f);
    if (fluxZ.size() != faceZ)
        fluxZ.assign(faceZ, 0.0f);

    const std::size_t cells = static_cast<std::size_t>(block.cells());
    for (std::vector<float>& field : primitive)
        if (field.size() != cells)
            field.assign(cells, 0.0f);
}

void GasModel::prepare() {
    cv1 = R1 / (gamma1 - 1.0f);
    cv2 = R2 / (gamma2 - 1.0f);
    cp1 = cv1 + R1;
    cp2 = cv2 + R2;
}

void fillGhostCells(Block& block,
                    const BlockBoundaries& sides,
                    const GasModel& gas) {
    const int g = block.ghost;
    const int gz = block.ghostZ();
    const int nx = block.nx;
    const int ny = block.ny;
    const int nz = block.nz;
    const float rowsTotal = static_cast<float>(
        sides.spanNy > 0 ? sides.spanNy : block.ny);
    const float columnsTotal = static_cast<float>(
        sides.spanNx > 0 ? sides.spanNx : block.nx);
    const float planesTotal = static_cast<float>(
        sides.spanNz > 0 ? sides.spanNz : block.nz);
    const float rowFirst = static_cast<float>(sides.spanJ0);
    const float columnFirst = static_cast<float>(sides.spanI0);
    const float planeFirst = static_cast<float>(sides.spanK0);

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= 64)
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j) {
        BlockBoundaries local = sides;
        local.inletY = (rowFirst + j + 0.5f) / rowsTotal;
        local.inletZ = (planeFirst + k + 0.5f) / planesTotal;
        for (int m = 1; m <= g; ++m) {
            mirrorSide(block, gas, sides.left, -m, j, k, m - 1, j, k, true,
                       local);
            mirrorSide(block, gas, sides.right, block.nx - 1 + m, j, k,
                       block.nx - m, j, k, true, local);
        }
    }

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * nx >= 64)
    for (int k = 0; k < nz; ++k)
    for (int i = 0; i < nx; ++i) {
        BlockBoundaries local = sides;
        local.inletY = (columnFirst + i + 0.5f) / columnsTotal;
        local.inletZ = (planeFirst + k + 0.5f) / planesTotal;
        for (int m = 1; m <= g; ++m) {
            mirrorSide(block, gas, sides.bottom, i, -m, k, i, m - 1, k, false,
                       local);
            mirrorSide(block, gas, sides.top, i, block.ny - 1 + m, k, i,
                       block.ny - m, k, false, local);
        }
    }

    if (block.spans()) {
        #pragma omp parallel for collapse(2) schedule(static) \
            if (ny * nx >= 64)
        for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            BlockBoundaries local = sides;
            local.inletY = (columnFirst + i + 0.5f) / columnsTotal;
            local.inletZ = (rowFirst + j + 0.5f) / rowsTotal;
            for (int m = 1; m <= g; ++m) {
                mirrorSpan(block, gas, sides.front, i, j, -m, i, j, m - 1,
                           local);
                mirrorSpan(block, gas, sides.back, i, j, block.nz - 1 + m, i, j,
                           block.nz - m, local);
            }
        }
    }

    const bool open[3][2] = {
        {!sides.left.interior, !sides.right.interior},
        {!sides.bottom.interior, !sides.top.interior},
        {!sides.front.interior, !sides.back.interior}};

    for (int sz = -1; sz <= 1; ++sz)
        for (int sy = -1; sy <= 1; ++sy)
            for (int sx = -1; sx <= 1; ++sx) {
                if ((sx != 0) + (sy != 0) + (sz != 0) < 2)
                    continue;
                if (sx < 0 && !open[0][0]) continue;
                if (sx > 0 && !open[0][1]) continue;
                if (sy < 0 && !open[1][0]) continue;
                if (sy > 0 && !open[1][1]) continue;
                if (sz < 0 && !open[2][0]) continue;
                if (sz > 0 && !open[2][1]) continue;

                const int firstI = sx < 0 ? -g : (sx > 0 ? block.nx : 0);
                const int lastI =
                    sx < 0 ? -1 : (sx > 0 ? block.nx + g - 1 : block.nx - 1);
                const int firstJ = sy < 0 ? -g : (sy > 0 ? block.ny : 0);
                const int lastJ =
                    sy < 0 ? -1 : (sy > 0 ? block.ny + g - 1 : block.ny - 1);
                const int firstK = sz < 0 ? -gz : (sz > 0 ? block.nz : 0);
                const int lastK =
                    sz < 0 ? -1 : (sz > 0 ? block.nz + gz - 1 : block.nz - 1);

                for (int k = firstK; k <= lastK; ++k)
                    for (int j = firstJ; j <= lastJ; ++j)
                        for (int i = firstI; i <= lastI; ++i) {
                            const int sourceI =
                                sx < 0 ? 0 : (sx > 0 ? block.nx - 1 : i);
                            const int sourceJ =
                                sy < 0 ? 0 : (sy > 0 ? block.ny - 1 : j);
                            const int sourceK =
                                sz < 0 ? 0 : (sz > 0 ? block.nz - 1 : k);
                            const Primitive q = primitiveOf(
                                block, gas,
                                block.index(sourceI, sourceJ, sourceK));
                            writeState(block, block.index(i, j, k), q);
                        }
            }
}

void fillSolidCells(Block& block, const GasModel& gas) {
    if (!block.solid)
        return;

    const int nx = block.nx;
    const int ny = block.ny;
    const int nz = block.nz;

    for (int layer = 0; layer < 2; ++layer) {
        #pragma omp parallel for collapse(2) schedule(static) \
            if (nz * ny >= 64)
        for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                solidCell(block, gas, i, j, k, layer);
    }
}

float blockTimeStep(const Block& block, const GasModel& gas, float cfl) {
    float worst = 0.0f;

    const int nx = block.nx;
    const int ny = block.ny;
    const int nz = block.nz;

    #pragma omp parallel if (nz * ny >= 64)
    {
        float local = 0.0f;

        #pragma omp for collapse(2) schedule(static) nowait
        for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                local = std::max(local, cellRate(block, gas, i, j, k));

        #pragma omp critical
        worst = std::max(worst, local);
    }

    if (!(worst > 0.0f))
        return 0.0f;
    return cfl / worst;
}

void advanceStage(Block& in,
                  const Block& keep,
                  Block& out,
                  const BlockBoundaries& sides,
                  const GasModel& gas,
                  float dt,
                  float a,
                  float b,
                  LimiterKind limiter,
                  float diffusivity,
                  Workspace& work) {
    fillSolidCells(in, gas);
    fillGhostCells(in, sides, gas);
    work.fit(in, kComponents);

    const int nx = in.nx;
    const int ny = in.ny;
    const int nz = in.nz;
    const int gz = in.ghostZ();
    const int ghost = in.ghost;
    float* __restrict fx = work.fluxX.data();
    float* __restrict fy = work.fluxY.data();
    float* __restrict fz = work.fluxZ.data();

    const int limiterCode = static_cast<int>(limiter);

    float* __restrict pRho = work.primitive[0].data();
    float* __restrict pU = work.primitive[1].data();
    float* __restrict pV = work.primitive[2].data();
    float* __restrict pW = work.primitive[3].data();
    float* __restrict pP = work.primitive[4].data();
    float* __restrict pY = work.primitive[5].data();
    float* __restrict pGamma = work.primitive[6].data();

    const int kFirst = -gz;
    const int kLast = nz + gz;
    const int jFirst = -ghost;
    const int jLast = ny + ghost;
    #pragma omp parallel for collapse(2) schedule(static) if (nz * ny >= 32)
    for (int k = kFirst; k < kLast; ++k)
    for (int j = jFirst; j < jLast; ++j) {
        const int row = in.index(-in.ghost, j, k);
        const int width = nx + 2 * in.ghost;
        for (int m = 0; m < width; ++m)
            fillPrimitive(in, gas, row + m, pRho, pU, pV, pW, pP, pY, pGamma);
    }

    PrimitiveField prim;
    prim.rho = pRho;
    prim.u = pU;
    prim.v = pV;
    prim.w = pW;
    prim.p = pP;
    prim.y = pY;
    prim.gamma = pGamma;

    #pragma omp parallel for collapse(2) schedule(static) if (nz * ny >= 32)
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i <= nx; ++i)
            faceFluxX(in, prim, gas, sides, limiterCode, i, j, k,
                      fx + ((static_cast<std::size_t>(k) * ny + j) * (nx + 1) +
                            i) * kComponents);

    #pragma omp parallel for collapse(2) schedule(static) if (nz * ny >= 32)
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i < nx; ++i)
            faceFluxY(in, prim, gas, sides, limiterCode, i, j, k,
                      fy + ((static_cast<std::size_t>(k) * (ny + 1) + j) * nx +
                            i) * kComponents);

    if (in.spans()) {
        #pragma omp parallel for collapse(2) schedule(static) if (nz * ny >= 32)
        for (int k = 0; k <= nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                faceFluxZ(in, prim, gas, sides, limiterCode, i, j, k,
                          fz + ((static_cast<std::size_t>(k) * ny + j) * nx +
                                i) * kComponents);
    }

    #pragma omp parallel for collapse(2) schedule(static) if (nz * ny >= 32)
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            combine(in, keep, out, gas, fx, fy, fz, i, j, k, dt, a, b,
                    diffusivity);
}

namespace {

int roundUp(double value) {
    return static_cast<int>(value + 0.5);
}

}

CompressibleRun::CompressibleRun(const Config& configuration, Mesh& meshIn)
    : cfg(configuration), mesh(meshIn) {
    nx = cfg.nx;
    ny = cfg.ny;
    nz = cfg.nz;
    volumetric = cfg.volumetric();
    dx = cfg.Lx / nx;
    dy = cfg.Ly / ny;
    dz = cfg.Lz / nz;
    outputPath = narrowToPath(cfg.outputDir);

    gas.gamma1 = cfg.gamma;
    gas.R1 = cfg.R;
    gas.gamma2 = cfg.gamma2;
    gas.R2 = cfg.R2;
    gas.species = cfg.twoSpecies();
    gas.active = cfg.speciesMode == SpeciesMode::Active;
    gas.prepare();

    const auto fill = [&](SideState& state, BoundarySide which) {
        const BoundarySpec& spec = cfg.boundaries[which];
        state.kind = spec.kind;
        state.noSlip = spec.kind == BoundaryKind::Wall ||
                       spec.kind == BoundaryKind::MovingWall;
        state.speed = spec.speedSet ? spec.speed : 0.0f;
        state.from = spec.from;
        state.to = spec.to;
        state.from2 = spec.from2;
        state.to2 = spec.to2;
        state.banded = spec.kind == BoundaryKind::Inlet &&
                       (spec.from > 0.0f || spec.to < 1.0f ||
                        spec.from2 > 0.0f || spec.to2 < 1.0f);
    };
    fill(sides.left, BoundarySide::Left);
    fill(sides.right, BoundarySide::Right);
    fill(sides.bottom, BoundarySide::Bottom);
    fill(sides.top, BoundarySide::Top);
    fill(sides.front, BoundarySide::Front);
    fill(sides.back, BoundarySide::Back);
    sides.pInf = cfg.pInf;
    sides.T0 = cfg.T0;
    sides.mach = cfg.machInlet;

    const std::size_t cells = static_cast<std::size_t>(nx) * ny * nz;
    solidMask.assign(cells, 0);
    solidVelX.assign(cells, 0.0f);
    solidVelY.assign(cells, 0.0f);
    solidVelZ.assign(cells, 0.0f);
    for (std::size_t id = 0; id < cells; ++id)
        solidMask[id] = mesh.solid[id] ? 1 : 0;

    allocate();
    buildGrid();
    setUpAmr();

#ifdef USE_CUDA
    if (cfg.useCuda && runtime::settings().useCuda && cfg.adaptive()) {
        std::cout << "\n!!! amrLevels and useCuda cannot both be on. The "
                     "refinement hierarchy lives on the host\n    and the "
                     "device kernels know about one grid, so a GPU run would "
                     "quietly solve the base\n    grid and throw the patches "
                     "away. This run stays on the CPU.\n\n";
    }
    if (cfg.useCuda && runtime::settings().useCuda && stretched) {
        std::cout << "\n!!! gridStretch and useCuda cannot both be on. The "
                     "device kernels index one cell size\n    per axis and "
                     "the stretched metrics live on the host, so a GPU run "
                     "would quietly\n    solve an even grid instead of the "
                     "one you asked for. This run stays on the CPU.\n\n";
    }
    if (cfg.useCuda && runtime::settings().useCuda && !stretched &&
        !cfg.adaptive() && compressibleCudaAvailable()) {
        device = compressibleCudaCreate(nx, ny, nz, ghost, cfg.twoSpecies());
        onDevice = device != nullptr;
        if (onDevice)
            compressibleCudaUploadSolid(device, solidMask.data(),
                                        solidVelX.data(), solidVelY.data(),
                                        solidVelZ.data());
    }
#endif
}

CompressibleRun::~CompressibleRun() {
#ifdef USE_CUDA
    if (device)
        compressibleCudaDestroy(device);
#endif
}

void CompressibleRun::allocate() {
    const std::size_t stride = static_cast<std::size_t>(nx) + 2 * ghost;
    const std::size_t rows = static_cast<std::size_t>(ny) + 2 * ghost;
    const std::size_t layers = volumetric
                                   ? static_cast<std::size_t>(nz) + 2 * ghost
                                   : 1;
    const std::size_t total = stride * rows * layers;
    const bool species = cfg.twoSpecies();

    const auto give = [&](std::vector<float>& field) {
        field.assign(total, 0.0f);
    };
    give(rho); give(rhou); give(rhov); give(rhow); give(rhoE);
    give(rho1); give(rhou1); give(rhov1); give(rhow1); give(rhoE1);
    give(rho2); give(rhou2); give(rhov2); give(rhow2); give(rhoE2);
    if (species) {
        give(rhoY); give(rhoY1); give(rhoY2);
    }

    const std::size_t cells = static_cast<std::size_t>(nx) * ny * nz;
    if (cfg.acousticFields) {
        pressureMean.assign(cells, 0.0f);
        pressureFast.assign(cells, 0.0f);
        pressureRms.assign(cells, 0.0f);
        crossingRate.assign(cells, 0.0f);
        lastSign.assign(cells, 0);
    }
    mics = cfg.resolvedMicrophones();
    micSamples.assign(mics.size(), {});
}

Block CompressibleRun::view(std::vector<float>& r,
                            std::vector<float>& ru,
                            std::vector<float>& rv,
                            std::vector<float>& rw,
                            std::vector<float>& re,
                            std::vector<float>& ry) {
    Block block;
    block.nx = nx;
    block.ny = ny;
    block.nz = nz;
    block.ghost = ghost;
    block.stride = nx + 2 * ghost;
    block.rows = ny + 2 * ghost;
    block.dx = dx;
    block.dy = dy;
    block.dz = dz;
    block.x0 = 0.0f;
    block.y0 = 0.0f;
    block.z0 = 0.0f;
    block.rho = r.data();
    block.rhou = ru.data();
    block.rhov = rv.data();
    block.rhow = rw.data();
    block.rhoE = re.data();
    block.rhoY = ry.empty() ? nullptr : ry.data();
    block.solid = solidMask.data();
    block.solidU = solidVelX.data();
    block.solidV = solidVelY.data();
    block.solidW = solidVelZ.data();
    applyGridToBlock(block);
    return block;
}

void CompressibleRun::initialise() {
    if (hasRestartState)
        return;

    Block block = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    const bool tube = cfg.caseType == CaseType::ShockTube;
    const float baseDensity = cfg.pInf / (cfg.R * cfg.T0);
    const float speedOfSound = std::sqrt(cfg.gamma * cfg.R * cfg.T0);
    const bool blows = cfg.boundaries[BoundarySide::Left].kind ==
                       BoundaryKind::Inlet;
    const float speed = tube ? 0.0f : (blows ? cfg.machInlet * speedOfSound
                                             : 0.0f);

    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const float fx = block.cellX(i) / cfg.Lx;
            const float fy = block.cellY(j) / cfg.Ly;
            const float fz = block.cellZ(k) / cfg.Lz;

            Primitive q;
            q.u = speed;
            q.v = 0.0f;
            q.w = 0.0f;
            q.y = 0.0f;

            if (cfg.twoSpecies()) {
                switch (cfg.phaseInit) {
                case PhaseInit::Layer:
                    q.y = fy < cfg.phaseLevel ? 1.0f : 0.0f;
                    break;
                case PhaseInit::Column:
                    q.y = fx < cfg.phaseLevel ? 1.0f : 0.0f;
                    break;
                case PhaseInit::Drop:
                default: {
                    const float ddx = (fx - cfg.phaseX) * cfg.Lx;
                    const float ddy = (fy - cfg.phaseY) * cfg.Ly;
                    if (volumetric) {
                        const float ddz = (fz - cfg.phaseZ) * cfg.Lz;
                        const float radius =
                            0.5f * cfg.phaseLevel *
                            std::min(std::min(cfg.Lx, cfg.Ly), cfg.Lz);
                        q.y = std::hypot(ddx, ddy, ddz) < radius ? 1.0f : 0.0f;
                    } else {
                        const float radius = 0.5f * cfg.phaseLevel *
                                             std::min(cfg.Lx, cfg.Ly);
                        q.y = std::hypot(ddx, ddy) < radius ? 1.0f : 0.0f;
                    }
                    break;
                }
                }
            }

            const bool far = tube && fx > 0.5f;
            const float pressureFactor = far ? 0.1f : 1.0f;
            const float densityFactor = far ? 0.125f : 1.0f;

            q.gamma = gas.gammaOf(q.y);
            const float gasR = gas.gasConstantOf(q.y);
            q.p = cfg.pInf * pressureFactor;
            q.rho = cfg.pInf / (gasR * cfg.T0) * densityFactor;
            writeState(block, block.index(i, j, k), q);
        }

    fillSolidCells(block, gas);
    fillGhostCells(block, sides, gas);
}

bool CompressibleRun::setInitialState(RestartData&& state,
                                      const std::string& prefix) {
    const std::size_t cells = static_cast<std::size_t>(nx) * ny * nz;
    if (state.stateRho.size() != cells || state.stateRhoU.size() != cells ||
        state.stateRhoV.size() != cells || state.stateRhoE.size() != cells)
        return false;

    restartBodies = state.bodies;
    if (stretched && state.gridFaceX.size() == faceX.size() &&
        state.gridFaceY.size() == faceY.size()) {
        faceX = state.gridFaceX;
        faceY = state.gridFaceY;
        if (volumetric && state.gridFaceZ.size() == faceZ.size()) {
            faceZ = state.gridFaceZ;
            for (int k = 0; k < nz; ++k) {
                const std::size_t at = static_cast<std::size_t>(k);
                cellDepths[at + ghost] = faceZ[at + 1] - faceZ[at];
                cellCentresZ[at + ghost] =
                    0.5f * (faceZ[at] + faceZ[at + 1]);
            }
            for (int m = 1; m <= ghost; ++m) {
                cellDepths[static_cast<std::size_t>(ghost - m)] =
                    cellDepths[static_cast<std::size_t>(ghost)];
                cellDepths[static_cast<std::size_t>(ghost + nz - 1 + m)] =
                    cellDepths[static_cast<std::size_t>(ghost + nz - 1)];
                cellCentresZ[static_cast<std::size_t>(ghost - m)] =
                    cellCentresZ[static_cast<std::size_t>(ghost - m + 1)] -
                    cellDepths[static_cast<std::size_t>(ghost - m)];
                cellCentresZ[static_cast<std::size_t>(ghost + nz - 1 + m)] =
                    cellCentresZ[static_cast<std::size_t>(ghost + nz - 2 + m)] +
                    cellDepths[static_cast<std::size_t>(ghost + nz - 1 + m)];
            }
        }
        for (int i = 0; i < nx; ++i) {
            const std::size_t at = static_cast<std::size_t>(i);
            cellWidths[at + ghost] = faceX[at + 1] - faceX[at];
            cellCentresX[at + ghost] =
                0.5f * (faceX[at] + faceX[at + 1]);
        }
        for (int j = 0; j < ny; ++j) {
            const std::size_t at = static_cast<std::size_t>(j);
            cellHeights[at + ghost] = faceY[at + 1] - faceY[at];
            cellCentresY[at + ghost] =
                0.5f * (faceY[at] + faceY[at + 1]);
        }
        for (int k = 1; k <= ghost; ++k) {
            cellWidths[static_cast<std::size_t>(ghost - k)] =
                cellWidths[static_cast<std::size_t>(ghost)];
            cellWidths[static_cast<std::size_t>(ghost + nx - 1 + k)] =
                cellWidths[static_cast<std::size_t>(ghost + nx - 1)];
            cellHeights[static_cast<std::size_t>(ghost - k)] =
                cellHeights[static_cast<std::size_t>(ghost)];
            cellHeights[static_cast<std::size_t>(ghost + ny - 1 + k)] =
                cellHeights[static_cast<std::size_t>(ghost + ny - 1)];
            cellCentresX[static_cast<std::size_t>(ghost - k)] =
                cellCentresX[static_cast<std::size_t>(ghost - k + 1)] -
                cellWidths[static_cast<std::size_t>(ghost - k)];
            cellCentresX[static_cast<std::size_t>(ghost + nx - 1 + k)] =
                cellCentresX[static_cast<std::size_t>(ghost + nx - 2 + k)] +
                cellWidths[static_cast<std::size_t>(ghost + nx - 1 + k)];
            cellCentresY[static_cast<std::size_t>(ghost - k)] =
                cellCentresY[static_cast<std::size_t>(ghost - k + 1)] -
                cellHeights[static_cast<std::size_t>(ghost - k)];
            cellCentresY[static_cast<std::size_t>(ghost + ny - 1 + k)] =
                cellCentresY[static_cast<std::size_t>(ghost + ny - 2 + k)] +
                cellHeights[static_cast<std::size_t>(ghost + ny - 1 + k)];
        }
    }
    Block block = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    const bool species = cfg.twoSpecies() && state.stateRhoY.size() == cells;
    const bool spanwise = state.stateRhoW.size() == cells;
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const std::size_t flat =
                (static_cast<std::size_t>(k) * ny + j) * nx + i;
            const int id = block.index(i, j, k);
            rho[id] = state.stateRho[flat];
            rhou[id] = state.stateRhoU[flat];
            rhov[id] = state.stateRhoV[flat];
            rhow[id] = spanwise ? state.stateRhoW[flat] : 0.0f;
            rhoE[id] = state.stateRhoE[flat];
            if (block.rhoY)
                rhoY[id] = species ? state.stateRhoY[flat] : 0.0f;
        }

    currentTime = state.currentTime;
    step = state.step;
    dt = state.dt;
    hasRestartState = true;
    if (!prefix.empty())
        framePrefix = prefix;
    fillSolidCells(block, gas);
    fillGhostCells(block, sides, gas);
    return true;
}

void CompressibleRun::computeStep() {
    Block current = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    Block stage1 = view(rho1, rhou1, rhov1, rhow1, rhoE1, rhoY1);
    Block stage2 = view(rho2, rhou2, rhov2, rhow2, rhoE2, rhoY2);

    const float diffusivity = cfg.twoSpecies() ? cfg.diffusivity : 0.0f;

#ifdef USE_CUDA
    if (onDevice) {
        const int limiter = static_cast<int>(cfg.limiter);
        compressibleCudaStage(device, current, 0, 0, 1, gas, sides, dt, 0.0f,
                              1.0f, limiter, diffusivity);
        compressibleCudaStage(device, current, 1, 0, 2, gas, sides, dt, 0.75f,
                              0.25f, limiter, diffusivity);
        compressibleCudaStage(device, current, 2, 0, 0, gas, sides, dt,
                              1.0f / 3.0f, 2.0f / 3.0f, limiter, diffusivity);
        return;
    }
#endif

    if (driver) {
        driver->advance(current, stage1, stage2, work, dt);
        return;
    }

    advanceStage(current, current, stage1, sides, gas, dt, 0.0f, 1.0f,
                 cfg.limiter, diffusivity, work);
    advanceStage(stage1, current, stage2, sides, gas, dt, 0.75f, 0.25f,
                 cfg.limiter, diffusivity, work);
    advanceStage(stage2, current, current, sides, gas, dt,
                 1.0f / 3.0f, 2.0f / 3.0f, cfg.limiter, diffusivity, work);
}

void CompressibleRun::syncFromDevice() {
#ifdef USE_CUDA
    if (!onDevice)
        return;
    float* host[6] = {rho.data(), rhou.data(), rhov.data(), rhow.data(),
                      rhoE.data(), rhoY.empty() ? nullptr : rhoY.data()};
    compressibleCudaDownload(device, 0, host);
#endif
}

void CompressibleRun::syncToDevice() {
#ifdef USE_CUDA
    if (!onDevice)
        return;
    const float* host[6] = {rho.data(), rhou.data(), rhov.data(), rhow.data(),
                            rhoE.data(),
                            rhoY.empty() ? nullptr : rhoY.data()};
    compressibleCudaUpload(device, 0, host);
#endif
}

float CompressibleRun::timeStep(const Block& block) {
#ifdef USE_CUDA
    if (onDevice)
        return compressibleCudaTimeStep(device, block, gas, cfg.CFL);
#endif
    if (driver)
        return driver->finestRate(block, cfg.CFL);
    return blockTimeStep(block, gas, cfg.CFL);
}

void CompressibleRun::updateAcoustics(float stepDt) {
    if (!cfg.acousticFields || stepDt <= 0.0f)
        return;

    Block block = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    const float window = std::max(cfg.acousticWindow, stepDt);
    const float alpha = std::min(1.0f, stepDt / window);

    const float alphaFast = std::min(1.0f, 16.0f * stepDt / window);
    const float rate = 1.0f / stepDt;

    const int nx = this->nx;
    const int ny = this->ny;
    const int nz = this->nz;

    #pragma omp parallel for collapse(2) schedule(static) if (nz * ny >= 64)
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const std::size_t flat =
                (static_cast<std::size_t>(k) * ny + j) * nx + i;
            if (solidMask[flat]) {
                pressureMean[flat] = 0.0f;
                pressureFast[flat] = 0.0f;
                pressureRms[flat] = 0.0f;
                crossingRate[flat] = 0.0f;
                continue;
            }
            const Primitive q = primitiveOf(block, gas, block.index(i, j, k));
            if (!acousticsReady) {
                pressureMean[flat] = q.p;
                pressureFast[flat] = q.p;
                continue;
            }
            pressureMean[flat] += alpha * (q.p - pressureMean[flat]);
            pressureFast[flat] += alphaFast * (q.p - pressureFast[flat]);
            const float fluctuation = q.p - pressureMean[flat];
            pressureRms[flat] +=
                alpha * (fluctuation * fluctuation - pressureRms[flat]);

            const int8_t sign = q.p > pressureFast[flat] ? 1 : -1;
            const float crossed =
                (lastSign[flat] != 0 && sign != lastSign[flat]) ? rate : 0.0f;
            crossingRate[flat] += alpha * (crossed - crossingRate[flat]);
            lastSign[flat] = sign;
        }
    }
    acousticsReady = true;
}

void CompressibleRun::sampleMicrophones() {
    if (mics.empty() || (step % std::max(1, cfg.micInterval)) != 0)
        return;

    Block block = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    micTimes.push_back(static_cast<float>(currentTime));
    for (std::size_t m = 0; m < mics.size(); ++m) {
        const int i = columnAt(mics[m].x);
        const int j = rowAt(mics[m].y);
        const int k = planeAt(mics[m].z);
        const Primitive q = primitiveOf(block, gas, block.index(i, j, k));
        micSamples[m].push_back(q.p);
    }
}

void CompressibleRun::writeMicrophones() const {
    if (mics.empty() || micTimes.size() < 4)
        return;

    std::error_code directoryError;
    std::filesystem::create_directories(outputPath, directoryError);
    std::ofstream out(outputPath / "microphones.txt");
    if (!out.is_open())
        return;

    const std::size_t samples = micTimes.size();
    const double span = micTimes.back() - micTimes.front();
    const double rate = span > 0.0 ? (samples - 1) / span : 0.0;

    out << "# t";
    for (std::size_t m = 0; m < mics.size(); ++m)
        out << " p" << (m + 1);
    out << "\n";
    for (std::size_t k = 0; k < samples; ++k) {
        out << micTimes[k];
        for (const auto& channel : micSamples)
            out << " " << channel[k];
        out << "\n";
    }

    std::cout << "\nMicrophones (" << samples << " samples at "
              << rate << " Hz):\n";
    if (volumetric)
        out << "#\n# summary: x y z SPL_dB peak_Hz\n";
    else
        out << "#\n# summary: x y SPL_dB peak_Hz\n";

    constexpr int kBins = 512;
    for (std::size_t m = 0; m < mics.size(); ++m) {
        const std::vector<float>& channel = micSamples[m];
        double mean = 0.0;
        for (float value : channel)
            mean += value;
        mean /= static_cast<double>(samples);

        double energy = 0.0;
        for (float value : channel)
            energy += (value - mean) * (value - mean);
        const double rms = std::sqrt(energy / static_cast<double>(samples));
        const double spl =
            rms > 0.0 ? 20.0 * std::log10(rms / cfg.acousticRef) : 0.0;

        double bestPower = 0.0;
        double bestFrequency = 0.0;
        const double nyquist = 0.5 * rate;
        for (int bin = 1; bin <= kBins; ++bin) {
            const double frequency = nyquist * bin / (kBins + 1.0);
            const double omegaBin = 2.0 * 3.14159265358979 * frequency / rate;
            const double coefficient = 2.0 * std::cos(omegaBin);
            double s1 = 0.0, s2 = 0.0;
            for (float value : channel) {
                const double s0 = (value - mean) + coefficient * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            const double power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
            if (power > bestPower) {
                bestPower = power;
                bestFrequency = frequency;
            }
        }

        char line[200];
        if (volumetric)
            std::snprintf(line, sizeof(line),
                          "  mic %zu at (%.4g, %.4g, %.4g): %.1f dB, peak "
                          "%.1f Hz",
                          m + 1, static_cast<double>(mics[m].x),
                          static_cast<double>(mics[m].y),
                          static_cast<double>(mics[m].z), spl, bestFrequency);
        else
            std::snprintf(line, sizeof(line),
                          "  mic %zu at (%.4g, %.4g): %.1f dB, peak %.1f Hz",
                          m + 1, static_cast<double>(mics[m].x),
                          static_cast<double>(mics[m].y), spl, bestFrequency);
        std::cout << line << "\n";
        out << "# " << mics[m].x << " " << mics[m].y << " ";
        if (volumetric)
            out << mics[m].z << " ";
        out << spl << " " << bestFrequency << "\n";
    }
    std::cout << "Written " << pathToConsole(outputPath / "microphones.txt")
              << "\n";
}

std::vector<float> CompressibleRun::primitive(const char* what) const {
    const std::size_t cells = static_cast<std::size_t>(nx) * ny * nz;
    std::vector<float> out(cells, 0.0f);
    Block block = const_cast<CompressibleRun*>(this)->view(
        const_cast<std::vector<float>&>(rho),
        const_cast<std::vector<float>&>(rhou),
        const_cast<std::vector<float>&>(rhov),
        const_cast<std::vector<float>&>(rhow),
        const_cast<std::vector<float>&>(rhoE),
        const_cast<std::vector<float>&>(rhoY));

    const std::string key = what;
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const std::size_t flat =
                (static_cast<std::size_t>(k) * ny + j) * nx + i;
            const Primitive q = primitiveOf(block, gas, block.index(i, j, k));
            const float gasR = gas.gasConstantOf(q.y);
            const float temperature = q.p / (q.rho * gasR);
            const float speedOfSound = std::sqrt(q.gamma * q.p / q.rho);
            const float speed = volumetric ? std::hypot(q.u, q.v, q.w)
                                           : std::hypot(q.u, q.v);
            if (key == "pressure")
                out[flat] = q.p;
            else if (key == "density")
                out[flat] = q.rho;
            else if (key == "temperature")
                out[flat] = temperature;
            else if (key == "mach")
                out[flat] = speed / speedOfSound;
            else if (key == "speedofsound")
                out[flat] = speedOfSound;
            else if (key == "species")
                out[flat] = q.y;
            else if (key == "entropy")
                out[flat] = gasR / (q.gamma - 1.0f) *
                            std::log(q.p / std::pow(q.rho, q.gamma));
            else if (key == "u")
                out[flat] = q.u;
            else if (key == "v")
                out[flat] = q.v;
            else if (key == "w")
                out[flat] = q.w;
            else if (key == "speed")
                out[flat] = speed;
            else if (key == "pfluct")
                out[flat] = pressureMean.empty()
                                ? 0.0f
                                : q.p - pressureMean[flat];
            else if (key == "spl")
                out[flat] =
                    pressureRms.empty() || !(pressureRms[flat] > 0.0f)
                        ? 0.0f
                        : 20.0f * std::log10(std::sqrt(pressureRms[flat]) /
                                             cfg.acousticRef);
            else if (key == "pitch")
                out[flat] =
                    crossingRate.empty() ? 0.0f : 0.5f * crossingRate[flat];
        }
    return out;
}

void CompressibleRun::saveVTK(int stepNumber) const {
    constexpr std::size_t kBufferWords = 4096;
    std::vector<uint32_t> buffer(kBufferWords);
    std::error_code directoryError;
    std::filesystem::create_directories(outputPath, directoryError);

    std::filesystem::path filename = outputPath;
    filename /= framePrefix + "_" + std::to_string(stepNumber) + ".vtk";
    std::ofstream fout(filename, std::ios::binary);
    if (!fout) {
        std::cerr << "Cannot open " << pathToConsole(filename)
                  << " for writing.\n";
        return;
    }

    const auto writeArray = [&](const float* values, std::size_t count) {
        std::size_t done = 0;
        while (done < count) {
            const std::size_t take = std::min(kBufferWords, count - done);
            for (std::size_t k = 0; k < take; ++k) {
                uint32_t word;
                std::memcpy(&word, values + done + k, sizeof(float));
                buffer[k] = ((word & 0x000000FFu) << 24) |
                            ((word & 0x0000FF00u) << 8) |
                            ((word & 0x00FF0000u) >> 8) |
                            ((word & 0xFF000000u) >> 24);
            }
            fout.write(reinterpret_cast<const char*>(buffer.data()),
                       static_cast<std::streamsize>(take * sizeof(uint32_t)));
            done += take;
        }
    };

    fout << "# vtk DataFile Version 3.0\n"
         << "Fluid Solver output, step " << stepNumber << "\n"
         << "BINARY\n";
    if (stretched) {
        fout << "DATASET RECTILINEAR_GRID\n"
             << "DIMENSIONS " << nx + 1 << " " << ny + 1 << " " << nz + 1
             << "\n"
             << "X_COORDINATES " << nx + 1 << " float\n";
        writeArray(faceX.data(), faceX.size());
        fout << "\nY_COORDINATES " << ny + 1 << " float\n";
        writeArray(faceY.data(), faceY.size());
        fout << "\nZ_COORDINATES " << nz + 1 << " float\n";
        writeArray(faceZ.data(), faceZ.size());
        fout << "\n";
    } else {
        fout << "DATASET STRUCTURED_POINTS\n"
             << "DIMENSIONS " << nx + 1 << " " << ny + 1 << " " << nz + 1
             << "\n"
             << "ORIGIN 0 0 0\n"
             << "SPACING " << dx << " " << dy << " " << dz << "\n";
    }
    fout << "CELL_DATA " << nx * ny * nz << "\n";


    const auto writeScalar = [&](const char* name,
                                 const std::vector<float>& values) {
        fout << "SCALARS " << name << " float 1\nLOOKUP_TABLE default\n";
        writeArray(values.data(), values.size());
        fout << "\n";
    };

    const std::vector<float> pressure = primitive("pressure");
    const std::vector<float> density = primitive("density");
    writeScalar("pressure", pressure);
    writeScalar("density", density);
    if (cfg.twoSpecies())
        writeScalar("species", primitive("species"));

    fout << "SCALARS solid unsigned_char 1\nLOOKUP_TABLE default\n";
    fout.write(reinterpret_cast<const char*>(solidMask.data()),
               static_cast<std::streamsize>(solidMask.size()));
    fout << "\n";

    const std::vector<float> uCell = primitive("u");
    const std::vector<float> vCell = primitive("v");
    const std::vector<float> wCell = primitive("w");
    const std::size_t cells = static_cast<std::size_t>(nx) * ny * nz;
    std::vector<float> interleaved(cells * 3, 0.0f);
    for (std::size_t id = 0; id < cells; ++id) {
        interleaved[3 * id] = uCell[id];
        interleaved[3 * id + 1] = vCell[id];
        interleaved[3 * id + 2] = wCell[id];
    }
    fout << "VECTORS velocity float\n";
    writeArray(interleaved.data(), interleaved.size());
    fout << "\n";

    std::string wanted = cfg.extraFields;
    for (char& c : wanted)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::size_t position = 0;
    while (position <= wanted.size()) {
        const std::size_t comma = wanted.find(',', position);
        std::string key = wanted.substr(
            position, comma == std::string::npos ? std::string::npos
                                                 : comma - position);
        position = comma == std::string::npos ? wanted.size() + 1 : comma + 1;
        while (!key.empty() && key.front() == ' ')
            key.erase(key.begin());
        while (!key.empty() && key.back() == ' ')
            key.pop_back();
        if (key.empty() || key == "density" || key == "species" ||
            key == "pressure")
            continue;

        const char* name = nullptr;
        if (key == "temperature") name = "temperature";
        else if (key == "mach") name = "mach";
        else if (key == "speedofsound") name = "speedOfSound";
        else if (key == "entropy") name = "entropy";
        else if (key == "speed") name = "speed";
        else if (key == "objectid") name = "objectId";
        else if (key == "pfluct") name = "pFluctuation";
        else if (key == "spl") name = "SPL";
        else if (key == "pitch") name = "pitch";
        if (!name)
            continue;

        if (key == "objectid") {
            std::vector<float> ids(cells, 0.0f);
            for (std::size_t id = 0; id < cells; ++id)
                ids[id] = static_cast<float>(mesh.objectId[id]);
            writeScalar(name, ids);
        } else {
            writeScalar(name, primitive(key.c_str()));
        }
    }

    Config stored = cfg;
    stored.restart = true;
    std::string configText = stored.serialize();
    configText += "restartTime=" + std::to_string(currentTime) + "\n";
    configText += "restartStep=" + std::to_string(stepNumber) + "\n";
    configText += "restartDt=" + std::to_string(dt) + "\n";
    configText += "formatVersion=" + std::to_string(FRAME_FORMAT_VERSION) +
                  "\n";
    if (bodiesMove) {
        std::ostringstream bodyLine;
        bodyLine << "bodyState=";
        for (const RigidBody& body : bodies) {
            if (!body.everFree && !body.prescribed)
                continue;
            bodyLine << std::setprecision(
                            std::numeric_limits<double>::max_digits10)
                     << body.object << ":x=" << body.x << ",y=" << body.y
                     << ",z=" << body.z << ",qw=" << body.qw
                     << ",qx=" << body.qx << ",qy=" << body.qy
                     << ",qz=" << body.qz
                     << std::setprecision(
                            std::numeric_limits<float>::max_digits10)
                     << ",vx=" << body.vx << ",vy=" << body.vy
                     << ",vz=" << body.vz << ",omegaX=" << body.omegaX
                     << ",omegaY=" << body.omegaY << ",omega=" << body.omega
                     << ";";
        }
        bodyLine << "\n";
        configText += bodyLine.str();
    }

    const int arrays = (cfg.twoSpecies() ? 7 : 6) +
                       (stretched ? (volumetric ? 3 : 2) : 0);
    fout << "FIELD RestartData " << arrays << "\n";
    fout << "configText 1 " << configText.size() << " char\n";
    fout.write(configText.data(),
               static_cast<std::streamsize>(configText.size()));
    fout << "\n";

    Block block = const_cast<CompressibleRun*>(this)->view(
        const_cast<std::vector<float>&>(rho),
        const_cast<std::vector<float>&>(rhou),
        const_cast<std::vector<float>&>(rhov),
        const_cast<std::vector<float>&>(rhow),
        const_cast<std::vector<float>&>(rhoE),
        const_cast<std::vector<float>&>(rhoY));

    const auto writeField = [&](const char* name,
                                const std::vector<float>& source) {
        std::vector<float> packed(cells, 0.0f);
        for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const std::size_t flat =
                    (static_cast<std::size_t>(k) * ny + j) * nx + i;
                packed[flat] = source[block.index(i, j, k)];
            }
        fout << name << " 1 " << cells << " float\n";
        writeArray(packed.data(), packed.size());
        fout << "\n";
    };

    if (stretched) {
        fout << "gridFaceX 1 " << faceX.size() << " float\n";
        writeArray(faceX.data(), faceX.size());
        fout << "\ngridFaceY 1 " << faceY.size() << " float\n";
        writeArray(faceY.data(), faceY.size());
        if (volumetric) {
            fout << "\ngridFaceZ 1 " << faceZ.size() << " float\n";
            writeArray(faceZ.data(), faceZ.size());
        }
        fout << "\n";
    }

    writeField("stateRho", rho);
    writeField("stateRhoU", rhou);
    writeField("stateRhoV", rhov);
    writeField("stateRhoW", rhow);
    writeField("stateRhoE", rhoE);
    if (cfg.twoSpecies())
        writeField("stateRhoY", rhoY);

    if (stepNumber % (std::max(1, cfg.saveInterval) * 10) == 0 ||
        stepNumber == 0)
        std::cout << "Saved " << pathToConsole(filename) << std::endl;
}

void CompressibleRun::reportStep() const {
    Block block = const_cast<CompressibleRun*>(this)->view(
        const_cast<std::vector<float>&>(rho),
        const_cast<std::vector<float>&>(rhou),
        const_cast<std::vector<float>&>(rhov),
        const_cast<std::vector<float>&>(rhow),
        const_cast<std::vector<float>&>(rhoE),
        const_cast<std::vector<float>&>(rhoY));

    float peakMach = 0.0f, lowPressure = std::numeric_limits<float>::max();
    float highPressure = 0.0f;
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            if (solidMask[(static_cast<std::size_t>(k) * ny + j) * nx + i])
                continue;
            const Primitive q = primitiveOf(block, gas, block.index(i, j, k));
            const float speedOfSound = std::sqrt(q.gamma * q.p / q.rho);
            const float speed = volumetric ? std::hypot(q.u, q.v, q.w)
                                           : std::hypot(q.u, q.v);
            peakMach = std::max(peakMach, speed / speedOfSound);
            lowPressure = std::min(lowPressure, q.p);
            highPressure = std::max(highPressure, q.p);
        }

    char line[220];
    std::snprintf(line, sizeof(line),
                  "step %6d  t = %.6f s  dt = %.3e  Mach max %.3f  "
                  "p %.4g .. %.4g Pa",
                  step, currentTime, static_cast<double>(dt),
                  static_cast<double>(peakMach),
                  static_cast<double>(lowPressure),
                  static_cast<double>(highPressure));
    std::cout << line << "\n";
}

void CompressibleRun::run() {
    initialise();
    resolveBodyMotion();
    reportBodies();
    regridIfDue();
    reportAmr();

    std::error_code directoryError;
    std::filesystem::create_directories(outputPath, directoryError);

    Block block = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    syncToDevice();
    if (dt <= 0.0f)
        dt = timeStep(block) * cfg.dtSafety;

    const double target = cfg.restart && cfg.addTime > 0.0
                              ? currentTime + cfg.addTime
                              : cfg.totalTime;

    progress::begin("Fluid Solver", currentTime, target, cfg.outputDir);

    if (!hasRestartState)
        saveVTK(step);

    bool stopped = false;
    int sinceReport = 0;
    while (currentTime < target - 1e-12) {
        if ((step % std::max(1, cfg.dtUpdateInterval)) == 0)
            dt = timeStep(block) * cfg.dtSafety;
        if (!(dt > 0.0f)) {
            std::cerr << "\nThe time step collapsed to zero, which means the "
                         "state stopped being a state.\n";
            break;
        }
        const double remaining = target - currentTime;
        if (remaining <= 0.0)
            break;
        if (static_cast<double>(dt) > remaining) {
            const float clipped = static_cast<float>(remaining);
            if (!(clipped > 0.0f))
                break;
            dt = clipped;
        }

        regridIfDue();
        computeStep();
        if (bodiesMove) {
            syncFromDevice();
            advanceBodies(dt);
            syncToDevice();
        }
        currentTime += dt;
        ++step;
        ++sinceReport;

        const bool wanted = (step % std::max(1, cfg.saveInterval)) == 0;
        if (cfg.acousticFields || !mics.empty() || wanted)
            syncFromDevice();

        updateAcoustics(dt);
        sampleMicrophones();

        if (wanted) {
            saveVTK(step);
            writeAmrFrame(step);
        }

        progress::update(currentTime);
        if (sinceReport >= std::max(1, cfg.saveInterval)) {
            reportStep();
            sinceReport = 0;
        }
        if (progress::stopRequested()) {
            stopped = true;
            break;
        }
    }

    syncFromDevice();
    saveVTK(step);
    writeAmrFrame(step);
    reportStep();
    writeMicrophones();
    writeMicrophoneAudio();
    progress::finish(!stopped);

    std::cout << "\nSimulation finished at t = " << currentTime << " s after "
              << step << " steps.\n";
}

void CompressibleRun::resolveBodyMotion() {
    bodies.clear();
    bodiesMove = false;
    bodiesFree = false;
    if (cfg.bodyMotion.empty())
        return;

    std::vector<BodyMotion> motions;
    std::string error;
    if (!parseBodyMotion(cfg.bodyMotion, motions, error)) {
        std::cout << "\n!!! " << error << "\n    No body moves.\n";
        return;
    }
    if (motions.empty())
        return;

    if (mesh.objects.empty()) {
        std::cout << "\n!!! bodyMotion was given but the domain holds no body "
                     "at all, so there is nothing to move.\n";
        return;
    }
    if (!mesh.prepareMotion()) {
        std::cout << "\n!!! the mask this run started from cannot be moved: it "
                     "came out of a frame rather than\n    a model, and moving "
                     "a body means cutting its outline again every step. Give "
                     "the run a geometryFile\n    or profiles= and the bodies "
                     "will move.\n";
        return;
    }

    std::vector<BodyGeometry> geometry(mesh.objects.size() + 1);
    for (std::size_t id = 1; id < geometry.size(); ++id) {
        const Mesh::SolidObject& body = mesh.objects[id - 1];
        geometry[id].cx = static_cast<float>(body.cx);
        geometry[id].cy = static_cast<float>(body.cy);
        geometry[id].cz = static_cast<float>(body.cz);
        geometry[id].radius = static_cast<float>(body.radius);
        geometry[id].volume = static_cast<float>(body.volume);
        for (int term = 0; term < 9; ++term)
            geometry[id].inertia[term] =
                static_cast<float>(body.inertia[term]);
    }

    const float fluidDensity = cfg.pInf / (cfg.R * cfg.T0);
    std::vector<std::string> notes;
    buildRigidBodies(motions, geometry, bodies, fluidDensity, notes);
    for (const std::string& note : notes)
        std::cout << "\n!!! " << note << "\n";

    for (const RigidBody& body : bodies) {
        if (body.everFree)
            bodiesFree = true;
        if (body.everFree || body.prescribed)
            bodiesMove = true;
    }
    if (!bodiesMove)
        return;
    bodyCollisions = cfg.bodyCollisions;

    if (cfg.bodyCoupling == BodyCoupling::Weak)
        for (RigidBody& body : bodies) {
            body.addedMass = 0.0f;
            for (int term = 0; term < 9; ++term)
                body.addedInertia[term] = 0.0f;
        }

    for (RigidBody& body : bodies)
        body.sampleVelocity(currentTime);

    for (const RestartData::BodyState& saved : restartBodies) {
        if (saved.object < 1 ||
            static_cast<std::size_t>(saved.object) >= bodies.size())
            continue;
        RigidBody& body = bodies[saved.object];
        body.x = saved.x;
        body.y = saved.y;
        body.z = saved.z;
        body.qw = saved.qw;
        body.qx = saved.qx;
        body.qy = saved.qy;
        body.qz = saved.qz;
        if (body.free) {
            body.vx = saved.vx;
            body.vy = saved.vy;
            body.vz = saved.vz;
            body.omegaX = saved.omegaX;
            body.omegaY = saved.omegaY;
            body.omega = saved.omega;
        }
    }

    applyBodyPoses();
    refreshSolidMask();
    if (tree && tree->active())
        for (int which = 0; which < tree->depth(); ++which) {
            if (tree->level(which).patches.empty())
                break;
            tree->setSolidFromPoint(which, solidMask, nx, ny, nz);
        }
}

void CompressibleRun::reportBodies() const {
    if (!bodiesMove)
        return;

    constexpr float degToRad = 3.14159265358979f / 180.0f;
    std::cout << "Bodies that travel:\n";
    for (const RigidBody& body : bodies) {
        if (!body.everFree && !body.prescribed)
            continue;
        std::cout << "  object " << body.object << " at (" << body.cx << ", "
                  << body.cy;
        if (volumetric)
            std::cout << ", " << body.cz;
        std::cout << ") m, ";
        if (body.free)
            std::cout << "free, mass " << body.mass << " kg, added "
                      << body.addedMass << " kg";
        else
            std::cout << "on rails";
        if (body.pinX || body.pinY || body.pinZ || body.pinRotX ||
            body.pinRotY || body.pinRot) {
            std::cout << ", pinned in";
            if (body.pinX) std::cout << " x";
            if (body.pinY) std::cout << " y";
            if (body.pinZ) std::cout << " z";
            if (body.pinRotX) std::cout << " rotation about x";
            if (body.pinRotY) std::cout << " rotation about y";
            if (body.pinRot) std::cout << " rotation";
        }
        std::cout << "\n";
    }
    if (bodiesFree)
        std::cout << "  the force on a free body is the pressure integral "
                     "over its own faces; the wall\n  ghosts mirror about the "
                     "body velocity, so a moving body pushes on the gas and "
                     "the\n  gas pushes back.\n";
    if (bodyCollisions)
        std::cout << "  bodyCollisions is on, restitution "
                  << cfg.bodyRestitution << ".\n";
    (void)degToRad;
}

void CompressibleRun::applyBodyPoses() {
    for (const RigidBody& body : bodies) {
        if (!body.everFree && !body.prescribed)
            continue;
        Mesh::BodyPose pose;
        pose.x = body.x;
        pose.y = body.y;
        pose.z = body.z;
        pose.qw = body.qw;
        pose.qx = body.qx;
        pose.qy = body.qy;
        pose.qz = body.qz;
        mesh.setPose(body.object, pose);
    }
}

void CompressibleRun::refreshSolidMask() {
    const std::size_t cells = static_cast<std::size_t>(nx) * ny * nz;
    std::vector<uint8_t> before = solidMask;

    mesh.updateSolid();
    for (std::size_t id = 0; id < cells; ++id)
        solidMask[id] = mesh.solid[id] ? 1 : 0;

    const std::vector<int>& owner = mesh.ownership();
    std::fill(solidVelX.begin(), solidVelX.end(), 0.0f);
    std::fill(solidVelY.begin(), solidVelY.end(), 0.0f);
    std::fill(solidVelZ.begin(), solidVelZ.end(), 0.0f);
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const std::size_t flat =
                (static_cast<std::size_t>(k) * ny + j) * nx + i;
            if (!solidMask[flat])
                continue;
            const int id = owner.empty() ? 0 : owner[flat];
            if (id <= 0 || static_cast<std::size_t>(id) >= bodies.size())
                continue;
            const RigidBody& body = bodies[id];
            const float armX =
                (i + 0.5f) * dx - (body.cx + static_cast<float>(body.x));
            const float armY =
                (j + 0.5f) * dy - (body.cy + static_cast<float>(body.y));
            const float armZ =
                (k + 0.5f) * dz - (body.cz + static_cast<float>(body.z));
            solidVelX[flat] = body.vx - body.omega * armY + body.omegaY * armZ;
            solidVelY[flat] = body.vy + body.omega * armX - body.omegaX * armZ;
            solidVelZ[flat] = body.vz + body.omegaX * armY - body.omegaY * armX;
        }

    Block block = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const std::size_t flat =
                (static_cast<std::size_t>(k) * ny + j) * nx + i;
            if (solidMask[flat] || !before[flat])
                continue;

            float sumRho = 0.0f, sumP = 0.0f, sumY = 0.0f;
            int count = 0;
            const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                                       {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
            for (int m = 0; m < 6; ++m) {
                const int ni = i + offsets[m][0];
                const int nj = j + offsets[m][1];
                const int nk = k + offsets[m][2];
                if (ni < 0 || ni >= nx || nj < 0 || nj >= ny || nk < 0 ||
                    nk >= nz)
                    continue;
                if (solidMask[(static_cast<std::size_t>(nk) * ny + nj) * nx +
                              ni])
                    continue;
                const Primitive n =
                    primitiveOf(block, gas, block.index(ni, nj, nk));
                sumRho += n.rho;
                sumP += n.p;
                sumY += n.y;
                ++count;
            }

            Primitive q;
            if (count > 0) {
                const float inv = 1.0f / static_cast<float>(count);
                q.rho = sumRho * inv;
                q.p = sumP * inv;
                q.y = sumY * inv;
            } else {
                q.rho = cfg.pInf / (cfg.R * cfg.T0);
                q.p = cfg.pInf;
                q.y = 0.0f;
            }
            q.u = solidVelX[flat];
            q.v = solidVelY[flat];
            q.w = solidVelZ[flat];
            q.gamma = gammaOf(gas, q.y);
            writeState(block, block.index(i, j, k), q);
        }

#ifdef USE_CUDA
    if (onDevice)
        compressibleCudaUploadSolid(device, solidMask.data(),
                                    solidVelX.data(), solidVelY.data(),
                                    solidVelZ.data());
#endif
}

void CompressibleRun::bodyForces() {
    if (!bodiesMove)
        return;

    for (RigidBody& body : bodies) {
        body.forceX = 0.0f;
        body.forceY = 0.0f;
        body.forceZ = 0.0f;
        body.torqueX = 0.0f;
        body.torqueY = 0.0f;
        body.torque = 0.0f;
    }
    if (!bodiesFree)
        return;

    const std::vector<int>& owner = mesh.ownership();
    if (owner.empty())
        return;

    Block block = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    const int plane = nx * ny;
    const float faceX = dy * dz;
    const float faceY = dx * dz;
    const float faceZ = dx * dy;

    const auto push = [&](int solidIndex, float fx, float fy, float fz,
                          float px, float py, float pz) {
        const int id = owner[solidIndex];
        if (id <= 0 || static_cast<std::size_t>(id) >= bodies.size())
            return;
        RigidBody& body = bodies[id];
        const float armX = px - (body.cx + static_cast<float>(body.x));
        const float armY = py - (body.cy + static_cast<float>(body.y));
        const float armZ = pz - (body.cz + static_cast<float>(body.z));
        body.forceX += fx;
        body.forceY += fy;
        body.forceZ += fz;
        body.torqueX += armY * fz - armZ * fy;
        body.torqueY += armZ * fx - armX * fz;
        body.torque += armX * fy - armY * fx;
    };

    for (int k = 0; k < nz; ++k) {
        const float z = (k + 0.5f) * dz;
        for (int j = 0; j < ny; ++j) {
        const int row = (k * ny + j) * nx;
        const float y = (j + 0.5f) * dy;
        for (int i = 1; i < nx; ++i) {
            const bool left = solidMask[row + i - 1] != 0;
            const bool right = solidMask[row + i] != 0;
            if (left == right)
                continue;
            const int fluid = left ? i : i - 1;
            const Primitive q =
                primitiveOf(block, gas, block.index(fluid, j, k));
            const float force = q.p * faceX * (left ? -1.0f : 1.0f);
            push(row + (left ? i - 1 : i), force, 0.0f, 0.0f, i * dx, y, z);
        }
        }
    }

    for (int k = 0; k < nz; ++k) {
        const float z = (k + 0.5f) * dz;
        for (int j = 1; j < ny; ++j) {
        const int row = (k * ny + j) * nx;
        for (int i = 0; i < nx; ++i) {
            const bool low = solidMask[row - nx + i] != 0;
            const bool high = solidMask[row + i] != 0;
            if (low == high)
                continue;
            const int fluid = low ? j : j - 1;
            const Primitive q =
                primitiveOf(block, gas, block.index(i, fluid, k));
            const float force = q.p * faceY * (low ? -1.0f : 1.0f);
            push((low ? row - nx : row) + i, 0.0f, force, 0.0f,
                 (i + 0.5f) * dx, j * dy, z);
        }
        }
    }

    for (int k = 1; k < nz; ++k) {
        const float z = k * dz;
        for (int j = 0; j < ny; ++j) {
        const int row = (k * ny + j) * nx;
        const float y = (j + 0.5f) * dy;
        for (int i = 0; i < nx; ++i) {
            const bool front = solidMask[row - plane + i] != 0;
            const bool back = solidMask[row + i] != 0;
            if (front == back)
                continue;
            const int fluid = front ? k : k - 1;
            const Primitive q =
                primitiveOf(block, gas, block.index(i, j, fluid));
            const float force = q.p * faceZ * (front ? -1.0f : 1.0f);
            push((front ? row - plane : row) + i, 0.0f, 0.0f, force,
                 (i + 0.5f) * dx, y, z);
        }
        }
    }
}

void CompressibleRun::advanceBodies(float stepDt) {
    if (!bodiesMove)
        return;

    if (bodiesFree) {
        bodyForces();
        for (RigidBody& body : bodies)
            if (body.free)
                body.integrate(stepDt);
    }

    const double middle = currentTime + 0.5 * static_cast<double>(stepDt);
    for (RigidBody& body : bodies)
        if (body.prescribed || body.everFree)
            body.step(middle, stepDt);

    if (bodyCollisions)
        resolveBodyCollisions(bodies, mesh.ownership(), mesh.contested(),
                              cfg.nx, cfg.ny, cfg.nz, cfg.Lx, cfg.Ly, cfg.Lz,
                              cfg.bodyRestitution, stepDt, contactsReported);

    for (RigidBody& body : bodies)
        if (body.prescribed || body.everFree)
            body.advancePose(stepDt);

    applyBodyPoses();
    refreshSolidMask();
}

void CompressibleRun::writeMicrophoneAudio() const {
    if (!cfg.recordsAudio() || mics.empty() || micTimes.size() < 8)
        return;

    const double first = micTimes.front();
    const double span = micTimes.back() - first;
    if (!(span > 0.0))
        return;

    const double speed = cfg.micAudioSpeed > 0.0f ? cfg.micAudioSpeed : 1.0;
    const int rate = cfg.micAudioRate;
    const double audioSeconds = span / speed;
    const long long frames =
        static_cast<long long>(audioSeconds * rate);
    if (frames < 2)
        return;

    const std::size_t samples = micTimes.size();
    std::cout << "\nAudio (" << rate << " Hz, " << audioSeconds
              << " s per file";
    if (speed != 1.0)
        std::cout << ", " << span << " s of flow slowed by " << (1.0 / speed)
                  << "x";
    std::cout << "):\n";

    for (std::size_t m = 0; m < mics.size(); ++m) {
        const std::vector<float>& channel = micSamples[m];
        if (channel.size() != samples)
            continue;

        double mean = 0.0;
        for (float value : channel)
            mean += value;
        mean /= static_cast<double>(samples);

        std::vector<double> track(static_cast<std::size_t>(frames), 0.0);
        std::size_t cursor = 0;
        for (long long k = 0; k < frames; ++k) {
            const double windowStart =
                first + span * static_cast<double>(k) / frames;
            const double windowEnd =
                first + span * static_cast<double>(k + 1) / frames;

            while (cursor + 1 < samples && micTimes[cursor] < windowStart)
                ++cursor;

            double sum = 0.0;
            int count = 0;
            for (std::size_t s = cursor;
                 s < samples && micTimes[s] < windowEnd; ++s) {
                sum += channel[s] - mean;
                ++count;
            }
            if (count > 0) {
                track[static_cast<std::size_t>(k)] =
                    sum / static_cast<double>(count);
                continue;
            }

            const double when = 0.5 * (windowStart + windowEnd);
            std::size_t hi = cursor;
            while (hi + 1 < samples && micTimes[hi] < when)
                ++hi;
            const std::size_t lo = hi > 0 ? hi - 1 : 0;
            const double t0 = micTimes[lo];
            const double t1 = micTimes[hi];
            const double weight =
                t1 > t0 ? (when - t0) / (t1 - t0) : 0.0;
            track[static_cast<std::size_t>(k)] =
                (channel[lo] - mean) +
                weight * (channel[hi] - channel[lo]);
        }

        double drift = 0.0;
        for (double value : track)
            drift += value;
        drift /= static_cast<double>(track.size());

        double peak = 0.0;
        for (double& value : track) {
            value -= drift;
            peak = std::max(peak, std::fabs(value));
        }
        const double gain = peak > 0.0 ? 0.9 * 32767.0 / peak : 0.0;

        char name[64];
        std::snprintf(name, sizeof(name), "microphone%zu.wav", m + 1);
        const std::filesystem::path file = outputPath / name;
        std::ofstream out(file, std::ios::binary);
        if (!out.is_open())
            continue;

        const uint32_t dataBytes = static_cast<uint32_t>(frames * 2);
        const uint32_t sampleRate = static_cast<uint32_t>(rate);
        const auto put32 = [&](uint32_t value) {
            const unsigned char raw[4] = {
                static_cast<unsigned char>(value & 0xFF),
                static_cast<unsigned char>((value >> 8) & 0xFF),
                static_cast<unsigned char>((value >> 16) & 0xFF),
                static_cast<unsigned char>((value >> 24) & 0xFF)};
            out.write(reinterpret_cast<const char*>(raw), 4);
        };
        const auto put16 = [&](uint16_t value) {
            const unsigned char raw[2] = {
                static_cast<unsigned char>(value & 0xFF),
                static_cast<unsigned char>((value >> 8) & 0xFF)};
            out.write(reinterpret_cast<const char*>(raw), 2);
        };

        out.write("RIFF", 4);
        put32(36 + dataBytes);
        out.write("WAVE", 4);
        out.write("fmt ", 4);
        put32(16);
        put16(1);
        put16(1);
        put32(sampleRate);
        put32(sampleRate * 2);
        put16(2);
        put16(16);
        out.write("data", 4);
        put32(dataBytes);

        for (double value : track) {
            double scaled = value * gain;
            scaled = std::max(-32768.0, std::min(32767.0, scaled));
            put16(static_cast<uint16_t>(static_cast<int16_t>(
                scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5)));
        }
        out.close();

        char line[220];
        std::snprintf(line, sizeof(line),
                      "  mic %zu -> %s, full scale is %.4g Pa of fluctuation",
                      m + 1, name, peak);
        std::cout << line << "\n";
    }
    std::cout << "Written next to the frames in "
              << pathToConsole(outputPath) << "\n";
}

namespace {

std::vector<float> stretchAxis(int count,
                               double length,
                               double ratio,
                               double from,
                               double to) {
    std::vector<float> widths(static_cast<std::size_t>(count), 0.0f);
    if (count < 1)
        return widths;
    if (!(ratio > 1.0) || count < 4) {
        const float even = static_cast<float>(length / count);
        std::fill(widths.begin(), widths.end(), even);
        return widths;
    }

    const double even = length / count;
    const int first = std::clamp(static_cast<int>(from / even), 0, count - 1);
    const int last = std::clamp(static_cast<int>(to / even), first, count - 1);

    std::vector<double> weight(static_cast<std::size_t>(count), 1.0);
    double total = 0.0;
    for (int i = 0; i < count; ++i) {
        int away = 0;
        if (i < first)
            away = first - i;
        else if (i > last)
            away = i - last;
        weight[static_cast<std::size_t>(i)] = std::pow(ratio, away);
        total += weight[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < count; ++i)
        widths[static_cast<std::size_t>(i)] = static_cast<float>(
            length * weight[static_cast<std::size_t>(i)] / total);
    return widths;
}

std::vector<float> edgeAxis(int count, double length, double ratio) {
    std::vector<float> widths(static_cast<std::size_t>(count), 0.0f);
    if (count < 1)
        return widths;
    if (!(ratio > 1.0) || count < 4) {
        const float even = static_cast<float>(length / count);
        std::fill(widths.begin(), widths.end(), even);
        return widths;
    }
    std::vector<double> weight(static_cast<std::size_t>(count), 1.0);
    double total = 0.0;
    for (int i = 0; i < count; ++i) {
        const int away = std::min(i, count - 1 - i);
        weight[static_cast<std::size_t>(i)] = std::pow(ratio, away);
        total += weight[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < count; ++i)
        widths[static_cast<std::size_t>(i)] = static_cast<float>(
            length * weight[static_cast<std::size_t>(i)] / total);
    return widths;
}

}

void CompressibleRun::buildGrid() {
    stretched = cfg.stretchedGrid();

    const std::size_t across = static_cast<std::size_t>(nx) + 2 * ghost;
    const std::size_t along = static_cast<std::size_t>(ny) + 2 * ghost;
    const std::size_t through = static_cast<std::size_t>(nz) + 2 * ghost;
    cellWidths.assign(across, dx);
    cellHeights.assign(along, dy);
    cellDepths.assign(through, dz);
    cellCentresX.assign(across, 0.0f);
    cellCentresY.assign(along, 0.0f);
    cellCentresZ.assign(through, 0.0f);
    faceX.assign(static_cast<std::size_t>(nx) + 1, 0.0f);
    faceY.assign(static_cast<std::size_t>(ny) + 1, 0.0f);
    faceZ.assign(static_cast<std::size_t>(nz) + 1, 0.0f);

    std::vector<float> widths(static_cast<std::size_t>(nx), dx);
    std::vector<float> heights(static_cast<std::size_t>(ny), dy);
    std::vector<float> depths(static_cast<std::size_t>(nz), dz);

    if (stretched) {
        double lowX = 0.5 * cfg.Lx, highX = 0.5 * cfg.Lx;
        double lowY = 0.5 * cfg.Ly, highY = 0.5 * cfg.Ly;
        double lowZ = 0.5 * cfg.Lz, highZ = 0.5 * cfg.Lz;
        bool found = false;
        for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                if (!solidMask[(static_cast<std::size_t>(k) * ny + j) * nx + i])
                    continue;
                const double x = (i + 0.5) * dx;
                const double y = (j + 0.5) * dy;
                const double z = (k + 0.5) * dz;
                if (!found) {
                    lowX = highX = x;
                    lowY = highY = y;
                    lowZ = highZ = z;
                    found = true;
                    continue;
                }
                lowX = std::min(lowX, x);
                highX = std::max(highX, x);
                lowY = std::min(lowY, y);
                highY = std::max(highY, y);
                lowZ = std::min(lowZ, z);
                highZ = std::max(highZ, z);
            }

        const double padX = 0.5 * cfg.refineNear * cfg.Lx;
        const double padY = 0.5 * cfg.refineNear * cfg.Ly;
        const double padZ = 0.5 * cfg.refineNear * cfg.Lz;
        const double ratio = cfg.stretchRatio;

        switch (cfg.gridStretch) {
        case StretchKind::Edges:
            widths = edgeAxis(nx, cfg.Lx, ratio);
            heights = edgeAxis(ny, cfg.Ly, ratio);
            if (volumetric)
                depths = edgeAxis(nz, cfg.Lz, ratio);
            break;
        case StretchKind::Wake:
            widths = stretchAxis(nx, cfg.Lx, ratio,
                                 std::max(0.0, lowX - padX), cfg.Lx);
            heights = stretchAxis(ny, cfg.Ly, ratio,
                                  std::max(0.0, lowY - padY),
                                  std::min<double>(cfg.Ly, highY + padY));
            if (volumetric)
                depths = stretchAxis(nz, cfg.Lz, ratio,
                                     std::max(0.0, lowZ - padZ),
                                     std::min<double>(cfg.Lz, highZ + padZ));
            break;
        case StretchKind::Body:
        default:
            widths = stretchAxis(nx, cfg.Lx, ratio,
                                 std::max(0.0, lowX - padX),
                                 std::min<double>(cfg.Lx, highX + padX));
            heights = stretchAxis(ny, cfg.Ly, ratio,
                                  std::max(0.0, lowY - padY),
                                  std::min<double>(cfg.Ly, highY + padY));
            if (volumetric)
                depths = stretchAxis(nz, cfg.Lz, ratio,
                                     std::max(0.0, lowZ - padZ),
                                     std::min<double>(cfg.Lz, highZ + padZ));
            break;
        }
    }

    double walkX = 0.0;
    for (int i = 0; i < nx; ++i) {
        faceX[static_cast<std::size_t>(i)] = static_cast<float>(walkX);
        cellWidths[static_cast<std::size_t>(i) + ghost] =
            widths[static_cast<std::size_t>(i)];
        cellCentresX[static_cast<std::size_t>(i) + ghost] =
            static_cast<float>(walkX + 0.5 * widths[static_cast<std::size_t>(i)]);
        walkX += widths[static_cast<std::size_t>(i)];
    }
    faceX[static_cast<std::size_t>(nx)] = static_cast<float>(walkX);

    double walkY = 0.0;
    for (int j = 0; j < ny; ++j) {
        faceY[static_cast<std::size_t>(j)] = static_cast<float>(walkY);
        cellHeights[static_cast<std::size_t>(j) + ghost] =
            heights[static_cast<std::size_t>(j)];
        cellCentresY[static_cast<std::size_t>(j) + ghost] =
            static_cast<float>(walkY +
                               0.5 * heights[static_cast<std::size_t>(j)]);
        walkY += heights[static_cast<std::size_t>(j)];
    }
    faceY[static_cast<std::size_t>(ny)] = static_cast<float>(walkY);

    double walkZ = 0.0;
    for (int k = 0; k < nz; ++k) {
        faceZ[static_cast<std::size_t>(k)] = static_cast<float>(walkZ);
        cellDepths[static_cast<std::size_t>(k) + ghost] =
            depths[static_cast<std::size_t>(k)];
        cellCentresZ[static_cast<std::size_t>(k) + ghost] =
            static_cast<float>(walkZ +
                               0.5 * depths[static_cast<std::size_t>(k)]);
        walkZ += depths[static_cast<std::size_t>(k)];
    }
    faceZ[static_cast<std::size_t>(nz)] = static_cast<float>(walkZ);

    for (int k = 1; k <= ghost; ++k) {
        cellWidths[static_cast<std::size_t>(ghost - k)] =
            cellWidths[static_cast<std::size_t>(ghost)];
        cellWidths[static_cast<std::size_t>(ghost + nx - 1 + k)] =
            cellWidths[static_cast<std::size_t>(ghost + nx - 1)];
        cellHeights[static_cast<std::size_t>(ghost - k)] =
            cellHeights[static_cast<std::size_t>(ghost)];
        cellHeights[static_cast<std::size_t>(ghost + ny - 1 + k)] =
            cellHeights[static_cast<std::size_t>(ghost + ny - 1)];
        cellDepths[static_cast<std::size_t>(ghost - k)] =
            cellDepths[static_cast<std::size_t>(ghost)];
        cellDepths[static_cast<std::size_t>(ghost + nz - 1 + k)] =
            cellDepths[static_cast<std::size_t>(ghost + nz - 1)];

        cellCentresX[static_cast<std::size_t>(ghost - k)] =
            cellCentresX[static_cast<std::size_t>(ghost - k + 1)] -
            cellWidths[static_cast<std::size_t>(ghost - k)];
        cellCentresX[static_cast<std::size_t>(ghost + nx - 1 + k)] =
            cellCentresX[static_cast<std::size_t>(ghost + nx - 2 + k)] +
            cellWidths[static_cast<std::size_t>(ghost + nx - 1 + k)];
        cellCentresY[static_cast<std::size_t>(ghost - k)] =
            cellCentresY[static_cast<std::size_t>(ghost - k + 1)] -
            cellHeights[static_cast<std::size_t>(ghost - k)];
        cellCentresY[static_cast<std::size_t>(ghost + ny - 1 + k)] =
            cellCentresY[static_cast<std::size_t>(ghost + ny - 2 + k)] +
            cellHeights[static_cast<std::size_t>(ghost + ny - 1 + k)];
        cellCentresZ[static_cast<std::size_t>(ghost - k)] =
            cellCentresZ[static_cast<std::size_t>(ghost - k + 1)] -
            cellDepths[static_cast<std::size_t>(ghost - k)];
        cellCentresZ[static_cast<std::size_t>(ghost + nz - 1 + k)] =
            cellCentresZ[static_cast<std::size_t>(ghost + nz - 2 + k)] +
            cellDepths[static_cast<std::size_t>(ghost + nz - 1 + k)];
    }
}

void CompressibleRun::applyGridToBlock(Block& block) const {
    if (!stretched)
        return;
    block.widths = cellWidths.data();
    block.heights = cellHeights.data();
    block.centresX = cellCentresX.data();
    block.centresY = cellCentresY.data();
    if (volumetric) {
        block.depths = cellDepths.data();
        block.centresZ = cellCentresZ.data();
    }
}

int CompressibleRun::columnAt(float x) const {
    if (!stretched)
        return std::clamp(static_cast<int>(x / dx), 0, nx - 1);
    for (int i = 0; i < nx; ++i)
        if (x < faceX[static_cast<std::size_t>(i) + 1])
            return i;
    return nx - 1;
}

int CompressibleRun::rowAt(float y) const {
    if (!stretched)
        return std::clamp(static_cast<int>(y / dy), 0, ny - 1);
    for (int j = 0; j < ny; ++j)
        if (y < faceY[static_cast<std::size_t>(j) + 1])
            return j;
    return ny - 1;
}

int CompressibleRun::planeAt(float z) const {
    if (!volumetric)
        return 0;
    if (!stretched)
        return std::clamp(static_cast<int>(z / dz), 0, nz - 1);
    for (int k = 0; k < nz; ++k)
        if (z < faceZ[static_cast<std::size_t>(k) + 1])
            return k;
    return nz - 1;
}

void CompressibleRun::setUpAmr() {
    if (!cfg.adaptive())
        return;

    amr = std::make_unique<AmrSettings>();
    amr->levels = cfg.amrLevels;
    amr->regridEvery = cfg.amrEvery;
    amr->threshold = cfg.amrThreshold;
    amr->buffer = cfg.amrBuffer;
    amr->minSide = cfg.amrMinPatch;
    amr->maxSide = cfg.amrMaxPatch;
    parseAmrCriterion(cfg.amrCriterion, amr->criterion);

    tree = std::make_unique<AmrHierarchy>();
    tree->build(*amr, nx, ny, nz, dx, dy, dz, cfg.twoSpecies());

    driver = std::make_unique<AmrDriver>(
        *tree, gas, sides, cfg.limiter,
        cfg.twoSpecies() ? cfg.diffusivity : 0.0f);
}

void CompressibleRun::regridIfDue() {
    if (!tree || !amr)
        return;
    if (sinceRegrid > 0 && sinceRegrid < amr->regridEvery) {
        ++sinceRegrid;
        return;
    }
    sinceRegrid = 1;

    Block current = view(rho, rhou, rhov, rhow, rhoE, rhoY);
    tree->regrid(current, gas, *amr, solidMask);
}

void CompressibleRun::reportAmr() const {
    if (!tree || !tree->active())
        return;
    std::vector<int> patches;
    std::vector<long long> cells;
    tree->describe(patches, cells);
    const long long base = static_cast<long long>(nx) * ny * nz;
    long long total = base;
    std::cout << "Adaptive mesh: base " << nx << " x " << ny;
    if (volumetric)
        std::cout << " x " << nz;
    std::cout << " = " << base << " cells\n";
    for (std::size_t which = 0; which < patches.size(); ++which) {
        total += cells[which];
        std::cout << "  level " << (which + 1) << ": " << patches[which]
                  << " patches, " << cells[which] << " cells, "
                  << (1 << (which + 1)) << "x finer\n";
    }
    const double equivalent =
        static_cast<double>(base) *
        std::pow(volumetric ? 8.0 : 4.0,
                 static_cast<double>(patches.size()));
    std::cout << "  " << total << " cells against " << equivalent
              << " for the same resolution everywhere";
    if (equivalent > 0.0)
        std::cout << ", " << (100.0 * total / equivalent) << "% of it";
    std::cout << "\n";
}

void CompressibleRun::writeAmrFrame(int stepNumber) const {
    if (!tree || !tree->active())
        return;

    std::error_code directoryError;
    std::filesystem::create_directories(outputPath, directoryError);

    const std::string stem =
        framePrefix + "_" + std::to_string(stepNumber) + "_amr";
    std::vector<std::string> written;

    const auto writeAxis = [](std::ostream& out, const char* name, int count,
                              double origin, double step) {
        out << "        <" << name << ">\n          <DataArray type=\"Float32\" "
            << "format=\"ascii\" NumberOfComponents=\"1\">\n           ";
        for (int k = 0; k <= count; ++k)
            out << ' ' << (origin + k * step);
        out << "\n          </DataArray>\n        </" << name << ">\n";
    };

    for (int which = 0; which < tree->depth(); ++which) {
        const AmrLevel& here = tree->level(which);
        for (std::size_t index = 0; index < here.patches.size(); ++index) {
            const AmrPatch& patch = here.patches[index];
            const std::string name = stem + "_L" + std::to_string(which + 1) +
                                     "_P" + std::to_string(index) + ".vtr";
            std::ofstream out(outputPath / name);
            if (!out.is_open())
                continue;

            Block block = patch.view(0, here.dx, here.dy, here.dz);
            const int pnx = patch.box.nx;
            const int pny = patch.box.ny;
            const int pnz = patch.box.nz;
            const int extentZ = volumetric ? pnz : 0;
            out << "<?xml version=\"1.0\"?>\n"
                << "<VTKFile type=\"RectilinearGrid\" version=\"0.1\" "
                   "byte_order=\"LittleEndian\">\n"
                << "  <RectilinearGrid WholeExtent=\"0 " << pnx << " 0 " << pny
                << " 0 " << extentZ << "\">\n"
                << "    <Piece Extent=\"0 " << pnx << " 0 " << pny << " 0 "
                << extentZ << "\">\n"
                << "      <Coordinates>\n";
            writeAxis(out, "DataArray", pnx, patch.box.i0 * here.dx, here.dx);
            writeAxis(out, "DataArray", pny, patch.box.j0 * here.dy, here.dy);
            if (volumetric)
                writeAxis(out, "DataArray", pnz, patch.box.k0 * here.dz,
                          here.dz);
            else
                out << "        <DataArray type=\"Float32\" format=\"ascii\" "
                       "NumberOfComponents=\"1\">\n           0 0\n"
                       "        </DataArray>\n";
            out << "      </Coordinates>\n"
                << "      <CellData Scalars=\"density\">\n";

            const auto field = [&](const char* label, auto value) {
                out << "        <DataArray type=\"Float32\" Name=\"" << label
                    << "\" format=\"ascii\">\n         ";
                for (int k = 0; k < pnz; ++k)
                    for (int j = 0; j < pny; ++j)
                        for (int i = 0; i < pnx; ++i)
                            out << ' ' << value(i, j, k);
                out << "\n        </DataArray>\n";
            };

            field("density", [&](int i, int j, int k) {
                return primitiveOf(block, gas, block.index(i, j, k)).rho;
            });
            field("pressure", [&](int i, int j, int k) {
                return primitiveOf(block, gas, block.index(i, j, k)).p;
            });
            field("velocityX", [&](int i, int j, int k) {
                return primitiveOf(block, gas, block.index(i, j, k)).u;
            });
            field("velocityY", [&](int i, int j, int k) {
                return primitiveOf(block, gas, block.index(i, j, k)).v;
            });
            if (volumetric)
                field("velocityZ", [&](int i, int j, int k) {
                    return primitiveOf(block, gas, block.index(i, j, k)).w;
                });
            field("solid", [&](int i, int j, int k) {
                return static_cast<int>(
                    patch.solid[(static_cast<std::size_t>(k) * pny + j) * pnx +
                                i]);
            });
            field("level", [&](int, int, int) { return which + 1; });

            out << "      </CellData>\n    </Piece>\n  </RectilinearGrid>\n"
                << "</VTKFile>\n";
            written.push_back(name);
        }
    }

    std::ofstream index(outputPath / (stem + ".vtm"));
    if (!index.is_open())
        return;
    index << "<?xml version=\"1.0\"?>\n"
          << "<VTKFile type=\"vtkMultiBlockDataSet\" version=\"1.0\" "
             "byte_order=\"LittleEndian\">\n"
          << "  <vtkMultiBlockDataSet>\n"
          << "    <Block index=\"0\" name=\"base\">\n"
          << "      <DataSet index=\"0\" file=\"" << framePrefix << "_"
          << stepNumber << ".vtk\"/>\n"
          << "    </Block>\n";
    for (std::size_t which = 0; which < written.size(); ++which)
        index << "    <Block index=\"" << (which + 1) << "\" name=\""
              << written[which] << "\">\n      <DataSet index=\"0\" file=\""
              << written[which] << "\"/>\n    </Block>\n";
    index << "  </vtkMultiBlockDataSet>\n</VTKFile>\n";
}
