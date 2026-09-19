#pragma once
#include "SolverCompressible.hpp"

#include <cstdint>
#include <vector>

struct AmrBox {
    int i0 = 0;
    int j0 = 0;
    int k0 = 0;
    int nx = 0;
    int ny = 0;
    int nz = 1;

    int i1() const { return i0 + nx; }
    int j1() const { return j0 + ny; }
    int k1() const { return k0 + nz; }
    bool empty() const { return nx <= 0 || ny <= 0 || nz <= 0; }
    long long volume() const {
        return static_cast<long long>(nx) * ny * nz;
    }
    bool holds(int i, int j, int k = 0) const {
        return i >= i0 && i < i1() && j >= j0 && j < j1() && k >= k0 &&
               k < k1();
    }
};

AmrBox amrIntersect(const AmrBox& a, const AmrBox& b);
AmrBox amrGrow(const AmrBox& box, int by, int limitNx, int limitNy,
               int limitNz);
AmrBox amrRefine(const AmrBox& box, int ratio, bool spans);
AmrBox amrCoarsen(const AmrBox& box, int ratio, bool spans);

std::vector<AmrBox> amrCluster(const std::vector<uint8_t>& tags,
                               int nx,
                               int ny,
                               int nz,
                               int minSide,
                               int maxSide,
                               double fillTarget);

struct AmrPatch {
    AmrBox box;
    int parent = -1;
    int ghost = 2;
    int stride = 0;
    int rows = 0;
    int layers = 0;
    bool species = false;

    std::vector<float> sets[4][6];
    std::vector<uint8_t> solid;
    std::vector<float> solidU;
    std::vector<float> solidV;
    std::vector<float> solidW;
    Workspace work;

    void allocate(const AmrBox& region, int ghostWidth, bool carriesSpecies);
    Block view(int set, float dx, float dy, float dz);
    Block view(int set, float dx, float dy, float dz) const;
};

struct AmrLevel {
    std::vector<AmrPatch> patches;
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
    int ratio = 1;
    int nx = 0;
    int ny = 0;
    int nz = 1;
};

enum class AmrCriterion {
    Density,
    Vorticity,
    Species,
    Body,
    Everything
};

bool parseAmrCriterion(const std::string& text, AmrCriterion& out);

struct AmrSettings {
    int levels = 0;
    int regridEvery = 8;
    float threshold = 0.2f;
    int buffer = 2;
    int minSide = 8;
    int maxSide = 64;
    float fillTarget = 0.7f;
    AmrCriterion criterion = AmrCriterion::Everything;
};

class AmrHierarchy {
public:
    void build(const AmrSettings& settings,
               int baseNx,
               int baseNy,
               int baseNz,
               float baseDx,
               float baseDy,
               float baseDz,
               bool species);

    bool active() const { return !levels_.empty(); }
    int depth() const { return static_cast<int>(levels_.size()); }
    bool spans() const { return spans_; }
    AmrLevel& level(int which) { return levels_[which]; }
    const AmrLevel& level(int which) const { return levels_[which]; }

    void tagFrom(const Block& base,
                 const GasModel& gas,
                 const AmrSettings& settings,
                 std::vector<uint8_t>& tags) const;

    void regrid(const Block& base,
                const GasModel& gas,
                const AmrSettings& settings,
                const std::vector<uint8_t>& baseSolid);

    void seedLevel(int which, const Block& base);
    void carryOver(int which, const std::vector<AmrPatch>& previous);

    void fillGhostsFor(int which,
                       int parentIndex,
                       const Block& coarse,
                       const AmrBox& coarseBox);

    void averageDownFor(int which,
                        int parentIndex,
                        Block& coarse,
                        const AmrBox& coarseBox) const;

    Block coarseViewFor(int which, const Block& base, int patchIndex,
                        AmrBox& coarseBox);

    long long cellCount() const;
    void describe(std::vector<int>& patchesPerLevel,
                  std::vector<long long>& cellsPerLevel) const;

    void setSolidFromPoint(int which,
                           const std::vector<uint8_t>& baseSolid,
                           int baseNx,
                           int baseNy,
                           int baseNz);

private:
    std::vector<AmrLevel> levels_;
    bool species_ = false;
    bool spans_ = false;

    void interpolateInto(AmrPatch& patch,
                         int which,
                         const Block& coarse,
                         const AmrBox& coarseBox,
                         bool interiorToo);
};

void amrCopyOverlap(const Block& from, Block& to, const AmrBox& fromBox,
                    const AmrBox& toBox, bool species);
