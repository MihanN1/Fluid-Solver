#include "ResultView.hpp"

#include "ParameterInfo.hpp"

#include <algorithm>

namespace maskui {

void SliceCache::setSource(std::shared_ptr<const VtkFrame> frame) {
    source_ = std::move(frame);
    view_.reset();
    refresh();
}

void SliceCache::setPlane(SliceAxis axis, std::size_t index) {
    std::size_t planes = 0;
    if (source_) {
        planes = axis == SliceAxis::X
            ? source_->nx
            : (axis == SliceAxis::Y ? source_->ny : source_->nz);
    }
    const std::size_t wanted =
        planes == 0 ? 0 : std::min(index, planes - 1u);
    if (axis == axis_ && wanted == index_ && view_) {
        return;
    }
    axis_ = axis;
    index_ = wanted;
    refresh();
}

const std::shared_ptr<const VtkFrame>& SliceCache::source() const {
    return source_;
}

const std::shared_ptr<const VtkFrame>& SliceCache::view() const {
    return view_;
}

std::size_t SliceCache::planeCount() const {
    if (!source_) {
        return 0;
    }
    return axis_ == SliceAxis::X
        ? source_->nx
        : (axis_ == SliceAxis::Y ? source_->ny : source_->nz);
}

bool SliceCache::slicing() const {
    return source_ && source_->volumetric();
}

void SliceCache::refresh() {
    if (!source_) {
        view_.reset();
        index_ = 0;
        return;
    }
    if (!source_->volumetric()) {
        view_ = source_;
        index_ = 0;
        return;
    }
    const std::size_t planes = planeCount();
    index_ = planes == 0 ? 0 : std::min(index_, planes - 1u);
    view_ = std::make_shared<const VtkFrame>(
        extractSlice(*source_, axis_, index_));
    ++extractions_;
}

PickSelection selectionForPick(const Viewport3D::Pick& pick) {
    PickSelection selection;
    if (!pick.hit) {
        return selection;
    }
    if (pick.solidHit) {
        selection.target = PickTarget::Body;
        selection.body = pick.objectId >= 1 ? pick.objectId : 1;
        return selection;
    }
    if (pick.face >= 0 && pick.face < 6) {
        selection.target = PickTarget::Boundary;
        selection.side = pick.face;
    }
    return selection;
}

std::size_t boundaryKindRow(int side) {
    static const std::size_t rows[6] = {
        BcLeft, BcRight, BcBottom, BcTop, BcFront, BcBack};
    return side >= 0 && side < 6 ? rows[side] : ParameterCount;
}

std::size_t boundarySpeedRow(int side) {
    static const std::size_t rows[6] = {
        BcLeftSpeed, BcRightSpeed, BcBottomSpeed, BcTopSpeed,
        BcFrontSpeed, BcBackSpeed};
    return side >= 0 && side < 6 ? rows[side] : ParameterCount;
}

} // namespace maskui
