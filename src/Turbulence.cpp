#include "Turbulence.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace {
constexpr float kBetaStar = 0.09f;
constexpr float kKarman = 0.41f;
constexpr float kA1 = 0.31f;
constexpr float kSigmaK1 = 0.85f;
constexpr float kSigmaW1 = 0.5f;
constexpr float kBeta1 = 0.075f;
constexpr float kSigmaK2 = 1.0f;
constexpr float kSigmaW2 = 0.856f;
constexpr float kBeta2 = 0.0828f;
constexpr float kAlpha1 =
    kBeta1 / kBetaStar - kSigmaW1 * kKarman * kKarman / 0.3f;
constexpr float kAlpha2 =
    kBeta2 / kBetaStar - kSigmaW2 * kKarman * kKarman / 0.3f;
constexpr float kTiny = 1e-12f;
}

void TurbulenceModel::resize(int nxIn, int nyIn, int nzIn) {
    nx = nxIn;
    ny = nyIn;
    nz = nzIn;
    const std::size_t cells = static_cast<std::size_t>(nx) * ny * nz;
    nuT.assign(cells, 0.0f);
    strainMag.assign(cells, 0.0f);
    wallDist.assign(cells, 0.0f);
    if (k.size() != cells) {
        k.assign(cells, 0.0f);
        omega.assign(cells, 0.0f);
    }
    kNext.assign(cells, 0.0f);
    omegaNext.assign(cells, 0.0f);
}

void TurbulenceModel::setState(std::vector<float>&& kIn,
                               std::vector<float>&& omegaIn) {
    k = std::move(kIn);
    omega = std::move(omegaIn);
}

void TurbulenceModel::initialise(const Config& cfg,
                                 const std::vector<uint8_t>& solid,
                                 int nxIn,
                                 int nyIn,
                                 int nzIn,
                                 float dxIn,
                                 float dyIn,
                                 float dzIn,
                                 float molecularIn) {
    kind = cfg.turbulence;
    dx = dxIn;
    dy = dyIn;
    dz = dzIn;
    molecular = molecularIn;
    cs = cfg.Cs;
    if (kind == TurbulenceKind::None) {
        nx = nxIn;
        ny = nyIn;
        nz = nzIn;
        return;
    }

    const bool fresh =
        k.size() != static_cast<std::size_t>(nxIn) * nyIn * nzIn;
    resize(nxIn, nyIn, nzIn);
    buildWallDistance(solid);

    const float speed = std::fabs(cfg.U0) > 1e-6f ? std::fabs(cfg.U0)
                                                  : std::fabs(cfg.lidSpeed);
    const float length = cfg.turbLengthScale > 0.0f ? cfg.turbLengthScale
                                                    : 0.1f * cfg.Ly;
    inletK = 1.5f * (cfg.turbIntensity * speed) * (cfg.turbIntensity * speed);
    inletK = std::max(inletK, 1e-10f);
    inletOmega = std::sqrt(inletK) /
                 (std::pow(kBetaStar, 0.25f) * std::max(length, 1e-6f));
    inletOmega = std::max(inletOmega, 1e-6f);

    if (fresh && transported()) {
        std::fill(k.begin(), k.end(), inletK);
        std::fill(omega.begin(), omega.end(), inletOmega);
    }
}

