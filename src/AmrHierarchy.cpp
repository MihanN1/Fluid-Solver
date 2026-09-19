#include "AmrHierarchy.hpp"
#include "CompressibleKernels.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using namespace cfd;

std::string lowerCase(std::string text) {
    for (char& character : text)
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    return text;
}

float minmod(float back, float forward) {
    if (back * forward <= 0.0f)
        return 0.0f;
    return std::fabs(back) < std::fabs(forward) ? back : forward;
}

}

AmrBox amrIntersect(const AmrBox& a, const AmrBox& b) {
    AmrBox out;
    out.i0 = std::max(a.i0, b.i0);
    out.j0 = std::max(a.j0, b.j0);
    out.k0 = std::max(a.k0, b.k0);
    out.nx = std::min(a.i1(), b.i1()) - out.i0;
    out.ny = std::min(a.j1(), b.j1()) - out.j0;
    out.nz = std::min(a.k1(), b.k1()) - out.k0;
    if (out.nx < 0)
        out.nx = 0;
    if (out.ny < 0)
        out.ny = 0;
    if (out.nz < 0)
        out.nz = 0;
    return out;
}

AmrBox amrGrow(const AmrBox& box, int by, int limitNx, int limitNy,
               int limitNz) {
    AmrBox out;
    out.i0 = std::max(0, box.i0 - by);
    out.j0 = std::max(0, box.j0 - by);
    out.k0 = std::max(0, box.k0 - by);
    out.nx = std::min(limitNx, box.i1() + by) - out.i0;
    out.ny = std::min(limitNy, box.j1() + by) - out.j0;
    out.nz = std::min(limitNz, box.k1() + by) - out.k0;
    return out;
}

AmrBox amrRefine(const AmrBox& box, int ratio, bool spans) {
    AmrBox out;
    out.i0 = box.i0 * ratio;
    out.j0 = box.j0 * ratio;
    out.nx = box.nx * ratio;
    out.ny = box.ny * ratio;
    if (spans) {
        out.k0 = box.k0 * ratio;
        out.nz = box.nz * ratio;
    }
    return out;
}

AmrBox amrCoarsen(const AmrBox& box, int ratio, bool spans) {
    AmrBox out;
    out.i0 = box.i0 / ratio;
    out.j0 = box.j0 / ratio;
    out.nx = (box.i1() + ratio - 1) / ratio - out.i0;
    out.ny = (box.j1() + ratio - 1) / ratio - out.j0;
    if (spans) {
        out.k0 = box.k0 / ratio;
        out.nz = (box.k1() + ratio - 1) / ratio - out.k0;
    }
    return out;
}

