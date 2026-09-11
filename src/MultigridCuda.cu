#include "Multigrid.hpp"
#ifdef USE_CUDA
#include "MultigridCuda.cuh"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static const int BLOCK_X = 32;
static const int BLOCK_Y = 8;
static const int NORM_BLOCK = 256;
static const int MAX_NORM_BLOCKS = 1024;
static const int PRE_SMOOTH_SWEEPS = 2;
static const int POST_SMOOTH_SWEEPS = 2;
static const int COARSE_SMOOTH_SWEEPS = 50;

namespace {
// Same rule as on the CPU: the coarsest level needs enough sweeps to act as a
// solver rather than a smoother
int coarseSweeps(int nx, int ny, int nz) {
    const int planar = (nx > ny) ? nx : ny;
    const int wanted = 2 * ((planar > nz) ? planar : nz);
    if (wanted < COARSE_SMOOTH_SWEEPS) return COARSE_SMOOTH_SWEEPS;
    return (wanted > 400) ? 400 : wanted;
}

void checkCuda(cudaError_t status, const char* what, const char* file, int line) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "CUDA error at %s:%d during %s: %s\n",
                     file, line, what, cudaGetErrorString(status));
        std::abort();
    }
}

void checkLaunch(const char* what, const char* file, int line) {
    checkCuda(cudaGetLastError(), what, file, line);
}
}

#define CUDA_CHECK(call) checkCuda((call), #call, __FILE__, __LINE__)
#define CUDA_CHECK_LAUNCH(name) checkLaunch(name, __FILE__, __LINE__)

__global__ void smoothSORKernel(
    int nx,
    int ny,
    int nz,
    float* __restrict__ pressure,
    const float* __restrict__ rhs,
    const float* __restrict__ coefW,
    const float* __restrict__ coefE,
    const float* __restrict__ coefS,
    const float* __restrict__ coefN,
    const float* __restrict__ coefF,
    const float* __restrict__ coefB,
    const float* __restrict__ invDiag,
    float omega,
    int color)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= nx || j >= ny || k >= nz)
        return;
    if (((i + j + k) & 1) != color)
        return;

    const int plane = nx * ny;
    const int id = (k * ny + j) * nx + i;
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

__global__ void computeResidualKernel(
    int nx,
    int ny,
    int nz,
    const float* __restrict__ pressure,
    const float* __restrict__ rhs,
    const float* __restrict__ coefW,
    const float* __restrict__ coefE,
    const float* __restrict__ coefS,
    const float* __restrict__ coefN,
    const float* __restrict__ coefF,
    const float* __restrict__ coefB,
    const float* __restrict__ diag,
    float* __restrict__ residual)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= nx || j >= ny || k >= nz)
        return;

    const int plane = nx * ny;
    const int id = (k * ny + j) * nx + i;
    const float sum =
        coefW[id] * pressure[id - 1] +
        coefE[id] * pressure[id + 1] +
        coefS[id] * pressure[id - nx] +
        coefN[id] * pressure[id + nx] +
        coefF[id] * pressure[id - plane] +
        coefB[id] * pressure[id + plane];

    residual[id] = rhs[id] - (sum - diag[id] * pressure[id]);
}

// The two coarse cells a fine cell interpolates from, and their weights
struct Stencil1D {
    int coarse0, coarse1;
    float weight0, weight1;
};

static __device__ inline Stencil1D transferStencil(int i, int refine, int coarseN) {
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

__global__ void restrictKernel(
    int fineNx,
    int fineNy,
    int fineNz,
    int coarseNx,
    int coarseNy,
    int coarseNz,
    int refineX,
    int refineY,
    int refineZ,
    const float* __restrict__ fineSrc,
    const float* __restrict__ fineProlongWeight,
    const uint8_t* __restrict__ coarseSolid,
    const float* __restrict__ coarseDiag,
    float* __restrict__ coarseRhs)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= coarseNx || j >= coarseNy || k >= coarseNz)
        return;

    const int coarseId = (k * coarseNy + j) * coarseNx + i;

    if (coarseSolid[coarseId] || coarseDiag[coarseId] == 0.0f) {
        coarseRhs[coarseId] = 0.0f;
        return;
    }

    // Fine cells that can carry a non-zero weight into this coarse cell
    const int i0 = (refineX == 1) ? i : max(0, 2 * i - 1);
    const int i1 = (refineX == 1) ? i : min(fineNx - 1, 2 * i + 2);
    const int j0 = (refineY == 1) ? j : max(0, 2 * j - 1);
    const int j1 = (refineY == 1) ? j : min(fineNy - 1, 2 * j + 2);
    const int k0 = (refineZ == 1) ? k : max(0, 2 * k - 1);
    const int k1 = (refineZ == 1) ? k : min(fineNz - 1, 2 * k + 2);
    const int finePlane = fineNx * fineNy;

    float sum = 0.0f;
    for (int kk = k0; kk <= k1; ++kk) {
        const Stencil1D sz = transferStencil(kk, refineZ, coarseNz);
        float wz = 0.0f;
        if (sz.coarse0 == k) wz += sz.weight0;
        if (sz.coarse1 == k) wz += sz.weight1;
        if (wz == 0.0f)
            continue;

        const int finePage = kk * finePlane;
        for (int jj = j0; jj <= j1; ++jj) {
            const Stencil1D sy = transferStencil(jj, refineY, coarseNy);
            float wy = 0.0f;
            if (sy.coarse0 == j) wy += sy.weight0;
            if (sy.coarse1 == j) wy += sy.weight1;
            if (wy == 0.0f)
                continue;
            wy *= wz;

            const int fineRow = finePage + jj * fineNx;
            for (int ii = i0; ii <= i1; ++ii) {
                const int fineId = fineRow + ii;
                const float norm = fineProlongWeight[fineId];
                if (norm <= 0.0f)
                    continue;

                const Stencil1D sx = transferStencil(ii, refineX, coarseNx);
                float wx = 0.0f;
                if (sx.coarse0 == i) wx += sx.weight0;
                if (sx.coarse1 == i) wx += sx.weight1;
                if (wx == 0.0f)
                    continue;

                sum += (wx * wy / norm) * fineSrc[fineId];
            }
        }
    }

    coarseRhs[coarseId] =
        sum / static_cast<float>(refineX * refineY * refineZ);
}

