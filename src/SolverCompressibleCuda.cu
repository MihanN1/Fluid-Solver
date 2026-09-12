#include "SolverCompressible.hpp"
#ifdef USE_CUDA
#include "CompressibleKernels.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kBlockX = 32;
constexpr int kBlockY = 8;
constexpr int kReduceBlock = 256;
constexpr int kMaxReduceBlocks = 1024;

void checkCuda(cudaError_t status, const char* what, const char* file,
               int line) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "CUDA error at %s:%d during %s: %s\n", file, line,
                     what, cudaGetErrorString(status));
        std::abort();
    }
}

}

#define CFD_CUDA(call) checkCuda((call), #call, __FILE__, __LINE__)
#define CFD_CUDA_LAUNCH(name) \
    checkCuda(cudaGetLastError(), name, __FILE__, __LINE__)

namespace {

__global__ void primitiveKernel(Block in,
                                GasModel gas,
                                float* rho,
                                float* u,
                                float* v,
                                float* w,
                                float* p,
                                float* y,
                                float* gamma) {
    const int id = blockIdx.x * blockDim.x + threadIdx.x;
    if (id >= in.cells())
        return;
    cfd::fillPrimitive(in, gas, id, rho, u, v, w, p, y, gamma);
}

__global__ void fluxXKernel(Block in,
                            cfd::PrimitiveField prim,
                            GasModel gas,
                            BlockBoundaries sides,
                            int limiter,
                            float* fx) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i > in.nx || j >= in.ny)
        return;
    cfd::faceFluxX(in, prim, gas, sides, limiter, i, j, k,
                   fx + ((static_cast<long long>(k) * in.ny + j) *
                             (in.nx + 1) + i) * cfd::kComponents);
}

__global__ void fluxYKernel(Block in,
                            cfd::PrimitiveField prim,
                            GasModel gas,
                            BlockBoundaries sides,
                            int limiter,
                            float* fy) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= in.nx || j > in.ny)
        return;
    cfd::faceFluxY(in, prim, gas, sides, limiter, i, j, k,
                   fy + ((static_cast<long long>(k) * (in.ny + 1) + j) *
                             in.nx + i) * cfd::kComponents);
}

__global__ void fluxZKernel(Block in,
                            cfd::PrimitiveField prim,
                            GasModel gas,
                            BlockBoundaries sides,
                            int limiter,
                            float* fz) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= in.nx || j >= in.ny)
        return;
    cfd::faceFluxZ(in, prim, gas, sides, limiter, i, j, k,
                   fz + ((static_cast<long long>(k) * in.ny + j) * in.nx + i) *
                            cfd::kComponents);
}

__global__ void combineKernel(Block in,
                              Block keep,
                              Block out,
                              GasModel gas,
                              const float* fx,
                              const float* fy,
                              const float* fz,
                              float dt,
                              float a,
                              float b,
                              float diffusivity) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= in.nx || j >= in.ny)
        return;
    cfd::combine(in, keep, out, gas, fx, fy, fz, i, j, k, dt, a, b,
                 diffusivity);
}

__global__ void solidKernel(Block block, GasModel gas, int layer) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    const int k = blockIdx.z;
    if (i >= block.nx || j >= block.ny)
        return;
    cfd::solidCell(block, gas, i, j, k, layer);
}

__global__ void ghostRowKernel(Block block,
                               GasModel gas,
                               BlockBoundaries sides) {
    const int j = blockIdx.x * blockDim.x + threadIdx.x;
    const int k = blockIdx.z;
    if (j >= block.ny)
        return;
    BlockBoundaries local = sides;
    local.inletY = (j + 0.5f) / static_cast<float>(block.ny);
    local.inletZ = (k + 0.5f) / static_cast<float>(block.nz);
    for (int m = 1; m <= block.ghost; ++m) {
        cfd::mirrorSide(block, gas, sides.left, -m, j, k, m - 1, j, k, true,
                        local);
        cfd::mirrorSide(block, gas, sides.right, block.nx - 1 + m, j, k,
                        block.nx - m, j, k, true, local);
    }
}

