#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace maskui {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Triangle3 {
    Vec3 v0;
    Vec3 v1;
    Vec3 v2;
};

struct GeometryBounds {
    Vec3 minimum;
    Vec3 maximum;
    Vec3 centre;
    double characteristicLength = 0.0;
    bool valid = false;
};

struct SectionFrame {
    Vec3 centre;
    Vec3 axisX;
    Vec3 axisY;
    Vec3 normal;
    double extent = 0.0;
    double tolerance = 0.0;
    bool valid = false;
};

struct SectionSegment {
    Vec3 first;
    Vec3 second;
};

struct MaskParameters {
    double Lx = 1.0;
    double Ly = 1.0;
    int nx = 50;
    int ny = 50;
    double sliceAngleX = 0.0;
    double sliceAngleY = 0.0;
    double sliceAngleZ = 0.0;
    double sliceRotation = 0.0;
    bool invertSection = false;
};

struct MaskResult {
    std::vector<int> cells;
    std::vector<std::vector<Vec2>> contours;
    int solidCellCount = 0;
    std::string error;
    bool success = false;
};

class GeometryProcessor {
public:
    bool load(const std::filesystem::path& filename, std::string& error);
    void clear();

    bool empty() const;
    const std::filesystem::path& sourcePath() const;
    const std::vector<Triangle3>& triangles() const;
    const GeometryBounds& bounds() const;

    SectionFrame sectionFrame(const MaskParameters& parameters) const;
    std::vector<SectionSegment> sectionSegments(
        const MaskParameters& parameters) const;
    MaskResult generateMask(const MaskParameters& parameters) const;
    MaskResult rasterizeContours(
        const MaskParameters& parameters,
        std::vector<std::vector<Vec2>> contours) const;

private:
    bool loadOBJ(const std::filesystem::path& filename, std::string& error);
    bool loadSTL(const std::filesystem::path& filename, std::string& error);
    void updateBounds();
    std::vector<SectionSegment> buildSectionSegments(
        const SectionFrame& frame) const;
    std::vector<std::vector<Vec2>> buildContours(
        const MaskParameters& parameters,
        const SectionFrame& frame,
        std::string& error) const;

    std::filesystem::path sourcePath_;
    std::vector<Triangle3> triangles_;
    GeometryBounds bounds_;
};

} // namespace maskui