__global__ void prolongateKernel(
    int fineNx,
    int fineNy,
    int fineNz,
    int coarseNx,
    int coarseNy,
    int coarseNz,
    int refineX,
    int refineY,
    int refineZ,
    float* __restrict__ finePressure,
    const float* __restrict__ coarsePressure,
    const float* __restrict__ fineProlongWeight,
    const uint8_t* __restrict__ coarseSolid,
    int addMode)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= fineNx || j >= fineNy || k >= fineNz)
        return;

    const int fineId = (k * fineNy + j) * fineNx + i;
    const float norm = fineProlongWeight[fineId];
    if (norm <= 0.0f) {
        if (!addMode)
            finePressure[fineId] = 0.0f;
        return;
    }

    const Stencil1D sx = transferStencil(i, refineX, coarseNx);
    const Stencil1D sy = transferStencil(j, refineY, coarseNy);
    const Stencil1D sz = transferStencil(k, refineZ, coarseNz);

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
    const int coarsePlane = coarseNx * coarseNy;

    float value = 0.0f;
    #pragma unroll
    for (int c = 0; c < 8; ++c) {
        if (weights[c] == 0.0f)
            continue;
        const int coarseId = coarseZ[c] * coarsePlane +
                             coarseY[c] * coarseNx + coarseX[c];
        if (!coarseSolid[coarseId])
            value += weights[c] * coarsePressure[coarseId];
    }

    if (addMode)
        finePressure[fineId] += value / norm;
    else
        finePressure[fineId] = value / norm;
}

__global__ void zeroSolidPressureKernel(
    int cellCount,
    float* __restrict__ pressure,
    float* __restrict__ rhs,
    const float* __restrict__ invDiag)
{
    const int id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id >= cellCount)
        return;
    if (invDiag[id] == 0.0f) {
        pressure[id] = 0.0f;
        rhs[id] = 0.0f;
    }
}

__global__ void computeNormKernel(
    int cellCount,
    const float* __restrict__ values,
    float* __restrict__ partial)
{
    __shared__ float shared[NORM_BLOCK];

    float sum = 0.0f;
    for (int id = blockIdx.x * blockDim.x + threadIdx.x;
         id < cellCount;
         id += blockDim.x * gridDim.x) {
        const float x = values[id];
        sum += x * x;
    }

    shared[threadIdx.x] = sum;
    __syncthreads();

    for (unsigned stride = blockDim.x / 2u; stride > 0u; stride >>= 1) {
        if (threadIdx.x < stride)
            shared[threadIdx.x] += shared[threadIdx.x + stride];
        __syncthreads();
    }

    if (threadIdx.x == 0)
        partial[blockIdx.x] = shared[0];
}

__global__ void applyOperatorKernel(
    int nx,
    int ny,
    int nz,
    const float* __restrict__ x,
    const float* __restrict__ coefW,
    const float* __restrict__ coefE,
    const float* __restrict__ coefS,
    const float* __restrict__ coefN,
    const float* __restrict__ coefF,
    const float* __restrict__ coefB,
    const float* __restrict__ diag,
    float* __restrict__ out)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= nx || j >= ny || k >= nz)
        return;
    const int plane = nx * ny;
    const int id = (k * ny + j) * nx + i;
    const float sum =
        coefW[id] * x[id - 1] + coefE[id] * x[id + 1] +
        coefS[id] * x[id - nx] + coefN[id] * x[id + nx] +
        coefF[id] * x[id - plane] + coefB[id] * x[id + plane];
    out[id] = diag[id] * x[id] - sum;
}

__global__ void dotKernel(
    int cellCount,
    const float* __restrict__ a,
    const float* __restrict__ b,
    float* __restrict__ partial)
{
    __shared__ float shared[NORM_BLOCK];

    float sum = 0.0f;
    for (int id = blockIdx.x * blockDim.x + threadIdx.x;
         id < cellCount;
         id += blockDim.x * gridDim.x)
        sum += a[id] * b[id];

    shared[threadIdx.x] = sum;
    __syncthreads();

    for (unsigned stride = blockDim.x / 2u; stride > 0u; stride >>= 1) {
        if (threadIdx.x < stride)
            shared[threadIdx.x] += shared[threadIdx.x + stride];
        __syncthreads();
    }

    if (threadIdx.x == 0)
        partial[blockIdx.x] = shared[0];
}

