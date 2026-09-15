#include "VtkFrame.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_OPENMP)
#include <omp.h>
#endif
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace maskui {
namespace {

// Legacy VTK binary payloads are big endian and the solver writes millions of
// words per frame. Reading them one at a time through an istream cost more than
// everything else in this file put together, so the whole array is copied in
// one go and swapped in place here instead.
inline std::uint32_t swapWord(std::uint32_t value) {
#if defined(_MSC_VER)
    return _byteswap_ulong(value);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(value);
#else
    return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) | ((value & 0xFF000000u) >> 24);
#endif
}

void swapWords(std::uint32_t* words, std::size_t count) {
    std::size_t index = 0;
#if defined(__AVX2__)
    // Eight words per shuffle. The mask reverses the four bytes of each 32-bit
    // lane, which is exactly the swap, and it applies per 128-bit half - hence
    // the same 16 bytes twice.
    const __m256i mask = _mm256_setr_epi8(
        3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
        3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    for (; index + 8 <= count; index += 8) {
        __m256i chunk = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(words + index));
        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(words + index),
            _mm256_shuffle_epi8(chunk, mask));
    }
#endif
    for (; index < count; ++index) {
        words[index] = swapWord(words[index]);
    }
}

// Worth a thread hand-off only when there is enough work to pay for it; a small
// frame is faster on one core than on eight.
constexpr std::size_t PARALLEL_ELEMENT_THRESHOLD = 32768;

// The whole file is in memory before this runs, so the cursor is a pair of
// pointers rather than a stream. Every token used to be an std::string built by
// operator>>, and every binary word a four-byte istream::read; both are gone.
class TokenCursor {
public:
    TokenCursor(const char* begin, const char* end)
        : cursor_(begin), end_(end) {}

    bool empty() {
        fill();
        return !hasToken_;
    }

    std::string_view peek() {
        fill();
        if (!hasToken_) {
            throw VtkParseError("Unexpected end of VTK file");
        }
        return token_;
    }

    std::string take() {
        std::string token(peek());
        hasToken_ = false;
        return token;
    }

    // Bulk reads. One memcpy plus one vectorised swap per array, instead of a
    // stream round trip per value.
    void readBigEndianFloats(
        float* destination,
        std::size_t count,
        const std::string& context) {
        readWords(destination, count, context);
    }

    void readBigEndianInts(
        std::int32_t* destination,
        std::size_t count,
        const std::string& context) {
        readWords(destination, count, context);
    }

    void expect(const std::string& expected) {
        const std::string actual = take();
        if (actual != expected) {
            throw VtkParseError(
                "Expected token '" + expected + "', found '" + actual + "'");
        }
    }

    void beginBinaryPayload(const std::string& context) {
        if (hasToken_) {
            throw VtkParseError(
                "Internal token state precedes binary " + context);
        }
        while (cursor_ != end_) {
            const char character = *cursor_++;
            if (character == '\n') {
                return;
            }
            if (character == '\r') {
                if (cursor_ != end_ && *cursor_ == '\n') {
                    ++cursor_;
                }
                return;
            }
            if (character != ' ' && character != '\t') {
                throw VtkParseError(
                    "Binary " + context +
                    " must start after its declaration line");
            }
        }
        throw VtkParseError(
            "Unexpected end of VTK file before binary " + context);
    }

    float takeBigEndianFloat(const std::string& context) {
        const std::uint32_t bits = takeBigEndianWord(context);
        float value = 0.0f;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    int takeBigEndianInt(const std::string& context) {
        const std::uint32_t bits = takeBigEndianWord(context);
        std::int32_t value = 0;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return static_cast<int>(value);
    }

    void skipBinaryValues(
        std::size_t count,
        const std::string& type,
        const std::string& context) {
        if (type != "float" && type != "int") {
            throw VtkParseError(
                "Unsupported binary VTK value type for " +
                context + ": " + type);
        }
        constexpr std::size_t valueBytes = 4;
        if (count > std::numeric_limits<std::size_t>::max() / valueBytes) {
            throw VtkParseError("Binary VTK payload size overflow: " + context);
        }
        require(count * valueBytes, "skipping " + context);
        cursor_ += count * valueBytes;
    }

    std::string takeBinaryBytes(
        std::size_t count,
        const std::string& context) {
        require(count, "reading " + context);
        std::string bytes(cursor_, cursor_ + count);
        cursor_ += count;
        return bytes;
    }

    void endBinaryPayload(const std::string& context) {
        if (cursor_ == end_) {
            return;
        }
        if (*cursor_ == '\r') {
            ++cursor_;
            if (cursor_ != end_ && *cursor_ == '\n') {
                ++cursor_;
            }
            return;
        }
        if (*cursor_ == '\n') {
            ++cursor_;
            return;
        }
        throw VtkParseError(
            "Binary " + context +
            " must end before a line boundary or end of file");
    }

private:
    static bool isSpace(char character) {
        return character == ' ' || character == '\t' || character == '\n' ||
               character == '\r' || character == '\v' || character == '\f';
    }

    void require(std::size_t bytes, const std::string& context) const {
        if (static_cast<std::size_t>(end_ - cursor_) < bytes) {
            throw VtkParseError(
                "Truncated binary VTK payload while " + context);
        }
    }

    template <typename T>
    void readWords(
        T* destination,
        std::size_t count,
        const std::string& context) {
        static_assert(sizeof(T) == 4, "VTK binary words are four bytes");
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw VtkParseError("Binary VTK payload size overflow: " + context);
        }
        const std::size_t byteCount = count * sizeof(T);
        require(byteCount, "reading " + context);
        std::memcpy(destination, cursor_, byteCount);
        cursor_ += byteCount;
        swapWords(reinterpret_cast<std::uint32_t*>(destination), count);
    }

    std::uint32_t takeBigEndianWord(const std::string& context) {
        std::uint32_t word = 0;
        require(sizeof(word), "reading " + context);
        std::memcpy(&word, cursor_, sizeof(word));
        cursor_ += sizeof(word);
        return swapWord(word);
    }

    void fill() {
        if (hasToken_) {
            return;
        }
        while (cursor_ != end_ && isSpace(*cursor_)) {
            ++cursor_;
        }
        if (cursor_ == end_) {
            return;
        }
        const char* start = cursor_;
        while (cursor_ != end_ && !isSpace(*cursor_)) {
            ++cursor_;
        }
        token_ = std::string_view(
            start, static_cast<std::size_t>(cursor_ - start));
        hasToken_ = true;
    }

    const char* cursor_ = nullptr;
    const char* end_ = nullptr;
    std::string_view token_;
    bool hasToken_ = false;
};

std::string trimCarriageReturn(std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

int parseInteger(const std::string& token, const std::string& context) {
    int value = 0;
    const char* begin = token.data();
    const char* end = begin + token.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) {
        throw VtkParseError("Invalid integer for " + context + ": " + token);
    }
    return value;
}

double parseNumber(const std::string& token, const std::string& context) {
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size()) {
        throw VtkParseError(
            "Invalid floating-point value for " + context + ": " + token);
    }
    return value;
}

void parseRestartConfig(VtkFrame& frame, const std::string& configText) {
    std::istringstream lines(configText);
    std::string line;
    while (std::getline(lines, line)) {
        line = trimCarriageReturn(std::move(line));
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }
        frame.restart.config[line.substr(0, equals)] =
            line.substr(equals + 1);
    }

    const auto readDouble = [&](const char* key) -> std::optional<double> {
        const auto found = frame.restart.config.find(key);
        if (found == frame.restart.config.end()) {
            return std::nullopt;
        }
        try {
            const double value = parseNumber(found->second, key);
            if (!std::isfinite(value)) {
                throw VtkParseError(std::string(key) + " is not finite");
            }
            return value;
        } catch (const std::exception&) {
            frame.warnings.push_back(
                std::string("Ignored invalid restart metadata: ") + key);
            return std::nullopt;
        }
    };
    const auto readInteger = [&](const char* key) -> std::optional<int> {
        const auto found = frame.restart.config.find(key);
        if (found == frame.restart.config.end()) {
            return std::nullopt;
        }
        try {
            return parseInteger(found->second, key);
        } catch (const std::exception&) {
            frame.warnings.push_back(
                std::string("Ignored invalid restart metadata: ") + key);
            return std::nullopt;
        }
    };

    frame.restart.currentTime = readDouble("restartTime");
    frame.restart.totalTime = readDouble("totalTime");
    frame.restart.restartDt = readDouble("restartDt");
    frame.restart.restartStep = readInteger("restartStep");
}