namespace {

inline std::size_t tagAt(int nx, int ny, int i, int j, int k) {
    return (static_cast<std::size_t>(k) * ny + j) * nx + i;
}

long long countTags(const std::vector<uint8_t>& tags, int nx, int ny,
                    const AmrBox& box) {
    long long total = 0;
    for (int k = box.k0; k < box.k1(); ++k)
        for (int j = box.j0; j < box.j1(); ++j)
            for (int i = box.i0; i < box.i1(); ++i)
                total += tags[tagAt(nx, ny, i, j, k)] ? 1 : 0;
    return total;
}

AmrBox shrinkToTags(const std::vector<uint8_t>& tags, int nx, int ny,
                    const AmrBox& box) {
    int lowI = box.i1(), highI = box.i0 - 1;
    int lowJ = box.j1(), highJ = box.j0 - 1;
    int lowK = box.k1(), highK = box.k0 - 1;
    for (int k = box.k0; k < box.k1(); ++k)
        for (int j = box.j0; j < box.j1(); ++j)
            for (int i = box.i0; i < box.i1(); ++i) {
                if (!tags[tagAt(nx, ny, i, j, k)])
                    continue;
                lowI = std::min(lowI, i);
                highI = std::max(highI, i);
                lowJ = std::min(lowJ, j);
                highJ = std::max(highJ, j);
                lowK = std::min(lowK, k);
                highK = std::max(highK, k);
            }
    AmrBox out;
    if (highI < lowI || highJ < lowJ || highK < lowK)
        return out;
    out.i0 = lowI;
    out.j0 = lowJ;
    out.k0 = lowK;
    out.nx = highI - lowI + 1;
    out.ny = highJ - lowJ + 1;
    out.nz = highK - lowK + 1;
    return out;
}

void clusterInto(const std::vector<uint8_t>& tags,
                 int nx,
                 int ny,
                 const AmrBox& region,
                 int minSide,
                 int maxSide,
                 double fillTarget,
                 int depth,
                 std::vector<AmrBox>& out) {
    const AmrBox box = shrinkToTags(tags, nx, ny, region);
    if (box.empty())
        return;

    const long long tagged = countTags(tags, nx, ny, box);
    if (tagged <= 0)
        return;

    const double fill = static_cast<double>(tagged) /
                        static_cast<double>(box.volume());
    const bool small = box.nx <= minSide && box.ny <= minSide &&
                       box.nz <= minSide;
    const bool fits = box.nx <= maxSide && box.ny <= maxSide &&
                      box.nz <= maxSide;

    if ((fill >= fillTarget && fits) || small || depth >= 12) {
        if (fits) {
            out.push_back(box);
            return;
        }
    }

    const bool splitX = box.nx >= box.ny && box.nx >= box.nz;
    const bool splitY = !splitX && box.ny >= box.nz;
    const int span = splitX ? box.nx : (splitY ? box.ny : box.nz);
    if (span < 2 * minSide && fits) {
        out.push_back(box);
        return;
    }

    std::vector<long long> signature(static_cast<std::size_t>(span), 0);
    for (int k = box.k0; k < box.k1(); ++k)
        for (int j = box.j0; j < box.j1(); ++j)
            for (int i = box.i0; i < box.i1(); ++i) {
                if (!tags[tagAt(nx, ny, i, j, k)])
                    continue;
                const int slot =
                    splitX ? i - box.i0 : (splitY ? j - box.j0 : k - box.k0);
                ++signature[static_cast<std::size_t>(slot)];
            }

    int cut = -1;
    for (int at = minSide; at <= span - minSide; ++at)
        if (signature[static_cast<std::size_t>(at)] == 0) {
            cut = at;
            break;
        }

    if (cut < 0) {
        long long best = -1;
        for (int at = minSide; at <= span - minSide; ++at) {
            const long long left =
                signature[static_cast<std::size_t>(at)] -
                signature[static_cast<std::size_t>(at - 1)];
            const long long right =
                signature[static_cast<std::size_t>(at)] -
                (at + 1 < span ? signature[static_cast<std::size_t>(at + 1)]
                               : 0);
            const long long turn = std::llabs(left - right);
            if (turn > best) {
                best = turn;
                cut = at;
            }
        }
    }

    if (cut < minSide || cut > span - minSide) {
        if (fits) {
            out.push_back(box);
            return;
        }
        cut = span / 2;
        if (cut < 1)
            return;
    }

    AmrBox low = box;
    AmrBox high = box;
    if (splitX) {
        low.nx = cut;
        high.i0 = box.i0 + cut;
        high.nx = box.nx - cut;
    } else if (splitY) {
        low.ny = cut;
        high.j0 = box.j0 + cut;
        high.ny = box.ny - cut;
    } else {
        low.nz = cut;
        high.k0 = box.k0 + cut;
        high.nz = box.nz - cut;
    }
    clusterInto(tags, nx, ny, low, minSide, maxSide, fillTarget, depth + 1,
                out);
    clusterInto(tags, nx, ny, high, minSide, maxSide, fillTarget, depth + 1,
                out);
}

}

std::vector<AmrBox> amrCluster(const std::vector<uint8_t>& tags,
                               int nx,
                               int ny,
                               int nz,
                               int minSide,
                               int maxSide,
                               double fillTarget) {
    std::vector<AmrBox> out;
    AmrBox whole;
    whole.i0 = 0;
    whole.j0 = 0;
    whole.k0 = 0;
    whole.nx = nx;
    whole.ny = ny;
    whole.nz = nz;
    clusterInto(tags, nx, ny, whole, std::max(1, minSide),
                std::max(minSide, maxSide), fillTarget, 0, out);
    return out;
}

bool parseAmrCriterion(const std::string& text, AmrCriterion& out) {
    const std::string name = lowerCase(text);
    if (name == "density" || name == "shock") {
        out = AmrCriterion::Density;
        return true;
    }
    if (name == "vorticity" || name == "wake") {
        out = AmrCriterion::Vorticity;
        return true;
    }
    if (name == "species" || name == "mixing") {
        out = AmrCriterion::Species;
        return true;
    }
    if (name == "body" || name == "wall") {
        out = AmrCriterion::Body;
        return true;
    }
    if (name == "everything" || name == "all" || name.empty()) {
        out = AmrCriterion::Everything;
        return true;
    }
    return false;
}

