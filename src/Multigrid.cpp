#include "Multigrid.hpp"
#include <cstdlib>
#include "Runtime.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#ifdef __AVX2__
#include <immintrin.h>
#endif

static const int PRE_SMOOTH_SWEEPS = 2;
static const int POST_SMOOTH_SWEEPS = 2;
static const int COARSE_SMOOTH_SWEEPS = 50;
// Only spawn OpenMP threads when the level is tall enough to pay for them
static const int PARALLEL_ROWS_MIN = 32;

namespace {
// On the coarsest level the smoother acts as a solver, so it needs enough
// sweeps to push information across the whole grid
int coarseSweeps(int nx, int ny, int nz) {
    const int planar = (nx > ny) ? nx : ny;
    const int wanted = 2 * ((planar > nz) ? planar : nz);
    if (wanted < COARSE_SMOOTH_SWEEPS) return COARSE_SMOOTH_SWEEPS;
    return (wanted > 400) ? 400 : wanted;
}

#ifdef __AVX2__
// Sum of the 8 lanes
float horizontalSum(__m256 v) {
    __m128 lo = _mm256_castps256_ps128(v);
    __m128 hi = _mm256_extractf128_ps(v, 1);
    lo = _mm_add_ps(lo, hi);
    lo = _mm_add_ps(lo, _mm_movehl_ps(lo, lo));
    lo = _mm_add_ss(lo, _mm_shuffle_ps(lo, lo, 0x55));
    return _mm_cvtss_f32(lo);
}
#endif
}

Multigrid::Multigrid(int nx, int ny, int nz, float dx, float dy, float dz,
                     int minCoarseSize)
    :
    nx(nx),
    ny(ny),
    nz(nz),
    dx(dx),
    dy(dy),
    dz(dz),
    minCoarseSize(minCoarseSize < 4 ? 4 : minCoarseSize)
{
}

Multigrid::~Multigrid() {
#ifdef USE_CUDA
    freeDevice();
#endif
}

void Multigrid::buildHierarchy() {
    gridLevels.clear();
    int curNx = nx;
    int curNy = ny;
    int curNz = nz;
    float curDx = dx;
    float curDy = dy;
    float curDz = dz;

    while (true) {
        Level grid;
        grid.nx = curNx;
        grid.ny = curNy;
        grid.nz = curNz;
        grid.cellCount = curNx * curNy * curNz;
        grid.dx = curDx;
        grid.dy = curDy;
        grid.dz = curDz;

        const int halo = std::max(curNx * curNy, 8);
        grid.pressure.init(grid.cellCount, halo);
        grid.residual.init(grid.cellCount, halo);

        const size_t padded = static_cast<size_t>(grid.cellCount) + 16u;
        grid.rhs.assign(padded, 0.0f);
        grid.coefW.assign(padded, 0.0f);
        grid.coefE.assign(padded, 0.0f);
        grid.coefS.assign(padded, 0.0f);
        grid.coefN.assign(padded, 0.0f);
        grid.coefF.assign(padded, 0.0f);
        grid.coefB.assign(padded, 0.0f);
        grid.diag.assign(padded, 0.0f);
        grid.invDiag.assign(padded, 0.0f);
        grid.solid.assign(static_cast<size_t>(grid.cellCount), 0);
        grid.faceX.assign(
            static_cast<size_t>(grid.nx + 1) * grid.ny * grid.nz, 1.0f);
        grid.faceY.assign(
            static_cast<size_t>(grid.nx) * (grid.ny + 1) * grid.nz, 1.0f);
        grid.faceZ.assign(
            static_cast<size_t>(grid.nx) * grid.ny * (grid.nz + 1), 1.0f);
        gridLevels.push_back(std::move(grid));

        // Semi-coarsening: a point smoother only damps the error along the axis
        // it is strongly coupled to, so on a grid with dx << dy we coarsen the
        // over-resolved axis alone to drive the coarse grids towards isotropy.
        // Only an even count is coarsened, otherwise the last coarse cell would
        // cover a single fine cell and restriction would stop being the
        // transpose of prolongation, which makes the V-cycle diverge.
        bool canX = (curNx > minCoarseSize) && (curNx % 2 == 0);
        bool canY = (curNy > minCoarseSize) && (curNy % 2 == 0);
        bool canZ = (curNz > minCoarseSize) && (curNz % 2 == 0);

        // Not a strict comparison: a ratio of exactly two is the case the rule
        // exists for, and Lx=2, Ly=1 on a square cell count lands on it every
        // time. Letting it through as isotropic carried the anisotropy down
        // the whole hierarchy and cost a level and most of the convergence.
        float smallest = 0.0f;
        if (canX && (smallest == 0.0f || curDx < smallest)) smallest = curDx;
        if (canY && (smallest == 0.0f || curDy < smallest)) smallest = curDy;
        if (canZ && (smallest == 0.0f || curDz < smallest)) smallest = curDz;
        if (canX && curDx >= 2.0f * smallest) canX = false;
        if (canY && curDy >= 2.0f * smallest) canY = false;
        if (canZ && curDz >= 2.0f * smallest) canZ = false;

        if (!canX && !canY && !canZ)
            break;

        const int nextNx = canX ? (curNx + 1) / 2 : curNx;
        const int nextNy = canY ? (curNy + 1) / 2 : curNy;
        const int nextNz = canZ ? (curNz + 1) / 2 : curNz;
        curDx *= static_cast<float>(curNx) / static_cast<float>(nextNx);
        curDy *= static_cast<float>(curNy) / static_cast<float>(nextNy);
        curDz *= static_cast<float>(curNz) / static_cast<float>(nextNz);
        gridLevels.back().refineX = canX ? 2 : 1;
        gridLevels.back().refineY = canY ? 2 : 1;
        gridLevels.back().refineZ = canZ ? 2 : 1;
        curNx = nextNx;
        curNy = nextNy;
        curNz = nextNz;
    }

    levels = static_cast<int>(gridLevels.size());
}

namespace {
// The two coarse cells a fine cell interpolates from, and their weights
struct Stencil1D {
    int coarse0, coarse1;
    float weight0, weight1;
};

Stencil1D transferStencil(int i, int refine, int coarseN) {
    Stencil1D s;
    if (refine == 1) {
        s.coarse0 = s.coarse1 = i;
        s.weight0 = 1.0f;
        s.weight1 = 0.0f;
        return s;
    }

    s.coarse0 = i >> 1;
    s.coarse1 =
        ((i & 1) == 0) ?
        (s.coarse0 > 0 ? s.coarse0 - 1 : s.coarse0) :
        (s.coarse0 + 1 < coarseN ? s.coarse0 + 1 : s.coarse0);

    if (s.coarse1 == s.coarse0) {
        s.weight0 = 1.0f;
        s.weight1 = 0.0f;
    } else {
        s.weight0 = 0.75f;
        s.weight1 = 0.25f;
    }
    return s;
}
}