void TurbulenceModel::buildWallDistance(const std::vector<uint8_t>& solid) {
    const bool volumetric = nz > 1;
    const int plane = nx * ny;
    const int cells = plane * nz;
    const float far = std::numeric_limits<float>::max();
    std::vector<float> best(static_cast<std::size_t>(cells), far);
    std::deque<int> queue;

    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const int id = (k * ny + j) * nx + i;
                if (solid[id]) {
                    best[id] = 0.0f;
                    queue.push_back(id);
                    continue;
                }
                if (i == 0 || i == nx - 1 || j == 0 || j == ny - 1 ||
                    (volumetric && (k == 0 || k == nz - 1))) {
                    float edge = std::min(
                        std::min((i + 0.5f) * dx, (nx - i - 0.5f) * dx),
                        std::min((j + 0.5f) * dy, (ny - j - 0.5f) * dy));
                    if (volumetric)
                        edge = std::min(
                            edge, std::min((k + 0.5f) * dz,
                                           (nz - k - 0.5f) * dz));
                    best[id] = edge;
                    queue.push_back(id);
                }
            }

    const float diagXY = std::hypot(dx, dy);
    const float diagXZ = std::hypot(dx, dz);
    const float diagYZ = std::hypot(dy, dz);
    const float diagXYZ = std::hypot(dx, dy, dz);
    const float step[26] = {dx, dx, dy, dy,
                            diagXY, diagXY, diagXY, diagXY,
                            dz, dz,
                            diagXZ, diagXZ, diagXZ, diagXZ,
                            diagYZ, diagYZ, diagYZ, diagYZ,
                            diagXYZ, diagXYZ, diagXYZ, diagXYZ,
                            diagXYZ, diagXYZ, diagXYZ, diagXYZ};
    const int offsetI[26] = {-1, 1, 0, 0, -1, 1, -1, 1,
                             0, 0, -1, 1, -1, 1, 0, 0, 0, 0,
                             -1, 1, -1, 1, -1, 1, -1, 1};
    const int offsetJ[26] = {0, 0, -1, 1, -1, -1, 1, 1,
                             0, 0, 0, 0, 0, 0, -1, 1, -1, 1,
                             -1, -1, 1, 1, -1, -1, 1, 1};
    const int offsetK[26] = {0, 0, 0, 0, 0, 0, 0, 0,
                             -1, 1, -1, -1, 1, 1, -1, -1, 1, 1,
                             -1, -1, -1, -1, 1, 1, 1, 1};
    const int neighbours = volumetric ? 26 : 8;

    while (!queue.empty()) {
        const int id = queue.front();
        queue.pop_front();
        const int i = id % nx;
        const int j = (id / nx) % ny;
        const int k = id / plane;
        for (int n = 0; n < neighbours; ++n) {
            const int ni = i + offsetI[n];
            const int nj = j + offsetJ[n];
            const int nk = k + offsetK[n];
            if (ni < 0 || ni >= nx || nj < 0 || nj >= ny || nk < 0 ||
                nk >= nz)
                continue;
            const int other = (nk * ny + nj) * nx + ni;
            if (solid[other])
                continue;
            const float candidate = best[id] + step[n];
            if (candidate < best[other] - 1e-9f) {
                best[other] = candidate;
                queue.push_back(other);
            }
        }
    }

    const float floorDistance = volumetric
                                    ? 0.5f * std::min(std::min(dx, dy), dz)
                                    : 0.5f * std::min(dx, dy);
    for (int id = 0; id < cells; ++id)
        wallDist[id] = solid[id] ? 0.0f : std::max(best[id], floorDistance);
}

void TurbulenceModel::setGhosts(const WallGhost& leftIn,
                                const WallGhost& rightIn,
                                const WallGhost& bottomIn,
                                const WallGhost& topIn,
                                const WallGhost& frontIn,
                                const WallGhost& backIn) {
    left = leftIn;
    right = rightIn;
    bottom = bottomIn;
    top = topIn;
    front = frontIn;
    back = backIn;
}

