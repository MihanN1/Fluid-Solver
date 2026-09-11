#pragma once
#include "SolverCompressible.hpp"

#include <cmath>

namespace cfd {

constexpr int kComponents = 6;
constexpr float kTiny = 1e-12f;
constexpr float kFloor = 1e-9f;

struct Primitive {
    float rho, u, v, w, p, y, gamma;
};

struct PrimitiveField {
    const float* rho = nullptr;
    const float* u = nullptr;
    const float* v = nullptr;
    const float* w = nullptr;
    const float* p = nullptr;
    const float* y = nullptr;
    const float* gamma = nullptr;

    CFD_HD Primitive at(int id) const {
        Primitive out;
        out.rho = rho[id];
        out.u = u[id];
        out.v = v[id];
        out.w = w[id];
        out.p = p[id];
        out.y = y[id];
        out.gamma = gamma[id];
        return out;
    }
};

CFD_HD inline float gammaOf(const GasModel& gas, float y) {
    if (!gas.species || !gas.active)
        return gas.gamma1;
    const float cp = gas.cp1 + y * (gas.cp2 - gas.cp1);
    const float cv = gas.cv1 + y * (gas.cv2 - gas.cv1);
    return cp / cv;
}

CFD_HD inline float gasConstantOf(const GasModel& gas, float y) {
    if (!gas.species || !gas.active)
        return gas.R1;
    return gas.R1 + y * (gas.R2 - gas.R1);
}

CFD_HD inline float clampTo(float value, float low, float high) {
    return fminf(fmaxf(value, low), high);
}

CFD_HD inline float limitSlope(float back, float forward, int kind) {
    if (back * forward <= 0.0f)
        return 0.0f;
    const float sign = back > 0.0f ? 1.0f : -1.0f;
    const float a = fabsf(back);
    const float b = fabsf(forward);
    if (kind == 0)
        return sign * fminf(a, b);
    if (kind == 2)
        return sign * fmaxf(fminf(2.0f * a, b), fminf(a, 2.0f * b));
    return 2.0f * back * forward / (back + forward);
}

CFD_HD inline Primitive primitiveOf(const Block& block,
                                    const GasModel& gas,
                                    int id) {
    Primitive out;
    out.rho = fmaxf(block.rho[id], kFloor);
    const float inv = 1.0f / out.rho;
    out.u = block.rhou[id] * inv;
    out.v = block.rhov[id] * inv;
    out.w = block.rhow ? block.rhow[id] * inv : 0.0f;
    out.y = block.rhoY ? clampTo(block.rhoY[id] * inv, 0.0f, 1.0f) : 0.0f;
    out.gamma = gammaOf(gas, out.y);
    const float energy = block.rhoE[id] * inv;
    const float kinetic = 0.5f * (out.u * out.u + out.v * out.v);
    out.p = fmaxf((out.gamma - 1.0f) * out.rho *
                      (energy - kinetic - 0.5f * out.w * out.w),
                  kFloor);
    return out;
}

CFD_HD inline void conservativeOf(const Primitive& q, float* out) {
    out[0] = q.rho;
    out[1] = q.rho * q.u;
    out[2] = q.rho * q.v;
    out[3] = q.rho * q.w;
    out[4] = q.p / (q.gamma - 1.0f) + 0.5f * q.rho * (q.u * q.u + q.v * q.v);
    out[4] += 0.5f * q.rho * q.w * q.w;
    out[5] = q.rho * q.y;
}

CFD_HD inline void writeState(Block& block, int id, const Primitive& q) {
    float conserved[kComponents];
    conservativeOf(q, conserved);
    block.rho[id] = conserved[0];
    block.rhou[id] = conserved[1];
    block.rhov[id] = conserved[2];
    if (block.rhow)
        block.rhow[id] = conserved[3];
    block.rhoE[id] = conserved[4];
    if (block.rhoY)
        block.rhoY[id] = conserved[5];
}

CFD_HD inline void hllc(const Primitive& left,
                        const Primitive& right,
                        int axis,
                        float* flux) {
    const float uL = axis == 0 ? left.u : (axis == 1 ? left.v : left.w);
    const float uR = axis == 0 ? right.u : (axis == 1 ? right.v : right.w);
    const float tL = axis == 0 ? left.v : left.u;
    const float tR = axis == 0 ? right.v : right.u;
    const float sideL = axis == 2 ? left.v : left.w;
    const float sideR = axis == 2 ? right.v : right.w;

    const float invRhoL = 1.0f / left.rho;
    const float invRhoR = 1.0f / right.rho;
    const float invGamL = 1.0f / (left.gamma - 1.0f);
    const float invGamR = 1.0f / (right.gamma - 1.0f);

    const float aL = sqrtf(left.gamma * left.p * invRhoL);
    const float aR = sqrtf(right.gamma * right.p * invRhoR);

    const float sL = fminf(uL - aL, uR - aR);
    const float sR = fmaxf(uL + aL, uR + aR);

    float energyL =
        left.p * invGamL + 0.5f * left.rho * (uL * uL + tL * tL);
    float energyR =
        right.p * invGamR + 0.5f * right.rho * (uR * uR + tR * tR);
    energyL += 0.5f * left.rho * sideL * sideL;
    energyR += 0.5f * right.rho * sideR * sideR;

    const float mL = left.rho * (sL - uL);
    const float mR = right.rho * (sR - uR);
    const float denominator = mL - mR;
    const float sM = (right.p - left.p + mL * uL - mR * uR) /
                     (fabsf(denominator) > kTiny ? denominator : kTiny);

    const float fL[kComponents] = {left.rho * uL,
                                   left.rho * uL * uL + left.p,
                                   left.rho * uL * tL,
                                   left.rho * uL * sideL,
                                   (energyL + left.p) * uL,
                                   left.rho * uL * left.y};
    const float fR[kComponents] = {right.rho * uR,
                                   right.rho * uR * uR + right.p,
                                   right.rho * uR * tR,
                                   right.rho * uR * sideR,
                                   (energyR + right.p) * uR,
                                   right.rho * uR * right.y};

    const float gapL = sL - sM;
    const float gapR = sR - sM;
    const float factorL = mL / (fabsf(gapL) > kTiny ? gapL : kTiny);
    const float factorR = mR / (fabsf(gapR) > kTiny ? gapR : kTiny);

    const float pushL = (sM - uL) * (sM + left.p / (mL != 0.0f ? mL : kTiny));
    const float pushR = (sM - uR) * (sM + right.p / (mR != 0.0f ? mR : kTiny));

    const float starL[kComponents] = {factorL, factorL * sM, factorL * tL,
                                      factorL * sideL,
                                      factorL * (energyL * invRhoL + pushL),
                                      factorL * left.y};
    const float starR[kComponents] = {factorR, factorR * sM, factorR * tR,
                                      factorR * sideR,
                                      factorR * (energyR * invRhoR + pushR),
                                      factorR * right.y};

    const float uLc[kComponents] = {left.rho, left.rho * uL, left.rho * tL,
                                    left.rho * sideL, energyL,
                                    left.rho * left.y};
    const float uRc[kComponents] = {right.rho, right.rho * uR, right.rho * tR,
                                    right.rho * sideR, energyR,
                                    right.rho * right.y};

    const float wL = sL >= 0.0f ? 1.0f : 0.0f;
    const float wR = (wL == 0.0f && sR <= 0.0f) ? 1.0f : 0.0f;
    const float middle = 1.0f - wL - wR;
    const float wSL = middle * (sM >= 0.0f ? 1.0f : 0.0f);
    const float wSR = middle * (sM < 0.0f ? 1.0f : 0.0f);

    for (int c = 0; c < kComponents; ++c) {
        const float starFluxL = fL[c] + sL * (starL[c] - uLc[c]);
        const float starFluxR = fR[c] + sR * (starR[c] - uRc[c]);
        flux[c] = wL * fL[c] + wSL * starFluxL + wSR * starFluxR + wR * fR[c];
    }

    if (axis == 1) {
        const float swap = flux[1];
        flux[1] = flux[2];
        flux[2] = swap;
    } else if (axis == 2) {
        const float normal = flux[1];
        flux[1] = flux[2];
        flux[2] = flux[3];
        flux[3] = normal;
    }
}

struct Spacing {
    float back = 1.0f;
    float forward = 1.0f;
    float half = 0.5f;
};

CFD_HD inline Primitive reconstruct(const Primitive& centre,
                                    const Primitive& back,
                                    const Primitive& forward,
                                    float side,
                                    int limiter,
                                    const GasModel& gas,
                                    const Spacing& step) {
    const float reach = side * step.half;
    const float invBack = 1.0f / step.back;
    const float invForward = 1.0f / step.forward;
    const auto edge = [&](float low, float middle, float high) {
        return middle + reach * limitSlope((middle - low) * invBack,
                                           (high - middle) * invForward,
                                           limiter);
    };

    Primitive out = centre;
    out.rho = edge(back.rho, centre.rho, forward.rho);
    out.u = edge(back.u, centre.u, forward.u);
    out.v = edge(back.v, centre.v, forward.v);
    out.w = edge(back.w, centre.w, forward.w);
    out.p = edge(back.p, centre.p, forward.p);
    out.y = edge(back.y, centre.y, forward.y);
    out.rho = fmaxf(out.rho, kFloor);
    out.p = fmaxf(out.p, kFloor);
    out.y = clampTo(out.y, 0.0f, 1.0f);
    out.gamma = gammaOf(gas, out.y);
    return out;
}

CFD_HD inline Spacing spacingX(const Block& block, int i) {
    Spacing step;
    if (!block.widths)
        return step;
    const float here = block.widthAt(i);
    step.back = 0.5f * (block.widthAt(i - 1) + here);
    step.forward = 0.5f * (here + block.widthAt(i + 1));
    step.half = 0.5f * here;
    return step;
}

CFD_HD inline Spacing spacingY(const Block& block, int j) {
    Spacing step;
    if (!block.heights)
        return step;
    const float here = block.heightAt(j);
    step.back = 0.5f * (block.heightAt(j - 1) + here);
    step.forward = 0.5f * (here + block.heightAt(j + 1));
    step.half = 0.5f * here;
    return step;
}

CFD_HD inline Spacing spacingZ(const Block& block, int k) {
    Spacing step;
    if (!block.depths)
        return step;
    const float here = block.depthAt(k);
    step.back = 0.5f * (block.depthAt(k - 1) + here);
    step.forward = 0.5f * (here + block.depthAt(k + 1));
    step.half = 0.5f * here;
    return step;
}

CFD_HD inline void mirrorSide(Block& block,
                const GasModel& gas,
                const SideState& side,
                int i,
                int j,
                int k,
                int mirrorI,
                int mirrorJ,
                int mirrorK,
                bool horizontal,
                const BlockBoundaries& sides) {
    if (side.interior)
        return;
    const int target = block.index(i, j, k);
    const int source = block.index(mirrorI, mirrorJ, mirrorK);
    Primitive q = primitiveOf(block, gas, source);

    switch (side.kind) {
    case BoundaryKind::Wall:
    case BoundaryKind::MovingWall:
        if (horizontal) {
            q.u = -q.u;
            q.v = side.noSlip ? 2.0f * side.speed - q.v : q.v;
            q.w = side.noSlip ? -q.w : q.w;
        } else {
            q.v = -q.v;
            q.u = side.noSlip ? 2.0f * side.speed - q.u : q.u;
            q.w = side.noSlip ? -q.w : q.w;
        }
        break;
    case BoundaryKind::Slip:
        if (horizontal)
            q.u = -q.u;
        else
            q.v = -q.v;
        break;
    case BoundaryKind::Inlet: {
        const float gamma = gammaOf(gas, q.y);
        const float gasR = gasConstantOf(gas, q.y);
        const float speedOfSound = sqrtf(gamma * gasR * sides.T0);
        const float speed = sides.mach * speedOfSound;
        const float open =
            side.banded ? ((sides.inletY >= side.from &&
                            sides.inletY <= side.to &&
                            sides.inletZ >= side.from2 &&
                            sides.inletZ <= side.to2) ? 1.0f : 0.0f)
                        : 1.0f;
        if (open == 0.0f) {
            if (horizontal)
                q.u = -q.u;
            else
                q.v = -q.v;
            break;
        }
        const float density = sides.pInf / (gasR * sides.T0);
        q.rho = density;
        if (horizontal) {
            q.u = (i < 0) ? speed : -speed;
            q.v = 0.0f;
            q.w = 0.0f;
        } else {
            q.v = (j < 0) ? speed : -speed;
            q.u = 0.0f;
            q.w = 0.0f;
        }
        if (sides.mach >= 1.0f)
            q.p = sides.pInf;
        break;
    }
    case BoundaryKind::Outlet:
    default:
        const float gamma = gammaOf(gas, q.y);
        const float speedOfSound = sqrtf(gamma * q.p / q.rho);
        const float normal = horizontal ? fabsf(q.u) : fabsf(q.v);
        if (normal < speedOfSound)
            q.p = sides.pInf;
        break;
    }

    writeState(block, target, q);
}

CFD_HD inline void mirrorSpan(Block& block,
                const GasModel& gas,
                const SideState& side,
                int i,
                int j,
                int k,
                int mirrorI,
                int mirrorJ,
                int mirrorK,
                const BlockBoundaries& sides) {
    if (side.interior)
        return;
    const int target = block.index(i, j, k);
    const int source = block.index(mirrorI, mirrorJ, mirrorK);
    Primitive q = primitiveOf(block, gas, source);

    switch (side.kind) {
    case BoundaryKind::Wall:
    case BoundaryKind::MovingWall:
        q.w = -q.w;
        q.u = side.noSlip ? 2.0f * side.speed - q.u : q.u;
        q.v = side.noSlip ? -q.v : q.v;
        break;
    case BoundaryKind::Slip:
        q.w = -q.w;
        break;
    case BoundaryKind::Inlet: {
        const float gamma = gammaOf(gas, q.y);
        const float gasR = gasConstantOf(gas, q.y);
        const float speedOfSound = sqrtf(gamma * gasR * sides.T0);
        const float speed = sides.mach * speedOfSound;
        const float open =
            side.banded ? ((sides.inletY >= side.from &&
                            sides.inletY <= side.to &&
                            sides.inletZ >= side.from2 &&
                            sides.inletZ <= side.to2) ? 1.0f : 0.0f)
                        : 1.0f;
        if (open == 0.0f) {
            q.w = -q.w;
            break;
        }
        const float density = sides.pInf / (gasR * sides.T0);
        q.rho = density;
        q.w = (k < 0) ? speed : -speed;
        q.u = 0.0f;
        q.v = 0.0f;
        if (sides.mach >= 1.0f)
            q.p = sides.pInf;
        break;
    }
    case BoundaryKind::Outlet:
    default:
        const float gamma = gammaOf(gas, q.y);
        const float speedOfSound = sqrtf(gamma * q.p / q.rho);
        const float normal = fabsf(q.w);
        if (normal < speedOfSound)
            q.p = sides.pInf;
        break;
    }

    writeState(block, target, q);
}


CFD_HD inline int flatCell(const Block& block, int i, int j, int k) {
    return (k * block.ny + j) * block.nx + i;
}

CFD_HD inline bool touchesFluid(const Block& block, int i, int j, int k) {
    const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                               {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
    for (int m = 0; m < 6; ++m) {
        const int ni = i + offsets[m][0];
        const int nj = j + offsets[m][1];
        const int nk = k + offsets[m][2];
        if (ni < 0 || ni >= block.nx || nj < 0 || nj >= block.ny ||
            nk < 0 || nk >= block.nz)
            continue;
        if (!block.solid[flatCell(block, ni, nj, nk)])
            return true;
    }
    return false;
}

CFD_HD inline void solidCell(Block& block,
                             const GasModel& gas,
                             int i,
                             int j,
                             int k,
                             int layer) {
    if (!block.solid || !block.solid[flatCell(block, i, j, k)])
        return;
    const bool touching = touchesFluid(block, i, j, k);
    if (layer == 0 ? !touching : touching)
        return;

    float sumRho = 0.0f, sumU = 0.0f, sumV = 0.0f, sumW = 0.0f, sumP = 0.0f,
          sumY = 0.0f;
    int count = 0;
    int normalX = 0, normalY = 0, normalZ = 0;
    const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                               {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
    for (int m = 0; m < 6; ++m) {
        const int ni = i + offsets[m][0];
        const int nj = j + offsets[m][1];
        const int nk = k + offsets[m][2];
        if (ni < 0 || ni >= block.nx || nj < 0 || nj >= block.ny ||
            nk < 0 || nk >= block.nz)
            continue;
        const bool wanted = layer == 0
                                ? !block.solid[flatCell(block, ni, nj, nk)]
                                : (block.solid[flatCell(block, ni, nj, nk)] &&
                                   touchesFluid(block, ni, nj, nk));
        if (!wanted)
            continue;
        const Primitive q = primitiveOf(block, gas, block.index(ni, nj, nk));
        sumRho += q.rho;
        sumU += q.u;
        sumV += q.v;
        sumW += q.w;
        sumP += q.p;
        sumY += q.y;
        normalX += offsets[m][0];
        normalY += offsets[m][1];
        normalZ += offsets[m][2];
        ++count;
    }
    if (count == 0)
        return;

    const float inv = 1.0f / static_cast<float>(count);
    Primitive q;
    q.rho = sumRho * inv;
    q.u = sumU * inv;
    q.v = sumV * inv;
    q.w = sumW * inv;
    q.p = sumP * inv;
    q.y = sumY * inv;

    const int here = flatCell(block, i, j, k);
    const float wallU = block.solidU ? block.solidU[here] : 0.0f;
    const float wallV = block.solidV ? block.solidV[here] : 0.0f;
    const float wallW = block.solidW ? block.solidW[here] : 0.0f;

    if (layer == 0) {
        const float length =
            sqrtf(static_cast<float>(normalX * normalX + normalY * normalY +
                                     normalZ * normalZ));
        if (length > 0.0f) {
            const float nxDir = normalX / length;
            const float nyDir = normalY / length;
            const float nzDir = normalZ / length;
            const float relU = q.u - wallU;
            const float relV = q.v - wallV;
            const float relW = q.w - wallW;
            float dot = relU * nxDir + relV * nyDir;
            dot += relW * nzDir;
            q.u = wallU + relU - 2.0f * dot * nxDir;
            q.v = wallV + relV - 2.0f * dot * nyDir;
            q.w = wallW + relW - 2.0f * dot * nzDir;
        } else {
            q.u = 2.0f * wallU - q.u;
            q.v = 2.0f * wallV - q.v;
            q.w = 2.0f * wallW - q.w;
        }
    } else {
        q.u = 2.0f * wallU - q.u;
        q.v = 2.0f * wallV - q.v;
        q.w = 2.0f * wallW - q.w;
    }
    q.gamma = gammaOf(gas, q.y);
    writeState(block, block.index(i, j, k), q);
}

CFD_HD inline bool blocksFlow(const SideState& side) {
    return !side.interior && (side.kind == BoundaryKind::Wall ||
                              side.kind == BoundaryKind::MovingWall ||
                              side.kind == BoundaryKind::Slip);
}

CFD_HD inline void faceFluxX(const Block& in,
                             const PrimitiveField& prim,
                             const GasModel& gas,
                             const BlockBoundaries& sides,
                             int limiter,
                             int i,
                             int j,
                             int k,
                             float* face) {
    const int nx = in.nx;
    const int id = in.index(i, j, k);
    const Primitive backLeft = prim.at(id - 2);
    const Primitive centreLeft = prim.at(id - 1);
    const Primitive centreRight = prim.at(id);
    const Primitive forwardRight = prim.at(id + 1);

    const Primitive left = reconstruct(centreLeft, backLeft, centreRight,
                                       1.0f, limiter, gas,
                                       spacingX(in, i - 1));
    const Primitive right = reconstruct(centreRight, centreLeft, forwardRight,
                                        -1.0f, limiter, gas, spacingX(in, i));

    const bool maskLeft =
        in.solid && i > 0 && in.solid[flatCell(in, i - 1, j, k)];
    const bool maskRight =
        in.solid && i < nx && in.solid[flatCell(in, i, j, k)];
    const bool solidLeft = maskLeft || (i == 0 && blocksFlow(sides.left));
    const bool solidRight = maskRight || (i == nx && blocksFlow(sides.right));
    if (solidLeft || solidRight) {
        float wallPressure = 0.0f;
        if (solidLeft != solidRight) {
            const Primitive& side = solidLeft ? right : left;
            float wall = 0.0f;
            if (in.solidU) {
                if (maskLeft)
                    wall = in.solidU[flatCell(in, i - 1, j, k)];
                else if (maskRight)
                    wall = in.solidU[flatCell(in, i, j, k)];
            }
            const float approach =
                solidLeft ? wall - side.u : side.u - wall;
            const float speedOfSound = sqrtf(side.gamma * side.p / side.rho);
            wallPressure = side.p + side.rho * speedOfSound * approach;
            if (!(wallPressure > kFloor))
                wallPressure = kFloor;
        }
        face[0] = 0.0f;
        face[1] = wallPressure;
        face[2] = 0.0f;
        face[3] = 0.0f;
        face[4] = 0.0f;
        face[5] = 0.0f;
        return;
    }
    hllc(left, right, 0, face);
}

CFD_HD inline void faceFluxY(const Block& in,
                             const PrimitiveField& prim,
                             const GasModel& gas,
                             const BlockBoundaries& sides,
                             int limiter,
                             int i,
                             int j,
                             int k,
                             float* face) {
    const int ny = in.ny;
    const int id = in.index(i, j, k);
    const int step = in.stride;
    const Primitive backLow = prim.at(id - 2 * step);
    const Primitive centreLow = prim.at(id - step);
    const Primitive centreHigh = prim.at(id);
    const Primitive forwardHigh = prim.at(id + step);

    const Primitive low = reconstruct(centreLow, backLow, centreHigh, 1.0f,
                                      limiter, gas, spacingY(in, j - 1));
    const Primitive high = reconstruct(centreHigh, centreLow, forwardHigh,
                                       -1.0f, limiter, gas, spacingY(in, j));

    const bool maskLow =
        in.solid && j > 0 && in.solid[flatCell(in, i, j - 1, k)];
    const bool maskHigh =
        in.solid && j < ny && in.solid[flatCell(in, i, j, k)];
    const bool solidLow = maskLow || (j == 0 && blocksFlow(sides.bottom));
    const bool solidHigh = maskHigh || (j == ny && blocksFlow(sides.top));
    if (solidLow || solidHigh) {
        float wallPressure = 0.0f;
        if (solidLow != solidHigh) {
            const Primitive& side = solidLow ? high : low;
            float wall = 0.0f;
            if (in.solidV) {
                if (maskLow)
                    wall = in.solidV[flatCell(in, i, j - 1, k)];
                else if (maskHigh)
                    wall = in.solidV[flatCell(in, i, j, k)];
            }
            const float approach =
                solidLow ? wall - side.v : side.v - wall;
            const float speedOfSound = sqrtf(side.gamma * side.p / side.rho);
            wallPressure = side.p + side.rho * speedOfSound * approach;
            if (!(wallPressure > kFloor))
                wallPressure = kFloor;
        }
        face[0] = 0.0f;
        face[1] = 0.0f;
        face[2] = wallPressure;
        face[3] = 0.0f;
        face[4] = 0.0f;
        face[5] = 0.0f;
        return;
    }
    hllc(low, high, 1, face);
}

CFD_HD inline void faceFluxZ(const Block& in,
                             const PrimitiveField& prim,
                             const GasModel& gas,
                             const BlockBoundaries& sides,
                             int limiter,
                             int i,
                             int j,
                             int k,
                             float* face) {
    const int nz = in.nz;
    const int id = in.index(i, j, k);
    const int step = in.plane();
    const Primitive backLow = prim.at(id - 2 * step);
    const Primitive centreLow = prim.at(id - step);
    const Primitive centreHigh = prim.at(id);
    const Primitive forwardHigh = prim.at(id + step);

    const Primitive low = reconstruct(centreLow, backLow, centreHigh, 1.0f,
                                      limiter, gas, spacingZ(in, k - 1));
    const Primitive high = reconstruct(centreHigh, centreLow, forwardHigh,
                                       -1.0f, limiter, gas, spacingZ(in, k));

    const bool maskLow =
        in.solid && k > 0 && in.solid[flatCell(in, i, j, k - 1)];
    const bool maskHigh =
        in.solid && k < nz && in.solid[flatCell(in, i, j, k)];
    const bool solidLow = maskLow || (k == 0 && blocksFlow(sides.front));
    const bool solidHigh = maskHigh || (k == nz && blocksFlow(sides.back));
    if (solidLow || solidHigh) {
        float wallPressure = 0.0f;
        if (solidLow != solidHigh) {
            const Primitive& side = solidLow ? high : low;
            float wall = 0.0f;
            if (in.solidW) {
                if (maskLow)
                    wall = in.solidW[flatCell(in, i, j, k - 1)];
                else if (maskHigh)
                    wall = in.solidW[flatCell(in, i, j, k)];
            }
            const float approach =
                solidLow ? wall - side.w : side.w - wall;
            const float speedOfSound = sqrtf(side.gamma * side.p / side.rho);
            wallPressure = side.p + side.rho * speedOfSound * approach;
            if (!(wallPressure > kFloor))
                wallPressure = kFloor;
        }
        face[0] = 0.0f;
        face[1] = 0.0f;
        face[2] = 0.0f;
        face[3] = wallPressure;
        face[4] = 0.0f;
        face[5] = 0.0f;
        return;
    }
    hllc(low, high, 2, face);
}

CFD_HD inline void combine(const Block& in,
                           const Block& keep,
                           Block& out,
                           const GasModel& gas,
                           const float* fx,
                           const float* fy,
                           const float* fz,
                           int i,
                           int j,
                           int k,
                           float dt,
                           float a,
                           float b,
                           float diffusivity) {
    const int nx = in.nx;
    const int ny = in.ny;
    const int id = in.index(i, j, k);
    const bool spans = in.spans();
    if (in.solid && in.solid[flatCell(in, i, j, k)]) {
        out.rho[id] = in.rho[id];
        out.rhou[id] = in.rhou[id];
        out.rhov[id] = in.rhov[id];
        if (out.rhow)
            out.rhow[id] = in.rhow[id];
        out.rhoE[id] = in.rhoE[id];
        if (out.rhoY)
            out.rhoY[id] = in.rhoY[id];
        return;
    }

    const float invDx = 1.0f / in.widthAt(i);
    const float invDy = 1.0f / in.heightAt(j);
    const float invDz = 1.0f / in.depthAt(k);
    const long long xLow =
        ((static_cast<long long>(k) * ny + j) * (nx + 1) + i) * kComponents;
    const long long xHigh = xLow + kComponents;
    const long long yLow =
        ((static_cast<long long>(k) * (ny + 1) + j) * nx + i) * kComponents;
    const long long yHigh =
        ((static_cast<long long>(k) * (ny + 1) + j + 1) * nx + i) * kComponents;
    const long long zLow =
        ((static_cast<long long>(k) * ny + j) * nx + i) * kComponents;
    const long long zHigh =
        ((static_cast<long long>(k + 1) * ny + j) * nx + i) * kComponents;

    const bool carries = in.rhoY != nullptr;
    const bool moves = in.rhow != nullptr;
    const float current[kComponents] = {in.rho[id], in.rhou[id], in.rhov[id],
                                        moves ? in.rhow[id] : 0.0f,
                                        in.rhoE[id],
                                        carries ? in.rhoY[id] : 0.0f};
    const float kept[kComponents] = {keep.rho[id], keep.rhou[id],
                                     keep.rhov[id],
                                     moves ? keep.rhow[id] : 0.0f,
                                     keep.rhoE[id],
                                     carries ? keep.rhoY[id] : 0.0f};

    float updated[kComponents];
    for (int c = 0; c < kComponents; ++c) {
        float divergence = (fx[xHigh + c] - fx[xLow + c]) * invDx +
                           (fy[yHigh + c] - fy[yLow + c]) * invDy;
        if (spans)
            divergence += (fz[zHigh + c] - fz[zLow + c]) * invDz;
        updated[c] = current[c] - dt * divergence;
    }

    if (carries && diffusivity > 0.0f) {
        const float here = in.rhoY[id] / fmaxf(in.rho[id], kFloor);
        const float east = in.rhoY[id + 1] / fmaxf(in.rho[id + 1], kFloor);
        const float west = in.rhoY[id - 1] / fmaxf(in.rho[id - 1], kFloor);
        const float north =
            in.rhoY[id + in.stride] / fmaxf(in.rho[id + in.stride], kFloor);
        const float south =
            in.rhoY[id - in.stride] / fmaxf(in.rho[id - in.stride], kFloor);
        float laplacian;
        if (in.stretched()) {
            const Spacing across = spacingX(in, i);
            const Spacing along = spacingY(in, j);
            laplacian = ((east - here) / across.forward -
                         (here - west) / across.back) *
                            invDx +
                        ((north - here) / along.forward -
                         (here - south) / along.back) *
                            invDy;
        } else {
            laplacian = (east - 2.0f * here + west) * invDx * invDx +
                        (north - 2.0f * here + south) * invDy * invDy;
        }
        if (spans) {
            const int step = in.plane();
            const float back =
                in.rhoY[id - step] / fmaxf(in.rho[id - step], kFloor);
            const float front =
                in.rhoY[id + step] / fmaxf(in.rho[id + step], kFloor);
            if (in.stretched()) {
                const Spacing through = spacingZ(in, k);
                laplacian += ((front - here) / through.forward -
                              (here - back) / through.back) *
                             invDz;
            } else {
                laplacian +=
                    (front - 2.0f * here + back) * invDz * invDz;
            }
        }
        updated[5] += dt * diffusivity * in.rho[id] * laplacian;
    }

    out.rho[id] = fmaxf(a * kept[0] + b * updated[0], kFloor);
    out.rhou[id] = a * kept[1] + b * updated[1];
    out.rhov[id] = a * kept[2] + b * updated[2];
    if (out.rhow)
        out.rhow[id] = a * kept[3] + b * updated[3];
    out.rhoE[id] = a * kept[4] + b * updated[4];
    if (out.rhoY)
        out.rhoY[id] = clampTo(a * kept[5] + b * updated[5], 0.0f, out.rho[id]);

    const float density = out.rho[id];
    const float spanwise = out.rhow ? out.rhow[id] : 0.0f;
    float kinetic =
        0.5f * (out.rhou[id] * out.rhou[id] + out.rhov[id] * out.rhov[id]) /
        density;
    kinetic += 0.5f * spanwise * spanwise / density;
    const float internal = out.rhoE[id] - kinetic;
    const float y = out.rhoY ? out.rhoY[id] / density : 0.0f;
    const float floorEnergy = kFloor / (gammaOf(gas, y) - 1.0f);
    if (internal < floorEnergy)
        out.rhoE[id] = kinetic + floorEnergy;
}

CFD_HD inline float cellRate(const Block& block,
                             const GasModel& gas,
                             int i,
                             int j,
                             int k) {
    if (block.solid && block.solid[flatCell(block, i, j, k)])
        return 0.0f;
    const Primitive q = primitiveOf(block, gas, block.index(i, j, k));
    const float speedOfSound = sqrtf(q.gamma * q.p / q.rho);
    const float rate = (fabsf(q.u) + speedOfSound) / block.widthAt(i) +
                       (fabsf(q.v) + speedOfSound) / block.heightAt(j);
    if (!block.spans())
        return rate;
    return rate + (fabsf(q.w) + speedOfSound) / block.depthAt(k);
}

CFD_HD inline void fillPrimitive(const Block& block,
                                 const GasModel& gas,
                                 int id,
                                 float* rho,
                                 float* u,
                                 float* v,
                                 float* w,
                                 float* p,
                                 float* y,
                                 float* gamma) {
    const float density = fmaxf(block.rho[id], kFloor);
    const float inv = 1.0f / density;
    const float velocityX = block.rhou[id] * inv;
    const float velocityY = block.rhov[id] * inv;
    const float velocityZ = block.rhow ? block.rhow[id] * inv : 0.0f;
    const float fraction =
        block.rhoY ? clampTo(block.rhoY[id] * inv, 0.0f, 1.0f) : 0.0f;
    const float ratio = gammaOf(gas, fraction);
    const float energy = block.rhoE[id] * inv;
    float kinetic =
        0.5f * (velocityX * velocityX + velocityY * velocityY);
    kinetic += 0.5f * velocityZ * velocityZ;
    rho[id] = density;
    u[id] = velocityX;
    v[id] = velocityY;
    w[id] = velocityZ;
    y[id] = fraction;
    gamma[id] = ratio;
    p[id] = fmaxf((ratio - 1.0f) * density * (energy - kinetic), kFloor);
}

}