void Multigrid::buildTransferTables(int fineLevel) {
    Level& fine = gridLevels[fineLevel];
    const int coarseLevel = fineLevel + 1;
    fine.transferX.clear();
    fine.transferY.clear();
    fine.transferZ.clear();
    if (coarseLevel >= levels)
        return;
    const Level& coarse = gridLevels[coarseLevel];

    fine.transferX.resize(static_cast<size_t>(fine.nx));
    for (int i = 0; i < fine.nx; ++i) {
        const Stencil1D s = transferStencil(i, fine.refineX, coarse.nx);
        fine.transferX[i] = {s.coarse0, s.coarse1, s.weight0, s.weight1};
    }
    fine.transferY.resize(static_cast<size_t>(fine.ny));
    for (int j = 0; j < fine.ny; ++j) {
        const Stencil1D s = transferStencil(j, fine.refineY, coarse.ny);
        fine.transferY[j] = {s.coarse0, s.coarse1, s.weight0, s.weight1};
    }
    fine.transferZ.resize(static_cast<size_t>(fine.nz));
    for (int k = 0; k < fine.nz; ++k) {
        const Stencil1D s = transferStencil(k, fine.refineZ, coarse.nz);
        fine.transferZ[k] = {s.coarse0, s.coarse1, s.weight0, s.weight1};
    }

    const auto invert = [](const std::vector<Level::Transfer>& forward,
                           int coarseCount,
                           std::vector<Level::Gather>& out) {
        out.assign(static_cast<size_t>(coarseCount), Level::Gather{});
        for (int fine = 0; fine < static_cast<int>(forward.size()); ++fine) {
            const Level::Transfer& entry = forward[fine];
            const auto add = [&](int coarse, float weight) {
                if (weight == 0.0f || coarse < 0 || coarse >= coarseCount)
                    return;
                Level::Gather& target = out[static_cast<size_t>(coarse)];
                for (int k = 0; k < target.count; ++k)
                    if (target.fine[k] == fine) {
                        target.weight[k] += weight;
                        return;
                    }
                if (target.count < 4) {
                    target.fine[target.count] = fine;
                    target.weight[target.count] = weight;
                    ++target.count;
                }
            };
            add(entry.coarse0, entry.weight0);
            add(entry.coarse1, entry.weight1);
        }
    };
    invert(fine.transferX, coarse.nx, fine.gatherX);
    invert(fine.transferY, coarse.ny, fine.gatherY);
    invert(fine.transferZ, coarse.nz, fine.gatherZ);
}

void Multigrid::markSolidLevels() {
    for (int l = 0; l < levels; ++l) {
        Level& grid = gridLevels[l];
        grid.anySolid = false;
        for (uint8_t value : grid.solid)
            if (value) {
                grid.anySolid = true;
                break;
            }
    }
}

void Multigrid::buildTransferWeights(int fineLevel) {
    const int coarseLevel = fineLevel + 1;
    Level& fine = gridLevels[fineLevel];

    fine.prolongWeight.assign(static_cast<size_t>(fine.cellCount), 0.0f);

    if (fine.transferX.empty() || fine.transferY.empty() ||
        fine.transferZ.empty())
        buildTransferTables(fineLevel);
    if (coarseLevel >= levels)
        return;

    const Level& coarse = gridLevels[coarseLevel];
    const int coarsePlane = coarse.nx * coarse.ny;
    #pragma omp parallel for collapse(2) schedule(static) \
        if (fine.nz * fine.ny >= PARALLEL_ROWS_MIN)
    for (int k = 0; k < fine.nz; ++k) {
        for (int j = 0; j < fine.ny; ++j) {
            const Level::Transfer sz = fine.transferZ[k];
            const Level::Transfer sy = fine.transferY[j];
            for (int i = 0; i < fine.nx; ++i) {
                const int fineId = (k * fine.ny + j) * fine.nx + i;
                if (fine.solid[fineId] || fine.diag[fineId] == 0.0f)
                    continue;
                const Level::Transfer sx = fine.transferX[i];

                const int coarseX[8] = {
                    sx.coarse0, sx.coarse1, sx.coarse0, sx.coarse1,
                    sx.coarse0, sx.coarse1, sx.coarse0, sx.coarse1};
                const int coarseY[8] = {
                    sy.coarse0, sy.coarse0, sy.coarse1, sy.coarse1,
                    sy.coarse0, sy.coarse0, sy.coarse1, sy.coarse1};
                const int coarseZ[8] = {
                    sz.coarse0, sz.coarse0, sz.coarse0, sz.coarse0,
                    sz.coarse1, sz.coarse1, sz.coarse1, sz.coarse1};
                const float weights[8] = {
                    sx.weight0 * sy.weight0 * sz.weight0,
                    sx.weight1 * sy.weight0 * sz.weight0,
                    sx.weight0 * sy.weight1 * sz.weight0,
                    sx.weight1 * sy.weight1 * sz.weight0,
                    sx.weight0 * sy.weight0 * sz.weight1,
                    sx.weight1 * sy.weight0 * sz.weight1,
                    sx.weight0 * sy.weight1 * sz.weight1,
                    sx.weight1 * sy.weight1 * sz.weight1};

                float weight = 0.0f;
                for (int c = 0; c < 8; ++c) {
                    if (weights[c] == 0.0f)
                        continue;
                    if (!coarse.solid[coarseZ[c] * coarsePlane +
                                      coarseY[c] * coarse.nx + coarseX[c]])
                        weight += weights[c];
                }
                fine.prolongWeight[fineId] = weight;
            }
        }
    }
}

