#pragma once
#include "Config.hpp"

#include <cstdint>
#include <string>
#include <vector>

class PhaseField {
public:
    void initialise(const Config& cfg,
                    const std::vector<int>& solid,
                    float dx,
                    float dy,
                    float dz,
                    std::string& warning);

    void advect(const std::vector<float>& u,
                const std::vector<float>& v,
                const std::vector<float>& w,
                const std::vector<uint8_t>& solid,
                float dt,
                float dx,
                float dy,
                float dz);

    void computeCurvature(const std::vector<uint8_t>& solid,
                          float dx,
                          float dy,
                          float dz,
                          float contactAngleDegrees);

    const std::vector<float>& curvature() const { return kappa; }
    const std::vector<float>& gradientMagnitude() const { return gradMag; }

    void refreshProperties(const std::vector<uint8_t>& solid);

    float maxCourant(const std::vector<float>& u,
                     const std::vector<float>& v,
                     const std::vector<float>& w,
                     float dx,
                     float dy,
                     float dz) const;

    double totalVolume(float cellVolume) const;

    bool active() const { return nx > 0; }

    const std::vector<float>& fraction() const { return c; }
    std::vector<float>& fraction() { return c; }
    const std::vector<float>& density() const { return rho; }
    const std::vector<float>& viscosity() const { return mu; }

    const std::vector<float>& faceInvRhoX() const { return invRhoX; }
    const std::vector<float>& faceInvRhoY() const { return invRhoY; }
    const std::vector<float>& faceInvRhoZ() const { return invRhoZ; }

    void resize(int nxIn, int nyIn, int nzIn);
    void setFluids(float rho1In, float rho2In, float mu1In, float mu2In);
    void setScheme(VofScheme scheme) { vof = scheme; }

    void setMixing(MixingKind kind, LimiterKind limiterKind, float diffusivity) {
        mixing = kind;
        limiter = limiterKind;
        diffusion = diffusivity;
    }

private:
    int nx = 0, ny = 0, nz = 1;
    float rho1 = 1000.0f, rho2 = 1.225f;
    float mu1 = 1e-3f, mu2 = 1.8e-5f;
    VofScheme vof = VofScheme::Hric;
    MixingKind mixing = MixingKind::Immiscible;
    LimiterKind limiter = LimiterKind::VanLeer;
    float diffusion = 0.0f;

    std::vector<float> c;
    std::vector<float> kappa;
    std::vector<float> gradMag;
    std::vector<float> normalX, normalY, normalZ;
    std::vector<float> fluxX, fluxY, fluxZ;

    std::vector<float> previous;

    std::vector<float> inflowLeft, inflowRight, inflowBottom, inflowTop;
    std::vector<float> inflowFront, inflowBack;
    std::vector<float> rho, mu;

    std::vector<float> invRhoCell;
    std::vector<float> nuFaceX, nuFaceY, nuFaceZ;
    std::vector<float> invRhoX, invRhoY, invRhoZ;

    void buildNormals(const std::vector<uint8_t>& solid, float dx, float dy,
                      float dz, float contactAngleDegrees);

    template <int Scheme>
    void advectImpl(const std::vector<float>& u,
                    const std::vector<float>& v,
                    const std::vector<float>& w,
                    const std::vector<uint8_t>& solid,
                    float dt,
                    float dx,
                    float dy,
                    float dz);
};

bool loadPhaseFile(const std::string& path,
                   int nx,
                   int ny,
                   int nz,
                   std::vector<float>& out,
                   std::string& error);