void AmrPatch::allocate(const AmrBox& region, int ghostWidth,
                        bool carriesSpecies) {
    box = region;
    ghost = ghostWidth;
    species = carriesSpecies;
    stride = box.nx + 2 * ghost;
    rows = box.ny + 2 * ghost;
    layers = box.nz + 2 * (box.nz > 1 ? ghost : 0);
    const std::size_t total = static_cast<std::size_t>(stride) *
                              static_cast<std::size_t>(rows) *
                              static_cast<std::size_t>(layers);
    for (int set = 0; set < 4; ++set)
        for (int component = 0; component < 6; ++component) {
            if (component == 5 && !species) {
                sets[set][component].clear();
                continue;
            }
            sets[set][component].assign(total, 0.0f);
        }
    const std::size_t cells = static_cast<std::size_t>(box.nx) *
                              static_cast<std::size_t>(box.ny) *
                              static_cast<std::size_t>(box.nz);
    solid.assign(cells, 0);
    solidU.assign(cells, 0.0f);
    solidV.assign(cells, 0.0f);
    solidW.assign(cells, 0.0f);
}

Block AmrPatch::view(int set, float dx, float dy, float dz) {
    Block block;
    block.nx = box.nx;
    block.ny = box.ny;
    block.nz = box.nz;
    block.ghost = ghost;
    block.stride = stride;
    block.rows = rows;
    block.dx = dx;
    block.dy = dy;
    block.dz = dz;
    block.x0 = box.i0 * dx;
    block.y0 = box.j0 * dy;
    block.z0 = box.k0 * dz;
    block.rho = sets[set][0].data();
    block.rhou = sets[set][1].data();
    block.rhov = sets[set][2].data();
    block.rhow = sets[set][3].data();
    block.rhoE = sets[set][4].data();
    block.rhoY = species ? sets[set][5].data() : nullptr;
    block.solid = solid.data();
    block.solidU = solidU.data();
    block.solidV = solidV.data();
    block.solidW = solidW.data();
    return block;
}

Block AmrPatch::view(int set, float dx, float dy, float dz) const {
    return const_cast<AmrPatch*>(this)->view(set, dx, dy, dz);
}

void AmrHierarchy::build(const AmrSettings& settings,
                         int baseNx,
                         int baseNy,
                         int baseNz,
                         float baseDx,
                         float baseDy,
                         float baseDz,
                         bool species) {
    levels_.clear();
    species_ = species;
    spans_ = baseNz > 1;
    if (settings.levels <= 0)
        return;

    levels_.resize(static_cast<std::size_t>(settings.levels));
    int ratio = 1;
    for (int which = 0; which < settings.levels; ++which) {
        ratio *= 2;
        AmrLevel& here = levels_[static_cast<std::size_t>(which)];
        here.ratio = ratio;
        here.dx = baseDx / ratio;
        here.dy = baseDy / ratio;
        here.dz = spans_ ? baseDz / ratio : baseDz;
        here.nx = baseNx * ratio;
        here.ny = baseNy * ratio;
        here.nz = spans_ ? baseNz * ratio : 1;
    }
}