void Multigrid::buildCoefficients(Level& grid) {
    const int nx = grid.nx;
    const int ny = grid.ny;
    const int nz = grid.nz;
    const int plane = nx * ny;
    const float invDx2 = 1.0f / (grid.dx * grid.dx);
    const float invDy2 = 1.0f / (grid.dy * grid.dy);
    const float invDz2 = 1.0f / (grid.dz * grid.dz);

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= PARALLEL_ROWS_MIN)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            const int row = (k * ny + j) * nx;
            for (int i = 0; i < nx; ++i) {
                const int id = row + i;

                if (grid.solid[id]) {
                    grid.coefW[id] = grid.coefE[id] = 0.0f;
                    grid.coefS[id] = grid.coefN[id] = 0.0f;
                    grid.coefF[id] = grid.coefB[id] = 0.0f;
                    grid.diag[id] = 0.0f;
                    grid.invDiag[id] = 0.0f;
                    continue;
                }
                // A link is only opened towards a fluid neighbour, so the solid
                // walls are baked into the stencil instead of patched afterwards
                const int faceRowX = (k * ny + j) * (nx + 1);
                const int faceRowY = (k * (ny + 1) + j) * nx;
                const int faceRowZ = row;
                const float wW = grid.faceX[faceRowX + i];
                const float wE = grid.faceX[faceRowX + i + 1];
                const float wS = grid.faceY[faceRowY + i];
                const float wN = grid.faceY[faceRowY + nx + i];
                const float wF = grid.faceZ[faceRowZ + i];
                const float wB = grid.faceZ[faceRowZ + plane + i];

                const float coefW = (i > 0      && !grid.solid[id - 1])  ? wW * invDx2 : 0.0f;
                const float coefE = (i < nx - 1 && !grid.solid[id + 1])  ? wE * invDx2 : 0.0f;
                const float coefS = (j > 0      && !grid.solid[id - nx]) ? wS * invDy2 : 0.0f;
                const float coefN = (j < ny - 1 && !grid.solid[id + nx]) ? wN * invDy2 : 0.0f;
                const float coefF = (k > 0      && !grid.solid[id - plane]) ? wF * invDz2 : 0.0f;
                const float coefB = (k < nz - 1 && !grid.solid[id + plane]) ? wB * invDz2 : 0.0f;

                float diag = coefW + coefE + coefS + coefN + coefF + coefB;

                if (i == 0 && pressureBC.left == PressureSideBC::Dirichlet)
                    diag += 2.0f * wW * invDx2;
                if (i == nx - 1 && pressureBC.right == PressureSideBC::Dirichlet)
                    diag += 2.0f * wE * invDx2;
                if (j == 0 && pressureBC.bottom == PressureSideBC::Dirichlet)
                    diag += 2.0f * wS * invDy2;
                if (j == ny - 1 && pressureBC.top == PressureSideBC::Dirichlet)
                    diag += 2.0f * wN * invDy2;
                if (k == 0 && pressureBC.front == PressureSideBC::Dirichlet)
                    diag += 2.0f * wF * invDz2;
                if (k == nz - 1 && pressureBC.back == PressureSideBC::Dirichlet)
                    diag += 2.0f * wB * invDz2;

                grid.coefW[id] = coefW;
                grid.coefE[id] = coefE;
                grid.coefS[id] = coefS;
                grid.coefN[id] = coefN;
                grid.coefF[id] = coefF;
                grid.coefB[id] = coefB;

                if (diag > 0.0f) {
                    grid.diag[id] = diag;
                    grid.invDiag[id] = 1.0f / diag;
                } else {
                    grid.diag[id] = 0.0f;
                    grid.invDiag[id] = 0.0f;
                }
            }
        }
    }
}

void Multigrid::setGeometry(const std::vector<uint8_t>& solid,
                            bool keepSolution) {
    if (gridLevels.empty())
        buildHierarchy();

    std::copy(solid.begin(),
              solid.begin() + gridLevels[0].cellCount,
              gridLevels[0].solid.begin());

    // A coarse cell is solid only if every fine cell under it is solid
    for (int l = 1; l < levels; ++l) {
        const Level& fine = gridLevels[l - 1];
        Level& coarse = gridLevels[l];

        for (int k = 0; k < coarse.nz; ++k) {
            for (int j = 0; j < coarse.ny; ++j) {
                for (int i = 0; i < coarse.nx; ++i) {
                    const int i0 = i * fine.refineX;
                    const int i1 = std::min(i0 + fine.refineX - 1, fine.nx - 1);
                    const int j0 = j * fine.refineY;
                    const int j1 = std::min(j0 + fine.refineY - 1, fine.ny - 1);
                    const int k0 = k * fine.refineZ;
                    const int k1 = std::min(k0 + fine.refineZ - 1, fine.nz - 1);

                    bool allSolid = true;
                    for (int kk = k0; kk <= k1 && allSolid; ++kk)
                        for (int jj = j0; jj <= j1 && allSolid; ++jj)
                            for (int ii = i0; ii <= i1 && allSolid; ++ii)
                                if (!fine.solid[(kk * fine.ny + jj) * fine.nx + ii])
                                    allSolid = false;

                    coarse.solid[(k * coarse.ny + j) * coarse.nx + i] =
                        allSolid ? 1 : 0;
                }
            }
        }
    }

    coarsenFaceWeights();

    for (int l = 0; l < levels; ++l)
        buildCoefficients(gridLevels[l]);

    markSolidLevels();
    for (int l = 0; l < levels; ++l)
        buildTransferWeights(l);

    geometryReady = true;
    if (!keepSolution)
        firstSolve = true;

#ifdef USE_CUDA
    if (useCuda)
        setGeometryCuda(keepSolution);
#endif
}

void Multigrid::setPressureBC(const MultigridBC& bc) {
    pressureBC = bc;
    pressureSingular =
        bc.left != PressureSideBC::Dirichlet &&
        bc.right != PressureSideBC::Dirichlet &&
        bc.bottom != PressureSideBC::Dirichlet &&
        bc.top != PressureSideBC::Dirichlet &&
        bc.front != PressureSideBC::Dirichlet &&
        bc.back != PressureSideBC::Dirichlet;
    if (geometryReady)
        rebuildCoefficients();
}

void Multigrid::setCoefficients(const std::vector<float>& faceX,
                                const std::vector<float>& faceY,
                                const std::vector<float>& faceZ) {
    if (gridLevels.empty())
        buildHierarchy();

    const size_t wantX = static_cast<size_t>(nx + 1) * ny * nz;
    const size_t wantY = static_cast<size_t>(nx) * (ny + 1) * nz;
    const size_t wantZ = static_cast<size_t>(nx) * ny * (nz + 1);
    const bool uniform = faceX.size() < wantX || faceY.size() < wantY;

    if (uniform) {
        if (coefficientsUniform)
            return;
        std::fill(gridLevels[0].faceX.begin(), gridLevels[0].faceX.end(), 1.0f);
        std::fill(gridLevels[0].faceY.begin(), gridLevels[0].faceY.end(), 1.0f);
        std::fill(gridLevels[0].faceZ.begin(), gridLevels[0].faceZ.end(), 1.0f);
        coefficientsUniform = true;
    } else {
        std::copy(faceX.begin(), faceX.begin() + wantX,
                  gridLevels[0].faceX.begin());
        std::copy(faceY.begin(), faceY.begin() + wantY,
                  gridLevels[0].faceY.begin());
        if (faceZ.size() < wantZ)
            std::fill(gridLevels[0].faceZ.begin(), gridLevels[0].faceZ.end(),
                      1.0f);
        else
            std::copy(faceZ.begin(), faceZ.begin() + wantZ,
                      gridLevels[0].faceZ.begin());
        coefficientsUniform = false;
    }

    if (geometryReady)
        rebuildCoefficients();
}

void Multigrid::rebuildCoefficients() {
    coarsenFaceWeights();
    for (int l = 0; l < levels; ++l)
        buildCoefficients(gridLevels[l]);

#ifdef USE_CUDA
    if (useCuda && deviceReady)
        uploadCoefficientsCuda();
#endif
}

