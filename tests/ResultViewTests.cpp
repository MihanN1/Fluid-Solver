#include "ParameterInfo.hpp"
#include "ResultView.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

namespace {

int fail(const std::string& message) {
    std::cout << message << "\n";
    return 1;
}

std::shared_ptr<maskui::VtkFrame> buildVolume(
    std::size_t nx,
    std::size_t ny,
    std::size_t nz) {
    auto frame = std::make_shared<maskui::VtkFrame>();
    frame->association = maskui::VtkDataAssociation::Cell;
    frame->nx = nx;
    frame->ny = ny;
    frame->nz = nz;
    frame->spacingX = 1.0 / static_cast<double>(nx);
    frame->spacingY = 1.0 / static_cast<double>(ny);
    frame->spacingZ = 1.0 / static_cast<double>(nz);
    const std::size_t count = nx * ny * nz;
    frame->pressure.assign(count, 0.0f);
    frame->solid.assign(count, 0u);
    frame->velocity.assign(count, maskui::Velocity{});
    frame->velocityMagnitude.assign(count, 0.0f);
    frame->pressureFinite.assign(count, 1u);
    frame->velocityFinite.assign(count, 1u);
    for (std::size_t k = 0; k < nz; ++k)
        for (std::size_t j = 0; j < ny; ++j)
            for (std::size_t i = 0; i < nx; ++i) {
                const std::size_t index = frame->cellIndex(i, j, k);
                frame->pressure[index] = static_cast<float>(
                    i + 1000u * j + 1000000u * k);
                frame->velocity[index] = {
                    static_cast<float>(i),
                    static_cast<float>(j),
                    static_cast<float>(k)};
                frame->velocityMagnitude[index] = static_cast<float>(
                    std::sqrt(static_cast<double>(i * i + j * j + k * k)));
            }
    return frame;
}

} // namespace