void AmrHierarchy::tagFrom(const Block& base,
                           const GasModel& gas,
                           const AmrSettings& settings,
                           std::vector<uint8_t>& tags) const {
    const int nx = base.nx;
    const int ny = base.ny;
    const int nz = base.nz;
    const bool spans = base.spans();
    tags.assign(static_cast<std::size_t>(nx) * ny * nz, 0);
    if (nx < 3 || ny < 3)
        return;

    std::vector<float> score(static_cast<std::size_t>(nx) * ny * nz, 0.0f);
    const bool wantDensity = settings.criterion == AmrCriterion::Density ||
                             settings.criterion == AmrCriterion::Everything;
    const bool wantVorticity = settings.criterion == AmrCriterion::Vorticity ||
                               settings.criterion == AmrCriterion::Everything;
    const bool wantSpecies = (settings.criterion == AmrCriterion::Species ||
                              settings.criterion == AmrCriterion::Everything) &&
                             base.rhoY != nullptr;
    const bool wantBody = settings.criterion == AmrCriterion::Body ||
                          settings.criterion == AmrCriterion::Everything;

    const int lowK = spans ? 1 : 0;
    const int highK = spans ? nz - 1 : 1;

    float worst = 0.0f;
    for (int k = lowK; k < highK; ++k)
        for (int j = 1; j < ny - 1; ++j)
            for (int i = 1; i < nx - 1; ++i) {
                const std::size_t flat = tagAt(nx, ny, i, j, k);
                if (base.solid && base.solid[flat])
                    continue;

                const Primitive here =
                    primitiveOf(base, gas, base.index(i, j, k));
                const Primitive east =
                    primitiveOf(base, gas, base.index(i + 1, j, k));
                const Primitive west =
                    primitiveOf(base, gas, base.index(i - 1, j, k));
                const Primitive north =
                    primitiveOf(base, gas, base.index(i, j + 1, k));
                const Primitive south =
                    primitiveOf(base, gas, base.index(i, j - 1, k));

                float value = 0.0f;
                if (wantDensity) {
                    float jump = std::fabs(east.rho - west.rho) +
                                 std::fabs(north.rho - south.rho);
                    if (spans)
                        jump += std::fabs(
                            primitiveOf(base, gas, base.index(i, j, k + 1))
                                .rho -
                            primitiveOf(base, gas, base.index(i, j, k - 1))
                                .rho);
                    value = std::max(value, jump / std::max(here.rho, kFloor));
                }
                if (wantVorticity) {
                    if (spans) {
                        const Primitive back =
                            primitiveOf(base, gas, base.index(i, j, k + 1));
                        const Primitive front =
                            primitiveOf(base, gas, base.index(i, j, k - 1));
                        const float curlX =
                            (north.w - south.w) / (2.0f * base.heightAt(j)) -
                            (back.v - front.v) / (2.0f * base.depthAt(k));
                        const float curlY =
                            (back.u - front.u) / (2.0f * base.depthAt(k)) -
                            (east.w - west.w) / (2.0f * base.widthAt(i));
                        const float curlZ =
                            (east.v - west.v) / (2.0f * base.widthAt(i)) -
                            (north.u - south.u) / (2.0f * base.heightAt(j));
                        const float curl = std::sqrt(curlX * curlX +
                                                     curlY * curlY +
                                                     curlZ * curlZ);
                        const float scale =
                            std::max(1.0f, std::fabs(here.u) +
                                               std::fabs(here.v) +
                                               std::fabs(here.w));
                        value = std::max(value, curl * base.widthAt(i) / scale);
                    } else {
                        const float curl =
                            (north.u - south.u) / (2.0f * base.heightAt(j)) -
                            (east.v - west.v) / (2.0f * base.widthAt(i));
                        const float scale =
                            std::max(1.0f,
                                     std::fabs(here.u) + std::fabs(here.v));
                        value = std::max(
                            value, std::fabs(curl) * base.widthAt(i) / scale);
                    }
                }
                if (wantSpecies) {
                    float jump = std::fabs(east.y - west.y) +
                                 std::fabs(north.y - south.y);
                    if (spans)
                        jump += std::fabs(
                            primitiveOf(base, gas, base.index(i, j, k + 1)).y -
                            primitiveOf(base, gas, base.index(i, j, k - 1)).y);
                    value = std::max(value, jump);
                }
                score[flat] = value;
                worst = std::max(worst, value);
            }

    const float cut = settings.threshold * worst;
    for (std::size_t flat = 0; flat < score.size(); ++flat)
        if (worst > 0.0f && score[flat] >= cut && score[flat] > 0.0f)
            tags[flat] = 1;

    if (wantBody && base.solid) {
        const int reachK = spans ? 1 : 0;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    if (!base.solid[tagAt(nx, ny, i, j, k)])
                        continue;
                    for (int dk = -reachK; dk <= reachK; ++dk)
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di) {
                                const int ni = i + di;
                                const int nj = j + dj;
                                const int nk = k + dk;
                                if (ni < 0 || ni >= nx || nj < 0 || nj >= ny ||
                                    nk < 0 || nk >= nz)
                                    continue;
                                const std::size_t at = tagAt(nx, ny, ni, nj, nk);
                                if (!base.solid[at])
                                    tags[at] = 1;
                            }
                }
    }

    if (settings.buffer > 0) {
        std::vector<uint8_t> grown = tags;
        const int reach = settings.buffer;
        const int reachK = spans ? reach : 0;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    if (!tags[tagAt(nx, ny, i, j, k)])
                        continue;
                    for (int dk = -reachK; dk <= reachK; ++dk)
                        for (int dj = -reach; dj <= reach; ++dj)
                            for (int di = -reach; di <= reach; ++di) {
                                const int ni = i + di;
                                const int nj = j + dj;
                                const int nk = k + dk;
                                if (ni < 0 || ni >= nx || nj < 0 || nj >= ny ||
                                    nk < 0 || nk >= nz)
                                    continue;
                                grown[tagAt(nx, ny, ni, nj, nk)] = 1;
                            }
                }
        tags.swap(grown);
    }
}