void Multigrid::coarsenFaceWeights() {
    if (coefficientsUniform) {
        for (int l = 1; l < levels; ++l) {
            std::fill(gridLevels[l].faceX.begin(),
                      gridLevels[l].faceX.end(), 1.0f);
            std::fill(gridLevels[l].faceY.begin(),
                      gridLevels[l].faceY.end(), 1.0f);
            std::fill(gridLevels[l].faceZ.begin(),
                      gridLevels[l].faceZ.end(), 1.0f);
        }
        return;
    }

    for (int l = 1; l < levels; ++l) {
        const Level& fine = gridLevels[l - 1];
        Level& coarse = gridLevels[l];
        const int rx = fine.refineX;
        const int ry = fine.refineY;
        const int rz = fine.refineZ;

        #pragma omp parallel for collapse(2) schedule(static) \
            if (coarse.nz * coarse.ny >= PARALLEL_ROWS_MIN)
        for (int k = 0; k < coarse.nz; ++k) {
            for (int j = 0; j < coarse.ny; ++j) {
                for (int i = 0; i <= coarse.nx; ++i) {
                    const int fi = std::min(i * rx, fine.nx);
                    float total = 0.0f;
                    int count = 0;
                    for (int kk = k * rz;
                         kk < std::min((k + 1) * rz, fine.nz);
                         ++kk) {
                        for (int jj = j * ry;
                             jj < std::min((j + 1) * ry, fine.ny);
                             ++jj) {
                            total += fine.faceX[(kk * fine.ny + jj) *
                                                (fine.nx + 1) + fi];
                            ++count;
                        }
                    }
                    coarse.faceX[(k * coarse.ny + j) * (coarse.nx + 1) + i] =
                        count ? total / static_cast<float>(count) : 1.0f;
                }
            }
        }

        #pragma omp parallel for collapse(2) schedule(static) \
            if (coarse.nz * coarse.ny >= PARALLEL_ROWS_MIN)
        for (int k = 0; k < coarse.nz; ++k) {
            for (int j = 0; j <= coarse.ny; ++j) {
                const int fj = std::min(j * ry, fine.ny);
                for (int i = 0; i < coarse.nx; ++i) {
                    float total = 0.0f;
                    int count = 0;
                    for (int kk = k * rz;
                         kk < std::min((k + 1) * rz, fine.nz);
                         ++kk) {
                        for (int ii = i * rx;
                             ii < std::min((i + 1) * rx, fine.nx);
                             ++ii) {
                            total += fine.faceY[(kk * (fine.ny + 1) + fj) *
                                                fine.nx + ii];
                            ++count;
                        }
                    }
                    coarse.faceY[(k * (coarse.ny + 1) + j) * coarse.nx + i] =
                        count ? total / static_cast<float>(count) : 1.0f;
                }
            }
        }

        #pragma omp parallel for collapse(2) schedule(static) \
            if (coarse.nz * coarse.ny >= PARALLEL_ROWS_MIN)
        for (int k = 0; k <= coarse.nz; ++k) {
            for (int j = 0; j < coarse.ny; ++j) {
                const int fk = std::min(k * rz, fine.nz);
                for (int i = 0; i < coarse.nx; ++i) {
                    float total = 0.0f;
                    int count = 0;
                    for (int jj = j * ry;
                         jj < std::min((j + 1) * ry, fine.ny);
                         ++jj) {
                        for (int ii = i * rx;
                             ii < std::min((i + 1) * rx, fine.nx);
                             ++ii) {
                            total += fine.faceZ[(fk * fine.ny + jj) *
                                                fine.nx + ii];
                            ++count;
                        }
                    }
                    coarse.faceZ[(k * coarse.ny + j) * coarse.nx + i] =
                        count ? total / static_cast<float>(count) : 1.0f;
                }
            }
        }
    }
}

void Multigrid::removeNullSpace(float* values, const Level& grid) const {
    if (!pressureSingular)
        return;

    double total = 0.0;
    int count = 0;
    const uint8_t* __restrict solid = grid.solid.data();
    for (int id = 0; id < grid.cellCount; ++id) {
        if (solid[id])
            continue;
        total += static_cast<double>(values[id]);
        ++count;
    }
    if (count < 1)
        return;

    const float mean = static_cast<float>(total / count);
    int id = 0;
#ifdef __AVX2__
    if (runtime::avx2) {
        const __m256 meanVec = _mm256_set1_ps(mean);
        const __m256i zeroBytes = _mm256_setzero_si256();
        for (; id + 8 <= grid.cellCount; id += 8) {
            const __m128i bytes =
                _mm_loadl_epi64(reinterpret_cast<const __m128i*>(solid + id));
            const __m256i wide = _mm256_cvtepu8_epi32(bytes);
            const __m256 keep = _mm256_castsi256_ps(
                _mm256_cmpeq_epi32(wide, zeroBytes));
            const __m256 shifted =
                _mm256_sub_ps(_mm256_loadu_ps(values + id), meanVec);
            _mm256_storeu_ps(values + id, _mm256_and_ps(keep, shifted));
        }
    }
#endif
    for (; id < grid.cellCount; ++id)
        values[id] = solid[id] ? 0.0f : values[id] - mean;
}

void Multigrid::setUseCuda(bool enable) {
#ifdef USE_CUDA
    // A build with the toolkit in it still runs on machines with no device
    // behind it, and every allocation on that path ends in abort(). Asking
    // first costs one call and turns a dead process into a CPU run.
    useCuda = enable && cudaDeviceAvailable();
    if (enable && !useCuda)
        std::fprintf(stderr,
                     "No usable CUDA device; the pressure solve runs on the "
                     "CPU.\n");
    if (useCuda && geometryReady)
        setGeometryCuda(false);
#else
    (void)enable;
    useCuda = false;
#endif
}