std::size_t checkedGridCount(
    int nx,
    int ny,
    int nz,
    const std::string& context) {
    if (nx <= 0 || ny <= 0 || nz <= 0) {
        throw VtkParseError(context + " dimensions must be positive");
    }
    const std::size_t x = static_cast<std::size_t>(nx);
    const std::size_t y = static_cast<std::size_t>(ny);
    const std::size_t z = static_cast<std::size_t>(nz);
    if (x > std::numeric_limits<std::size_t>::max() / y) {
        throw VtkParseError(context + " dimensions overflow the sample count");
    }
    if (x * y > std::numeric_limits<std::size_t>::max() / z) {
        throw VtkParseError(context + " dimensions overflow the sample count");
    }
    return x * y * z;
}

void includeValue(DataRange& range, double value) {
    if (!std::isfinite(value)) {
        return;
    }
    if (!range.available) {
        range.available = true;
        range.minimum = value;
        range.maximum = value;
        return;
    }
    range.minimum = std::min(range.minimum, value);
    range.maximum = std::max(range.maximum, value);
}

DataRange trimmedRange(std::vector<float>& values) {
    DataRange range;
    if (values.empty()) {
        return range;
    }
    const std::size_t last = values.size() - 1u;
    // Half a percent at each end, and never the whole thing: a frame
    // of forty cells still has a low and a high.
    const std::size_t low =
        static_cast<std::size_t>(static_cast<double>(last) * 0.005);
    const std::size_t high =
        static_cast<std::size_t>(static_cast<double>(last) * 0.995);
    std::nth_element(values.begin(), values.begin() + low, values.end());
    range.minimum = static_cast<double>(values[low]);
    std::nth_element(values.begin() + low, values.begin() + high, values.end());
    range.maximum = static_cast<double>(values[high]);
    range.available = true;
    if (range.maximum < range.minimum) {
        std::swap(range.minimum, range.maximum);
    }
    return range;
}

bool allRequiredArraysPresent(
    bool hasPressure,
    bool hasSolid,
    bool hasVelocity) {
    return hasPressure && hasSolid && hasVelocity;
}

void skipScalarArray(
    TokenCursor& cursor,
    std::size_t sampleCount,
    int componentCount,
    const std::string& type,
    bool binary,
    const std::string& name) {
    if (componentCount <= 0) {
        throw VtkParseError("SCALARS component count must be positive");
    }
    const std::size_t components = static_cast<std::size_t>(componentCount);
    if (sampleCount > std::numeric_limits<std::size_t>::max() / components) {
        throw VtkParseError("SCALARS array size overflow");
    }
    const std::size_t valueCount = sampleCount * components;
    if (binary) {
        cursor.beginBinaryPayload("SCALARS " + name);
        cursor.skipBinaryValues(
            valueCount,
            type,
            "SCALARS " + name);
        cursor.endBinaryPayload("SCALARS " + name);
        return;
    }
    for (std::size_t index = 0; index < valueCount; ++index) {
        parseNumber(cursor.take(), "SCALARS " + name);
    }
}

void skipVectorArray(
    TokenCursor& cursor,
    std::size_t sampleCount,
    const std::string& type,
    bool binary,
    const std::string& name) {
    if (binary) {
        cursor.beginBinaryPayload("VECTORS " + name);
        cursor.skipBinaryValues(
            3 * sampleCount,
            type,
            "VECTORS " + name);
        cursor.endBinaryPayload("VECTORS " + name);
        return;
    }
    for (std::size_t index = 0; index < 3 * sampleCount; ++index) {
        parseNumber(cursor.take(), "VECTORS " + name);
    }
}

