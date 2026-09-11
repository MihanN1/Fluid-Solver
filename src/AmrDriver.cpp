#include "AmrDriver.hpp"
#include "CompressibleKernels.hpp"

#include <algorithm>
#include <cmath>

namespace {

using namespace cfd;

}

AmrDriver::AmrDriver(AmrHierarchy& tree,
                     const GasModel& gas,
                     const BlockBoundaries& sides,
                     LimiterKind limiter,
                     float diffusivity)
    : tree_(tree),
      gas_(gas),
      sides_(sides),
      limiter_(limiter),
      diffusivity_(diffusivity) {}

BlockBoundaries AmrDriver::patchSides(int which,
                                      const AmrPatch& patch) const {
    const AmrLevel& here = tree_.level(which);
    BlockBoundaries out = sides_;
    out.left.interior = patch.box.i0 > 0;
    out.right.interior = patch.box.i1() < here.nx;
    out.bottom.interior = patch.box.j0 > 0;
    out.top.interior = patch.box.j1() < here.ny;
    out.front.interior = tree_.spans() && patch.box.k0 > 0;
    out.back.interior = tree_.spans() && patch.box.k1() < here.nz;
    out.spanI0 = patch.box.i0;
    out.spanJ0 = patch.box.j0;
    out.spanK0 = patch.box.k0;
    out.spanNx = here.nx;
    out.spanNy = here.ny;
    out.spanNz = here.nz;
    return out;
}

void AmrDriver::stageBlock(Block& current,
                           Block& stage1,
                           Block& stage2,
                           Workspace& work,
                           const BlockBoundaries& sides,
                           float dt) {
    const float coefficients[3][2] = {
        {0.0f, 1.0f}, {0.75f, 0.25f}, {1.0f / 3.0f, 2.0f / 3.0f}};
    Block* from[3] = {&current, &stage1, &stage2};
    Block* into[3] = {&stage1, &stage2, &current};

    for (int stage = 0; stage < 3; ++stage)
        advanceStage(*from[stage], current, *into[stage], sides, gas_, dt,
                     coefficients[stage][0], coefficients[stage][1], limiter_,
                     diffusivity_, work);
}

float AmrDriver::finestRate(const Block& base, float cfl) const {
    float best = blockTimeStep(base, gas_, cfl);
    for (int which = 0; which < tree_.depth(); ++which) {
        const AmrLevel& here = tree_.level(which);
        const float subcycles = static_cast<float>(1 << (which + 1));
        for (const AmrPatch& patch : here.patches) {
            Block block =
                const_cast<AmrPatch&>(patch).view(0, here.dx, here.dy,
                                                  here.dz);
            const float step = blockTimeStep(block, gas_, cfl) * subcycles;
            if (!(step > 0.0f))
                continue;
            best = best > 0.0f ? std::min(best, step) : step;
        }
    }
    return best;
}

void AmrDriver::advance(Block& base,
                        Block& baseStage1,
                        Block& baseStage2,
                        Workspace& baseWork,
                        float dt) {
    stageBlock(base, baseStage1, baseStage2, baseWork, sides_, dt);
    if (!tree_.active() || tree_.level(0).patches.empty())
        return;

    AmrBox whole;
    whole.nx = base.nx;
    whole.ny = base.ny;
    whole.nz = base.nz;
    advanceChildren(0, -1, base, whole, dt);
}

void AmrDriver::advanceChildren(int which,
                                int parentIndex,
                                Block& coarse,
                                const AmrBox& coarseBox,
                                float dtCoarse) {
    if (which >= tree_.depth())
        return;
    AmrLevel& here = tree_.level(which);

    std::vector<std::size_t> mine;
    for (std::size_t index = 0; index < here.patches.size(); ++index)
        if (here.patches[index].parent == parentIndex)
            mine.push_back(index);
    if (mine.empty())
        return;

    const float dtFine = 0.5f * dtCoarse;

    for (int substep = 0; substep < 2; ++substep) {
        tree_.fillGhostsFor(which, parentIndex, coarse, coarseBox);

        for (std::size_t index : mine) {
            AmrPatch& patch = here.patches[index];
            Block current = patch.view(0, here.dx, here.dy, here.dz);
            Block stage1 = patch.view(1, here.dx, here.dy, here.dz);
            Block stage2 = patch.view(2, here.dx, here.dy, here.dz);
            const BlockBoundaries local = patchSides(which, patch);
            stageBlock(current, stage1, stage2, patch.work, local, dtFine);

            Block block = patch.view(0, here.dx, here.dy, here.dz);
            advanceChildren(which + 1, static_cast<int>(index), block,
                            patch.box, dtFine);
        }
    }

    tree_.averageDownFor(which, parentIndex, coarse, coarseBox);
}