void Multigrid::smoothSOR(
    int level,
    float omega,
    int sweeps)
{
    Level& grid = gridLevels[level];
    const int nx = grid.nx;
    const int ny = grid.ny;
    const int nz = grid.nz;
    const int plane = nx * ny;

    float* const       pressure = grid.pressure.data();
    const float* const rhs      = grid.rhs.data();
    const float* const coefW    = grid.coefW.data();
    const float* const coefE    = grid.coefE.data();
    const float* const coefS    = grid.coefS.data();
    const float* const coefN    = grid.coefN.data();
    const float* const coefF    = grid.coefF.data();
    const float* const coefB    = grid.coefB.data();
    const float* const invDiag  = grid.invDiag.data();

#ifdef __AVX2__
    // Red-black ordering: only every second lane of a vector is written back
    const __m256i laneEven = _mm256_setr_epi32(-1, 0, -1, 0, -1, 0, -1, 0);
    const __m256i laneOdd  = _mm256_setr_epi32(0, -1, 0, -1, 0, -1, 0, -1);
    const __m256 omegaVec  = _mm256_set1_ps(omega);
#endif

    #pragma omp parallel if (nz * ny >= PARALLEL_ROWS_MIN)
    for (int sweep = 0; sweep < sweeps; ++sweep) {
        for (int color = 0; color < 2; ++color) {
            #pragma omp for collapse(2) schedule(static)
            for (int k = 0; k < nz; ++k) {
                for (int j = 0; j < ny; ++j) {
                    const int row = (k * ny + j) * nx;
                    const int parity = color ^ ((j + k) & 1);

                    int i = 0;
#ifdef __AVX2__
                    const __m256i lane = parity ? laneOdd : laneEven;
                    for (; runtime::avx2 && i + 8 <= nx; i += 8) {
                        const int id = row + i;

                        const __m256 pCentre =
                            _mm256_loadu_ps(pressure + id);
                        const __m256 pLeft =
                            _mm256_loadu_ps(pressure + id - 1);
                        const __m256 pRight =
                            _mm256_loadu_ps(pressure + id + 1);
                        const __m256 pBot =
                            _mm256_loadu_ps(pressure + id - nx);
                        const __m256 pTop =
                            _mm256_loadu_ps(pressure + id + nx);
                        const __m256 pFront =
                            _mm256_loadu_ps(pressure + id - plane);
                        const __m256 pBack =
                            _mm256_loadu_ps(pressure + id + plane);

                        __m256 sum =
                            _mm256_mul_ps(_mm256_loadu_ps(coefW + id), pLeft);
                        sum = _mm256_add_ps(sum,
                            _mm256_mul_ps(_mm256_loadu_ps(coefE + id), pRight));
                        sum = _mm256_add_ps(sum,
                            _mm256_mul_ps(_mm256_loadu_ps(coefS + id), pBot));
                        sum = _mm256_add_ps(sum,
                            _mm256_mul_ps(_mm256_loadu_ps(coefN + id), pTop));
                        sum = _mm256_add_ps(sum,
                            _mm256_mul_ps(_mm256_loadu_ps(coefF + id), pFront));
                        sum = _mm256_add_ps(sum,
                            _mm256_mul_ps(_mm256_loadu_ps(coefB + id), pBack));

                        const __m256 pNew =
                            _mm256_mul_ps(
                                _mm256_sub_ps(sum, _mm256_loadu_ps(rhs + id)),
                                _mm256_loadu_ps(invDiag + id));

                        const __m256 relaxed =
                            _mm256_add_ps(
                                pCentre,
                                _mm256_mul_ps(
                                    omegaVec,
                                    _mm256_sub_ps(pNew, pCentre)));

                        _mm256_maskstore_ps(pressure + id, lane, relaxed);
                    }
#endif
                    // Whatever the vector loop left, and on a build without AVX2
                    // the whole row: same red-black stride, same arithmetic.
                    for (int ii = i + parity; ii < nx; ii += 2) {
                        const int id = row + ii;
                        const float sum =
                            coefW[id] * pressure[id - 1] +
                            coefE[id] * pressure[id + 1] +
                            coefS[id] * pressure[id - nx] +
                            coefN[id] * pressure[id + nx] +
                            coefF[id] * pressure[id - plane] +
                            coefB[id] * pressure[id + plane];
                        const float pNew = (sum - rhs[id]) * invDiag[id];
                        pressure[id] += omega * (pNew - pressure[id]);
                    }
                }
            }
        }
    }
}

void Multigrid::computeResidual(int level) {
    Level& grid = gridLevels[level];
    const int nx = grid.nx;
    const int ny = grid.ny;
    const int nz = grid.nz;
    const int plane = nx * ny;

    const float* const pressure = grid.pressure.data();
    const float* const rhs      = grid.rhs.data();
    const float* const coefW    = grid.coefW.data();
    const float* const coefE    = grid.coefE.data();
    const float* const coefS    = grid.coefS.data();
    const float* const coefN    = grid.coefN.data();
    const float* const coefF    = grid.coefF.data();
    const float* const coefB    = grid.coefB.data();
    const float* const diag     = grid.diag.data();
    float* const       residual = grid.residual.data();

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= PARALLEL_ROWS_MIN)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            const int row = (k * ny + j) * nx;

            int i = 0;
#ifdef __AVX2__
            for (; runtime::avx2 && i + 8 <= nx; i += 8) {
                const int id = row + i;
                const __m256 pCentre =
                    _mm256_loadu_ps(pressure + id);
                const __m256 pLeft =
                    _mm256_loadu_ps(pressure + id - 1);
                const __m256 pRight =
                    _mm256_loadu_ps(pressure + id + 1);
                const __m256 pBot =
                    _mm256_loadu_ps(pressure + id - nx);
                const __m256 pTop =
                    _mm256_loadu_ps(pressure + id + nx);
                const __m256 pFront =
                    _mm256_loadu_ps(pressure + id - plane);
                const __m256 pBack =
                    _mm256_loadu_ps(pressure + id + plane);

                __m256 sum =
                    _mm256_mul_ps(_mm256_loadu_ps(coefW + id), pLeft);
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefE + id), pRight));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefS + id), pBot));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefN + id), pTop));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefF + id), pFront));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefB + id), pBack));

                // Ap = (neighbour sum) - diag * p, residual = rhs - Ap
                const __m256 Ap =
                    _mm256_sub_ps(
                        sum,
                        _mm256_mul_ps(_mm256_loadu_ps(diag + id), pCentre));

                _mm256_storeu_ps(
                    residual + id,
                    _mm256_sub_ps(_mm256_loadu_ps(rhs + id), Ap));
            }
#endif

            for (; i < nx; ++i) {
                const int id = row + i;
                const float sum =
                    coefW[id] * pressure[id - 1] +
                    coefE[id] * pressure[id + 1] +
                    coefS[id] * pressure[id - nx] +
                    coefN[id] * pressure[id + nx] +
                    coefF[id] * pressure[id - plane] +
                    coefB[id] * pressure[id + plane];
                residual[id] = rhs[id] - (sum - diag[id] * pressure[id]);
            }
        }
    }
}

void Multigrid::applyOperator(int level, const float* x, float* out) const {
    const Level& grid = gridLevels[level];
    const int nx = grid.nx;
    const int ny = grid.ny;
    const int nz = grid.nz;
    const int plane = nx * ny;
    const float* const coefW = grid.coefW.data();
    const float* const coefE = grid.coefE.data();
    const float* const coefS = grid.coefS.data();
    const float* const coefN = grid.coefN.data();
    const float* const coefF = grid.coefF.data();
    const float* const coefB = grid.coefB.data();
    const float* const diag = grid.diag.data();

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= PARALLEL_ROWS_MIN)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            const int row = (k * ny + j) * nx;
            int i = 0;
#ifdef __AVX2__
            for (; runtime::avx2 && i + 8 <= nx; i += 8) {
                const int id = row + i;
                __m256 sum = _mm256_mul_ps(_mm256_loadu_ps(coefW + id),
                                           _mm256_loadu_ps(x + id - 1));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefE + id),
                                  _mm256_loadu_ps(x + id + 1)));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefS + id),
                                  _mm256_loadu_ps(x + id - nx)));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefN + id),
                                  _mm256_loadu_ps(x + id + nx)));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefF + id),
                                  _mm256_loadu_ps(x + id - plane)));
                sum = _mm256_add_ps(sum,
                    _mm256_mul_ps(_mm256_loadu_ps(coefB + id),
                                  _mm256_loadu_ps(x + id + plane)));
                _mm256_storeu_ps(
                    out + id,
                    _mm256_sub_ps(_mm256_mul_ps(_mm256_loadu_ps(diag + id),
                                                _mm256_loadu_ps(x + id)),
                                  sum));
            }