__global__ void residualFromOperatorKernel(
    int cellCount,
    const float* __restrict__ b,
    const float* __restrict__ ax,
    const float* __restrict__ diag,
    float* __restrict__ r)
{
    const int id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id >= cellCount)
        return;
    r[id] = (diag[id] > 0.0f) ? (b[id] - ax[id]) : 0.0f;
}

__global__ void negateKernel(
    int cellCount,
    const float* __restrict__ src,
    float* __restrict__ dst)
{
    const int id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id < cellCount)
        dst[id] = -src[id];
}

__global__ void combineKernel(
    int cellCount,
    const float* __restrict__ z,
    float beta,
    float* __restrict__ d)
{
    const int id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id < cellCount)
        d[id] = z[id] + beta * d[id];
}

__global__ void stepKernel(
    int cellCount,
    float alpha,
    const float* __restrict__ d,
    const float* __restrict__ q,
    float* __restrict__ x,
    float* __restrict__ r,
    float* __restrict__ previous)
{
    const int id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id >= cellCount)
        return;
    previous[id] = r[id];
    x[id] += alpha * d[id];
    r[id] -= alpha * q[id];
}

__global__ void differenceKernel(
    int cellCount,
    const float* __restrict__ before,
    const float* __restrict__ after,
    float* __restrict__ out)
{
    const int id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id < cellCount)
        out[id] = before[id] - after[id];
}

__global__ void blendKernel(
    int cellCount,
    float alpha,
    const float* __restrict__ saved,
    float* __restrict__ values)
{
    const int id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id < cellCount)
        values[id] = saved[id] + alpha * (values[id] - saved[id]);
}

// Device memory. The previous version called cudaMalloc/cudaFree for every
// level on every pressure solve, i.e. tens of allocations per time step, and
// that dominated the GPU path completely. Here the whole hierarchy is allocated
// once when the geometry is set and released in the destructor. MUCH better.

bool Multigrid::cudaDeviceAvailable() {
    int count = 0;
    const cudaError_t status = cudaGetDeviceCount(&count);
    if (status != cudaSuccess || count < 1) {
        // A failed query latches the error, and the next unrelated CUDA call
        // would be the one to report it
        cudaGetLastError();
        return false;
    }
    return true;
}

void Multigrid::freeDevice() {
    for (DeviceLevel& d : deviceLevels) {
        if (d.pressureAlloc) cudaFree(d.pressureAlloc);
        if (d.residualAlloc) cudaFree(d.residualAlloc);
        if (d.rhs)           cudaFree(d.rhs);
        if (d.coefW)         cudaFree(d.coefW);
        if (d.coefE)         cudaFree(d.coefE);
        if (d.coefS)         cudaFree(d.coefS);
        if (d.coefN)         cudaFree(d.coefN);
        if (d.coefF)         cudaFree(d.coefF);
        if (d.coefB)         cudaFree(d.coefB);
        if (d.diag)          cudaFree(d.diag);
        if (d.invDiag)       cudaFree(d.invDiag);
        if (d.solid)         cudaFree(d.solid);
        if (d.prolongWeight) cudaFree(d.prolongWeight);
        d = DeviceLevel{};
    }
    deviceLevels.clear();

    if (deviceCgAlloc) {
        cudaFree(deviceCgAlloc);
        deviceCgAlloc = nullptr;
        deviceCgX = deviceCgR = deviceCgZ = deviceCgD = nullptr;
        deviceCgQ = deviceCgPrev = deviceCgB = nullptr;
        deviceSavedPressure = deviceSavedResidual = nullptr;
        deviceCgCells = 0;
    }

    if (deviceReduceBuffer) {
        cudaFree(deviceReduceBuffer);
        deviceReduceBuffer = nullptr;
    }
    if (hostReduceBuffer) {
        cudaFreeHost(hostReduceBuffer);
        hostReduceBuffer = nullptr;
    }
    deviceReady = false;
}

void Multigrid::allocateDevice() {
    freeDevice();
    deviceLevels.resize(levels);

    for (int l = 0; l < levels; ++l) {
        const Level& grid = gridLevels[l];
        DeviceLevel& d = deviceLevels[l];

        d.halo = (grid.nx * grid.ny > 8) ? grid.nx * grid.ny : 8;
        const size_t padded =
            static_cast<size_t>(grid.cellCount) + 2u * d.halo + 16u;
        const size_t plain = static_cast<size_t>(grid.cellCount) + 16u;

        CUDA_CHECK(cudaMalloc(&d.pressureAlloc, padded * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.residualAlloc, padded * sizeof(float)));
        CUDA_CHECK(cudaMemset(d.pressureAlloc, 0, padded * sizeof(float)));
        CUDA_CHECK(cudaMemset(d.residualAlloc, 0, padded * sizeof(float)));
        d.pressure = d.pressureAlloc + d.halo;
        d.residual = d.residualAlloc + d.halo;

        CUDA_CHECK(cudaMalloc(&d.rhs, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.coefW, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.coefE, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.coefS, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.coefN, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.coefF, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.coefB, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.diag, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.invDiag, plain * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&d.solid,
                              static_cast<size_t>(grid.cellCount) * sizeof(uint8_t)));
        CUDA_CHECK(cudaMalloc(&d.prolongWeight, plain * sizeof(float)));
        CUDA_CHECK(cudaMemset(d.rhs, 0, plain * sizeof(float)));
        CUDA_CHECK(cudaMemset(d.prolongWeight, 0, plain * sizeof(float)));
    }

    int blocks = (gridLevels[0].cellCount + NORM_BLOCK - 1) / NORM_BLOCK;
    if (blocks > MAX_NORM_BLOCKS)
        blocks = MAX_NORM_BLOCKS;
    if (blocks < 1)
        blocks = 1;
    reduceBlocks = blocks;

    CUDA_CHECK(cudaMalloc(&deviceReduceBuffer,
                          static_cast<size_t>(reduceBlocks) * sizeof(float)));
    CUDA_CHECK(cudaMallocHost(&hostReduceBuffer,
                              static_cast<size_t>(reduceBlocks) * sizeof(float)));
}

