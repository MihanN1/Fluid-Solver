#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace maskui {

struct DataRange {
    bool available = false;
    double minimum = 0.0;
    double maximum = 0.0;
};

struct Velocity {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class VtkDataAssociation {
    Point,
    Cell
};

struct VtkRestartMetadata {
    bool hasConfigText = false;
    bool restartCapable = false;
    std::optional<double> currentTime;
    std::optional<double> totalTime;
    std::optional<double> restartDt;
    std::optional<int> restartStep;
    std::unordered_map<std::string, std::string> config;
};

struct VtkFrame {
    std::filesystem::path sourcePath;
    std::string title;
    int frameNumber = -1;
    VtkDataAssociation association = VtkDataAssociation::Point;
    std::size_t nx = 0;
    std::size_t ny = 0;
    std::size_t nz = 1;
    double originX = 0.0;
    double originY = 0.0;
    double originZ = 0.0;
    double spacingX = 0.0;
    double spacingY = 0.0;
    double spacingZ = 0.0;
    std::vector<double> faceX;
    std::vector<double> faceY;
    std::vector<double> faceZ;

    bool rectilinear() const { return !faceX.empty() && !faceY.empty(); }
    bool volumetric() const { return nz > 1; }
    double cellLeft(std::size_t i) const {
        return rectilinear() ? faceX[i] : originX + i * spacingX;
    }
    double cellRight(std::size_t i) const {
        return rectilinear() ? faceX[i + 1] : originX + (i + 1) * spacingX;
    }
    double cellBottom(std::size_t j) const {
        return rectilinear() ? faceY[j] : originY + j * spacingY;
    }
    double cellTop(std::size_t j) const {
        return rectilinear() ? faceY[j + 1] : originY + (j + 1) * spacingY;
    }
    double cellFront(std::size_t k) const {
        return faceZ.size() > 1 ? faceZ[k] : originZ + k * spacingZ;
    }
    double cellBack(std::size_t k) const {
        return faceZ.size() > 1 ? faceZ[k + 1] : originZ + (k + 1) * spacingZ;
    }
    double cellCentreX(std::size_t i) const {
        return 0.5 * (cellLeft(i) + cellRight(i));
    }
    double cellCentreY(std::size_t j) const {
        return 0.5 * (cellBottom(j) + cellTop(j));
    }
    double cellCentreZ(std::size_t k) const {
        return 0.5 * (cellFront(k) + cellBack(k));
    }
    double spanX() const {
        return rectilinear() ? faceX.back() - faceX.front() : nx * spacingX;
    }
    double spanY() const {
        return rectilinear() ? faceY.back() - faceY.front() : ny * spacingY;
    }
    double spanZ() const {
        return faceZ.size() > 1 ? faceZ.back() - faceZ.front() : nz * spacingZ;
    }
    std::size_t columnAt(double x) const;
    std::size_t rowAt(double y) const;
    std::size_t planeAt(double z) const;
    std::vector<float> pressure;
    std::vector<std::uint8_t> solid;
    std::vector<Velocity> velocity;
    std::vector<float> velocityMagnitude;
    std::vector<std::uint8_t> pressureFinite;
    std::vector<std::uint8_t> velocityFinite;
    DataRange pressureRange;
    DataRange velocityXRange;
    DataRange velocityYRange;
    DataRange velocityMagnitudeRange;
    // The same two fields with the outermost half-percent at each end left
    // out. A colour scale stretched by a handful of cells maps everything else
    // onto three shades; these are what the view falls back on instead.
    DataRange pressureTrimmedRange;
    DataRange velocityMagnitudeTrimmedRange;

    std::vector<std::string> scalarNames;
    std::unordered_map<std::string, std::vector<float>> scalars;
    std::unordered_map<std::string, DataRange> scalarRanges;
    std::unordered_map<std::string, DataRange> scalarTrimmedRanges;

    VtkRestartMetadata restart;
    std::vector<std::string> warnings;

    std::size_t cellIndex(std::size_t i, std::size_t j) const;
    std::size_t cellIndex(std::size_t i, std::size_t j, std::size_t k) const;
    std::size_t decodedByteSize() const;
};

enum class SliceAxis {
    X,
    Y,
    Z
};

VtkFrame extractSlice(const VtkFrame& volume, SliceAxis axis, std::size_t index);

struct ResultImageTransform {
    double screenOriginX = 0.0;
    double screenOriginY = 0.0;
    double pixelWidth = 0.0;
    double pixelHeight = 0.0;
};

struct VtkPixelSample {
    std::size_t x = 0;
    std::size_t y = 0;
    double physicalX = 0.0;
    double physicalY = 0.0;
    float pressure = 0.0f;
    float speed = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    bool solid = false;
    bool pressureFinite = false;
    bool speedFinite = false;
};

std::optional<VtkPixelSample> sampleVtkPixel(
    const VtkFrame& frame,
    const ResultImageTransform& transform,
    double screenX,
    double screenY);

struct AdaptiveFrameWindow {
    std::size_t divisor = 1;
    std::vector<std::size_t> nearestIndices;
};

AdaptiveFrameWindow planAdaptiveFrameWindow(
    std::size_t frameCount,
    std::size_t centerIndex,
    std::size_t maximumResidentFrames);

class DecodedFrameCache {
public:
    explicit DecodedFrameCache(std::size_t byteBudget);

    std::shared_ptr<const VtkFrame> find(std::size_t frameIndex);
    bool contains(std::size_t frameIndex) const;
    void insert(
        std::size_t frameIndex,
        std::shared_ptr<const VtkFrame> frame);
    void retainOnly(const std::vector<std::size_t>& frameIndices);
    void clear();
    void setByteBudget(std::size_t byteBudget);
    std::size_t entryCount() const;
    std::size_t usedBytes() const;
    std::size_t byteBudget() const;

private:
    struct Entry {
        std::shared_ptr<const VtkFrame> frame;
        std::size_t bytes = 0;
        std::list<std::size_t>::iterator recency;
    };

    std::size_t byteBudget_ = 0;
    std::size_t usedBytes_ = 0;
    std::list<std::size_t> recency_;
    std::unordered_map<std::size_t, Entry> entries_;
};

class VtkParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct VtkSeriesLoadResult {
    std::vector<VtkFrame> frames;
    std::vector<std::string> rejected;
};

struct VtkFrameDescriptor {
    std::filesystem::path sourcePath;
    int frameNumber = -1;
    std::size_t warningCount = 0;
};

struct VtkSeriesCatalog {
    std::vector<VtkFrameDescriptor> frames;
    std::vector<std::string> rejected;
    VtkFrame activeFrame;
    DataRange pressureRange;
    DataRange velocityMagnitudeRange;
    DataRange pressureTrimmedRange;
    DataRange velocityMagnitudeTrimmedRange;
    std::size_t warningCount = 0;
};

class VtkFrameParser {
public:
    static VtkFrame parse(const std::filesystem::path& path);
    static std::optional<int> frameNumberFromFilename(
        const std::filesystem::path& path);
    static std::vector<std::filesystem::path> discoverFrames(
        const std::filesystem::path& directory);
    static std::vector<VtkFrame> parseSeries(
        const std::vector<std::filesystem::path>& paths);
    static VtkSeriesLoadResult parseRecoverableSeries(
        const std::vector<std::filesystem::path>& paths);
    static VtkSeriesCatalog indexSeries(
        const std::vector<std::filesystem::path>& paths,
        bool recoverable);
    static void validateCompatibility(
        const VtkFrame& reference,
        const VtkFrame& frame);
    static void validateSeries(const std::vector<VtkFrame>& frames);
};

} // namespace maskui