__global__ void ghostColumnKernel(Block block,
                                  GasModel gas,
                                  BlockBoundaries sides) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int k = blockIdx.z;
    if (i >= block.nx)
        return;
    BlockBoundaries local = sides;
    local.inletY = (i + 0.5f) / static_cast<float>(block.nx);
    local.inletZ = (k + 0.5f) / static_cast<float>(block.nz);
    for (int m = 1; m <= block.ghost; ++m) {
        cfd::mirrorSide(block, gas, sides.bottom, i, -m, k, i, m - 1, k, false,
                        local);
        cfd::mirrorSide(block, gas, sides.top, i, block.ny - 1 + m, k, i,
                        block.ny - m, k, false, local);
    }
}

__global__ void ghostPlaneKernel(Block block,
                                 GasModel gas,
                                 BlockBoundaries sides) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= block.nx || j >= block.ny)
        return;
    BlockBoundaries local = sides;
    local.inletY = (i + 0.5f) / static_cast<float>(block.nx);
    local.inletZ = (j + 0.5f) / static_cast<float>(block.ny);
    for (int m = 1; m <= block.ghost; ++m) {
        cfd::mirrorSpan(block, gas, sides.front, i, j, -m, i, j, m - 1,
                        local);
        cfd::mirrorSpan(block, gas, sides.back, i, j, block.nz - 1 + m, i, j,
                        block.nz - m, local);
    }
}

__global__ void ghostCornerKernel(Block block,
                                  GasModel gas,
                                  BlockBoundaries sides) {
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    const int g = block.ghost;
    const int gz = block.ghostZ();
    const int spanI = 2 * g + block.nx;
    const int spanJ = 2 * g + block.ny;
    const int spanK = 2 * gz + block.nz;
    if (lane >= spanI * spanJ * spanK)
        return;
    const int i = lane % spanI - g;
    const int j = lane / spanI % spanJ - g;
    const int k = lane / (spanI * spanJ) - gz;

    const int sx = i < 0 ? -1 : (i >= block.nx ? 1 : 0);
    const int sy = j < 0 ? -1 : (j >= block.ny ? 1 : 0);
    const int sz = k < 0 ? -1 : (k >= block.nz ? 1 : 0);
    if ((sx != 0) + (sy != 0) + (sz != 0) < 2)
        return;
    if (sx < 0 && sides.left.interior) return;
    if (sx > 0 && sides.right.interior) return;
    if (sy < 0 && sides.bottom.interior) return;
    if (sy > 0 && sides.top.interior) return;
    if (sz < 0 && sides.front.interior) return;
    if (sz > 0 && sides.back.interior) return;

    const int sourceI = sx < 0 ? 0 : (sx > 0 ? block.nx - 1 : i);
    const int sourceJ = sy < 0 ? 0 : (sy > 0 ? block.ny - 1 : j);
    const int sourceK = sz < 0 ? 0 : (sz > 0 ? block.nz - 1 : k);
    const cfd::Primitive q =
        cfd::primitiveOf(block, gas, block.index(sourceI, sourceJ, sourceK));
    cfd::writeState(block, block.index(i, j, k), q);
}

__global__ void rateKernel(Block block, GasModel gas, float* partials) {
    __shared__ float shared[kReduceBlock];
    const int total = block.nx * block.ny * block.nz;
    const int lane = threadIdx.x;
    float best = 0.0f;
    for (int id = blockIdx.x * blockDim.x + lane; id < total;
         id += blockDim.x * gridDim.x) {
        const int i = id % block.nx;
        const int j = id / block.nx % block.ny;
        const int k = id / (block.nx * block.ny);
        best = fmaxf(best, cfd::cellRate(block, gas, i, j, k));
    }
    shared[lane] = best;
    __syncthreads();
    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (lane < stride)
            shared[lane] = fmaxf(shared[lane], shared[lane + stride]);
        __syncthreads();
    }
    if (lane == 0)
        partials[blockIdx.x] = shared[0];
}

}