void Multigrid::setGeometryCuda(bool keepSolution) {
    if (!keepSolution || !deviceReady)
        allocateDevice();
    for (int l = 0; l < levels; ++l) {
        const Level& grid = gridLevels[l];
        DeviceLevel& d = deviceLevels[l];
        const size_t plain = static_cast<size_t>(grid.cellCount) + 16u;

        CUDA_CHECK(cudaMemcpy(d.coefW, grid.coefW.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefE, grid.coefE.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefS, grid.coefS.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefN, grid.coefN.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefF, grid.coefF.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefB, grid.coefB.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.diag, grid.diag.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.invDiag, grid.invDiag.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.solid, grid.solid.data(),
                              static_cast<size_t>(grid.cellCount) * sizeof(uint8_t),
                              cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.prolongWeight, grid.prolongWeight.data(),
                              static_cast<size_t>(grid.cellCount) * sizeof(float),
                              cudaMemcpyHostToDevice));
        if (!keepSolution)
            CUDA_CHECK(cudaMemset(d.pressureAlloc, 0,
                                  (static_cast<size_t>(grid.cellCount)
                                   + 2u * d.halo + 16u) * sizeof(float)));
    }

    deviceReady = true;
}

void Multigrid::uploadCoefficientsCuda() {
    if (!deviceReady)
        return;

    for (int l = 0; l < levels; ++l) {
        const Level& grid = gridLevels[l];
        DeviceLevel& d = deviceLevels[l];
        const size_t plain = static_cast<size_t>(grid.cellCount) + 16u;

        CUDA_CHECK(cudaMemcpy(d.coefW, grid.coefW.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefE, grid.coefE.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefS, grid.coefS.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefN, grid.coefN.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefF, grid.coefF.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.coefB, grid.coefB.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.diag, grid.diag.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d.invDiag, grid.invDiag.data(),
                              plain * sizeof(float), cudaMemcpyHostToDevice));
    }
}

void Multigrid::smoothSORCuda(
    int level,
    float omega,
    int sweeps)
{
    const Level& grid = gridLevels[level];
    const DeviceLevel& d = deviceLevels[level];

    const dim3 block(BLOCK_X, BLOCK_Y);
    const dim3 launchGrid((grid.nx + BLOCK_X - 1) / BLOCK_X,
                          (grid.ny + BLOCK_Y - 1) / BLOCK_Y,
                          grid.nz);

    for (int sweep = 0; sweep < sweeps; ++sweep) {
        for (int color = 0; color < 2; ++color) {
            // Consecutive launches on the same stream are ordered, so the black
            // half sweep is guaranteed to observe every red update. The old code
            // relied on the same property but also ran a separate applyBC kernel
            // that wrote the cells its neighbours were reading, which is exactly
            // where the GPU and CPU results used to diverge.
            smoothSORKernel<<<launchGrid, block>>>(
                grid.nx, grid.ny, grid.nz, d.pressure, d.rhs,
                d.coefW, d.coefE, d.coefS, d.coefN, d.coefF, d.coefB,
                d.invDiag, omega, color);
            CUDA_CHECK_LAUNCH("smoothSORKernel");
        }
    }
}

void Multigrid::computeResidualCuda(int level) {
    const Level& grid = gridLevels[level];
    const DeviceLevel& d = deviceLevels[level];

    const dim3 block(BLOCK_X, BLOCK_Y);
    const dim3 launchGrid((grid.nx + BLOCK_X - 1) / BLOCK_X,
                          (grid.ny + BLOCK_Y - 1) / BLOCK_Y,
                          grid.nz);

    computeResidualKernel<<<launchGrid, block>>>(
        grid.nx, grid.ny, grid.nz, d.pressure, d.rhs,
        d.coefW, d.coefE, d.coefS, d.coefN, d.coefF, d.coefB,
        d.diag, d.residual);
    CUDA_CHECK_LAUNCH("computeResidualKernel");
}

float Multigrid::computeResidualNormCuda(int level) {
    const Level& grid = gridLevels[level];
    const DeviceLevel& d = deviceLevels[level];

    computeNormKernel<<<reduceBlocks, NORM_BLOCK>>>(
        grid.cellCount, d.residual, deviceReduceBuffer);
    CUDA_CHECK_LAUNCH("computeNormKernel");

    CUDA_CHECK(cudaMemcpy(hostReduceBuffer, deviceReduceBuffer,
                          static_cast<size_t>(reduceBlocks) * sizeof(float),
                          cudaMemcpyDeviceToHost));

    double total = 0.0;
    for (int b = 0; b < reduceBlocks; ++b)
        total += hostReduceBuffer[b];
    return static_cast<float>(std::sqrt(total));
}