void AmrHierarchy::regrid(const Block& base,
                          const GasModel& gas,
                          const AmrSettings& settings,
                          const std::vector<uint8_t>& baseSolid) {
    if (levels_.empty())
        return;

    std::vector<uint8_t> tags;
    tagFrom(base, gas, settings, tags);

    struct Seed {
        AmrBox box;
        int parent;
    };
    std::vector<Seed> seeds;
    for (const AmrBox& found :
         amrCluster(tags, base.nx, base.ny, base.nz, settings.minSide,
                    std::max(settings.minSide, settings.maxSide / 2),
                    settings.fillTarget))
        seeds.push_back({found, -1});

    std::vector<std::vector<AmrPatch>> previous;
    previous.reserve(levels_.size());
    for (AmrLevel& here : levels_)
        previous.push_back(std::move(here.patches));

    for (int which = 0; which < depth(); ++which) {
        AmrLevel& here = levels_[static_cast<std::size_t>(which)];
        std::vector<AmrPatch> built;
        built.reserve(seeds.size());

        for (const Seed& seed : seeds) {
            AmrBox fine = amrRefine(seed.box, 2, spans_);
            fine.i0 = std::max(0, fine.i0);
            fine.j0 = std::max(0, fine.j0);
            fine.nx = std::min(here.nx, fine.i1()) - fine.i0;
            fine.ny = std::min(here.ny, fine.j1()) - fine.j0;
            fine.nx -= fine.nx % 2;
            fine.ny -= fine.ny % 2;
            if (spans_) {
                fine.k0 = std::max(0, fine.k0);
                fine.nz = std::min(here.nz, fine.k1()) - fine.k0;
                fine.nz -= fine.nz % 2;
                if (fine.nz < 4)
                    continue;
            }
            if (fine.nx < 4 || fine.ny < 4)
                continue;
            AmrPatch patch;
            patch.allocate(fine, base.ghost, species_);
            patch.parent = seed.parent;
            built.push_back(std::move(patch));
        }

        here.patches = std::move(built);
        if (here.patches.empty()) {
            for (int deeper = which; deeper < depth(); ++deeper)
                levels_[static_cast<std::size_t>(deeper)].patches.clear();
            return;
        }

        setSolidFromPoint(which, baseSolid, base.nx, base.ny, base.nz);
        seedLevel(which, base);
        carryOver(which, previous[static_cast<std::size_t>(which)]);

        if (which + 1 >= depth())
            break;

        std::vector<Seed> next;
        const int trim = std::max(2, settings.buffer);
        for (std::size_t index = 0; index < here.patches.size(); ++index) {
            AmrBox shrunk = here.patches[index].box;
            shrunk.i0 += trim;
            shrunk.j0 += trim;
            shrunk.nx -= 2 * trim;
            shrunk.ny -= 2 * trim;
            shrunk.nx -= shrunk.nx % 2;
            shrunk.ny -= shrunk.ny % 2;
            if (spans_) {
                shrunk.k0 += trim;
                shrunk.nz -= 2 * trim;
                shrunk.nz -= shrunk.nz % 2;
                if (shrunk.nz < 4)
                    continue;
            }
            if (shrunk.nx >= 4 && shrunk.ny >= 4)
                next.push_back({shrunk, static_cast<int>(index)});
        }
        seeds = std::move(next);
        if (seeds.empty()) {
            for (int deeper = which + 1; deeper < depth(); ++deeper)
                levels_[static_cast<std::size_t>(deeper)].patches.clear();
            return;
        }
    }
}

void AmrHierarchy::setSolidFromPoint(int which,
                                     const std::vector<uint8_t>& baseSolid,
                                     int baseNx,
                                     int baseNy,
                                     int baseNz) {
    AmrLevel& here = levels_[static_cast<std::size_t>(which)];
    const int ratio = here.ratio;
    for (AmrPatch& patch : here.patches) {
        for (int k = 0; k < patch.box.nz; ++k)
            for (int j = 0; j < patch.box.ny; ++j)
                for (int i = 0; i < patch.box.nx; ++i) {
                    const int globalI = patch.box.i0 + i;
                    const int globalJ = patch.box.j0 + j;
                    const int globalK = patch.box.k0 + k;
                    const int coarseI = std::min(baseNx - 1, globalI / ratio);
                    const int coarseJ = std::min(baseNy - 1, globalJ / ratio);
                    const int coarseK =
                        spans_ ? std::min(baseNz - 1, globalK / ratio) : 0;
                    patch.solid[tagAt(patch.box.nx, patch.box.ny, i, j, k)] =
                        baseSolid[tagAt(baseNx, baseNy, coarseI, coarseJ,
                                        coarseK)];
                }
    }
}