std::optional<int> titleFrameNumber(const std::string& title) {
    static const std::regex pattern(R"(.*\bstep ([0-9]+)\s*$)");
    std::smatch match;
    if (!std::regex_match(title, match, pattern)) {
        return std::nullopt;
    }
    try {
        const long long value = std::stoll(match[1].str());
        if (value > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return static_cast<int>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

std::size_t VtkFrame::cellIndex(std::size_t i, std::size_t j) const {
    return cellIndex(i, j, 0);
}

std::size_t VtkFrame::cellIndex(
    std::size_t i,
    std::size_t j,
    std::size_t k) const {
    if (i >= nx || j >= ny || k >= nz) {
        throw std::out_of_range("VTK cell index is outside the logical grid");
    }
    return (k * ny + j) * nx + i;
}

std::size_t VtkFrame::decodedByteSize() const {
    std::size_t bytes = sourcePath.native().capacity() *
        sizeof(std::filesystem::path::value_type);
    bytes += title.capacity();
    bytes += pressure.capacity() * sizeof(float);
    bytes += solid.capacity() * sizeof(std::uint8_t);
    bytes += velocity.capacity() * sizeof(Velocity);
    bytes += velocityMagnitude.capacity() * sizeof(float);
    bytes += pressureFinite.capacity() * sizeof(std::uint8_t);
    bytes += velocityFinite.capacity() * sizeof(std::uint8_t);
    for (const std::string& name : scalarNames) {
        bytes += name.capacity();
        const auto found = scalars.find(name);
        if (found != scalars.end())
            bytes += found->second.capacity() * sizeof(float);
    }
    for (const std::string& warning : warnings) {
        bytes += warning.capacity();
    }
    return bytes;
}

std::optional<VtkPixelSample> sampleVtkPixel(
    const VtkFrame& frame,
    const ResultImageTransform& transform,
    double screenX,
    double screenY) {
    if (frame.nx == 0 || frame.ny == 0 ||
        transform.pixelWidth <= 0.0 || transform.pixelHeight <= 0.0) {
        return std::nullopt;
    }

    const double localX = screenX - transform.screenOriginX;
    const double localY = screenY - transform.screenOriginY;
    const double imageWidth =
        static_cast<double>(frame.nx) * transform.pixelWidth;
    const double imageHeight =
        static_cast<double>(frame.ny) * transform.pixelHeight;
    if (localX < 0.0 || localY < 0.0 ||
        localX >= imageWidth || localY >= imageHeight) {
        return std::nullopt;
    }

    std::size_t x = static_cast<std::size_t>(
        std::floor(localX / transform.pixelWidth));
    const std::size_t displayY = static_cast<std::size_t>(
        std::floor(localY / transform.pixelHeight));
    std::size_t y = frame.ny - 1u - displayY;
    if (frame.rectilinear()) {
        x = frame.columnAt(frame.originX +
                           (static_cast<double>(x) + 0.5) * frame.spanX() /
                               static_cast<double>(frame.nx));
        y = frame.rowAt(frame.originY +
                        (static_cast<double>(y) + 0.5) * frame.spanY() /
                            static_cast<double>(frame.ny));
    }
    const std::size_t index = frame.cellIndex(x, y);
    const double sampleOffset =
        frame.association == VtkDataAssociation::Cell ? 0.5 : 0.0;

    VtkPixelSample sample;
    sample.x = x;
    sample.y = y;
    sample.physicalX =
        frame.rectilinear()
            ? frame.cellCentreX(x)
            : frame.originX +
                  (static_cast<double>(x) + sampleOffset) * frame.spacingX;
    sample.physicalY =
        frame.rectilinear()
            ? frame.cellCentreY(y)
            : frame.originY +
                  (static_cast<double>(y) + sampleOffset) * frame.spacingY;
    sample.pressure = frame.pressure[index];
    sample.speed = frame.velocityMagnitude[index];
    sample.velocityX = frame.velocity[index].x;
    sample.velocityY = frame.velocity[index].y;
    sample.solid = frame.solid[index] != 0;
    sample.pressureFinite = frame.pressureFinite[index] != 0;
    sample.speedFinite = frame.velocityFinite[index] != 0;
    return sample;
}

VtkFrame extractSlice(
    const VtkFrame& volume,
    SliceAxis axis,
    std::size_t index) {
    if (volume.nx == 0 || volume.ny == 0 || volume.nz == 0) {
        throw std::out_of_range("VTK slice source has no cells");
    }
    const int removed = axis == SliceAxis::X ? 0 : (axis == SliceAxis::Y ? 1 : 2);
    const int first = axis == SliceAxis::X ? 1 : 0;
    const int second = axis == SliceAxis::Z ? 1 : 2;

    const auto axisCount = [&volume](int which) -> std::size_t {
        return which == 0 ? volume.nx : (which == 1 ? volume.ny : volume.nz);
    };
    const auto axisOrigin = [&volume](int which) -> double {
        return which == 0
            ? volume.originX
            : (which == 1 ? volume.originY : volume.originZ);
    };
    const auto axisSpacing = [&volume](int which) -> double {
        return which == 0
            ? volume.spacingX
            : (which == 1 ? volume.spacingY : volume.spacingZ);
    };
    const auto axisFaceCount = [&volume](int which) -> std::size_t {
        return which == 0
            ? volume.faceX.size()
            : (which == 1 ? volume.faceY.size() : volume.faceZ.size());
    };
    const auto axisLow = [&volume](int which, std::size_t cell) -> double {
        return which == 0
            ? volume.cellLeft(cell)
            : (which == 1 ? volume.cellBottom(cell) : volume.cellFront(cell));
    };
    const auto axisHigh = [&volume](int which, std::size_t cell) -> double {
        return which == 0
            ? volume.cellRight(cell)
            : (which == 1 ? volume.cellTop(cell) : volume.cellBack(cell));
    };

    if (index >= axisCount(removed)) {
        throw std::out_of_range("VTK slice index is outside the volume");
    }

    VtkFrame slice;
    slice.sourcePath = volume.sourcePath;
    slice.title = volume.title;
    slice.frameNumber = volume.frameNumber;
    slice.association = volume.association;
    slice.nx = axisCount(first);
    slice.ny = axisCount(second);
    slice.nz = 1;
    slice.originX = axisOrigin(first);
    slice.originY = axisOrigin(second);
    slice.originZ = axisLow(removed, index);
    slice.spacingX = axisSpacing(first);
    slice.spacingY = axisSpacing(second);
    slice.spacingZ = axisHigh(removed, index) - axisLow(removed, index);

    if (axisFaceCount(first) > 1 || axisFaceCount(second) > 1) {
        const auto faces = [&](int which, std::vector<double>& out) {
            const std::size_t count = axisCount(which);
            out.resize(count + 1u);
            for (std::size_t cell = 0; cell < count; ++cell) {
                out[cell] = axisLow(which, cell);
            }
            out[count] = axisHigh(which, count - 1u);
        };
        faces(first, slice.faceX);
        faces(second, slice.faceY);
    }
    if (axisFaceCount(removed) > 1) {
        slice.faceZ = {
            axisLow(removed, index),
            axisHigh(removed, index)
        };
    }

    const std::size_t sampleCount = slice.nx * slice.ny;
    slice.pressure.resize(sampleCount);
    slice.solid.resize(sampleCount);
    slice.velocity.resize(sampleCount);
    slice.velocityMagnitude.resize(sampleCount);
    slice.pressureFinite.resize(sampleCount);
    slice.velocityFinite.resize(sampleCount);
    slice.scalarNames = volume.scalarNames;
    for (const std::string& name : slice.scalarNames) {
        slice.scalars.emplace(name, std::vector<float>(sampleCount));
    }

    for (std::size_t q = 0; q < slice.ny; ++q) {
        for (std::size_t p = 0; p < slice.nx; ++p) {
            std::size_t cell[3] = {0, 0, 0};
            cell[static_cast<std::size_t>(first)] = p;
            cell[static_cast<std::size_t>(second)] = q;
            cell[static_cast<std::size_t>(removed)] = index;
            const std::size_t source =
                volume.cellIndex(cell[0], cell[1], cell[2]);
            const std::size_t target = q * slice.nx + p;

            slice.pressure[target] = volume.pressure[source];
            slice.solid[target] = volume.solid[source];
            slice.velocityMagnitude[target] = volume.velocityMagnitude[source];
            slice.pressureFinite[target] = volume.pressureFinite[source];
            slice.velocityFinite[target] = volume.velocityFinite[source];

            const Velocity value = volume.velocity[source];
            const float components[3] = {value.x, value.y, value.z};
            slice.velocity[target] = {
                components[first],
                components[second],
                components[removed]
            };

            for (const std::string& name : slice.scalarNames) {
                slice.scalars.at(name)[target] =
                    volume.scalars.at(name)[source];
            }
        }
    }

    std::vector<float> pressureSamples;
    std::vector<float> magnitudeSamples;
    pressureSamples.reserve(sampleCount);
    magnitudeSamples.reserve(sampleCount);
    for (std::size_t cell = 0; cell < sampleCount; ++cell) {
        if (slice.solid[cell] != 0) {
            continue;
        }
        if (slice.pressureFinite[cell] != 0) {
            includeValue(slice.pressureRange, slice.pressure[cell]);
            pressureSamples.push_back(slice.pressure[cell]);
        }
        includeValue(slice.velocityXRange, slice.velocity[cell].x);
        includeValue(slice.velocityYRange, slice.velocity[cell].y);
        if (slice.velocityFinite[cell] != 0) {
            includeValue(
                slice.velocityMagnitudeRange, slice.velocityMagnitude[cell]);
            magnitudeSamples.push_back(slice.velocityMagnitude[cell]);
        }
    }
    slice.pressureTrimmedRange = trimmedRange(pressureSamples);
    slice.velocityMagnitudeTrimmedRange = trimmedRange(magnitudeSamples);

    for (const std::string& name : slice.scalarNames) {
        const std::vector<float>& values = slice.scalars.at(name);
        DataRange full;
        std::vector<float> finite;
        finite.reserve(values.size());
        for (float value : values) {
            if (!std::isfinite(value))
                continue;
            includeValue(full, value);
            finite.push_back(value);
        }
        slice.scalarRanges[name] = full;
        slice.scalarTrimmedRanges[name] = trimmedRange(finite);
    }

    slice.restart = volume.restart;
    slice.warnings = volume.warnings;
    return slice;
}

AdaptiveFrameWindow planAdaptiveFrameWindow(
    std::size_t frameCount,
    std::size_t centerIndex,
    std::size_t maximumResidentFrames) {
    AdaptiveFrameWindow window;
    if (frameCount == 0) {
        return window;
    }

    centerIndex = std::min(centerIndex, frameCount - 1u);
    maximumResidentFrames = std::max<std::size_t>(1, maximumResidentFrames);
    if (frameCount == 1) {
        window.nearestIndices.push_back(0);
        return window;
    }

    window.divisor = 2;
    const auto dividedCount = [frameCount](std::size_t divisor) {
        return frameCount / divisor +
            static_cast<std::size_t>(frameCount % divisor != 0);
    };
    while (dividedCount(window.divisor) > maximumResidentFrames &&
           window.divisor < frameCount) {
        if (window.divisor >
            std::numeric_limits<std::size_t>::max() / 2u) {
            break;
        }
        window.divisor *= 2u;
    }

    const std::size_t targetCount = std::max<std::size_t>(
        1,
        std::min(frameCount, dividedCount(window.divisor)));
    const std::size_t leftRadius = (targetCount - 1u) / 2u;
    std::size_t begin = centerIndex > leftRadius
        ? centerIndex - leftRadius
        : 0u;
    if (begin > frameCount - targetCount) {
        begin = frameCount - targetCount;
    }
    const std::size_t end = begin + targetCount;

    window.nearestIndices.reserve(targetCount);
    window.nearestIndices.push_back(centerIndex);
    for (std::size_t distance = 1;
         window.nearestIndices.size() < targetCount;
         ++distance) {
        if (centerIndex <= std::numeric_limits<std::size_t>::max() - distance) {
            const std::size_t right = centerIndex + distance;
            if (right < end) {
                window.nearestIndices.push_back(right);
            }
        }
        if (window.nearestIndices.size() == targetCount) {
            break;
        }
        if (centerIndex >= distance) {
            const std::size_t left = centerIndex - distance;
            if (left >= begin) {
                window.nearestIndices.push_back(left);
            }
        }
    }
    return window;
}

DecodedFrameCache::DecodedFrameCache(std::size_t byteBudget)
    : byteBudget_(byteBudget) {
}

std::shared_ptr<const VtkFrame> DecodedFrameCache::find(
    std::size_t frameIndex) {
    const auto found = entries_.find(frameIndex);
    if (found == entries_.end()) {
        return {};
    }
    recency_.splice(recency_.begin(), recency_, found->second.recency);
    found->second.recency = recency_.begin();
    return found->second.frame;
}

bool DecodedFrameCache::contains(std::size_t frameIndex) const {
    return entries_.find(frameIndex) != entries_.end();
}

void DecodedFrameCache::insert(
    std::size_t frameIndex,
    std::shared_ptr<const VtkFrame> frame) {
    const auto existing = entries_.find(frameIndex);
    if (existing != entries_.end()) {
        usedBytes_ -= existing->second.bytes;
        recency_.erase(existing->second.recency);
        entries_.erase(existing);
    }
    if (!frame) {
        return;
    }

    const std::size_t bytes = frame->decodedByteSize();
    if (bytes > byteBudget_) {
        return;
    }
    while (!recency_.empty() && usedBytes_ > byteBudget_ - bytes) {
        const std::size_t leastRecent = recency_.back();
        const auto evicted = entries_.find(leastRecent);
        usedBytes_ -= evicted->second.bytes;
        entries_.erase(evicted);
        recency_.pop_back();
    }

    recency_.push_front(frameIndex);
    entries_.emplace(
        frameIndex,
        Entry{std::move(frame), bytes, recency_.begin()});
    usedBytes_ += bytes;
}

void DecodedFrameCache::retainOnly(
    const std::vector<std::size_t>& frameIndices) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        if (std::find(
                frameIndices.begin(),
                frameIndices.end(),
                entry->first) != frameIndices.end()) {
            ++entry;
            continue;
        }
        usedBytes_ -= entry->second.bytes;
        recency_.erase(entry->second.recency);
        entry = entries_.erase(entry);
    }
}

void DecodedFrameCache::clear() {
    entries_.clear();
    recency_.clear();
    usedBytes_ = 0;
}

void DecodedFrameCache::setByteBudget(std::size_t byteBudget) {
    byteBudget_ = byteBudget;
    while (!recency_.empty() && usedBytes_ > byteBudget_) {
        const std::size_t leastRecent = recency_.back();
        const auto evicted = entries_.find(leastRecent);
        if (evicted != entries_.end()) {
            usedBytes_ -= evicted->second.bytes;
            entries_.erase(evicted);
        }
        recency_.pop_back();
    }
}

std::size_t DecodedFrameCache::entryCount() const {
    return entries_.size();
}

std::size_t DecodedFrameCache::usedBytes() const {
    return usedBytes_;
}

std::size_t DecodedFrameCache::byteBudget() const {
    return byteBudget_;
}

VtkFrame VtkFrameParser::parse(const std::filesystem::path& path) {
    // One read for the whole file. Everything below then works on memory, which
    // is what makes a frame decode in a couple of milliseconds instead of the
    // stream round trip it used to cost per value.
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        throw VtkParseError("Cannot open VTK file: " + path.string());
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 0) {
        throw VtkParseError("Cannot measure VTK file: " + path.string());
    }
    std::vector<char> data(static_cast<std::size_t>(fileSize));
    input.seekg(0, std::ios::beg);
    if (!data.empty() &&
        !input.read(data.data(), static_cast<std::streamsize>(data.size()))) {
        throw VtkParseError("Cannot read VTK file: " + path.string());
    }
    input.close();

    const char* readCursor = data.data();
    const char* const readEnd = data.data() + data.size();
    const auto nextLine = [&readCursor, readEnd](std::string& line) {
        if (readCursor == readEnd) {
            return false;
        }
        const char* const start = readCursor;
        while (readCursor != readEnd && *readCursor != '\n') {
            ++readCursor;
        }
        line.assign(start, static_cast<std::size_t>(readCursor - start));
        if (readCursor != readEnd) {
            ++readCursor;
        }
        return true;
    };

    std::string version;
    std::string title;
    std::string encoding;
    if (!nextLine(version) || !nextLine(title) || !nextLine(encoding)) {
        throw VtkParseError("VTK file is missing its three-line header");
    }
    version = trimCarriageReturn(version);
    title = trimCarriageReturn(title);
    encoding = trimCarriageReturn(encoding);
    if (version.rfind("# vtk DataFile Version ", 0) != 0) {
        throw VtkParseError("Unsupported VTK version header");
    }
    const bool binary = encoding == "BINARY";
    if (!binary && encoding != "ASCII") {
        throw VtkParseError(
            "Only legacy ASCII or BINARY VTK files are supported");
    }

    TokenCursor cursor(readCursor, readEnd);
    cursor.expect("DATASET");
    const std::string datasetKind = cursor.take();
    const bool rectilinear = datasetKind == "RECTILINEAR_GRID";
    if (!rectilinear && datasetKind != "STRUCTURED_POINTS") {
        throw VtkParseError(
            "Only STRUCTURED_POINTS and RECTILINEAR_GRID frames are "
            "supported, not " + datasetKind);
    }
    cursor.expect("DIMENSIONS");
    const int pointNx = parseInteger(cursor.take(), "DIMENSIONS nx");
    const int pointNy = parseInteger(cursor.take(), "DIMENSIONS ny");
    const int pointNz = parseInteger(cursor.take(), "DIMENSIONS nz");
    const std::size_t pointCount =
        checkedGridCount(pointNx, pointNy, pointNz, "POINT");

    double originX = 0.0;
    double originY = 0.0;
    double originZ = 0.0;
    double spacingX = 0.0;
    double spacingY = 0.0;
    double spacingZ = 0.0;
    std::vector<double> faceX;
    std::vector<double> faceY;
    std::vector<double> faceZ;

    if (rectilinear) {
        const auto axis = [&](const char* keyword,
                              int expected,
                              std::vector<double>& out) {
            cursor.expect(keyword);
            const int count = parseInteger(cursor.take(), keyword);
            if (count != expected) {
                throw VtkParseError(
                    std::string(keyword) + " must carry " +
                    std::to_string(expected) + " values to match DIMENSIONS");
            }
            const std::string type = cursor.take();
            if (type != "float" && type != "double") {
                throw VtkParseError(
                    std::string(keyword) + " must be float or double");
            }
            out.resize(static_cast<std::size_t>(count));
            if (binary && type == "float") {
                std::vector<float> raw(static_cast<std::size_t>(count));
                cursor.beginBinaryPayload(keyword);
                cursor.readBigEndianFloats(raw.data(),
                                           static_cast<std::size_t>(count),
                                           keyword);
                cursor.endBinaryPayload(keyword);
                for (int k = 0; k < count; ++k)
                    out[static_cast<std::size_t>(k)] =
                        raw[static_cast<std::size_t>(k)];
            } else {
                for (int k = 0; k < count; ++k)
                    out[static_cast<std::size_t>(k)] =
                        parseNumber(cursor.take(), keyword);
            }
        };

        axis("X_COORDINATES", pointNx, faceX);
        axis("Y_COORDINATES", pointNy, faceY);
        axis("Z_COORDINATES", pointNz, faceZ);

        const auto rising = [](const std::vector<double>& values,
                               const char* what) {
            for (std::size_t k = 0; k < values.size(); ++k) {
                if (!std::isfinite(values[k]))
                    throw VtkParseError(std::string(what) +
                                        " values must be finite");
                if (k > 0 && !(values[k] > values[k - 1]))
                    throw VtkParseError(
                        std::string(what) +
                        " must increase: a cell of zero or negative width is "
                        "not a cell");
            }
        };
        rising(faceX, "X_COORDINATES");
        rising(faceY, "Y_COORDINATES");
        rising(faceZ, "Z_COORDINATES");

        originX = faceX.front();
        originY = faceY.front();
        originZ = faceZ.front();
        spacingX = (faceX.back() - faceX.front()) /
                   std::max<std::size_t>(1, faceX.size() - 1);
        spacingY = (faceY.back() - faceY.front()) /
                   std::max<std::size_t>(1, faceY.size() - 1);
        spacingZ = (faceZ.back() - faceZ.front()) /
                   std::max<std::size_t>(1, faceZ.size() - 1);
        if (faceZ.size() < 2) {
            faceZ.clear();
            spacingZ = 1.0;
        }
    } else {
        cursor.expect("ORIGIN");
        originX = parseNumber(cursor.take(), "ORIGIN x");
        originY = parseNumber(cursor.take(), "ORIGIN y");
        originZ = parseNumber(cursor.take(), "ORIGIN z");
        if (!std::isfinite(originX) ||
            !std::isfinite(originY) ||
            !std::isfinite(originZ)) {
            throw VtkParseError("ORIGIN values must be finite");
        }

        cursor.expect("SPACING");
        spacingX = parseNumber(cursor.take(), "SPACING x");
        spacingY = parseNumber(cursor.take(), "SPACING y");
        spacingZ = parseNumber(cursor.take(), "SPACING z");
        if (!std::isfinite(spacingX) ||
            !std::isfinite(spacingY) ||
            !std::isfinite(spacingZ) ||
            spacingX <= 0.0 ||
            spacingY <= 0.0 ||
            (pointNz > 1 && spacingZ <= 0.0)) {
            throw VtkParseError(
                "SPACING x and y must be positive, SPACING z must be positive "
                "on a frame more than one plane deep, and all spacing values "
                "must be finite");
        }
        if (spacingZ <= 0.0) {
            spacingZ = 1.0;
        }
    }

    const std::string associationToken = cursor.take();
    VtkDataAssociation association = VtkDataAssociation::Point;
    int logicalNx = pointNx;
    int logicalNy = pointNy;
    int logicalNz = pointNz;
    std::size_t sampleCount = pointCount;
    if (associationToken == "CELL_DATA") {
        if (pointNx < 2 || pointNy < 2) {
            throw VtkParseError(
                "CELL_DATA requires DIMENSIONS nx and ny to be at least 2");
        }
        association = VtkDataAssociation::Cell;
        logicalNx = pointNx - 1;
        logicalNy = pointNy - 1;
        logicalNz = pointNz > 1 ? pointNz - 1 : 1;
        sampleCount =
            checkedGridCount(logicalNx, logicalNy, logicalNz, "CELL");
    } else if (associationToken != "POINT_DATA") {
        throw VtkParseError(
            "Expected POINT_DATA or CELL_DATA, found '" +
            associationToken + "'");
    }

    const int declaredSampleCount = parseInteger(
        cursor.take(),
        associationToken + " count");
    if (declaredSampleCount < 0 ||
        static_cast<std::size_t>(declaredSampleCount) != sampleCount) {
        throw VtkParseError(
            associationToken +
            " count does not equal the logical sample-grid size");
    }

    VtkFrame frame;
    frame.sourcePath = path;
    frame.title = title;
    frame.association = association;
    frame.nx = static_cast<std::size_t>(logicalNx);
    frame.ny = static_cast<std::size_t>(logicalNy);
    frame.nz = static_cast<std::size_t>(logicalNz);
    frame.originX = originX;
    frame.originY = originY;
    frame.originZ = originZ;
    frame.spacingX = spacingX;
    frame.spacingY = spacingY;
    frame.spacingZ = spacingZ;
    if (rectilinear) {
        frame.faceX = std::move(faceX);
        frame.faceY = std::move(faceY);
        frame.faceZ = std::move(faceZ);
    }
    frame.frameNumber = frameNumberFromFilename(path).value_or(-1);

    bool hasPressure = false;
    bool hasSolid = false;
    bool hasVelocity = false;
    bool hasRestartConfig = false;
    bool hasUFace = false;
    bool hasVFace = false;
    bool hasPRaw = false;
    bool hasFacePack = false;
    std::size_t uFaceCount = 0;
    std::size_t vFaceCount = 0;
    std::size_t pRawCount = 0;
    std::size_t conservedArrays = 0;
    std::size_t conservedCount = 0;

    while (!cursor.empty()) {
        const std::string declaration = cursor.take();
        if (declaration == "SCALARS") {
            const std::string name = cursor.take();
            const std::string type = cursor.take();
            int componentCount = 1;
            if (cursor.peek() != "LOOKUP_TABLE") {
                componentCount =
                    parseInteger(cursor.take(), "SCALARS component count");
            }
            cursor.expect("LOOKUP_TABLE");
            cursor.expect("default");

            if (name == "pressure") {
                if (hasPressure) {
                    throw VtkParseError("Duplicate pressure array");
                }
                if (type != "float" || componentCount != 1) {
                    throw VtkParseError(
                        "pressure must be SCALARS pressure float with one component");
                }
                frame.pressure.reserve(sampleCount);
                if (binary) {
                    cursor.beginBinaryPayload("pressure");
                    frame.pressure.resize(sampleCount);
                    cursor.readBigEndianFloats(
                        frame.pressure.data(), sampleCount, "pressure");
                    cursor.endBinaryPayload("pressure");
                } else {
                    for (std::size_t index = 0;
                         index < sampleCount;
                         ++index) {
                        frame.pressure.push_back(
                            static_cast<float>(
                                parseNumber(cursor.take(), "pressure")));
                    }
                }
                hasPressure = true;
            } else if (name == "solid") {
                if (hasSolid) {
                    throw VtkParseError("Duplicate solid array");
                }
                const bool byteSolid =
                    type == "unsigned_char" || type == "char";
                if ((type != "int" && !byteSolid) || componentCount != 1) {
                    throw VtkParseError(
                        "solid must be SCALARS solid int, unsigned_char or "
                        "char, with one component");
                }
                frame.solid.reserve(sampleCount);
                if (binary && byteSolid) {
                    cursor.beginBinaryPayload("solid");
                    const std::string bytes =
                        cursor.takeBinaryBytes(sampleCount, "solid");
                    frame.solid.resize(sampleCount);
                    unsigned char invalid = 0;
                    for (std::size_t index = 0; index < sampleCount; ++index) {
                        const unsigned char value =
                            static_cast<unsigned char>(bytes[index]);
                        invalid |= static_cast<unsigned char>(value & ~1u);
                        frame.solid[index] =
                            static_cast<std::uint8_t>(value != 0);
                    }
                    if (invalid != 0) {
                        throw VtkParseError("solid values must be 0 or 1");
                    }
                    cursor.endBinaryPayload("solid");
                } else if (binary) {
                    cursor.beginBinaryPayload("solid");
                    // Read as 32-bit words, then narrow to the byte the frame
                    // keeps. The scratch buffer is worth it: it turns 100k
                    // stream reads into one memcpy and one swap pass.
                    std::vector<std::int32_t> rawSolid(sampleCount);
                    cursor.readBigEndianInts(
                        rawSolid.data(), sampleCount, "solid");
                    frame.solid.resize(sampleCount);
                    std::int32_t invalid = 0;
                    for (std::size_t index = 0; index < sampleCount; ++index) {
                        const std::int32_t value = rawSolid[index];
                        invalid |= (value & ~std::int32_t(1));
                        frame.solid[index] =
                            static_cast<std::uint8_t>(value != 0);
                    }
                    if (invalid != 0) {
                        throw VtkParseError("solid values must be 0 or 1");
                    }
                    cursor.endBinaryPayload("solid");
                } else {
                    for (std::size_t index = 0;
                         index < sampleCount;
                         ++index) {
                        const int value =
                            parseInteger(cursor.take(), "solid");
                        if (value != 0 && value != 1) {
                            throw VtkParseError(
                                "solid values must be 0 or 1");
                        }
                        frame.solid.push_back(
                            static_cast<std::uint8_t>(value));
                    }
                }
                hasSolid = true;
            } else if (type == "float" && componentCount == 1 &&
                       frame.scalars.find(name) == frame.scalars.end()) {
                std::vector<float> values(sampleCount);
                if (binary) {
                    cursor.beginBinaryPayload(name);
                    cursor.readBigEndianFloats(
                        values.data(), sampleCount, name);
                    cursor.endBinaryPayload(name);
                } else {
                    for (std::size_t index = 0; index < sampleCount; ++index) {
                        values[index] =
                            static_cast<float>(parseNumber(cursor.take(), name));
                    }
                }
                frame.scalarNames.push_back(name);
                frame.scalars.emplace(name, std::move(values));
            } else {
                skipScalarArray(
                    cursor,
                    sampleCount,
                    componentCount,
                    type,
                    binary,
                    name);
                frame.warnings.push_back(
                    "Ignored unsupported scalar array: " + name);
            }
        } else if (declaration == "VECTORS") {
            const std::string name = cursor.take();
            const std::string type = cursor.take();
            if (name == "velocity") {
                if (hasVelocity) {
                    throw VtkParseError("Duplicate velocity array");
                }
                if (type != "float") {
                    throw VtkParseError(
                        "velocity must be VECTORS velocity float");
                }
                frame.velocity.reserve(sampleCount);
                if (binary) {
                    cursor.beginBinaryPayload("velocity");
                    // Velocity is three interleaved floats per sample and the
                    // struct is exactly that, so the array lands straight in it.
                    static_assert(
                        sizeof(Velocity) == 3 * sizeof(float),
                        "Velocity must be three tightly packed floats");
                    frame.velocity.resize(sampleCount);
                    cursor.readBigEndianFloats(
                        reinterpret_cast<float*>(frame.velocity.data()),
                        sampleCount * 3u,
                        "velocity");
                    cursor.endBinaryPayload("velocity");
                } else {
                    for (std::size_t index = 0;
                         index < sampleCount;
                         ++index) {
                        frame.velocity.push_back({
                            static_cast<float>(
                                parseNumber(cursor.take(), "velocity x")),
                            static_cast<float>(
                                parseNumber(cursor.take(), "velocity y")),
                            static_cast<float>(
                                parseNumber(cursor.take(), "velocity z"))
                        });
                    }
                }
                hasVelocity = true;
            } else {
                if (!allRequiredArraysPresent(
                        hasPressure, hasSolid, hasVelocity)) {
                    throw VtkParseError(
                        "Unknown vector array before required arrays: " + name);
                }
                skipVectorArray(
                    cursor,
                    sampleCount,
                    type,
                    binary,
                    name);
                frame.warnings.push_back(
                    "Ignored unsupported vector array: " + name);
            }
        } else if (declaration == "FIELD") {
            if (!allRequiredArraysPresent(
                    hasPressure, hasSolid, hasVelocity)) {
                throw VtkParseError(
                    "FIELD data appears before required display arrays");
            }
            if (!binary) {
                throw VtkParseError(
                    "RestartData requires a BINARY legacy VTK frame");
            }
            const std::string fieldName = cursor.take();
            const int arrayCount =
                parseInteger(cursor.take(), "FIELD array count");
            if (arrayCount < 0) {
                throw VtkParseError("FIELD array count must be non-negative");
            }
            for (int array = 0; array < arrayCount; ++array) {
                const std::string name = cursor.take();
                const int components = parseInteger(
                    cursor.take(), "FIELD " + name + " components");
                const int tuples = parseInteger(
                    cursor.take(), "FIELD " + name + " tuples");
                const std::string type = cursor.take();
                if (components <= 0 || tuples < 0) {
                    throw VtkParseError(
                        "Invalid component or tuple count for FIELD " + name);
                }
                const std::size_t componentCount =
                    static_cast<std::size_t>(components);
                const std::size_t tupleCount =
                    static_cast<std::size_t>(tuples);
                if (tupleCount != 0 &&
                    componentCount >
                        std::numeric_limits<std::size_t>::max() /
                            tupleCount) {
                    throw VtkParseError("FIELD array size overflow: " + name);
                }
                const std::size_t valueCount = componentCount * tupleCount;
                cursor.beginBinaryPayload("FIELD " + name);
                if (type == "char" || type == "unsigned_char") {
                    const std::string bytes =
                        cursor.takeBinaryBytes(valueCount, "FIELD " + name);
                    if (name == "configText") {
                        if (hasRestartConfig || componentCount != 1) {
                            throw VtkParseError(
                                "Duplicate or malformed configText FIELD");
                        }
                        parseRestartConfig(frame, bytes);
                        hasRestartConfig = true;
                        frame.restart.hasConfigText = true;
                    } else if (name == "facePack") {
                        hasFacePack = componentCount == 1 && !bytes.empty();
                    }
                } else if (type == "float" || type == "int") {
                    cursor.skipBinaryValues(
                        valueCount, type, "FIELD " + name);
                    if (name == "uFace") {
                        hasUFace = type == "float" && componentCount == 1;
                        uFaceCount = valueCount;
                    } else if (name == "vFace") {
                        hasVFace = type == "float" && componentCount == 1;
                        vFaceCount = valueCount;
                    } else if (name == "pRaw") {
                        hasPRaw = type == "float" && componentCount == 1;
                        pRawCount = valueCount;
                    } else if (
                        name == "stateRho" || name == "stateRhoU" ||
                        name == "stateRhoV" || name == "stateRhoE") {
                        // The compressible solver stores the conserved
                        // variables themselves rather than face velocities and
                        // a raw pressure, which is what an incompressible run
                        // leaves behind. Its own reader restarts from these,
                        // and the window used to call every one of its frames
                        // uncontinuable and put a warning on it.
                        if (type == "float" && componentCount == 1 &&
                            valueCount > 0 &&
                            (conservedArrays == 0 ||
                             valueCount == conservedCount)) {
                            ++conservedArrays;
                            conservedCount = valueCount;
                        }
                    }
                } else {
                    throw VtkParseError(
                        "Unsupported FIELD type " + type + " for " + name);
                }
                cursor.endBinaryPayload("FIELD " + name);
            }
            if (fieldName != "RestartData") {
                frame.warnings.push_back(
                    "Ignored non-standard FIELD group: " + fieldName);
            }
        } else {
            throw VtkParseError(
                "Unsupported or misplaced VTK declaration: " + declaration);
        }
    }

    if (!allRequiredArraysPresent(hasPressure, hasSolid, hasVelocity)) {
        throw VtkParseError(
            "VTK frame is missing pressure, solid, or velocity");
    }

    const bool legacyArraysMatch =
        frame.nx < std::numeric_limits<std::size_t>::max() &&
        hasUFace && hasVFace && hasPRaw &&
        uFaceCount == (frame.nx + 1u) * frame.ny &&
        vFaceCount == frame.nx * (frame.ny + 1u) &&
        pRawCount == frame.nx * frame.ny;
    const bool conservedArraysMatch =
        conservedArrays == 4u &&
        frame.nx < std::numeric_limits<std::size_t>::max() &&
        conservedCount == frame.nx * frame.ny * frame.nz;
    const bool restartSizesMatch =
        legacyArraysMatch || hasFacePack || conservedArraysMatch;
    frame.restart.restartCapable =
        hasRestartConfig && restartSizesMatch &&
        frame.restart.currentTime.has_value() &&
        frame.restart.restartStep.has_value();
    if (hasRestartConfig && !restartSizesMatch) {
        frame.warnings.push_back(
            "RestartData state arrays do not match the visible grid");
    }

    frame.pressureFinite.resize(sampleCount, 0);
    frame.velocityFinite.resize(sampleCount, 0);
    frame.velocityMagnitude.resize(
        sampleCount,
        std::numeric_limits<float>::quiet_NaN());

    // Finite masks, speed and the four ranges in one sweep. Split across cores
    // when the grid is large enough to pay for the hand-off; each thread keeps
    // private counters and ranges and they are merged afterwards, so the result
    // does not depend on how many threads ran.
    std::size_t nonFinitePressureCount = 0;
    std::size_t nonFiniteVelocityCount = 0;
    const float* const pressureData = frame.pressure.data();
    const Velocity* const velocityData = frame.velocity.data();
    const std::uint8_t* const solidData = frame.solid.data();
    std::uint8_t* const pressureFiniteData = frame.pressureFinite.data();
    std::uint8_t* const velocityFiniteData = frame.velocityFinite.data();
    float* const magnitudeData = frame.velocityMagnitude.data();

#if defined(_OPENMP)
#pragma omp parallel if (sampleCount >= PARALLEL_ELEMENT_THRESHOLD)
#endif
    {
        std::size_t localNonFinitePressure = 0;
        std::size_t localNonFiniteVelocity = 0;
        DataRange localPressure;
        DataRange localVelocityX;
        DataRange localVelocityY;
        DataRange localMagnitude;

#if defined(_OPENMP)
#pragma omp for schedule(static) nowait
#endif
        for (std::ptrdiff_t signedIndex = 0;
             signedIndex < static_cast<std::ptrdiff_t>(sampleCount);
             ++signedIndex) {
            const std::size_t index =
                static_cast<std::size_t>(signedIndex);
            const float pressureValue = pressureData[index];
            const bool pressureIsFinite = std::isfinite(pressureValue);
            pressureFiniteData[index] =
                static_cast<std::uint8_t>(pressureIsFinite);
            if (!pressureIsFinite) {
                ++localNonFinitePressure;
            }

            const Velocity velocity = velocityData[index];
            const bool componentsAreFinite =
                std::isfinite(velocity.x) &&
                std::isfinite(velocity.y) &&
                std::isfinite(velocity.z);
            // std::hypot guards against overflow that the plain square root
            // would hit, but it is an order of magnitude slower and the guard
            // only matters near the float limits - so it is kept for those.
            float magnitude = std::numeric_limits<float>::quiet_NaN();
            if (componentsAreFinite) {
                const float ax = std::fabs(velocity.x);
                const float ay = std::fabs(velocity.y);
                const float az = std::fabs(velocity.z);
                constexpr float safeLimit = 1.0e18f;
                magnitude = (ax < safeLimit && ay < safeLimit && az < safeLimit)
                    ? std::sqrt(ax * ax + ay * ay + az * az)
                    : std::hypot(velocity.x, velocity.y, velocity.z);
            }
            const bool magnitudeIsFinite = std::isfinite(magnitude);
            if (magnitudeIsFinite) {
                magnitudeData[index] = magnitude;
            }
            velocityFiniteData[index] = static_cast<std::uint8_t>(
                componentsAreFinite && magnitudeIsFinite);
            if (!componentsAreFinite || !magnitudeIsFinite) {
                ++localNonFiniteVelocity;
            }

            if (solidData[index] != 0) {
                continue;
            }
            if (pressureIsFinite) {
                includeValue(localPressure, pressureValue);
            }
            includeValue(localVelocityX, velocity.x);
            includeValue(localVelocityY, velocity.y);
            if (magnitudeIsFinite) {
                includeValue(localMagnitude, magnitude);
            }
        }

#if defined(_OPENMP)
#pragma omp critical(vtkFrameReduce)
#endif
        {
            nonFinitePressureCount += localNonFinitePressure;
            nonFiniteVelocityCount += localNonFiniteVelocity;
            if (localPressure.available) {
                includeValue(frame.pressureRange, localPressure.minimum);
                includeValue(frame.pressureRange, localPressure.maximum);
            }
            if (localVelocityX.available) {
                includeValue(frame.velocityXRange, localVelocityX.minimum);
                includeValue(frame.velocityXRange, localVelocityX.maximum);
            }
            if (localVelocityY.available) {
                includeValue(frame.velocityYRange, localVelocityY.minimum);
                includeValue(frame.velocityYRange, localVelocityY.maximum);
            }
            if (localMagnitude.available) {
                includeValue(
                    frame.velocityMagnitudeRange, localMagnitude.minimum);
                includeValue(
                    frame.velocityMagnitudeRange, localMagnitude.maximum);
            }
        }
    }

    // The trimmed ranges: the same fields with the outermost half-percent at
    // each end left out.
    //
    // A colour scale stretched by a handful of cells - a stagnation point, the
    // first step of an impulsive start, one frame that caught the solver
    // mid-transient - maps everything else onto two or three shades and the
    // picture goes flat. These give the view something to fall back on that
    // still covers 99% of what is actually in the frame, and they are computed
    // here, once, because doing it per redraw over a million cells is not free.
    {
        std::vector<float> pressureSamples;
        std::vector<float> magnitudeSamples;
        pressureSamples.reserve(sampleCount);
        magnitudeSamples.reserve(sampleCount);
        for (std::size_t index = 0; index < sampleCount; ++index) {
            if (solidData[index] != 0) {
                continue;
            }
            if (pressureFiniteData[index] != 0) {
                pressureSamples.push_back(pressureData[index]);
            }
            if (velocityFiniteData[index] != 0) {
                magnitudeSamples.push_back(magnitudeData[index]);
            }
        }

        frame.pressureTrimmedRange = trimmedRange(pressureSamples);
        frame.velocityMagnitudeTrimmedRange = trimmedRange(magnitudeSamples);

        for (const std::string& name : frame.scalarNames) {
            const std::vector<float>& values = frame.scalars.at(name);
            DataRange full;
            std::vector<float> finite;
            finite.reserve(values.size());
            for (float value : values) {
                if (!std::isfinite(value))
                    continue;
                includeValue(full, value);
                finite.push_back(value);
            }
            frame.scalarRanges[name] = full;
            frame.scalarTrimmedRanges[name] = trimmedRange(finite);
        }
    }

    if (nonFinitePressureCount != 0) {
        frame.warnings.push_back(
            std::to_string(nonFinitePressureCount) +
            " pressure sample(s) are non-finite");
    }
    if (nonFiniteVelocityCount != 0) {
        frame.warnings.push_back(
            std::to_string(nonFiniteVelocityCount) +
            " velocity sample(s) are non-finite");
    }

    if (frame.frameNumber < 0) {
        frame.warnings.push_back(
            "Filename does not match a solver solution frame");
    } else {
        const std::optional<int> titleStep = titleFrameNumber(title);
        if (titleStep && *titleStep != frame.frameNumber) {
            frame.warnings.push_back(
                "Title step does not match filename step");
        }
    }

    return frame;
}