float Multigrid::computeRhsNormCuda(int level) {
    const Level& grid = gridLevels[level];
    const DeviceLevel& d = deviceLevels[level];

    computeNormKernel<<<reduceBlocks, NORM_BLOCK>>>(
        grid.cellCount, d.rhs, deviceReduceBuffer);
    CUDA_CHECK_LAUNCH("computeNormKernel");

    CUDA_CHECK(cudaMemcpy(hostReduceBuffer, deviceReduceBuffer,
                          static_cast<size_t>(reduceBlocks) * sizeof(float),
                          cudaMemcpyDeviceToHost));

    double total = 0.0;
    for (int b = 0; b < reduceBlocks; ++b)
        total += hostReduceBuffer[b];
    return static_cast<float>(std::sqrt(total));
}

void Multigrid::restrictResidualCuda(int fineLevel) {
    const int coarseLevel = fineLevel + 1;
    if (coarseLevel >= levels)
        return;

    const Level& fine = gridLevels[fineLevel];
    const Level& coarse = gridLevels[coarseLevel];
    const DeviceLevel& dFine = deviceLevels[fineLevel];
    const DeviceLevel& dCoarse = deviceLevels[coarseLevel];

    // Clear the coarse solution: the V-cycle solves for a correction there
    CUDA_CHECK(cudaMemset(dCoarse.pressure, 0,
                          static_cast<size_t>(coarse.cellCount) * sizeof(float)));

    const dim3 block(BLOCK_X, BLOCK_Y);
    const dim3 launchGrid((coarse.nx + BLOCK_X - 1) / BLOCK_X,
                          (coarse.ny + BLOCK_Y - 1) / BLOCK_Y,
                          coarse.nz);

    restrictKernel<<<launchGrid, block>>>(
        fine.nx, fine.ny, fine.nz, coarse.nx, coarse.ny, coarse.nz,
        fine.refineX, fine.refineY, fine.refineZ,
        dFine.residual, dFine.prolongWeight,
        dCoarse.solid, dCoarse.diag, dCoarse.rhs);
    CUDA_CHECK_LAUNCH("restrictKernel(residual)");
}

void Multigrid::restrictRHSCuda(int fineLevel) {
    const int coarseLevel = fineLevel + 1;
    if (coarseLevel >= levels)
        return;

    const Level& fine = gridLevels[fineLevel];
    const Level& coarse = gridLevels[coarseLevel];
    const DeviceLevel& dFine = deviceLevels[fineLevel];
    const DeviceLevel& dCoarse = deviceLevels[coarseLevel];

    const dim3 block(BLOCK_X, BLOCK_Y);
    const dim3 launchGrid((coarse.nx + BLOCK_X - 1) / BLOCK_X,
                          (coarse.ny + BLOCK_Y - 1) / BLOCK_Y,
                          coarse.nz);

    restrictKernel<<<launchGrid, block>>>(
        fine.nx, fine.ny, fine.nz, coarse.nx, coarse.ny, coarse.nz,
        fine.refineX, fine.refineY, fine.refineZ,
        dFine.rhs, dFine.prolongWeight,
        dCoarse.solid, dCoarse.diag, dCoarse.rhs);
    CUDA_CHECK_LAUNCH("restrictKernel(rhs)");
}

void Multigrid::prolongateCorrectionCuda(int coarseLevel) {
    if (coarseLevel <= 0 || coarseLevel >= levels)
        return;

    const int fineLevel = coarseLevel - 1;
    const Level& fine = gridLevels[fineLevel];
    const Level& coarse = gridLevels[coarseLevel];
    const DeviceLevel& dFine = deviceLevels[fineLevel];
    const DeviceLevel& dCoarse = deviceLevels[coarseLevel];

    const dim3 block(BLOCK_X, BLOCK_Y);
    const dim3 launchGrid((fine.nx + BLOCK_X - 1) / BLOCK_X,
                          (fine.ny + BLOCK_Y - 1) / BLOCK_Y,
                          fine.nz);

    prolongateKernel<<<launchGrid, block>>>(
        fine.nx, fine.ny, fine.nz, coarse.nx, coarse.ny, coarse.nz,
        fine.refineX, fine.refineY, fine.refineZ,
        dFine.pressure, dCoarse.pressure, dFine.prolongWeight,
        dCoarse.solid, 1);
    CUDA_CHECK_LAUNCH("prolongateKernel(correction)");
}