struct CompressibleDevice {
    int nx = 0, ny = 0, nz = 1, ghost = 0;
    bool species = false;
    std::size_t cells = 0;

    float* fields[3][6] = {};
    uint8_t* solid = nullptr;
    float* solidU = nullptr;
    float* solidV = nullptr;
    float* solidW = nullptr;
    float* fluxX = nullptr;
    float* fluxY = nullptr;
    float* fluxZ = nullptr;
    float* primitive[7] = {};
    float* partials = nullptr;
    std::vector<float> partialHost;
};

bool compressibleCudaAvailable() {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess)
        return false;
    return count > 0;
}

CompressibleDevice* compressibleCudaCreate(int nx,
                                           int ny,
                                           int nz,
                                           int ghost,
                                           bool species) {
    CompressibleDevice* device = new CompressibleDevice();
    device->nx = nx;
    device->ny = ny;
    device->nz = nz;
    device->ghost = ghost;
    device->species = species;
    device->cells = static_cast<std::size_t>(nx + 2 * ghost) *
                    static_cast<std::size_t>(ny + 2 * ghost) *
                    static_cast<std::size_t>(nz > 1 ? nz + 2 * ghost : 1);

    const std::size_t bytes = device->cells * sizeof(float);
    for (int set = 0; set < 3; ++set)
        for (int c = 0; c < 6; ++c) {
            if (c == 5 && !species)
                continue;
            CFD_CUDA(cudaMalloc(&device->fields[set][c], bytes));
            CFD_CUDA(cudaMemset(device->fields[set][c], 0, bytes));
        }

    const std::size_t mask = static_cast<std::size_t>(nx) * ny * nz;
    CFD_CUDA(cudaMalloc(&device->solid, mask * sizeof(uint8_t)));
    CFD_CUDA(cudaMalloc(&device->solidU, mask * sizeof(float)));
    CFD_CUDA(cudaMalloc(&device->solidV, mask * sizeof(float)));
    CFD_CUDA(cudaMalloc(&device->solidW, mask * sizeof(float)));
    CFD_CUDA(cudaMemset(device->solidU, 0, mask * sizeof(float)));
    CFD_CUDA(cudaMemset(device->solidV, 0, mask * sizeof(float)));
    CFD_CUDA(cudaMemset(device->solidW, 0, mask * sizeof(float)));
    CFD_CUDA(cudaMalloc(&device->fluxX,
                        static_cast<std::size_t>(nx + 1) * ny * nz *
                            cfd::kComponents * sizeof(float)));
    CFD_CUDA(cudaMalloc(&device->fluxY,
                        static_cast<std::size_t>(nx) * (ny + 1) * nz *
                            cfd::kComponents * sizeof(float)));
    if (nz > 1)
        CFD_CUDA(cudaMalloc(&device->fluxZ,
                            static_cast<std::size_t>(nx) * ny * (nz + 1) *
                                cfd::kComponents * sizeof(float)));
    for (int c = 0; c < 7; ++c)
        CFD_CUDA(cudaMalloc(&device->primitive[c], bytes));
    CFD_CUDA(cudaMalloc(&device->partials,
                        kMaxReduceBlocks * sizeof(float)));
    device->partialHost.assign(kMaxReduceBlocks, 0.0f);
    return device;
}