std::optional<int> VtkFrameParser::frameNumberFromFilename(
    const std::filesystem::path& path) {
    static const std::regex pattern(
        R"(^solution_(?:[0-9]+_)*([0-9]+)\.vtk$)");
    std::smatch match;
    const std::string filename = path.filename().string();
    if (!std::regex_match(filename, match, pattern)) {
        return std::nullopt;
    }
    try {
        const long long value = std::stoll(match[1].str());
        if (value > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return static_cast<int>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::filesystem::path> VtkFrameParser::discoverFrames(
    const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        throw VtkParseError(
            "Frame directory does not exist: " + directory.string());
    }

    std::vector<std::pair<int, std::filesystem::path>> numberedPaths;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::optional<int> number =
            frameNumberFromFilename(entry.path());
        if (number) {
            numberedPaths.emplace_back(*number, entry.path());
        }
    }

    std::sort(
        numberedPaths.begin(),
        numberedPaths.end(),
        [](const auto& first, const auto& second) {
            if (first.first != second.first) {
                return first.first < second.first;
            }
            return first.second.string() < second.second.string();
        });

    for (std::size_t index = 1; index < numberedPaths.size(); ++index) {
        if (numberedPaths[index - 1].first == numberedPaths[index].first) {
            throw VtkParseError(
                "Duplicate VTK frame number: " +
                std::to_string(numberedPaths[index].first));
        }
    }

    std::vector<std::filesystem::path> paths;
    paths.reserve(numberedPaths.size());
    for (const auto& numberedPath : numberedPaths) {
        paths.push_back(numberedPath.second);
    }
    return paths;
}

namespace {

bool sameSeriesLayout(
    const VtkFrame& reference,
    const VtkFrame& frame) {
    return frame.association == reference.association &&
        frame.nx == reference.nx &&
        frame.ny == reference.ny &&
        frame.nz == reference.nz &&
        frame.originX == reference.originX &&
        frame.originY == reference.originY &&
        frame.originZ == reference.originZ &&
        frame.faceX == reference.faceX &&
        frame.faceY == reference.faceY &&
        frame.faceZ == reference.faceZ &&
        frame.spacingX == reference.spacingX &&
        frame.spacingY == reference.spacingY &&
        frame.spacingZ == reference.spacingZ &&
        frame.solid.size() == reference.solid.size();
}

void includeRange(DataRange& combined, const DataRange& range) {
    if (!range.available) {
        return;
    }
    if (!combined.available) {
        combined = range;
        return;
    }
    combined.minimum = std::min(combined.minimum, range.minimum);
    combined.maximum = std::max(combined.maximum, range.maximum);
}

VtkFrame layoutOf(const VtkFrame& frame) {
    VtkFrame layout;
    layout.association = frame.association;
    layout.nx = frame.nx;
    layout.ny = frame.ny;
    layout.nz = frame.nz;
    layout.originX = frame.originX;
    layout.originY = frame.originY;
    layout.originZ = frame.originZ;
    layout.faceX = frame.faceX;
    layout.faceY = frame.faceY;
    layout.faceZ = frame.faceZ;
    layout.spacingX = frame.spacingX;
    layout.spacingY = frame.spacingY;
    layout.spacingZ = frame.spacingZ;
    layout.solid = frame.solid;
    return layout;
}

} // namespace


std::vector<VtkFrame> VtkFrameParser::parseSeries(
    const std::vector<std::filesystem::path>& paths) {
    if (paths.empty()) {
        throw VtkParseError("No VTK frame files were selected");
    }

    std::vector<std::pair<int, std::filesystem::path>> numberedPaths;
    numberedPaths.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            throw VtkParseError(
                "VTK frame file does not exist: " + path.string());
        }
        const std::optional<int> number = frameNumberFromFilename(path);
        if (!number) {
            throw VtkParseError(
                "Expected a solver solution filename: " +
                path.filename().string());
        }
        numberedPaths.emplace_back(*number, path);
    }

    std::sort(
        numberedPaths.begin(),
        numberedPaths.end(),
        [](const auto& first, const auto& second) {
            if (first.first != second.first) {
                return first.first < second.first;
            }
            return first.second.string() < second.second.string();
        });

    for (std::size_t index = 1; index < numberedPaths.size(); ++index) {
        if (numberedPaths[index - 1].first == numberedPaths[index].first) {
            throw VtkParseError(
                "Duplicate VTK frame number: " +
                std::to_string(numberedPaths[index].first));
        }
    }

    std::vector<VtkFrame> frames;
    frames.reserve(numberedPaths.size());
    for (const auto& numberedPath : numberedPaths) {
        frames.push_back(parse(numberedPath.second));
    }
    validateSeries(frames);
    return frames;
}