void Multigrid::prolongateSolutionCuda(int coarseLevel) {
    if (coarseLevel <= 0 || coarseLevel >= levels)
        return;

    const int fineLevel = coarseLevel - 1;
    const Level& fine = gridLevels[fineLevel];
    const Level& coarse = gridLevels[coarseLevel];
    const DeviceLevel& dFine = deviceLevels[fineLevel];
    const DeviceLevel& dCoarse = deviceLevels[coarseLevel];

    const dim3 block(BLOCK_X, BLOCK_Y);
    const dim3 launchGrid((fine.nx + BLOCK_X - 1) / BLOCK_X,
                          (fine.ny + BLOCK_Y - 1) / BLOCK_Y,
                          fine.nz);

    prolongateKernel<<<launchGrid, block>>>(
        fine.nx, fine.ny, fine.nz, coarse.nx, coarse.ny, coarse.nz,
        fine.refineX, fine.refineY, fine.refineZ,
        dFine.pressure, dCoarse.pressure, dFine.prolongWeight,
        dCoarse.solid, 0);
    CUDA_CHECK_LAUNCH("prolongateKernel(solution)");
}

void Multigrid::allocateCgDevice() {
    const int count = gridLevels[0].cellCount;
    const int halo = gridLevels[0].nx * gridLevels[0].ny + 8;
    if (deviceCgCells == count && deviceCgAlloc)
        return;
    if (deviceCgAlloc) {
        cudaFree(deviceCgAlloc);
        deviceCgAlloc = nullptr;
    }
    const size_t stride = static_cast<size_t>(count + 2 * halo + 16);
    const size_t total = stride * 9u * sizeof(float);
    CUDA_CHECK(cudaMalloc(&deviceCgAlloc, total));
    CUDA_CHECK(cudaMemset(deviceCgAlloc, 0, total));
    float* base = deviceCgAlloc;
    const auto take = [&]() {
        float* out = base + halo;
        base += stride;
        return out;
    };
    deviceCgX = take();
    deviceCgR = take();
    deviceCgZ = take();
    deviceCgD = take();
    deviceCgQ = take();
    deviceCgPrev = take();
    deviceCgB = take();
    deviceSavedPressure = take();
    deviceSavedResidual = take();
    deviceCgCells = count;
}

float Multigrid::dotCuda(int count, const float* a, const float* b) {
    dotKernel<<<reduceBlocks, NORM_BLOCK>>>(count, a, b, deviceReduceBuffer);
    CUDA_CHECK_LAUNCH("dotKernel");
    CUDA_CHECK(cudaMemcpy(hostReduceBuffer, deviceReduceBuffer,
                          static_cast<size_t>(reduceBlocks) * sizeof(float),
                          cudaMemcpyDeviceToHost));
    double total = 0.0;
    for (int block = 0; block < reduceBlocks; ++block)
        total += hostReduceBuffer[block];
    return static_cast<float>(total);
}

void Multigrid::dampCorrectionCuda(int level) {
    const Level& grid = gridLevels[level];
    const DeviceLevel& d = deviceLevels[level];
    const int count = grid.cellCount;
    const int blocks = (count + NORM_BLOCK - 1) / NORM_BLOCK;

    computeResidualCuda(level);

    differenceKernel<<<blocks, NORM_BLOCK>>>(count, deviceSavedResidual,
                                             d.residual, deviceCgQ);
    CUDA_CHECK_LAUNCH("differenceKernel");

    const float numerator = dotCuda(count, deviceSavedResidual, deviceCgQ);
    const float denominator = dotCuda(count, deviceCgQ, deviceCgQ);
    float alpha = 1.0f;
    if (denominator > 1e-30f) {
        alpha = numerator / denominator;
        if (!std::isfinite(alpha))
            alpha = 1.0f;
        alpha = std::min(2.0f, std::max(0.0f, alpha));
    }
    if (alpha == 1.0f)
        return;

    blendKernel<<<blocks, NORM_BLOCK>>>(count, alpha, deviceSavedPressure,
                                        d.pressure);
    CUDA_CHECK_LAUNCH("blendKernel");
}

void Multigrid::vCycleCuda(
    int level,
    float smootherOmega,
    float coarseOmega)
{
    if (level == levels - 1) {
        smoothSORCuda(level, coarseOmega,
                      coarseSweeps(gridLevels[level].nx, gridLevels[level].ny,
                                   gridLevels[level].nz));
        return;
    }

    smoothSORCuda(level, smootherOmega, PRE_SMOOTH_SWEEPS);
    computeResidualCuda(level);
    restrictResidualCuda(level);
    vCycleCuda(level + 1, smootherOmega, coarseOmega);
    if (coefficientsUniform || level != 0) {
        prolongateCorrectionCuda(level + 1);
    } else {
        const Level& grid = gridLevels[level];
        const DeviceLevel& d = deviceLevels[level];
        const size_t bytes =
            static_cast<size_t>(grid.cellCount) * sizeof(float);
        CUDA_CHECK(cudaMemcpy(deviceSavedPressure, d.pressure, bytes,
                              cudaMemcpyDeviceToDevice));
        CUDA_CHECK(cudaMemcpy(deviceSavedResidual, d.residual, bytes,
                              cudaMemcpyDeviceToDevice));
        prolongateCorrectionCuda(level + 1);
        dampCorrectionCuda(level);
    }
    smoothSORCuda(level, smootherOmega, POST_SMOOTH_SWEEPS);
}