void compressibleCudaDestroy(CompressibleDevice* device) {
    if (!device)
        return;
    for (int set = 0; set < 3; ++set)
        for (int c = 0; c < 6; ++c)
            if (device->fields[set][c])
                cudaFree(device->fields[set][c]);
    if (device->solid)
        cudaFree(device->solid);
    if (device->solidU)
        cudaFree(device->solidU);
    if (device->solidV)
        cudaFree(device->solidV);
    if (device->solidW)
        cudaFree(device->solidW);
    if (device->fluxX)
        cudaFree(device->fluxX);
    if (device->fluxY)
        cudaFree(device->fluxY);
    if (device->fluxZ)
        cudaFree(device->fluxZ);
    for (int c = 0; c < 7; ++c)
        if (device->primitive[c])
            cudaFree(device->primitive[c]);
    if (device->partials)
        cudaFree(device->partials);
    delete device;
}

void compressibleCudaUploadSolid(CompressibleDevice* device,
                                 const uint8_t* mask,
                                 const float* velX,
                                 const float* velY,
                                 const float* velZ) {
    const std::size_t cells =
        static_cast<std::size_t>(device->nx) * device->ny * device->nz;
    CFD_CUDA(cudaMemcpy(device->solid, mask, cells, cudaMemcpyHostToDevice));
    if (velX)
        CFD_CUDA(cudaMemcpy(device->solidU, velX, cells * sizeof(float),
                            cudaMemcpyHostToDevice));
    if (velY)
        CFD_CUDA(cudaMemcpy(device->solidV, velY, cells * sizeof(float),
                            cudaMemcpyHostToDevice));
    if (velZ)
        CFD_CUDA(cudaMemcpy(device->solidW, velZ, cells * sizeof(float),
                            cudaMemcpyHostToDevice));
}

void compressibleCudaUpload(CompressibleDevice* device,
                            int set,
                            const float* const* host) {
    const std::size_t bytes = device->cells * sizeof(float);
    for (int c = 0; c < 6; ++c) {
        if (c == 5 && !device->species)
            continue;
        CFD_CUDA(cudaMemcpy(device->fields[set][c], host[c], bytes,
                            cudaMemcpyHostToDevice));
    }
}

void compressibleCudaDownload(CompressibleDevice* device,
                              int set,
                              float* const* host) {
    const std::size_t bytes = device->cells * sizeof(float);
    for (int c = 0; c < 6; ++c) {
        if (c == 5 && !device->species)
            continue;
        CFD_CUDA(cudaMemcpy(host[c], device->fields[set][c], bytes,
                            cudaMemcpyDeviceToHost));
    }
}

namespace {

Block deviceBlock(CompressibleDevice* device, int set, const Block& shape) {
    Block block = shape;
    block.rho = device->fields[set][0];
    block.rhou = device->fields[set][1];
    block.rhov = device->fields[set][2];
    block.rhow = device->fields[set][3];
    block.rhoE = device->fields[set][4];
    block.rhoY = device->species ? device->fields[set][5] : nullptr;
    block.solid = device->solid;
    block.solidU = device->solidU;
    block.solidV = device->solidV;
    block.solidW = device->solidW;
    return block;
}

}

float compressibleCudaTimeStep(CompressibleDevice* device,
                               const Block& shape,
                               const GasModel& gas,
                               float cfl) {
    const Block block = deviceBlock(device, 0, shape);
    const int total = block.nx * block.ny * block.nz;
    int blocks = (total + kReduceBlock - 1) / kReduceBlock;
    if (blocks > kMaxReduceBlocks)
        blocks = kMaxReduceBlocks;
    rateKernel<<<blocks, kReduceBlock>>>(block, gas, device->partials);
    CFD_CUDA_LAUNCH("rateKernel");
    CFD_CUDA(cudaMemcpy(device->partialHost.data(), device->partials,
                        blocks * sizeof(float), cudaMemcpyDeviceToHost));

    float worst = 0.0f;
    for (int k = 0; k < blocks; ++k)
        worst = std::max(worst, device->partialHost[k]);
    if (!(worst > 0.0f))
        return 0.0f;
    return cfl / worst;
}