Block AmrHierarchy::coarseViewFor(int which, const Block& base,
                                  int patchIndex, AmrBox& coarseBox) {
    if (which == 0) {
        coarseBox.i0 = 0;
        coarseBox.j0 = 0;
        coarseBox.k0 = 0;
        coarseBox.nx = base.nx;
        coarseBox.ny = base.ny;
        coarseBox.nz = base.nz;
        return base;
    }
    AmrLevel& above = levels_[static_cast<std::size_t>(which - 1)];
    AmrPatch& parent = above.patches[static_cast<std::size_t>(patchIndex)];
    coarseBox = parent.box;
    return parent.view(0, above.dx, above.dy, above.dz);
}

void AmrHierarchy::interpolateInto(AmrPatch& patch,
                                   int which,
                                   const Block& coarse,
                                   const AmrBox& coarseBox,
                                   bool interiorToo) {
    AmrLevel& here = levels_[static_cast<std::size_t>(which)];
    Block fine = patch.view(0, here.dx, here.dy, here.dz);
    const int ghost = patch.ghost;
    const int ghostZ = fine.ghostZ();
    const int plane = coarse.plane();

    const float* const source[6] = {coarse.rho,  coarse.rhou, coarse.rhov,
                                    coarse.rhow, coarse.rhoE, coarse.rhoY};
    float* const target[6] = {fine.rho,  fine.rhou, fine.rhov,
                              fine.rhow, fine.rhoE, fine.rhoY};
    const int components = species_ ? 6 : 5;

    for (int k = -ghostZ; k < patch.box.nz + ghostZ; ++k)
        for (int j = -ghost; j < patch.box.ny + ghost; ++j)
            for (int i = -ghost; i < patch.box.nx + ghost; ++i) {
                const bool interior = i >= 0 && i < patch.box.nx && j >= 0 &&
                                      j < patch.box.ny && k >= 0 &&
                                      k < patch.box.nz;
                if (interior && !interiorToo)
                    continue;

                const int globalI = patch.box.i0 + i;
                const int globalJ = patch.box.j0 + j;
                const int globalK = patch.box.k0 + k;
                const int coarseGlobalI =
                    globalI >= 0 ? globalI / 2 : -((-globalI + 1) / 2);
                const int coarseGlobalJ =
                    globalJ >= 0 ? globalJ / 2 : -((-globalJ + 1) / 2);
                const int coarseGlobalK =
                    spans_ ? (globalK >= 0 ? globalK / 2
                                           : -((-globalK + 1) / 2))
                           : globalK;

                const int localI = std::clamp(coarseGlobalI - coarseBox.i0,
                                              -coarse.ghost + 1,
                                              coarse.nx + coarse.ghost - 2);
                const int localJ = std::clamp(coarseGlobalJ - coarseBox.j0,
                                              -coarse.ghost + 1,
                                              coarse.ny + coarse.ghost - 2);
                const int localK =
                    spans_ ? std::clamp(coarseGlobalK - coarseBox.k0,
                                        -coarse.ghostZ() + 1,
                                        coarse.nz + coarse.ghostZ() - 2)
                           : 0;

                const int centre = coarse.index(localI, localJ, localK);
                const float offsetX =
                    (globalI - 2 * coarseGlobalI) == 0 ? -0.25f : 0.25f;
                const float offsetY =
                    (globalJ - 2 * coarseGlobalJ) == 0 ? -0.25f : 0.25f;
                const float offsetZ =
                    (globalK - 2 * coarseGlobalK) == 0 ? -0.25f : 0.25f;

                const int at = fine.index(i, j, k);
                for (int component = 0; component < components; ++component) {
                    const float* from = source[component];
                    if (!from || !target[component])
                        continue;
                    const float middle = from[centre];
                    const float slopeX = minmod(middle - from[centre - 1],
                                                from[centre + 1] - middle);
                    const float slopeY =
                        minmod(middle - from[centre - coarse.stride],
                               from[centre + coarse.stride] - middle);
                    if (spans_) {
                        const float slopeZ =
                            minmod(middle - from[centre - plane],
                                   from[centre + plane] - middle);
                        target[component][at] = middle + slopeX * offsetX +
                                                slopeY * offsetY +
                                                slopeZ * offsetZ;
                    } else {
                        target[component][at] =
                            middle + slopeX * offsetX + slopeY * offsetY;
                    }
                }
                if (target[0][at] < kFloor)
                    target[0][at] = kFloor;
            }
}