#endif
            for (; i < nx; ++i) {
                const int id = row + i;
                out[id] = diag[id] * x[id] -
                          (coefW[id] * x[id - 1] + coefE[id] * x[id + 1] +
                           coefS[id] * x[id - nx] + coefN[id] * x[id + nx] +
                           coefF[id] * x[id - plane] +
                           coefB[id] * x[id + plane]);
            }
        }
    }
}

namespace {
double dotProduct(const float* a, const float* b, int count) {
    double total = 0.0;
    #pragma omp parallel for schedule(static) reduction(+ : total) \
        if (count >= 4096)
    for (int id = 0; id < count; ++id)
        total += static_cast<double>(a[id]) * b[id];
    return total;
}
}

float Multigrid::solvePCG(std::vector<float>& pressure,
                          const std::vector<float>& rhs,
                          float smootherOmega,
                          float coarseOmega,
                          int maxCycles,
                          float tolerance,
                          float rhsScale) {
    Level& finest = gridLevels[0];
    const int count = finest.cellCount;
    const int halo = finest.nx * finest.ny + 8;

    if (static_cast<int>(cgPrevR.size()) != count) {
        cgX.init(count, halo);
        cgR.init(count, halo);
        cgZ.init(count, halo);
        cgD.init(count, halo);
        cgQ.init(count, halo);
        cgPrevR.assign(count, 0.0f);
    }

    float* const x = cgX.data();
    float* const r = cgR.data();
    float* const z = cgZ.data();
    float* const d = cgD.data();
    float* const q = cgQ.data();
    float* const previous = cgPrevR.data();
    float* const levelPressure = finest.pressure.data();
    float* const levelRhs = finest.rhs.data();

    #pragma omp parallel for schedule(static)
    for (int id = 0; id < count; ++id) {
        const bool active = finest.diag[id] > 0.0f;
        x[id] = active ? pressure[id] : 0.0f;
        levelRhs[id] = active ? rhs[id] : 0.0f;
    }
    removeNullSpace(levelRhs, finest);

    std::vector<float> b(count);
    for (int id = 0; id < count; ++id)
        b[id] = -levelRhs[id];

    const float norm = (rhsScale > 0.0f && !pressureSingular)
                           ? rhsScale
                           : computeVectorNorm(levelRhs, count);
    const float scale = (norm > 1e-20f) ? norm : 1.0f;

    if (firstSolve) {
        std::copy(x, x + count, levelPressure);
        fullMultigrid(smootherOmega, coarseOmega);
        std::copy(levelPressure, levelPressure + count, x);
        firstSolve = false;
    }

    applyOperator(0, x, q);
    #pragma omp parallel for schedule(static)
    for (int id = 0; id < count; ++id)
        r[id] = (finest.diag[id] > 0.0f) ? b[id] - q[id] : 0.0f;
    removeNullSpace(r, finest);

    lastCycles = 0;
    float relative = computeVectorNorm(r, count) / scale;
    double rzOld = 0.0;

    for (int cycle = 0; cycle < maxCycles && relative >= tolerance; ++cycle) {
        #pragma omp parallel for schedule(static)
        for (int id = 0; id < count; ++id)
            levelRhs[id] = -r[id];
        finest.pressure.zero();
        vCycle(0, smootherOmega, coarseOmega);
        removeNullSpace(levelPressure, finest);
        std::copy(levelPressure, levelPressure + count, z);

        const double rz = dotProduct(r, z, count);
        if (cycle == 0) {
            std::copy(z, z + count, d);
        } else {
            double numerator = 0.0;
            #pragma omp parallel for schedule(static) reduction(+ : numerator) \
                if (count >= 4096)
            for (int id = 0; id < count; ++id)
                numerator += static_cast<double>(r[id] - previous[id]) * z[id];
            const double beta =
                (std::fabs(rzOld) > 1e-300) ? std::max(0.0, numerator / rzOld)
                                            : 0.0;
            #pragma omp parallel for schedule(static)
            for (int id = 0; id < count; ++id)
                d[id] = z[id] + static_cast<float>(beta) * d[id];
        }

        applyOperator(0, d, q);
        const double dq = dotProduct(d, q, count);
        if (!(std::fabs(dq) > 1e-300))
            break;
        const float alpha = static_cast<float>(rz / dq);
        if (!std::isfinite(alpha))
            break;

        std::copy(r, r + count, previous);
        #pragma omp parallel for schedule(static)
        for (int id = 0; id < count; ++id) {
            x[id] += alpha * d[id];
            r[id] -= alpha * q[id];
        }
        removeNullSpace(r, finest);
        rzOld = rz;
        ++lastCycles;
        relative = computeVectorNorm(r, count) / scale;
    }

    removeNullSpace(x, finest);
    #pragma omp parallel for schedule(static)
    for (int id = 0; id < count; ++id) {
        pressure[id] = x[id];
        levelPressure[id] = x[id];
    }
    return relative;
}

void Multigrid::dampCorrection(int level) {
    Level& grid = gridLevels[level];
    const int count = grid.cellCount;
    float* const pressure = grid.pressure.data();
    const float* const saved = grid.savedPressure.data();
    const float* const before = grid.savedResidual.data();

    computeResidual(level);
    const float* const after = grid.residual.data();

    double numerator = 0.0;
    double denominator = 0.0;
    #pragma omp parallel for schedule(static) \
        reduction(+ : numerator, denominator) if (count >= 4096)
    for (int id = 0; id < count; ++id) {
        const double applied = static_cast<double>(before[id]) - after[id];
        numerator += static_cast<double>(before[id]) * applied;
        denominator += applied * applied;
    }

    float alpha = 1.0f;
    if (denominator > 1e-30) {
        alpha = static_cast<float>(numerator / denominator);
        if (!std::isfinite(alpha))
            alpha = 1.0f;
        alpha = std::min(2.0f, std::max(0.0f, alpha));
    }
    if (alpha == 1.0f)
        return;

    #pragma omp parallel for schedule(static) if (count >= 4096)
    for (int id = 0; id < count; ++id)
        pressure[id] = saved[id] + alpha * (pressure[id] - saved[id]);
}

float Multigrid::computeVectorNorm(const float* values, int count) {
    double total = 0.0;
    int start = 0;
#ifdef __AVX2__
    // Same runtime switch as the two kernels above: with AVX2 turned off the
    // whole vector block is skipped and start stays at zero, so the scalar
    // loop below covers every element rather than only the tail.
    if (runtime::avx2) {
    #pragma omp parallel reduction(+ : total) if (count >= 8192)
    {
        __m256 acc = _mm256_setzero_ps();

        #pragma omp for schedule(static) nowait
        for (int i = 0; i <= count - 8; i += 8) {
            const __m256 x = _mm256_loadu_ps(values + i);
            acc = _mm256_add_ps(acc, _mm256_mul_ps(x, x));
        }
        total += static_cast<double>(horizontalSum(acc));
    }
    start = (count / 8) * 8;
    }
#endif
    for (int i = start; i < count; ++i)
        total += static_cast<double>(values[i]) * static_cast<double>(values[i]);

    return static_cast<float>(std::sqrt(total));
}