void compressibleCudaStage(CompressibleDevice* device,
                           const Block& shape,
                           int inSet,
                           int keepSet,
                           int outSet,
                           const GasModel& gas,
                           const BlockBoundaries& sides,
                           float dt,
                           float a,
                           float b,
                           int limiter,
                           float diffusivity) {
    Block in = deviceBlock(device, inSet, shape);
    Block keep = deviceBlock(device, keepSet, shape);
    Block out = deviceBlock(device, outSet, shape);

    const dim3 threads(kBlockX, kBlockY);
    const dim3 fillGrid((in.nx + kBlockX - 1) / kBlockX,
                        (in.ny + kBlockY - 1) / kBlockY, in.nz);
    if (in.solid)
        for (int layer = 0; layer < 2; ++layer) {
            solidKernel<<<fillGrid, threads>>>(in, gas, layer);
            CFD_CUDA_LAUNCH("solidKernel");
        }

    ghostRowKernel<<<dim3((in.ny + 127) / 128, 1, in.nz), 128>>>(in, gas,
                                                                 sides);
    CFD_CUDA_LAUNCH("ghostRowKernel");
    ghostColumnKernel<<<dim3((in.nx + 127) / 128, 1, in.nz), 128>>>(in, gas,
                                                                    sides);
    CFD_CUDA_LAUNCH("ghostColumnKernel");
    if (in.spans()) {
        ghostPlaneKernel<<<dim3((in.nx + kBlockX - 1) / kBlockX,
                                (in.ny + kBlockY - 1) / kBlockY, 1),
                           threads>>>(in, gas, sides);
        CFD_CUDA_LAUNCH("ghostPlaneKernel");
    }
    const int corners = (in.nx + 2 * in.ghost) * (in.ny + 2 * in.ghost) *
                        (in.nz + 2 * in.ghostZ());
    ghostCornerKernel<<<(corners + 63) / 64, 64>>>(in, gas, sides);
    CFD_CUDA_LAUNCH("ghostCornerKernel");

    const dim3 faceX((in.nx + 1 + kBlockX) / kBlockX,
                     (in.ny + kBlockY - 1) / kBlockY, in.nz);
    const dim3 faceY((in.nx + kBlockX - 1) / kBlockX,
                     (in.ny + 1 + kBlockY) / kBlockY, in.nz);
    const dim3 faceZ((in.nx + kBlockX - 1) / kBlockX,
                     (in.ny + kBlockY - 1) / kBlockY, in.nz + 1);
    const dim3 cellGrid((in.nx + kBlockX - 1) / kBlockX,
                        (in.ny + kBlockY - 1) / kBlockY, in.nz);

    const int total = in.cells();
    primitiveKernel<<<(total + 255) / 256, 256>>>(
        in, gas, device->primitive[0], device->primitive[1],
        device->primitive[2], device->primitive[3], device->primitive[4],
        device->primitive[5], device->primitive[6]);
    CFD_CUDA_LAUNCH("primitiveKernel");

    cfd::PrimitiveField prim;
    prim.rho = device->primitive[0];
    prim.u = device->primitive[1];
    prim.v = device->primitive[2];
    prim.w = device->primitive[3];
    prim.p = device->primitive[4];
    prim.y = device->primitive[5];
    prim.gamma = device->primitive[6];

    fluxXKernel<<<faceX, threads>>>(in, prim, gas, sides, limiter, device->fluxX);
    CFD_CUDA_LAUNCH("fluxXKernel");
    fluxYKernel<<<faceY, threads>>>(in, prim, gas, sides, limiter, device->fluxY);
    CFD_CUDA_LAUNCH("fluxYKernel");
    if (in.spans()) {
        fluxZKernel<<<faceZ, threads>>>(in, prim, gas, sides, limiter,
                                        device->fluxZ);
        CFD_CUDA_LAUNCH("fluxZKernel");
    }
    combineKernel<<<cellGrid, threads>>>(in, keep, out, gas, device->fluxX,
                                         device->fluxY, device->fluxZ, dt, a,
                                         b, diffusivity);
    CFD_CUDA_LAUNCH("combineKernel");
}

#endif