void TurbulenceModel::computeStrain(const std::vector<float>& u,
                                    const std::vector<float>& v,
                                    const std::vector<float>& w,
                                    const std::vector<uint8_t>& solid) {
    const bool volumetric = nz > 1;
    const int plane = nx * ny;
    const int strideU = (nx + 1) * ny;
    const int strideV = nx * (ny + 1);
    const float invDx = 1.0f / dx;
    const float invDy = 1.0f / dy;
    const float invDz = 1.0f / dz;
    const float halfDx = 0.5f / dx;
    const float halfDy = 0.5f / dy;
    const float halfDz = 0.5f / dz;

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= 32)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            const int row = (k * ny + j) * nx;
            const int rowU = (k * ny + j) * (nx + 1);
            const int rowV = (k * (ny + 1) + j) * nx;
            for (int i = 0; i < nx; ++i) {
                const int id = row + i;
                if (solid[id]) {
                    strainMag[id] = 0.0f;
                    continue;
                }
                const float dudx = (u[rowU + i + 1] - u[rowU + i]) * invDx;
                const float dvdy = (v[rowV + nx + i] - v[rowV + i]) * invDy;

                const float uHere = 0.5f * (u[rowU + i] + u[rowU + i + 1]);
                const float vHere = 0.5f * (v[rowV + i] + v[rowV + nx + i]);

                const float uAbove =
                    (j + 1 >= ny)
                        ? top.sign * uHere + top.offset
                        : (solid[id + nx]
                               ? 2.0f * 0.5f *
                                         (u[rowU + nx + 1 + i] +
                                          u[rowU + nx + 2 + i]) -
                                     uHere
                               : 0.5f * (u[rowU + nx + 1 + i] +
                                         u[rowU + nx + 2 + i]));
                const float uBelow =
                    (j == 0)
                        ? bottom.sign * uHere + bottom.offset
                        : (solid[id - nx]
                               ? 2.0f * 0.5f *
                                         (u[rowU - nx - 1 + i] +
                                          u[rowU - nx + i]) -
                                     uHere
                               : 0.5f * (u[rowU - nx - 1 + i] +
                                         u[rowU - nx + i]));
                const float dudy = (uAbove - uBelow) * halfDy;

                const float vRight =
                    (i + 1 >= nx)
                        ? right.sign * vHere + right.offset
                        : (solid[id + 1]
                               ? 2.0f * 0.5f *
                                         (v[rowV + i + 1] +
                                          v[rowV + nx + i + 1]) -
                                     vHere
                               : 0.5f * (v[rowV + i + 1] +
                                         v[rowV + nx + i + 1]));
                const float vLeft =
                    (i == 0)
                        ? left.sign * vHere + left.offset
                        : (solid[id - 1]
                               ? 2.0f * 0.5f *
                                         (v[rowV + i - 1] +
                                          v[rowV + nx + i - 1]) -
                                     vHere
                               : 0.5f * (v[rowV + i - 1] +
                                         v[rowV + nx + i - 1]));
                const float dvdx = (vRight - vLeft) * halfDx;

                const float shear = 0.5f * (dudy + dvdx);
                if (!volumetric) {
                    strainMag[id] = std::sqrt(std::max(
                        0.0f, 2.0f * (dudx * dudx + dvdy * dvdy +
                                      2.0f * shear * shear)));
                    continue;
                }

                const float dwdz = (w[id + plane] - w[id]) * invDz;
                const float wHere = 0.5f * (w[id] + w[id + plane]);

                const float uBack =
                    (k + 1 >= nz)
                        ? back.sign * uHere + back.offset
                        : (solid[id + plane]
                               ? 2.0f * 0.5f *
                                         (u[rowU + strideU + i] +
                                          u[rowU + strideU + i + 1]) -
                                     uHere
                               : 0.5f * (u[rowU + strideU + i] +
                                         u[rowU + strideU + i + 1]));
                const float uFront =
                    (k == 0)
                        ? front.sign * uHere + front.offset
                        : (solid[id - plane]
                               ? 2.0f * 0.5f *
                                         (u[rowU - strideU + i] +
                                          u[rowU - strideU + i + 1]) -
                                     uHere
                               : 0.5f * (u[rowU - strideU + i] +
                                         u[rowU - strideU + i + 1]));
                const float dudz = (uBack - uFront) * halfDz;

                const float vBack =
                    (k + 1 >= nz)
                        ? back.sign * vHere + back.offset
                        : (solid[id + plane]
                               ? 2.0f * 0.5f *
                                         (v[rowV + strideV + i] +
                                          v[rowV + strideV + nx + i]) -
                                     vHere
                               : 0.5f * (v[rowV + strideV + i] +
                                         v[rowV + strideV + nx + i]));
                const float vFront =
                    (k == 0)
                        ? front.sign * vHere + front.offset
                        : (solid[id - plane]
                               ? 2.0f * 0.5f *
                                         (v[rowV - strideV + i] +
                                          v[rowV - strideV + nx + i]) -
                                     vHere
                               : 0.5f * (v[rowV - strideV + i] +
                                         v[rowV - strideV + nx + i]));
                const float dvdz = (vBack - vFront) * halfDz;

                const float wRight =
                    (i + 1 >= nx)
                        ? right.sign * wHere + right.offset
                        : (solid[id + 1]
                               ? 2.0f * 0.5f *
                                         (w[id + 1] + w[id + 1 + plane]) -
                                     wHere
                               : 0.5f * (w[id + 1] + w[id + 1 + plane]));
                const float wLeft =
                    (i == 0)
                        ? left.sign * wHere + left.offset
                        : (solid[id - 1]
                               ? 2.0f * 0.5f *
                                         (w[id - 1] + w[id - 1 + plane]) -
                                     wHere
                               : 0.5f * (w[id - 1] + w[id - 1 + plane]));
                const float dwdx = (wRight - wLeft) * halfDx;

                const float wAbove =
                    (j + 1 >= ny)
                        ? top.sign * wHere + top.offset
                        : (solid[id + nx]
                               ? 2.0f * 0.5f *
                                         (w[id + nx] + w[id + nx + plane]) -
                                     wHere
                               : 0.5f * (w[id + nx] + w[id + nx + plane]));
                const float wBelow =
                    (j == 0)
                        ? bottom.sign * wHere + bottom.offset
                        : (solid[id - nx]
                               ? 2.0f * 0.5f *
                                         (w[id - nx] + w[id - nx + plane]) -
                                     wHere
                               : 0.5f * (w[id - nx] + w[id - nx + plane]));
                const float dwdy = (wAbove - wBelow) * halfDy;

                const float shearXZ = 0.5f * (dudz + dwdx);
                const float shearYZ = 0.5f * (dvdz + dwdy);
                strainMag[id] = std::sqrt(std::max(
                    0.0f, 2.0f * (dudx * dudx + dvdy * dvdy + dwdz * dwdz +
                                  2.0f * (shear * shear + shearXZ * shearXZ +
                                          shearYZ * shearYZ))));
            }
        }
    }
}