float Multigrid::computeResidualNorm(int level) const {
    const Level& grid = gridLevels[level];
    return computeVectorNorm(grid.residual.data(), grid.cellCount);
}

// Grid transfer. The restriction is the exact transpose of the prolongation
// divided by the number of fine cells per coarse cell.
// The previous implementation restricted with a plain 2x2 average while
// prolongating bilinearly, so R was not P^T, and for some grid sizes the
// V-cycle amplified the error instead of reducing it - which is exactly what
// blew the solver up on 100x100 and 64x128 while 128x128 worked fine
// (during the tests, u know ehehe).

void Multigrid::restrictField(int fineLevel, const float* fineSrc) {
    const int coarseLevel = fineLevel + 1;
    if (coarseLevel >= levels)
        return;

    const Level& fine = gridLevels[fineLevel];
    Level& coarse = gridLevels[coarseLevel];

    const int refineX = fine.refineX;
    const int refineY = fine.refineY;
    const int refineZ = fine.refineZ;
    const float scale = 1.0f / static_cast<float>(refineX * refineY * refineZ);
    const int finePlane = fine.nx * fine.ny;

    float* const coarseRhs = coarse.rhs.data();
    const float* const prolongWeight = fine.prolongWeight.data();
    const Level::Gather* const gatherX = fine.gatherX.data();
    const Level::Gather* const gatherY = fine.gatherY.data();
    const Level::Gather* const gatherZ = fine.gatherZ.data();

    #pragma omp parallel for collapse(2) schedule(static) \
        if (coarse.nz * coarse.ny >= PARALLEL_ROWS_MIN)
    for (int k = 0; k < coarse.nz; ++k) {
        for (int j = 0; j < coarse.ny; ++j) {
            for (int i = 0; i < coarse.nx; ++i) {
                const int coarseId = (k * coarse.ny + j) * coarse.nx + i;

                if (coarse.solid[coarseId] || coarse.diag[coarseId] == 0.0f) {
                    coarseRhs[coarseId] = 0.0f;
                    continue;
                }
                const Level::Gather& gz = gatherZ[k];
                const Level::Gather& gy = gatherY[j];
                const Level::Gather& gx = gatherX[i];

                float sum = 0.0f;
                for (int c = 0; c < gz.count; ++c) {
                    const int finePage = gz.fine[c] * finePlane;
                    const float wz = gz.weight[c];
                    for (int b = 0; b < gy.count; ++b) {
                        const int fineRow = finePage + gy.fine[b] * fine.nx;
                        const float wy = gy.weight[b] * wz;
                        for (int a = 0; a < gx.count; ++a) {
                            const int fineId = fineRow + gx.fine[a];
                            const float norm = prolongWeight[fineId];
                            if (norm <= 0.0f)
                                continue;
                            const float share = gx.weight[a] * wy;
                            sum += ((norm == 1.0f) ? share : share / norm) *
                                   fineSrc[fineId];
                        }
                    }
                }
                coarseRhs[coarseId] = sum * scale;
            }
        }
    }
}

void Multigrid::restrictResidual(int fineLevel) {
    const int coarseLevel = fineLevel + 1;
    if (coarseLevel >= levels)
        return;
    gridLevels[coarseLevel].pressure.zero();
    restrictField(fineLevel, gridLevels[fineLevel].residual.data());
}

void Multigrid::restrictRHS(int fineLevel) {
    restrictField(fineLevel, gridLevels[fineLevel].rhs.data());
}

void Multigrid::prolongateCorrection(int coarseLevel) {
    if (coarseLevel <= 0 || coarseLevel >= levels)
        return;
    const int fineLevel = coarseLevel - 1;
    Level& fine = gridLevels[fineLevel];
    const Level& coarse = gridLevels[coarseLevel];

    float* const finePressure = fine.pressure.data();
    const float* const coarsePressure = coarse.pressure.data();

    const Level::Transfer* const transferX = fine.transferX.data();
    const Level::Transfer* const transferY = fine.transferY.data();
    const Level::Transfer* const transferZ = fine.transferZ.data();
    const int coarsePlane = coarse.nx * coarse.ny;

    #pragma omp parallel for collapse(2) schedule(static) \
        if (fine.nz * fine.ny >= PARALLEL_ROWS_MIN)
    for (int k = 0; k < fine.nz; ++k) {
        for (int j = 0; j < fine.ny; ++j) {
            const Level::Transfer sz = transferZ[k];
            const Level::Transfer sy = transferY[j];
            for (int i = 0; i < fine.nx; ++i) {
                const int fineId = (k * fine.ny + j) * fine.nx + i;
                const float norm = fine.prolongWeight[fineId];
                if (norm <= 0.0f)
                    continue;

                const Level::Transfer sx = transferX[i];

                const int coarseX[8] = {
                    sx.coarse0, sx.coarse1, sx.coarse0, sx.coarse1,
                    sx.coarse0, sx.coarse1, sx.coarse0, sx.coarse1};
                const int coarseY[8] = {
                    sy.coarse0, sy.coarse0, sy.coarse1, sy.coarse1,
                    sy.coarse0, sy.coarse0, sy.coarse1, sy.coarse1};
                const int coarseZ[8] = {
                    sz.coarse0, sz.coarse0, sz.coarse0, sz.coarse0,
                    sz.coarse1, sz.coarse1, sz.coarse1, sz.coarse1};
                const float weights[8] = {
                    sx.weight0 * sy.weight0 * sz.weight0,
                    sx.weight1 * sy.weight0 * sz.weight0,
                    sx.weight0 * sy.weight1 * sz.weight0,
                    sx.weight1 * sy.weight1 * sz.weight0,
                    sx.weight0 * sy.weight0 * sz.weight1,
                    sx.weight1 * sy.weight0 * sz.weight1,
                    sx.weight0 * sy.weight1 * sz.weight1,
                    sx.weight1 * sy.weight1 * sz.weight1};

                float value = 0.0f;
                for (int c = 0; c < 8; ++c) {
                    if (weights[c] == 0.0f)
                        continue;
                    const int coarseId = coarseZ[c] * coarsePlane +
                                         coarseY[c] * coarse.nx + coarseX[c];
                    if (!coarse.solid[coarseId])
                        value += weights[c] * coarsePressure[coarseId];
                }

                finePressure[fineId] += (norm == 1.0f) ? value : value / norm;
            }
        }
    }
}