void Multigrid::fullMultigridCuda(float smootherOmega, float coarseOmega) {
    const int coarsest = levels - 1;
    if (coarsest == 0) {
        smoothSORCuda(0, coarseOmega,
                      coarseSweeps(gridLevels[0].nx, gridLevels[0].ny,
                                   gridLevels[0].nz));
        return;
    }

    for (int level = 0; level < coarsest; ++level)
        restrictRHSCuda(level);

    CUDA_CHECK(cudaMemset(
        deviceLevels[coarsest].pressure, 0,
        static_cast<size_t>(gridLevels[coarsest].cellCount) * sizeof(float)));
    smoothSORCuda(coarsest, coarseOmega,
                  coarseSweeps(gridLevels[coarsest].nx,
                               gridLevels[coarsest].ny,
                               gridLevels[coarsest].nz));

    for (int level = coarsest; level > 0; --level) {
        prolongateSolutionCuda(level);
        vCycleCuda(level - 1, smootherOmega, coarseOmega);
    }
}

float Multigrid::solvePCGCuda(
    std::vector<float>& pressure,
    const std::vector<float>& rhs,
    float smootherOmega,
    float coarseOmega,
    int maxCycles,
    float tolerance,
    float rhsScale)
{
    const Level& finest = gridLevels[0];
    const DeviceLevel& d0 = deviceLevels[0];
    const int count = finest.cellCount;
    const size_t bytes = static_cast<size_t>(count) * sizeof(float);
    const int blocks = (count + NORM_BLOCK - 1) / NORM_BLOCK;
    const dim3 block2(BLOCK_X, BLOCK_Y);
    const dim3 grid2((finest.nx + BLOCK_X - 1) / BLOCK_X,
                     (finest.ny + BLOCK_Y - 1) / BLOCK_Y,
                     finest.nz);

    allocateCgDevice();

    std::vector<float> host(count);

    CUDA_CHECK(cudaMemcpy(deviceCgX, pressure.data(), bytes,
                          cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d0.rhs, rhs.data(), bytes, cudaMemcpyHostToDevice));
    zeroSolidPressureKernel<<<blocks, NORM_BLOCK>>>(count, deviceCgX, d0.rhs,
                                                    d0.invDiag);
    CUDA_CHECK_LAUNCH("zeroSolidPressureKernel");

    const auto pinLevel = [&](float* values) {
        if (!pressureSingular)
            return;
        CUDA_CHECK(cudaMemcpy(host.data(), values, bytes,
                              cudaMemcpyDeviceToHost));
        removeNullSpace(host.data(), finest);
        CUDA_CHECK(cudaMemcpy(values, host.data(), bytes,
                              cudaMemcpyHostToDevice));
    };
    const auto normOf = [&](const float* values) {
        return std::sqrt(std::max(0.0f, dotCuda(count, values, values)));
    };

    pinLevel(d0.rhs);

    negateKernel<<<blocks, NORM_BLOCK>>>(count, d0.rhs, deviceCgB);
    CUDA_CHECK_LAUNCH("negateKernel");

    const float norm = (rhsScale > 0.0f && !pressureSingular)
                           ? rhsScale
                           : computeRhsNormCuda(0);
    const float scale = (norm > 1e-20f) ? norm : 1.0f;

    if (firstSolve) {
        CUDA_CHECK(cudaMemcpy(d0.pressure, deviceCgX, bytes,
                              cudaMemcpyDeviceToDevice));
        fullMultigridCuda(smootherOmega, coarseOmega);
        CUDA_CHECK(cudaMemcpy(deviceCgX, d0.pressure, bytes,
                              cudaMemcpyDeviceToDevice));
        firstSolve = false;
    }

    applyOperatorKernel<<<grid2, block2>>>(
        finest.nx, finest.ny, finest.nz, deviceCgX, d0.coefW, d0.coefE,
        d0.coefS, d0.coefN, d0.coefF, d0.coefB, d0.diag, deviceCgQ);
    CUDA_CHECK_LAUNCH("applyOperatorKernel");
    residualFromOperatorKernel<<<blocks, NORM_BLOCK>>>(
        count, deviceCgB, deviceCgQ, d0.diag, deviceCgR);
    CUDA_CHECK_LAUNCH("residualFromOperatorKernel");
    pinLevel(deviceCgR);

    lastCycles = 0;
    float relative = normOf(deviceCgR) / scale;
    double rzOld = 0.0;

    for (int cycle = 0; cycle < maxCycles && relative >= tolerance; ++cycle) {
        negateKernel<<<blocks, NORM_BLOCK>>>(count, deviceCgR, d0.rhs);
        CUDA_CHECK_LAUNCH("negateKernel");
        CUDA_CHECK(cudaMemset(d0.pressure, 0, bytes));
        vCycleCuda(0, smootherOmega, coarseOmega);
        pinLevel(d0.pressure);
        CUDA_CHECK(cudaMemcpy(deviceCgZ, d0.pressure, bytes,
                              cudaMemcpyDeviceToDevice));

        const double rz = dotCuda(count, deviceCgR, deviceCgZ);
        if (cycle == 0) {
            CUDA_CHECK(cudaMemcpy(deviceCgD, deviceCgZ, bytes,
                                  cudaMemcpyDeviceToDevice));
        } else {
            differenceKernel<<<blocks, NORM_BLOCK>>>(count, deviceCgR,
                                                     deviceCgPrev, deviceCgQ);
            CUDA_CHECK_LAUNCH("differenceKernel");
            const double numerator = dotCuda(count, deviceCgQ, deviceCgZ);
            const double beta = (std::fabs(rzOld) > 1e-300)
                                    ? std::max(0.0, numerator / rzOld)
                                    : 0.0;
            combineKernel<<<blocks, NORM_BLOCK>>>(
                count, deviceCgZ, static_cast<float>(beta), deviceCgD);
            CUDA_CHECK_LAUNCH("combineKernel");
        }

        applyOperatorKernel<<<grid2, block2>>>(
            finest.nx, finest.ny, finest.nz, deviceCgD, d0.coefW, d0.coefE,
            d0.coefS, d0.coefN, d0.coefF, d0.coefB, d0.diag, deviceCgQ);
        CUDA_CHECK_LAUNCH("applyOperatorKernel");
        const double dq = dotCuda(count, deviceCgD, deviceCgQ);
        if (!(std::fabs(dq) > 1e-300))
            break;
        const float alpha = static_cast<float>(rz / dq);
        if (!std::isfinite(alpha))
            break;

        stepKernel<<<blocks, NORM_BLOCK>>>(count, alpha, deviceCgD, deviceCgQ,
                                           deviceCgX, deviceCgR, deviceCgPrev);
        CUDA_CHECK_LAUNCH("stepKernel");
        pinLevel(deviceCgR);
        rzOld = rz;
        ++lastCycles;
        relative = normOf(deviceCgR) / scale;
    }

    pinLevel(deviceCgX);
    CUDA_CHECK(cudaMemcpy(d0.pressure, deviceCgX, bytes,
                          cudaMemcpyDeviceToDevice));
    CUDA_CHECK(cudaMemcpy(pressure.data(), deviceCgX, bytes,
                          cudaMemcpyDeviceToHost));
    return relative;
}