void TurbulenceModel::smagorinsky(const std::vector<uint8_t>& solid) {
    const float width =
        nz > 1 ? std::cbrt(dx * dy * dz) : std::sqrt(dx * dy);

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= 32)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            const int row = (k * ny + j) * nx;
            for (int i = 0; i < nx; ++i) {
                const int id = row + i;
                if (solid[id]) {
                    nuT[id] = 0.0f;
                    continue;
                }

                const float strainHere = strainMag[id];
                const float distance = wallDist[id];
                const float yPlus =
                    distance *
                    std::sqrt(strainHere / std::max(molecular, 1e-20f));
                const float damping = 1.0f - std::exp(-yPlus / 26.0f);
                const float mixing =
                    std::min(cs * width * damping, kKarman * distance);
                nuT[id] = mixing * mixing * strainHere;
            }
        }
    }
}

void TurbulenceModel::kOmega(const std::vector<float>& u,
                             const std::vector<float>& v,
                             const std::vector<float>& w,
                             const std::vector<uint8_t>& solid,
                             float dt) {
    const bool volumetric = nz > 1;
    const int plane = nx * ny;
    const float invDx = 1.0f / dx;
    const float invDy = 1.0f / dy;
    const float invDz = 1.0f / dz;

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= 32)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            const int row = (k * ny + j) * nx;
            const int rowU = (k * ny + j) * (nx + 1);
            const int rowV = (k * (ny + 1) + j) * nx;
            for (int i = 0; i < nx; ++i) {
                const int id = row + i;
                if (solid[id]) {
                    kNext[id] = 0.0f;
                    omegaNext[id] = omega[id];
                    continue;
                }

                const float distance = wallDist[id];
                const float kHere = std::max(this->k[id], 0.0f);
                const float wHere = std::max(omega[id], 1e-8f);

                const auto sample = [&](const std::vector<float>& field,
                                        int di, int dj, int dk) {
                    const int ni = std::min(nx - 1, std::max(0, i + di));
                    const int nj = std::min(ny - 1, std::max(0, j + dj));
                    const int nk = std::min(nz - 1, std::max(0, k + dk));
                    const int other = (nk * ny + nj) * nx + ni;
                    return solid[other] ? field[id] : field[other];
                };

                const float dkdx =
                    (sample(this->k, 1, 0, 0) - sample(this->k, -1, 0, 0)) *
                    0.5f * invDx;
                const float dkdy =
                    (sample(this->k, 0, 1, 0) - sample(this->k, 0, -1, 0)) *
                    0.5f * invDy;
                const float dkdz =
                    (sample(this->k, 0, 0, 1) - sample(this->k, 0, 0, -1)) *
                    0.5f * invDz;
                const float dwdx =
                    (sample(omega, 1, 0, 0) - sample(omega, -1, 0, 0)) *
                    0.5f * invDx;
                const float dwdy =
                    (sample(omega, 0, 1, 0) - sample(omega, 0, -1, 0)) *
                    0.5f * invDy;
                const float dwdz =
                    (sample(omega, 0, 0, 1) - sample(omega, 0, 0, -1)) *
                    0.5f * invDz;

                const float crossDiffusion =
                    2.0f * kSigmaW2 / wHere *
                    (dkdx * dwdx + dkdy * dwdy + dkdz * dwdz);
                const float cdkw = std::max(crossDiffusion, 1e-10f);

                const float argA =
                    std::min(std::max(std::sqrt(kHere) /
                                          (kBetaStar * wHere * distance),
                                      500.0f * molecular /
                                          (distance * distance * wHere)),
                             4.0f * kSigmaW2 * kHere /
                                 (cdkw * distance * distance));
                const float f1 = std::tanh(argA * argA * argA * argA);
                const float argB =
                    std::max(2.0f * std::sqrt(kHere) /
                                 (kBetaStar * wHere * distance),
                             500.0f * molecular /
                                 (distance * distance * wHere));
                const float f2 = std::tanh(argB * argB);

                const float strainHere = strainMag[id];
                const float eddy =
                    kA1 * kHere /
                    std::max(kA1 * wHere, strainHere * f2 + kTiny);

                const float production =
                    std::min(eddy * strainHere * strainHere,
                             10.0f * kBetaStar * kHere * wHere);
                const float alpha = f1 * kAlpha1 + (1.0f - f1) * kAlpha2;
                const float beta = f1 * kBeta1 + (1.0f - f1) * kBeta2;
                const float sigmaK = f1 * kSigmaK1 + (1.0f - f1) * kSigmaK2;
                const float sigmaW = f1 * kSigmaW1 + (1.0f - f1) * kSigmaW2;

                const auto laplacian = [&](const std::vector<float>& field,
                                           float sigma) {
                    const float nuFace = molecular + sigma * eddy;
                    const float east = sample(field, 1, 0, 0);
                    const float west = sample(field, -1, 0, 0);
                    const float north = sample(field, 0, 1, 0);
                    const float south = sample(field, 0, -1, 0);
                    const float behind = sample(field, 0, 0, 1);
                    const float ahead = sample(field, 0, 0, -1);
                    return nuFace *
                           ((east - 2.0f * field[id] + west) * invDx * invDx +
                            (north - 2.0f * field[id] + south) * invDy * invDy +
                            (behind - 2.0f * field[id] + ahead) * invDz * invDz);
                };

                const float uHere = 0.5f * (u[rowU + i] + u[rowU + i + 1]);
                const float vHere = 0.5f * (v[rowV + i] + v[rowV + nx + i]);
                const float wCell = 0.5f * (w[id] + w[id + plane]);
                const float upwindK =
                    uHere * (uHere > 0.0f
                                 ? (kHere - sample(this->k, -1, 0, 0)) * invDx
                                 : (sample(this->k, 1, 0, 0) - kHere) * invDx) +
                    vHere * (vHere > 0.0f
                                 ? (kHere - sample(this->k, 0, -1, 0)) * invDy
                                 : (sample(this->k, 0, 1, 0) - kHere) * invDy) +
                    wCell * (wCell > 0.0f
                                 ? (kHere - sample(this->k, 0, 0, -1)) * invDz
                                 : (sample(this->k, 0, 0, 1) - kHere) * invDz);
                const float upwindW =
                    uHere * (uHere > 0.0f
                                 ? (wHere - sample(omega, -1, 0, 0)) * invDx
                                 : (sample(omega, 1, 0, 0) - wHere) * invDx) +
                    vHere * (vHere > 0.0f
                                 ? (wHere - sample(omega, 0, -1, 0)) * invDy
                                 : (sample(omega, 0, 1, 0) - wHere) * invDy) +
                    wCell * (wCell > 0.0f
                                 ? (wHere - sample(omega, 0, 0, -1)) * invDz
                                 : (sample(omega, 0, 0, 1) - wHere) * invDz);

                const float dissipation = kBetaStar * kHere * wHere;
                const float kUpdate =
                    kHere + dt * (production - dissipation - upwindK +
                                  laplacian(this->k, sigmaK));
                const float omegaUpdate =
                    wHere + dt * (alpha * strainHere * strainHere -
                                  beta * wHere * wHere - upwindW +
                                  laplacian(omega, sigmaW) +
                                  (1.0f - f1) * crossDiffusion);

                kNext[id] = std::max(kUpdate, 1e-12f);
                omegaNext[id] = std::max(omegaUpdate, 1e-6f);
            }
        }
    }

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= 32)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            const int row = (k * ny + j) * nx;
            for (int i = 0; i < nx; ++i) {
                const int id = row + i;
                if (solid[id])
                    continue;
                const bool nextToWall =
                    i == 0 || i == nx - 1 || j == 0 || j == ny - 1 ||
                    (volumetric && (k == 0 || k == nz - 1)) ||
                    solid[id - 1] || solid[id + 1] || solid[id - nx] ||
                    solid[id + nx] ||
                    (volumetric && (solid[id - plane] || solid[id + plane]));
                if (!nextToWall)
                    continue;
                const float distance = wallDist[id];
                omegaNext[id] =
                    60.0f * molecular / (kBeta1 * distance * distance);
                kNext[id] = 1e-12f;
            }
        }
    }

    k.swap(kNext);
    omega.swap(omegaNext);

    #pragma omp parallel for collapse(2) schedule(static) \
        if (nz * ny >= 32)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            const int row = (k * ny + j) * nx;
            for (int i = 0; i < nx; ++i) {
                const int id = row + i;
                if (solid[id]) {
                    nuT[id] = 0.0f;
                    continue;
                }
                const float distance = wallDist[id];
                const float wHere = std::max(omega[id], 1e-8f);
                const float argB =
                    std::max(2.0f * std::sqrt(std::max(this->k[id], 0.0f)) /
                                 (kBetaStar * wHere * distance),
                             500.0f * molecular /
                                 (distance * distance * wHere));
                const float f2 = std::tanh(argB * argB);
                nuT[id] = kA1 * std::max(this->k[id], 0.0f) /
                          std::max(kA1 * wHere, strainMag[id] * f2 + kTiny);
            }
        }
    }
}

void TurbulenceModel::advance(const std::vector<float>& u,
                              const std::vector<float>& v,
                              const std::vector<float>& w,
                              const std::vector<uint8_t>& solid,
                              float dt) {
    if (kind == TurbulenceKind::None)
        return;
    computeStrain(u, v, w, solid);
    if (kind == TurbulenceKind::Smagorinsky)
        smagorinsky(solid);
    else
        kOmega(u, v, w, solid, dt);
}

float TurbulenceModel::mixingCap() const {
    return cs * (nz > 1 ? std::cbrt(dx * dy * dz) : std::sqrt(dx * dy));
}

float TurbulenceModel::peakViscosity() const {
    float worst = 0.0f;
    for (float value : nuT)
        worst = std::max(worst, value);
    return worst;
}

float TurbulenceModel::sourceStepLimit() const {
    if (!transported())
        return 0.0f;
    float rate = 0.0f;
    for (float value : omega)
        rate = std::max(rate, value);
    if (!(rate > 0.0f))
        return 0.0f;
    return 1.0f / (kBetaStar * rate);
}