void Multigrid::prolongateSolution(int coarseLevel) {
    if (coarseLevel <= 0 || coarseLevel >= levels)
        return;
    const int fineLevel = coarseLevel - 1;
    Level& fine = gridLevels[fineLevel];
    const Level& coarse = gridLevels[coarseLevel];

    fine.pressure.zero();

    float* const finePressure = fine.pressure.data();
    const float* const coarsePressure = coarse.pressure.data();

    const Level::Transfer* const transferX = fine.transferX.data();
    const Level::Transfer* const transferY = fine.transferY.data();
    const Level::Transfer* const transferZ = fine.transferZ.data();
    const int coarsePlane = coarse.nx * coarse.ny;

    #pragma omp parallel for collapse(2) schedule(static) \
        if (fine.nz * fine.ny >= PARALLEL_ROWS_MIN)
    for (int k = 0; k < fine.nz; ++k) {
        for (int j = 0; j < fine.ny; ++j) {
            const Level::Transfer sz = transferZ[k];
            const Level::Transfer sy = transferY[j];
            for (int i = 0; i < fine.nx; ++i) {
                const int fineId = (k * fine.ny + j) * fine.nx + i;
                const float norm = fine.prolongWeight[fineId];
                if (norm <= 0.0f)
                    continue;

                const Level::Transfer sx = transferX[i];

                const int coarseX[8] = {
                    sx.coarse0, sx.coarse1, sx.coarse0, sx.coarse1,
                    sx.coarse0, sx.coarse1, sx.coarse0, sx.coarse1};
                const int coarseY[8] = {
                    sy.coarse0, sy.coarse0, sy.coarse1, sy.coarse1,
                    sy.coarse0, sy.coarse0, sy.coarse1, sy.coarse1};
                const int coarseZ[8] = {
                    sz.coarse0, sz.coarse0, sz.coarse0, sz.coarse0,
                    sz.coarse1, sz.coarse1, sz.coarse1, sz.coarse1};
                const float weights[8] = {
                    sx.weight0 * sy.weight0 * sz.weight0,
                    sx.weight1 * sy.weight0 * sz.weight0,
                    sx.weight0 * sy.weight1 * sz.weight0,
                    sx.weight1 * sy.weight1 * sz.weight0,
                    sx.weight0 * sy.weight0 * sz.weight1,
                    sx.weight1 * sy.weight0 * sz.weight1,
                    sx.weight0 * sy.weight1 * sz.weight1,
                    sx.weight1 * sy.weight1 * sz.weight1};

                float value = 0.0f;
                for (int c = 0; c < 8; ++c) {
                    if (weights[c] == 0.0f)
                        continue;
                    const int coarseId = coarseZ[c] * coarsePlane +
                                         coarseY[c] * coarse.nx + coarseX[c];
                    if (!coarse.solid[coarseId])
                        value += weights[c] * coarsePressure[coarseId];
                }

                finePressure[fineId] = (norm == 1.0f) ? value : value / norm;
            }
        }
    }
}

void Multigrid::vCycle(
    int level,
    float smootherOmega,
    float coarseOmega)
{
    if (level == levels - 1) {
        smoothSOR(level, coarseOmega,
                  coarseSweeps(gridLevels[level].nx, gridLevels[level].ny,
                               gridLevels[level].nz));
        return;
    }
    smoothSOR(level, smootherOmega, PRE_SMOOTH_SWEEPS);
    computeResidual(level);
    restrictResidual(level);
    vCycle(level + 1, smootherOmega, coarseOmega);
    if (coefficientsUniform) {
        prolongateCorrection(level + 1);
    } else {
        Level& grid = gridLevels[level];
        const int count = grid.cellCount;
        if (static_cast<int>(grid.savedPressure.size()) != count) {
            grid.savedPressure.assign(count, 0.0f);
            grid.savedResidual.assign(count, 0.0f);
        }
        std::copy(grid.pressure.data(), grid.pressure.data() + count,
                  grid.savedPressure.begin());
        std::copy(grid.residual.data(), grid.residual.data() + count,
                  grid.savedResidual.begin());
        prolongateCorrection(level + 1);
        dampCorrection(level);
    }
    smoothSOR(level, smootherOmega, POST_SMOOTH_SWEEPS);
}

void Multigrid::fullMultigrid(float smootherOmega, float coarseOmega) {
    const int coarsest = levels - 1;
    if (coarsest == 0) {
        smoothSOR(0, coarseOmega,
                  coarseSweeps(gridLevels[0].nx, gridLevels[0].ny,
                               gridLevels[0].nz));
        return;
    }
    for (int level = 0; level < coarsest; ++level)
        restrictRHS(level);

    gridLevels[coarsest].pressure.zero();
    smoothSOR(coarsest, coarseOmega,
              coarseSweeps(gridLevels[coarsest].nx, gridLevels[coarsest].ny,
                           gridLevels[coarsest].nz));

    for (int level = coarsest; level > 0; --level) {
        prolongateSolution(level);
        vCycle(level - 1, smootherOmega, coarseOmega);
    }
}

float Multigrid::solve(
    std::vector<float>& pressure,
    const std::vector<float>& rhs,
    float smootherOmega,
    float coarseOmega,
    int maxCycles,
    float tolerance,
    float rhsScale)
{
    if (!geometryReady) {
        std::fprintf(stderr, "Multigrid::solve called before setGeometry\n");
        return 0.0f;
    }

#ifdef USE_CUDA

    if (useCuda)
        return solveCuda(
            pressure,
            rhs,
            smootherOmega,
            coarseOmega,
            maxCycles,
            tolerance,
            rhsScale);
#endif

    if (!coefficientsUniform)
        return solvePCG(pressure, rhs, smootherOmega, coarseOmega,
                        maxCycles, tolerance, rhsScale);

    Level& finest = gridLevels[0];
    const int cellCount = finest.cellCount;

    float* const finestPressure = finest.pressure.data();
    float* const finestRhs = finest.rhs.data();

    // Solid cells have a zero diagonal, they take no part in the solve
    #pragma omp parallel for schedule(static)
    for (int id = 0; id < cellCount; ++id) {
        const bool active = (finest.diag[id] > 0.0f);
        finestPressure[id] = active ? pressure[id] : 0.0f;
        finestRhs[id] = active ? rhs[id] : 0.0f;
    }

    removeNullSpace(finestRhs, finest);

    const float rhsNorm = (rhsScale > 0.0f && !pressureSingular)
                              ? rhsScale
                              : computeVectorNorm(finestRhs, cellCount);
    const float scale = (rhsNorm > 1e-20f) ? rhsNorm : 1.0f;

    if (firstSolve) {
        // Nested iteration once, to build a good field from nothing. Later
        // steps start from the previous pressure, which is a far better guess
        // than anything a fresh FMG pass produces.
        fullMultigrid(smootherOmega, coarseOmega);
        firstSolve = false;
    }

    lastCycles = 0;
    float relative = 1.0f;

    // The first cycle runs whatever the residual says, because the field comes
    // from the previous step and one V-cycle costs less than the residual pass
    // that would find out it was not needed. So that pass is not made at all.
    for (int cycle = 0; cycle < maxCycles; ++cycle) {
        if (cycle > 0) {
            computeResidual(0);
            relative = computeResidualNorm(0) / scale;
            if (relative < tolerance)
                break;
        }
        vCycle(0, smootherOmega, coarseOmega);
        removeNullSpace(finestPressure, finest);
        ++lastCycles;
    }

    if (lastCycles > 0) {
        computeResidual(0);
        relative = computeResidualNorm(0) / scale;
    }

    std::copy(finestPressure, finestPressure + cellCount, pressure.begin());
    return relative;
}