void AmrHierarchy::seedLevel(int which, const Block& base) {
    AmrLevel& here = levels_[static_cast<std::size_t>(which)];
    for (std::size_t index = 0; index < here.patches.size(); ++index) {
        AmrPatch& patch = here.patches[index];
        AmrBox coarseBox;
        const Block coarse =
            coarseViewFor(which, base, patch.parent, coarseBox);
        interpolateInto(patch, which, coarse, coarseBox, true);
        for (int set = 1; set < 4; ++set)
            for (int component = 0; component < 6; ++component)
                if (!patch.sets[0][component].empty())
                    patch.sets[set][component] = patch.sets[0][component];
    }
}

void AmrHierarchy::carryOver(int which,
                             const std::vector<AmrPatch>& previous) {
    if (previous.empty())
        return;
    AmrLevel& here = levels_[static_cast<std::size_t>(which)];
    for (AmrPatch& into : here.patches) {
        Block destination = into.view(0, here.dx, here.dy, here.dz);
        for (const AmrPatch& from : previous) {
            Block origin = const_cast<AmrPatch&>(from).view(0, here.dx,
                                                            here.dy, here.dz);
            amrCopyOverlap(origin, destination, from.box, into.box, species_);
        }
        for (int set = 1; set < 4; ++set)
            for (int component = 0; component < 6; ++component)
                if (!into.sets[0][component].empty())
                    into.sets[set][component] = into.sets[0][component];
    }
}

void AmrHierarchy::fillGhostsFor(int which,
                                 int parentIndex,
                                 const Block& coarse,
                                 const AmrBox& coarseBox) {
    AmrLevel& here = levels_[static_cast<std::size_t>(which)];
    for (AmrPatch& patch : here.patches) {
        if (patch.parent != parentIndex)
            continue;
        interpolateInto(patch, which, coarse, coarseBox, false);
    }

    for (std::size_t target = 0; target < here.patches.size(); ++target) {
        AmrPatch& into = here.patches[target];
        if (into.parent != parentIndex)
            continue;
        Block destination = into.view(0, here.dx, here.dy, here.dz);
        for (std::size_t source = 0; source < here.patches.size(); ++source) {
            if (source == target)
                continue;
            const AmrPatch& from = here.patches[source];
            if (from.parent != parentIndex)
                continue;
            Block origin = from.view(0, here.dx, here.dy, here.dz);
            amrCopyOverlap(origin, destination, from.box, into.box, species_);
        }
    }
}

void amrCopyOverlap(const Block& from, Block& to, const AmrBox& fromBox,
                    const AmrBox& toBox, bool species) {
    AmrBox reach = toBox;
    reach.i0 -= to.ghost;
    reach.j0 -= to.ghost;
    reach.nx += 2 * to.ghost;
    reach.ny += 2 * to.ghost;
    reach.k0 -= to.ghostZ();
    reach.nz += 2 * to.ghostZ();
    const AmrBox shared = amrIntersect(reach, fromBox);
    if (shared.empty())
        return;

    for (int k = shared.k0; k < shared.k1(); ++k)
        for (int j = shared.j0; j < shared.j1(); ++j)
            for (int i = shared.i0; i < shared.i1(); ++i) {
                const int toIndex = to.index(i - toBox.i0, j - toBox.j0,
                                             k - toBox.k0);
                const int fromIndex = from.index(i - fromBox.i0,
                                                 j - fromBox.j0,
                                                 k - fromBox.k0);
                to.rho[toIndex] = from.rho[fromIndex];
                to.rhou[toIndex] = from.rhou[fromIndex];
                to.rhov[toIndex] = from.rhov[fromIndex];
                if (to.rhow && from.rhow)
                    to.rhow[toIndex] = from.rhow[fromIndex];
                to.rhoE[toIndex] = from.rhoE[fromIndex];
                if (species && to.rhoY && from.rhoY)
                    to.rhoY[toIndex] = from.rhoY[fromIndex];
            }
}