VtkSeriesLoadResult VtkFrameParser::parseRecoverableSeries(
    const std::vector<std::filesystem::path>& paths) {
    VtkSeriesLoadResult result;
    std::vector<std::pair<int, std::filesystem::path>> numberedPaths;
    numberedPaths.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            result.rejected.push_back(
                path.filename().string() +
                ": file does not exist or is not regular");
            continue;
        }
        const std::optional<int> number = frameNumberFromFilename(path);
        if (!number) {
            result.rejected.push_back(
                path.filename().string() +
                ": expected a solver solution filename");
            continue;
        }
        numberedPaths.emplace_back(*number, path);
    }

    std::sort(
        numberedPaths.begin(),
        numberedPaths.end(),
        [](const auto& first, const auto& second) {
            if (first.first != second.first) {
                return first.first < second.first;
            }
            return first.second.string() < second.second.string();
        });

    result.frames.reserve(numberedPaths.size());
    for (std::size_t index = 0; index < numberedPaths.size(); ++index) {
        const auto& numberedPath = numberedPaths[index];
        if (index != 0 &&
            numberedPaths[index - 1].first == numberedPath.first) {
            result.rejected.push_back(
                numberedPath.second.filename().string() +
                ": duplicate solver step " +
                std::to_string(numberedPath.first));
            continue;
        }
        try {
            VtkFrame frame = parse(numberedPath.second);
            if (!result.frames.empty() &&
                !sameSeriesLayout(result.frames.front(), frame)) {
                result.rejected.push_back(
                    numberedPath.second.filename().string() +
                    ": VTK frame series changes association, grid, or mask");
                continue;
            }
            result.frames.push_back(std::move(frame));
        } catch (const std::exception& exception) {
            result.rejected.push_back(
                numberedPath.second.filename().string() + ": " +
                exception.what());
        }
    }
    return result;
}

