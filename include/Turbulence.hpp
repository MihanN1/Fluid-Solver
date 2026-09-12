#pragma once
#include "Config.hpp"

#include <cstdint>
#include <vector>

struct WallGhost {
    float sign = 1.0f;
    float offset = 0.0f;
};

class TurbulenceModel {
public:
    void initialise(const Config& cfg,
                    const std::vector<uint8_t>& solid,
                    int nx,
                    int ny,
                    int nz,
                    float dx,
                    float dy,
                    float dz,
                    float molecular);

    void setGhosts(const WallGhost& leftIn,
                   const WallGhost& rightIn,
                   const WallGhost& bottomIn,
                   const WallGhost& topIn,
                   const WallGhost& frontIn,
                   const WallGhost& backIn);

    void advance(const std::vector<float>& u,
                 const std::vector<float>& v,
                 const std::vector<float>& w,
                 const std::vector<uint8_t>& solid,
                 float dt);

    bool active() const { return kind != TurbulenceKind::None; }
    bool transported() const { return kind == TurbulenceKind::KOmegaSST; }

    const std::vector<float>& viscosity() const { return nuT; }
    const std::vector<float>& kinetic() const { return k; }
    const std::vector<float>& frequency() const { return omega; }
    const std::vector<float>& distance() const { return wallDist; }
    const std::vector<float>& strain() const { return strainMag; }

    float mixingCap() const;
    float peakViscosity() const;
    float sourceStepLimit() const;

    void resize(int nxIn, int nyIn, int nzIn);
    void setState(std::vector<float>&& kIn, std::vector<float>&& omegaIn);
    bool hasState() const { return !k.empty(); }

private:
    TurbulenceKind kind = TurbulenceKind::None;
    int nx = 0, ny = 0, nz = 1;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    float molecular = 0.0f;
    float cs = 0.17f;
    float inletK = 0.0f, inletOmega = 0.0f;
    WallGhost left, right, bottom, top, front, back;

    std::vector<float> nuT;
    std::vector<float> k, omega;
    std::vector<float> kNext, omegaNext;
    std::vector<float> wallDist;
    std::vector<float> strainMag;

    void buildWallDistance(const std::vector<uint8_t>& solid);
    void computeStrain(const std::vector<float>& u,
                       const std::vector<float>& v,
                       const std::vector<float>& w,
                       const std::vector<uint8_t>& solid);
    void smagorinsky(const std::vector<uint8_t>& solid);
    void kOmega(const std::vector<float>& u,
                const std::vector<float>& v,
                const std::vector<float>& w,
                const std::vector<uint8_t>& solid,
                float dt);
};