void AmrHierarchy::averageDownFor(int which,
                                  int parentIndex,
                                  Block& coarse,
                                  const AmrBox& coarseBox) const {
    const AmrLevel& here = levels_[static_cast<std::size_t>(which)];
    for (const AmrPatch& patch : here.patches) {
        if (patch.parent != parentIndex)
            continue;
        Block fine = patch.view(0, here.dx, here.dy, here.dz);
        const AmrBox target =
            amrIntersect(amrCoarsen(patch.box, 2, spans_), coarseBox);
        for (int k = target.k0; k < target.k1(); ++k)
            for (int j = target.j0; j < target.j1(); ++j)
                for (int i = target.i0; i < target.i1(); ++i) {
                    const int fineI = i * 2 - patch.box.i0;
                    const int fineJ = j * 2 - patch.box.j0;
                    const int fineK =
                        spans_ ? k * 2 - patch.box.k0 : k - patch.box.k0;
                    if (fineI < 0 || fineJ < 0 || fineI + 1 >= patch.box.nx ||
                        fineJ + 1 >= patch.box.ny)
                        continue;
                    if (spans_ && (fineK < 0 || fineK + 1 >= patch.box.nz))
                        continue;

                    const int localI = i - coarseBox.i0;
                    const int localJ = j - coarseBox.j0;
                    const int localK = k - coarseBox.k0;
                    if (localI < 0 || localJ < 0 || localI >= coarse.nx ||
                        localJ >= coarse.ny)
                        continue;
                    if (localK < 0 || localK >= coarse.nz)
                        continue;
                    if (coarse.solid &&
                        coarse.solid[tagAt(coarse.nx, coarse.ny, localI,
                                           localJ, localK)])
                        continue;

                    const int coarseAt = coarse.index(localI, localJ, localK);
                    const int a = fine.index(fineI, fineJ, fineK);
                    const int b = fine.index(fineI + 1, fineJ, fineK);
                    const int c = fine.index(fineI, fineJ + 1, fineK);
                    const int d = fine.index(fineI + 1, fineJ + 1, fineK);
                    if (spans_) {
                        const int e = fine.index(fineI, fineJ, fineK + 1);
                        const int f = fine.index(fineI + 1, fineJ, fineK + 1);
                        const int g = fine.index(fineI, fineJ + 1, fineK + 1);
                        const int h =
                            fine.index(fineI + 1, fineJ + 1, fineK + 1);
                        const auto mean = [&](const float* field) {
                            return 0.125f * (field[a] + field[b] + field[c] +
                                             field[d] + field[e] + field[f] +
                                             field[g] + field[h]);
                        };
                        coarse.rho[coarseAt] = mean(fine.rho);
                        coarse.rhou[coarseAt] = mean(fine.rhou);
                        coarse.rhov[coarseAt] = mean(fine.rhov);
                        if (coarse.rhow && fine.rhow)
                            coarse.rhow[coarseAt] = mean(fine.rhow);
                        coarse.rhoE[coarseAt] = mean(fine.rhoE);
                        if (coarse.rhoY && fine.rhoY)
                            coarse.rhoY[coarseAt] = mean(fine.rhoY);
                    } else {
                        const auto mean = [&](const float* field) {
                            return 0.25f *
                                   (field[a] + field[b] + field[c] + field[d]);
                        };
                        coarse.rho[coarseAt] = mean(fine.rho);
                        coarse.rhou[coarseAt] = mean(fine.rhou);
                        coarse.rhov[coarseAt] = mean(fine.rhov);
                        coarse.rhoE[coarseAt] = mean(fine.rhoE);
                        if (coarse.rhoY && fine.rhoY)
                            coarse.rhoY[coarseAt] = mean(fine.rhoY);
                    }
                }
    }
}

long long AmrHierarchy::cellCount() const {
    long long total = 0;
    for (const AmrLevel& here : levels_)
        for (const AmrPatch& patch : here.patches)
            total += patch.box.volume();
    return total;
}

void AmrHierarchy::describe(std::vector<int>& patchesPerLevel,
                            std::vector<long long>& cellsPerLevel) const {
    patchesPerLevel.clear();
    cellsPerLevel.clear();
    for (const AmrLevel& here : levels_) {
        patchesPerLevel.push_back(static_cast<int>(here.patches.size()));
        long long cells = 0;
        for (const AmrPatch& patch : here.patches)
            cells += patch.box.volume();
        cellsPerLevel.push_back(cells);
    }
}