VtkSeriesCatalog VtkFrameParser::catalogSeries(
    const std::vector<std::filesystem::path>& paths,
    bool recoverable) {
    if (paths.empty()) {
        throw VtkParseError("No VTK frame files were selected");
    }

    VtkSeriesCatalog catalog;
    std::vector<std::pair<int, std::filesystem::path>> numberedPaths;
    numberedPaths.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            if (!recoverable) {
                throw VtkParseError(
                    "VTK frame file does not exist: " + path.string());
            }
            catalog.rejected.push_back(
                path.filename().string() +
                ": file does not exist or is not regular");
            continue;
        }
        const std::optional<int> number = frameNumberFromFilename(path);
        if (!number) {
            if (!recoverable) {
                throw VtkParseError(
                    "Expected a solver solution filename: " +
                    path.filename().string());
            }
            catalog.rejected.push_back(
                path.filename().string() +
                ": expected a solver solution filename");
            continue;
        }
        numberedPaths.emplace_back(*number, path);
    }

    std::sort(
        numberedPaths.begin(),
        numberedPaths.end(),
        [](const auto& first, const auto& second) {
            if (first.first != second.first) {
                return first.first < second.first;
            }
            return first.second.string() < second.second.string();
        });

    std::optional<VtkFrame> referenceLayout;
    for (std::size_t index = 0; index < numberedPaths.size(); ++index) {
        const auto& numberedPath = numberedPaths[index];
        if (index != 0 &&
            numberedPaths[index - 1].first == numberedPath.first) {
            if (!recoverable) {
                throw VtkParseError(
                    "Duplicate VTK frame number: " +
                    std::to_string(numberedPath.first));
            }
            catalog.rejected.push_back(
                numberedPath.second.filename().string() +
                ": duplicate solver step " +
                std::to_string(numberedPath.first));
            continue;
        }
        try {
            VtkFrame frame = parse(numberedPath.second);
            if (!referenceLayout) {
                referenceLayout = layoutOf(frame);
            } else if (!sameSeriesLayout(*referenceLayout, frame)) {
                throw VtkParseError(
                    "VTK frame series changes association, grid, or mask");
            }
            includeRange(catalog.pressureRange, frame.pressureRange);
            includeRange(
                catalog.pressureTrimmedRange, frame.pressureTrimmedRange);
            includeRange(
                catalog.velocityMagnitudeTrimmedRange,
                frame.velocityMagnitudeTrimmedRange);
            includeRange(
                catalog.velocityMagnitudeRange,
                frame.velocityMagnitudeRange);
            catalog.warningCount += frame.warnings.size();
            catalog.frames.push_back({
                frame.sourcePath,
                frame.frameNumber,
                frame.warnings.size()
            });
            if (index + 1u == numberedPaths.size()) {
                catalog.activeFrame = std::move(frame);
            }
        } catch (const std::exception& exception) {
            if (!recoverable) {
                throw;
            }
            catalog.rejected.push_back(
                numberedPath.second.filename().string() + ": " +
                exception.what());
        }
    }

    if (catalog.frames.empty()) {
        return catalog;
    }
    if (catalog.activeFrame.sourcePath != catalog.frames.back().sourcePath) {
        catalog.activeFrame = parse(catalog.frames.back().sourcePath);
        if (!referenceLayout ||
            !sameSeriesLayout(*referenceLayout, catalog.activeFrame)) {
            throw VtkParseError(
                "VTK frame changed while the series was being cataloged");
        }
    }
    return catalog;
}