int main() {
    using namespace maskui;

    {
        SliceCache cache;
        auto flat = buildVolume(8, 6, 1);
        cache.setSource(flat);
        if (cache.view() != flat)
            return fail("a flat frame is not shown as itself, so the 2D view "
                        "would be looking at a copy");
        if (cache.slicing())
            return fail("a flat frame reports that it is being sliced");
        cache.setPlane(SliceAxis::X, 4);
        if (cache.extractions() != 0)
            return fail("a flat frame was sliced, which is work nobody asked "
                        "for");
        if (cache.view() != flat)
            return fail("a flat frame stopped being shown as itself after the "
                        "plane was set");
    }

    {
        SliceCache cache;
        auto volume = buildVolume(8, 6, 4);
        cache.setSource(volume);
        if (!cache.slicing())
            return fail("a volume is not being sliced");
        const std::size_t built = cache.extractions();
        if (built != 1)
            return fail("setting the source did not build exactly one slice");
        const std::shared_ptr<const VtkFrame> first = cache.view();
        if (first->nx != 8 || first->ny != 6 || first->nz != 1)
            return fail("the Z slice of an 8x6x4 volume is not 8x6x1");

        cache.setPlane(SliceAxis::Z, cache.index());
        if (cache.extractions() != built || cache.view() != first)
            return fail("asking for the plane that is already shown rebuilt "
                        "it, which is the cost the cache exists to avoid");

        cache.setPlane(SliceAxis::Z, 2);
        if (cache.extractions() != built + 1)
            return fail("moving the plane did not rebuild the slice");
        if (cache.index() != 2)
            return fail("the plane did not move where it was asked to");

        cache.setPlane(SliceAxis::X, 2);
        if (cache.view()->nx != 6 || cache.view()->ny != 4)
            return fail("the X slice of an 8x6x4 volume is not 6x4: the "
                        "remaining axes are y and z");
        cache.setPlane(SliceAxis::Y, 0);
        if (cache.view()->nx != 8 || cache.view()->ny != 4)
            return fail("the Y slice of an 8x6x4 volume is not 8x4");

        cache.setPlane(SliceAxis::Y, 500);
        if (cache.index() != 5)
            return fail("a plane past the end was not clamped to the last one");

        const std::size_t beforeSource = cache.extractions();
        cache.setSource(buildVolume(8, 6, 4));
        if (cache.extractions() != beforeSource + 1)
            return fail("a new frame did not rebuild the slice exactly once");
        if (cache.axis() != SliceAxis::Y || cache.index() != 5)
            return fail("the new frame did not keep the plane the user was "
                        "looking at");
    }

    {
        const std::shared_ptr<const VtkFrame> volume = buildVolume(4, 4, 4);
        SliceCache cache;
        cache.setSource(volume);
        cache.setPlane(SliceAxis::Z, 1);
        const VtkFrame& slice = *cache.view();
        for (std::size_t j = 0; j < 4; ++j)
            for (std::size_t i = 0; i < 4; ++i) {
                const float wanted =
                    volume->pressure[volume->cellIndex(i, j, 1)];
                if (slice.pressure[slice.cellIndex(i, j)] != wanted)
                    return fail("the Z slice does not carry the plane it "
                                "names");
            }
        cache.setPlane(SliceAxis::X, 2);
        const VtkFrame& across = *cache.view();
        if (across.velocity[across.cellIndex(1, 3)].x !=
                volume->velocity[volume->cellIndex(2, 1, 3)].y ||
            across.velocity[across.cellIndex(1, 3)].y !=
                volume->velocity[volume->cellIndex(2, 1, 3)].z)
            return fail("the X slice did not permute the velocity into the "
                        "plane it is drawn in");
    }

    {
        Viewport3D::Pick miss;
        if (selectionForPick(miss).target != PickTarget::Nothing)
            return fail("a ray that hit nothing selected something");

        Viewport3D::Pick body;
        body.hit = true;
        body.solidHit = true;
        body.objectId = 3;
        const PickSelection bodyPick = selectionForPick(body);
        if (bodyPick.target != PickTarget::Body || bodyPick.body != 3)
            return fail("clicking a solid cell did not select its body");

        Viewport3D::Pick unnumbered;
        unnumbered.hit = true;
        unnumbered.solidHit = true;
        if (selectionForPick(unnumbered).body != 1)
            return fail("a solid cell with no objectId did not fall back to "
                        "body 1, which is what a single-body mask is");

        static const std::size_t kindRows[6] = {
            BcLeft, BcRight, BcBottom, BcTop, BcFront, BcBack};
        static const std::size_t speedRows[6] = {
            BcLeftSpeed, BcRightSpeed, BcBottomSpeed, BcTopSpeed,
            BcFrontSpeed, BcBackSpeed};
        for (int face = 0; face < 6; ++face) {
            Viewport3D::Pick side;
            side.hit = true;
            side.face = face;
            const PickSelection pick = selectionForPick(side);
            if (pick.target != PickTarget::Boundary || pick.side != face)
                return fail("face " + std::to_string(face) +
                            " did not select a boundary");
            if (boundaryKindRow(pick.side) != kindRows[face])
                return fail("face " + std::to_string(face) +
                            " selected the wrong boundary row");
            if (boundarySpeedRow(pick.side) != speedRows[face])
                return fail("face " + std::to_string(face) +
                            " selected the wrong boundary speed row");
        }
        if (boundaryKindRow(6) != ParameterCount ||
            boundarySpeedRow(-1) != ParameterCount)
            return fail("a face that does not exist named a row anyway");
    }

    {
        const std::shared_ptr<const VtkFrame> volume = buildVolume(128, 128, 128);
        SliceCache cache;
        const auto started = std::chrono::steady_clock::now();
        cache.setSource(volume);
        cache.setPlane(SliceAxis::Z, 40);
        const auto built = std::chrono::steady_clock::now();
        for (std::size_t repeat = 0; repeat < 64; ++repeat)
            cache.setPlane(SliceAxis::Z, 40);
        const auto cached = std::chrono::steady_clock::now();
        for (std::size_t plane = 0; plane < 64; ++plane)
            cache.setPlane(SliceAxis::Z, plane);
        const auto swept = std::chrono::steady_clock::now();
        const auto micros = [](auto from, auto to) {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       to - from).count();
        };
        std::cout << "128^3 slice: source and first plane "
                  << micros(started, built)
                  << " us, 64 repeats of the same plane "
                  << micros(built, cached) << " us, 64 different planes "
                  << micros(cached, swept) << " us\n";
        if (cache.extractions() != 2u + 64u)
            return fail("the repeated plane was rebuilt after all");
        if (micros(built, cached) > micros(cached, swept))
            return fail("holding the plane still costs as much as moving it, "
                        "so the cache is not doing its job");
    }

    std::cout << "ResultViewTests OK\n";
    return 0;
}