float Multigrid::solveCuda(
    std::vector<float>& pressure,
    const std::vector<float>& rhs,
    float smootherOmega,
    float coarseOmega,
    int maxCycles,
    float tolerance,
    float rhsScale)
{
    if (!deviceReady) {
        std::fprintf(stderr, "Multigrid::solveCuda called before setGeometry\n");
        return 0.0f;
    }

    if (!coefficientsUniform)
        return solvePCGCuda(pressure, rhs, smootherOmega, coarseOmega,
                            maxCycles, tolerance, rhsScale);

    const Level& finest = gridLevels[0];
    const DeviceLevel& dFinest = deviceLevels[0];
    const size_t bytes = static_cast<size_t>(finest.cellCount) * sizeof(float);

    CUDA_CHECK(cudaMemcpy(dFinest.rhs, rhs.data(), bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(dFinest.pressure, pressure.data(), bytes,
                          cudaMemcpyHostToDevice));

    // Solid cells have a zero diagonal, they take no part in the solve
    {
        const int block = 256;
        const int launchGrid = (finest.cellCount + block - 1) / block;
        zeroSolidPressureKernel<<<launchGrid, block>>>(
            finest.cellCount, dFinest.pressure, dFinest.rhs, dFinest.invDiag);
        CUDA_CHECK_LAUNCH("zeroSolidPressureKernel");
    }

    if (pressureSingular) {
        std::vector<float> hostRhs(finest.cellCount);
        CUDA_CHECK(cudaMemcpy(hostRhs.data(), dFinest.rhs,
                              static_cast<size_t>(finest.cellCount) * sizeof(float),
                              cudaMemcpyDeviceToHost));
        removeNullSpace(hostRhs.data(), finest);
        CUDA_CHECK(cudaMemcpy(dFinest.rhs, hostRhs.data(),
                              static_cast<size_t>(finest.cellCount) * sizeof(float),
                              cudaMemcpyHostToDevice));
    }

    const float rhsNorm = (rhsScale > 0.0f && !pressureSingular)
                              ? rhsScale
                              : computeRhsNormCuda(0);
    const float scale = (rhsNorm > 1e-20f) ? rhsNorm : 1.0f;

    if (firstSolve) {
        // Nested iteration once, to build a good field from nothing
        fullMultigridCuda(smootherOmega, coarseOmega);
        firstSolve = false;
    }

    lastCycles = 0;
    float relative = 1.0f;

    // Same as the CPU path: the first cycle is cheaper than finding out it was
    // not needed, and here that check also costs a device to host copy
    for (int cycle = 0; cycle < maxCycles; ++cycle) {
        if (cycle > 0) {
            computeResidualCuda(0);
            relative = computeResidualNormCuda(0) / scale;
            if (relative < tolerance)
                break;
        }

        vCycleCuda(0, smootherOmega, coarseOmega);
        if (pressureSingular) {
            std::vector<float> host(finest.cellCount);
            CUDA_CHECK(cudaMemcpy(host.data(), dFinest.pressure, bytes,
                                  cudaMemcpyDeviceToHost));
            removeNullSpace(host.data(), finest);
            CUDA_CHECK(cudaMemcpy(dFinest.pressure, host.data(), bytes,
                                  cudaMemcpyHostToDevice));
        }
        ++lastCycles;
    }

    if (lastCycles > 0) {
        computeResidualCuda(0);
        relative = computeResidualNormCuda(0) / scale;
    }

    CUDA_CHECK(cudaMemcpy(pressure.data(), dFinest.pressure, bytes,
                          cudaMemcpyDeviceToHost));
    return relative;
}

#endif // USE_CUDA