VtkSeriesCatalog VtkFrameParser::indexSeries(
    const std::vector<std::filesystem::path>& paths,
    bool recoverable) {
    if (paths.empty()) {
        throw VtkParseError("No VTK frame files were selected");
    }

    VtkSeriesCatalog catalog;
    std::vector<std::pair<int, std::filesystem::path>> numberedPaths;
    numberedPaths.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            if (!recoverable) {
                throw VtkParseError(
                    "VTK frame file does not exist: " + path.string());
            }
            catalog.rejected.push_back(
                path.filename().string() +
                ": file does not exist or is not regular");
            continue;
        }
        const std::optional<int> number = frameNumberFromFilename(path);
        if (!number) {
            if (!recoverable) {
                throw VtkParseError(
                    "Expected a solver solution filename: " +
                    path.filename().string());
            }
            catalog.rejected.push_back(
                path.filename().string() +
                ": expected a solver solution filename");
            continue;
        }
        numberedPaths.emplace_back(*number, path);
    }

    std::sort(
        numberedPaths.begin(),
        numberedPaths.end(),
        [](const auto& first, const auto& second) {
            if (first.first != second.first) {
                return first.first < second.first;
            }
            return first.second.string() < second.second.string();
        });

    for (std::size_t index = 0; index < numberedPaths.size(); ++index) {
        const auto& numberedPath = numberedPaths[index];
        if (index != 0 &&
            numberedPaths[index - 1].first == numberedPath.first) {
            if (!recoverable) {
                throw VtkParseError(
                    "Duplicate VTK frame number: " +
                    std::to_string(numberedPath.first));
            }
            catalog.rejected.push_back(
                numberedPath.second.filename().string() +
                ": duplicate solver step " +
                std::to_string(numberedPath.first));
            continue;
        }
        catalog.frames.push_back({
            numberedPath.second,
            numberedPath.first,
            0
        });
    }

    // Only the initially displayed frame is decoded. Earlier frames are
    // validated on demand so indexing cost does not scale with dataset bytes.
    while (!catalog.frames.empty()) {
        VtkFrameDescriptor& descriptor = catalog.frames.back();
        try {
            catalog.activeFrame = parse(descriptor.sourcePath);
            descriptor.warningCount = catalog.activeFrame.warnings.size();
            catalog.warningCount = descriptor.warningCount;
            catalog.pressureRange = catalog.activeFrame.pressureRange;
            catalog.velocityMagnitudeRange =
                catalog.activeFrame.velocityMagnitudeRange;
            catalog.pressureTrimmedRange =
                catalog.activeFrame.pressureTrimmedRange;
            catalog.velocityMagnitudeTrimmedRange =
                catalog.activeFrame.velocityMagnitudeTrimmedRange;
            break;
        } catch (const std::exception& exception) {
            if (!recoverable) {
                throw;
            }
            catalog.rejected.push_back(
                descriptor.sourcePath.filename().string() + ": " +
                exception.what());
            catalog.frames.pop_back();
        }
    }
    return catalog;
}

