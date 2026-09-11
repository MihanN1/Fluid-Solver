#pragma once

#include "Viewport3D.hpp"
#include "VtkFrame.hpp"

#include <cstddef>
#include <memory>

namespace maskui {

class SliceCache {
public:
    void setSource(std::shared_ptr<const VtkFrame> frame);
    void setPlane(SliceAxis axis, std::size_t index);

    const std::shared_ptr<const VtkFrame>& source() const;
    const std::shared_ptr<const VtkFrame>& view() const;

    SliceAxis axis() const { return axis_; }
    std::size_t index() const { return index_; }
    std::size_t planeCount() const;
    bool slicing() const;
    std::size_t extractions() const { return extractions_; }

private:
    void refresh();

    std::shared_ptr<const VtkFrame> source_;
    std::shared_ptr<const VtkFrame> view_;
    SliceAxis axis_ = SliceAxis::Z;
    std::size_t index_ = 0;
    std::size_t extractions_ = 0;
};

enum class PickTarget {
    Nothing,
    Body,
    Boundary
};

struct PickSelection {
    PickTarget target = PickTarget::Nothing;
    int body = 0;
    int side = -1;
};

PickSelection selectionForPick(const Viewport3D::Pick& pick);

std::size_t boundaryKindRow(int side);
std::size_t boundarySpeedRow(int side);

} // namespace maskui