void VtkFrameParser::validateCompatibility(
    const VtkFrame& reference,
    const VtkFrame& frame) {
    if (!sameSeriesLayout(reference, frame)) {
        throw VtkParseError(
            "VTK frame series changes association, grid, or solid mask");
    }
}

void VtkFrameParser::validateSeries(
    const std::vector<VtkFrame>& frames) {
    if (frames.empty()) {
        return;
    }
    const VtkFrame& reference = frames.front();
    for (std::size_t index = 1; index < frames.size(); ++index) {
        const VtkFrame& frame = frames[index];
        validateCompatibility(reference, frame);
    }
}

} // namespace maskui

namespace maskui {

std::size_t VtkFrame::columnAt(double x) const {
    if (nx == 0)
        return 0;
    if (!rectilinear()) {
        if (spacingX <= 0.0)
            return 0;
        const double cell = (x - originX) / spacingX;
        if (cell <= 0.0)
            return 0;
        const std::size_t index = static_cast<std::size_t>(cell);
        return index >= nx ? nx - 1 : index;
    }
    if (x <= faceX.front())
        return 0;
    if (x >= faceX.back())
        return nx - 1;
    const auto found = std::upper_bound(faceX.begin(), faceX.end(), x);
    const std::size_t index =
        static_cast<std::size_t>(found - faceX.begin());
    return index == 0 ? 0 : std::min(nx - 1, index - 1);
}

std::size_t VtkFrame::rowAt(double y) const {
    if (ny == 0)
        return 0;
    if (!rectilinear()) {
        if (spacingY <= 0.0)
            return 0;
        const double cell = (y - originY) / spacingY;
        if (cell <= 0.0)
            return 0;
        const std::size_t index = static_cast<std::size_t>(cell);
        return index >= ny ? ny - 1 : index;
    }
    if (y <= faceY.front())
        return 0;
    if (y >= faceY.back())
        return ny - 1;
    const auto found = std::upper_bound(faceY.begin(), faceY.end(), y);
    const std::size_t index =
        static_cast<std::size_t>(found - faceY.begin());
    return index == 0 ? 0 : std::min(ny - 1, index - 1);
}

std::size_t VtkFrame::planeAt(double z) const {
    if (nz == 0)
        return 0;
    if (faceZ.size() < 2) {
        if (spacingZ <= 0.0)
            return 0;
        const double cell = (z - originZ) / spacingZ;
        if (cell <= 0.0)
            return 0;
        const std::size_t index = static_cast<std::size_t>(cell);
        return index >= nz ? nz - 1 : index;
    }
    if (z <= faceZ.front())
        return 0;
    if (z >= faceZ.back())
        return nz - 1;
    const auto found = std::upper_bound(faceZ.begin(), faceZ.end(), z);
    const std::size_t index =
        static_cast<std::size_t>(found - faceZ.begin());
    return index == 0 ? 0 : std::min(nz - 1, index - 1);
}

} // namespace maskui
