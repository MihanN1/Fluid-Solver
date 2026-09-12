#include "Restart.hpp"
#include "AppPaths.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef _WIN32
// windows.h defines min and max as macros, which eats std::min in readSolid
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
// Same 16 KB stack buffer as the writer, for the same reason: no heap
// allocation and no per-value stream calls on arrays that are megabytes wide
constexpr size_t BUFFER_WORDS = 4096;

// Legacy VTK binary data is big endian; this is the writer's swap read backwards
inline uint32_t swapWord(uint32_t x) {
    return
        ((x & 0x000000FFu) << 24) |
        ((x & 0x0000FF00u) << 8 ) |
        ((x & 0x00FF0000u) >> 8 ) |
        ((x & 0xFF000000u) >> 24);
}

std::string trimCR(std::string line) {
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return line;
}

// A binary payload starts right after the newline that ends its declaration
bool skipToPayload(std::istream& in) {
    char c = '\0';
    while (in.get(c)) {
        if (c == '\n')
            return true;
    }
    return false;
}

bool skipBytes(std::istream& in, std::streamoff count) {
    return static_cast<bool>(in.seekg(count, std::ios::cur));
}

// Reads straight into the destination and swaps in place: the array is never
// copied and never passes through a temporary
bool readFloats(std::istream& in, std::vector<float>& dst, size_t count) {
    dst.resize(count);
    const std::streamsize bytes =
        static_cast<std::streamsize>(count * sizeof(float));
    in.read(reinterpret_cast<char*>(dst.data()), bytes);
    if (in.gcount() != bytes)
        return false;

    for (size_t k = 0; k < count; ++k) {
        uint32_t word;
        std::memcpy(&word, &dst[k], sizeof(word));
        word = swapWord(word);
        std::memcpy(&dst[k], &word, sizeof(word));
    }
    return true;
}

// solid is one byte in memory. Frames written now spell it out as one byte on
// disk as well and take the first branch; older ones spent an int32 per cell,
// and those are streamed through the fixed buffer instead of allocating a
// second full-size array.
bool readSolid(std::istream& in, std::vector<uint8_t>& dst, size_t count,
               size_t width) {
    dst.resize(count);

    if (width == 1) {
        const std::streamsize bytes = static_cast<std::streamsize>(count);
        in.read(reinterpret_cast<char*>(dst.data()), bytes);
        if (in.gcount() != bytes)
            return false;
        for (size_t k = 0; k < count; ++k)
            dst[k] = dst[k] ? 1 : 0;
        return true;
    }

    std::array<uint32_t, BUFFER_WORDS> buffer;
    size_t done = 0;

    while (done < count) {
        const size_t chunk = std::min(BUFFER_WORDS, count - done);
        const std::streamsize bytes =
            static_cast<std::streamsize>(chunk * sizeof(uint32_t));
        in.read(reinterpret_cast<char*>(buffer.data()), bytes);
        if (in.gcount() != bytes)
            return false;

        for (size_t k = 0; k < chunk; ++k)
            dst[done + k] = swapWord(buffer[k]) ? 1 : 0;

        done += chunk;
    }
    return true;
}

// --- packed face velocities ------------------------------------------------

inline uint32_t floatBits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float bitsToFloat(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The prediction, and the one line the writer and the reader must agree on to
// the last bit.
//
// It is done in double on purpose. In float, 2.0f*cell overflows to infinity
// for |cell| above 1.7e38 and the result is an infinity - unless the compiler
// contracts the expression into an FMA, which keeps the exact product and gives
// a finite answer instead. CMakeLists hands -mfma to the AVX2 build and not to
// the scalar one, so the two builds disagreed on exactly those values and a
// frame written by one would not unpack in the other. Doubling a float is exact
// in double whatever the compiler does with it, and the single conversion back
// rounds once, so every build agrees again.
inline float predictNextFace(float cell, float previous) {
    return static_cast<float>(2.0 * static_cast<double>(cell) -
                              static_cast<double>(previous));
}

bool takeWord(const std::string& in, size_t& pos, uint32_t& word) {
    if (pos + 4 > in.size())
        return false;
    word = (static_cast<uint32_t>(static_cast<unsigned char>(in[pos])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(in[pos + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(in[pos + 2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(in[pos + 3]));
    pos += 4;
    return true;
}

bool takeDelta(const std::string& in, size_t& pos, uint32_t& delta) {
    uint32_t zig = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (pos >= in.size())
            return false;
        const uint32_t byte = static_cast<unsigned char>(in[pos++]);
        zig |= (byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            const uint32_t sign = (zig & 1u) ? 0xFFFFFFFFu : 0u;
            delta = (zig >> 1) ^ sign;
            return true;
        }
    }
    return false;
}

// FNV-1a over the bit patterns in a fixed byte order, so the frame carries the
// same checksum wherever it was written
uint32_t hashFaces(const std::vector<float>& values, uint32_t hash) {
    for (float value : values) {
        const uint32_t bits = floatBits(value);
        for (int shift = 24; shift >= 0; shift -= 8) {
            hash ^= (bits >> shift) & 0xFFu;
            hash *= 16777619u;
        }
    }
    return hash;
}

bool parseBodyState(const std::string& entry, RestartData::BodyState& state) {
    const size_t colon = entry.find(':');
    if (colon == std::string::npos)
        return false;

    const std::string fields = entry.substr(colon + 1);
    if (fields.find('=') == std::string::npos) {
        double theta = 0.0;
        const int read = std::sscanf(
            entry.c_str(), "%d:%lf,%lf,%lf,%f,%f,%f",
            &state.object, &state.x, &state.y, &theta,
            &state.vx, &state.vy, &state.omega);
        if (read != 7)
            return false;
        state.qw = std::cos(0.5 * theta);
        state.qz = std::sin(0.5 * theta);
        return true;
    }

    state.object = std::atoi(entry.c_str());
    for (size_t pos = 0; pos < fields.size();) {
        size_t end = fields.find(',', pos);
        if (end == std::string::npos)
            end = fields.size();
        const std::string field = fields.substr(pos, end - pos);
        pos = end + 1;
        const size_t eq = field.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;

        const std::string name = field.substr(0, eq);
        const char* text = field.c_str() + eq + 1;
        if (name == "x")
            state.x = std::strtod(text, nullptr);
        else if (name == "y")
            state.y = std::strtod(text, nullptr);
        else if (name == "z")
            state.z = std::strtod(text, nullptr);
        else if (name == "qw")
            state.qw = std::strtod(text, nullptr);
        else if (name == "qx")
            state.qx = std::strtod(text, nullptr);
        else if (name == "qy")
            state.qy = std::strtod(text, nullptr);
        else if (name == "qz")
            state.qz = std::strtod(text, nullptr);
        else if (name == "vx")
            state.vx = std::strtof(text, nullptr);
        else if (name == "vy")
            state.vy = std::strtof(text, nullptr);
        else if (name == "vz")
            state.vz = std::strtof(text, nullptr);
        else if (name == "omegaX")
            state.omegaX = std::strtof(text, nullptr);
        else if (name == "omegaY")
            state.omegaY = std::strtof(text, nullptr);
        else if (name == "omega")
            state.omega = std::strtof(text, nullptr);
    }
    return true;
}

bool fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}
}

std::filesystem::path narrowToPath(const std::string& text) {
#ifdef _WIN32
    // Drag and drop, tab completion and copy-paste all like to leave a
    // separator behind, which turns the path into "a folder whose file name
    // is empty" and makes it stop existing
    std::string trimmed = text;
    while (trimmed.size() > 1 &&
           (trimmed.back() == '\\' || trimmed.back() == '/') &&
           trimmed[trimmed.size() - 2] != ':') {
        trimmed.pop_back();
    }

    std::filesystem::path fallback;
    for (UINT cp : {GetConsoleCP(), static_cast<UINT>(CP_ACP),
                    static_cast<UINT>(CP_UTF8)}) {
        if (cp == 0)
            continue;
        const int n = MultiByteToWideChar(cp, 0, trimmed.c_str(),
                                          static_cast<int>(trimmed.size()),
                                          nullptr, 0);
        if (n <= 0)
            continue;
        std::wstring wide(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(cp, 0, trimmed.c_str(),
                            static_cast<int>(trimmed.size()), &wide[0], n);
        const std::filesystem::path candidate(wide);
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec)
            return candidate;
        // Nothing exists under any of them (a directory about to be created,
        // say), so the console page wins: that is where the string came from
        if (fallback.empty())
            fallback = candidate;
    }
    return fallback.empty() ? std::filesystem::path(trimmed) : fallback;
#else
    return std::filesystem::path(text);
#endif
}

std::string pathToConsole(const std::filesystem::path& path) {
#ifdef _WIN32
    // path::string() would re-encode to the ANSI page and print mojibake in a
    // console running 866, and it throws outright on anything ANSI cannot
    // represent. Going out through the console page does neither.
    const std::wstring wide = path.wstring();
    if (wide.empty())
        return {};
    UINT cp = GetConsoleOutputCP();
    if (cp == 0)
        cp = CP_ACP;
    const int n = WideCharToMultiByte(cp, 0, wide.c_str(),
                                      static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string narrow(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(cp, 0, wide.c_str(), static_cast<int>(wide.size()),
                        &narrow[0], n, nullptr, nullptr);
    return narrow;
#else
    return path.string();
#endif
}

std::filesystem::path resolveRestartPath(const std::string& path,
                                         std::string& error) {
    error.clear();
    if (path.empty()) {
        error = "restartFile is empty. Point it at a .vtk frame or the folder "
                "that holds them.";
        return {};
    }

    std::filesystem::path given = narrowToPath(path);

    // "restartFile=output" means the folder the frames were written to, and
    // those now sit beside the executable rather than in whatever directory
    // the program was started from. An absolute path, and a relative one that
    // does resolve against the working directory, are both left alone.
    {
        std::error_code exists;
        if (given.is_relative() && !std::filesystem::exists(given, exists)) {
            const std::filesystem::path beside = executableDir() / given;
            if (std::filesystem::exists(beside, exists))
                given = beside;
        }
    }
    std::error_code ec;

    // "restartFile=output" means the folder the frames were written to, and
    // those now sit beside the executable rather than in whatever directory
    // the program was started from. An absolute path, and a relative one that
    // does resolve against the working directory, are both left alone.
    if (given.is_relative() && !std::filesystem::exists(given, ec)) {
        const std::filesystem::path beside = executableDir() / given;
        if (std::filesystem::exists(beside, ec))
            given = beside;
    }

    if (std::filesystem::is_directory(given, ec)) {
        std::filesystem::path newest;
        std::filesystem::file_time_type newestTime{};
        long long newestStep = -1;

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(given, ec)) {
            if (!entry.is_regular_file(ec))
                continue;
            if (lowerExtension(entry.path()) != ".vtk")
                continue;

            const std::filesystem::file_time_type written =
                entry.last_write_time(ec);
            if (ec)
                continue;

            // A short run writes its whole output inside one second and every
            // frame ends up with the same timestamp, after which the folder
            // order decides which one continues - which is to say, nothing
            // does. The step in the name breaks the tie.
            const std::string stem = entry.path().stem().string();
            const size_t underscore = stem.find_last_of('_');
            long long stepInName = -1;
            if (underscore != std::string::npos &&
                underscore + 1 < stem.size())
                stepInName = std::atoll(stem.c_str() + underscore + 1);

            if (newest.empty() ||
                written > newestTime ||
                (written == newestTime && stepInName > newestStep)) {
                newest = entry.path();
                newestTime = written;
                newestStep = stepInName;
            }
        }

        if (newest.empty()) {
            error = "No .vtk frames in " + pathToConsole(given);
            return {};
        }
        return newest;
    }

    if (!std::filesystem::is_regular_file(given, ec)) {
        error = "Not a file or a folder: " + pathToConsole(given);
        return {};
    }
    return given;
}

std::string packFaceVelocities(int nx, int ny, int nz,
                               const std::vector<float>& u,
                               const std::vector<float>& v,
                               const std::vector<float>& w,
                               const std::vector<float>& uCell,
                               const std::vector<float>& vCell,
                               const std::vector<float>& wCell) {
    const size_t cells = static_cast<size_t>(nx) * ny * nz;
    if (nx < 1 || ny < 1 || nz < 1 ||
        u.size() != static_cast<size_t>(nx + 1) * ny * nz ||
        v.size() != static_cast<size_t>(nx) * (ny + 1) * nz ||
        w.size() != static_cast<size_t>(nx) * ny * (nz + 1) ||
        uCell.size() != cells || vCell.size() != cells || wCell.size() != cells)
        return std::string();

    const size_t seeds = static_cast<size_t>(ny) * nz +
                         static_cast<size_t>(nx) * nz +
                         static_cast<size_t>(nx) * ny;

    std::string out;
    out.resize(4u + 4u * seeds + cells * 15u);
    char* cursor = out.data();

    const auto putWord = [&](uint32_t word) {
        *cursor++ = static_cast<char>((word >> 24) & 0xFFu);
        *cursor++ = static_cast<char>((word >> 16) & 0xFFu);
        *cursor++ = static_cast<char>((word >> 8) & 0xFFu);
        *cursor++ = static_cast<char>(word & 0xFFu);
    };
    const auto putDelta = [&](uint32_t delta) {
        const uint32_t sign = (delta & 0x80000000u) ? 0xFFFFFFFFu : 0u;
        uint32_t zig = (delta << 1) ^ sign;
        while (zig >= 0x80u) {
            *cursor++ = static_cast<char>((zig & 0x7Fu) | 0x80u);
            zig >>= 7;
        }
        *cursor++ = static_cast<char>(zig);
    };

    uint32_t checksum = 2166136261u;
    checksum = hashFaces(u, checksum);
    checksum = hashFaces(v, checksum);
    checksum = hashFaces(w, checksum);
    putWord(checksum);

    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            putWord(floatBits(
                u[(static_cast<size_t>(k) * ny + j) * (nx + 1)]));
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i)
            putWord(floatBits(
                v[static_cast<size_t>(k) * (ny + 1) * nx + i]));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            putWord(floatBits(w[static_cast<size_t>(j) * nx + i]));

    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j) {
            const size_t rowU = (static_cast<size_t>(k) * ny + j) * (nx + 1);
            const size_t rowC = (static_cast<size_t>(k) * ny + j) * nx;
            float previous = u[rowU];
            for (int i = 0; i < nx; ++i) {
                const float predicted =
                    predictNextFace(uCell[rowC + i], previous);
                previous = u[rowU + i + 1];
                putDelta(floatBits(previous) - floatBits(predicted));
            }
        }

    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j) {
            const size_t rowV = (static_cast<size_t>(k) * (ny + 1) + j) * nx;
            const size_t rowC = (static_cast<size_t>(k) * ny + j) * nx;
            for (int i = 0; i < nx; ++i) {
                const float predicted =
                    predictNextFace(vCell[rowC + i], v[rowV + i]);
                putDelta(floatBits(v[rowV + nx + i]) - floatBits(predicted));
            }
        }

    const size_t planeW = static_cast<size_t>(nx) * ny;
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j) {
            const size_t rowW = (static_cast<size_t>(k) * ny + j) * nx;
            for (int i = 0; i < nx; ++i) {
                const float predicted =
                    predictNextFace(wCell[rowW + i], w[rowW + i]);
                putDelta(floatBits(w[rowW + planeW + i]) -
                         floatBits(predicted));
            }
        }

    out.resize(static_cast<size_t>(cursor - out.data()));
    return out;
}

namespace {
// Fails with the outputs emptied, always. loadRestart decides whether a frame
// is an exact restart by the size of these three, so leaving a half filled
// array of the right length behind is the one way to make a truncated block
// look like a complete one - which is exactly what it used to do.
bool unpackFailed(std::vector<float>& u, std::vector<float>& v,
                  std::vector<float>& w) {
    u.clear();
    v.clear();
    w.clear();
    return false;
}

bool unpackBlock(int nx, int ny, int nz, bool withW,
                 const std::string& packed,
                 const std::vector<float>& uCell,
                 const std::vector<float>& vCell,
                 const std::vector<float>& wCell,
                 std::vector<float>& u,
                 std::vector<float>& v,
                 std::vector<float>& w) {
    u.assign(static_cast<size_t>(nx + 1) * ny * nz, 0.0f);
    v.assign(static_cast<size_t>(nx) * (ny + 1) * nz, 0.0f);
    w.assign(static_cast<size_t>(nx) * ny * (nz + 1), 0.0f);

    size_t pos = 0;
    uint32_t stored = 0;
    if (!takeWord(packed, pos, stored))
        return false;

    uint32_t bits = 0;
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j) {
            if (!takeWord(packed, pos, bits))
                return false;
            u[(static_cast<size_t>(k) * ny + j) * (nx + 1)] =
                bitsToFloat(bits);
        }
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i) {
            if (!takeWord(packed, pos, bits))
                return false;
            v[static_cast<size_t>(k) * (ny + 1) * nx + i] = bitsToFloat(bits);
        }
    if (withW)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                if (!takeWord(packed, pos, bits))
                    return false;
                w[static_cast<size_t>(j) * nx + i] = bitsToFloat(bits);
            }

    // Every face is corrected by its own delta before the next one is predicted
    // from it, so nothing accumulates along the march
    uint32_t delta = 0;
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j) {
            const size_t rowU = (static_cast<size_t>(k) * ny + j) * (nx + 1);
            const size_t rowC = (static_cast<size_t>(k) * ny + j) * nx;
            float previous = u[rowU];
            for (int i = 0; i < nx; ++i) {
                if (!takeDelta(packed, pos, delta))
                    return false;
                const float predicted =
                    predictNextFace(uCell[rowC + i], previous);
                previous = bitsToFloat(floatBits(predicted) + delta);
                u[rowU + i + 1] = previous;
            }
        }

    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j) {
            const size_t rowV = (static_cast<size_t>(k) * (ny + 1) + j) * nx;
            const size_t rowC = (static_cast<size_t>(k) * ny + j) * nx;
            for (int i = 0; i < nx; ++i) {
                if (!takeDelta(packed, pos, delta))
                    return false;
                const float predicted =
                    predictNextFace(vCell[rowC + i], v[rowV + i]);
                v[rowV + nx + i] = bitsToFloat(floatBits(predicted) + delta);
            }
        }

    if (withW) {
        const size_t planeW = static_cast<size_t>(nx) * ny;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j) {
                const size_t rowW = (static_cast<size_t>(k) * ny + j) * nx;
                for (int i = 0; i < nx; ++i) {
                    if (!takeDelta(packed, pos, delta))
                        return false;
                    const float predicted =
                        predictNextFace(wCell[rowW + i], w[rowW + i]);
                    w[rowW + planeW + i] =
                        bitsToFloat(floatBits(predicted) + delta);
                }
            }
    }

    uint32_t checksum = 2166136261u;
    checksum = hashFaces(u, checksum);
    checksum = hashFaces(v, checksum);
    if (withW)
        checksum = hashFaces(w, checksum);
    return checksum == stored;
}
}

bool unpackFaceVelocities(int nx, int ny, int nz,
                          const std::string& packed,
                          const std::vector<float>& uCell,
                          const std::vector<float>& vCell,
                          const std::vector<float>& wCell,
                          std::vector<float>& u,
                          std::vector<float>& v,
                          std::vector<float>& w) {
    const size_t cells = static_cast<size_t>(nx) * ny * nz;
    if (nx < 1 || ny < 1 || nz < 1 || uCell.size() != cells ||
        vCell.size() != cells || wCell.size() != cells)
        return unpackFailed(u, v, w);

    if (unpackBlock(nx, ny, nz, true, packed, uCell, vCell, wCell, u, v, w))
        return true;
    if (nz == 1 &&
        unpackBlock(nx, ny, nz, false, packed, uCell, vCell, wCell, u, v, w))
        return true;
    return unpackFailed(u, v, w);
}

bool loadRestart(const std::filesystem::path& file,
                 RestartData& out,
                 std::string& error) {
    error.clear();

    std::ifstream fin(file, std::ios::binary);
    if (!fin)
        return fail(error, "Cannot open " + pathToConsole(file));

    std::string version, title, encoding;
    if (!std::getline(fin, version) ||
        !std::getline(fin, title) ||
        !std::getline(fin, encoding))
        return fail(error, "Not a legacy VTK file: " + pathToConsole(file));

    if (trimCR(version).rfind("# vtk DataFile Version", 0) != 0)
        return fail(error, "Unsupported VTK header in " + pathToConsole(file));
    if (trimCR(encoding) != "BINARY")
        return fail(error, "Only BINARY legacy VTK frames can be continued");

    int pointNx = 0, pointNy = 0, pointNz = 0;
    long long declaredCells = -1;
    std::string configText;
    // The cell arrays are what the staggered fields are rebuilt from: exactly,
    // together with facePack, or approximately when a frame carries neither
    // that block nor the uFace/vFace arrays older frames spelled out
    std::string facePack;
    std::vector<float> cellPressure, cellVelocity;

    std::string token;
    while (fin >> token) {
        if (token == "DATASET") {
            fin >> token;
            if (token != "STRUCTURED_POINTS")
                return fail(error, "Only STRUCTURED_POINTS frames are supported");
        } else if (token == "DIMENSIONS") {
            fin >> pointNx >> pointNy >> pointNz;
            out.nx = pointNx - 1;
            out.ny = pointNy - 1;
            out.nz = pointNz > 1 ? pointNz - 1 : 1;
            if (pointNz < 1 || out.nx < 1 || out.ny < 1)
                return fail(error, "Bad DIMENSIONS in " + pathToConsole(file));
        } else if (token == "ORIGIN") {
            double ox, oy, oz;
            fin >> ox >> oy >> oz;
        } else if (token == "SPACING") {
            double sx, sy, sz;
            fin >> sx >> sy >> sz;
            out.dx = static_cast<float>(sx);
            out.dy = static_cast<float>(sy);
            out.dz = static_cast<float>(sz);
        } else if (token == "CELL_DATA") {
            fin >> declaredCells;
            if (declaredCells !=
                static_cast<long long>(out.nx) * out.ny * out.nz)
                return fail(error, "CELL_DATA count does not match DIMENSIONS");
        } else if (token == "POINT_DATA") {
            return fail(error, "POINT_DATA frames cannot seed the solver");
        } else if (token == "SCALARS") {
            std::string name, type, next;
            fin >> name >> type >> next;
            int components = 1;
            if (next != "LOOKUP_TABLE") {
                components = std::atoi(next.c_str());
                fin >> next;
            }
            std::string table;
            fin >> table;
            if (components < 1)
                return fail(error, "Bad component count for SCALARS " + name);
            if (!skipToPayload(fin))
                return fail(error, "Truncated frame before SCALARS " + name);

            const size_t count =
                static_cast<size_t>(out.nx) * out.ny * out.nz * components;
            // solid went from int32 to one byte a cell; everything else in a
            // SCALARS block is still four bytes wide
            const size_t width =
                (type == "unsigned_char" || type == "char") ? 1u : 4u;
            if (name == "solid") {
                if (!readSolid(fin, out.solid, count, width))
                    return fail(error, "Truncated solid array");
            } else if (name == "pressure" && width == 4) {
                if (!readFloats(fin, cellPressure, count))
                    return fail(error, "Truncated pressure array");
            } else if (name == "phase" && width == 4) {
                if (!readFloats(fin, out.phase, count))
                    return fail(error, "Truncated phase array");
            } else if (width == 4 && components == 1) {
                std::vector<float> values;
                if (!readFloats(fin, values, count))
                    return fail(error, "Truncated SCALARS " + name);
                out.extras.push_back({name, std::move(values)});
            } else if (!skipBytes(fin,
                                  static_cast<std::streamoff>(count * width))) {
                return fail(error, "Truncated SCALARS " + name);
            }
        } else if (token == "VECTORS") {
            std::string name, type;
            fin >> name >> type;
            if (!skipToPayload(fin))
                return fail(error, "Truncated frame before VECTORS " + name);

            const size_t count =
                static_cast<size_t>(out.nx) * out.ny * out.nz * 3;
            if (name == "velocity") {
                if (!readFloats(fin, cellVelocity, count))
                    return fail(error, "Truncated velocity array");
            } else if (!skipBytes(fin,
                                  static_cast<std::streamoff>(count * 4))) {
                return fail(error, "Truncated VECTORS " + name);
            }
        } else if (token == "FIELD") {
            std::string fieldName;
            int arrayCount = 0;
            fin >> fieldName >> arrayCount;

            for (int a = 0; a < arrayCount; ++a) {
                std::string arrayName, arrayType;
                long long components = 0, tuples = 0;
                fin >> arrayName >> components >> tuples >> arrayType;
                if (components < 1 || tuples < 0)
                    return fail(error, "Bad FIELD array " + arrayName);
                if (!skipToPayload(fin))
                    return fail(error, "Truncated frame before " + arrayName);

                const size_t count =
                    static_cast<size_t>(components) *
                    static_cast<size_t>(tuples);

                if (arrayType == "char" || arrayType == "unsigned_char") {
                    if (arrayName == "configText") {
                        configText.resize(count);
                        fin.read(&configText[0],
                                 static_cast<std::streamsize>(count));
                        if (fin.gcount() !=
                            static_cast<std::streamsize>(count))
                            return fail(error, "Truncated configText");
                        out.hasConfigText = true;
                    } else if (arrayName == "facePack") {
                        facePack.resize(count);
                        fin.read(&facePack[0],
                                 static_cast<std::streamsize>(count));
                        if (fin.gcount() !=
                            static_cast<std::streamsize>(count))
                            return fail(error, "Truncated facePack");
                    } else if (!skipBytes(
                                   fin,
                                   static_cast<std::streamoff>(count))) {
                        return fail(error, "Truncated FIELD " + arrayName);
                    }
                } else if (arrayType == "float" || arrayType == "int") {
                    bool ok = true;
                    if (arrayName == "uFace")
                        ok = readFloats(fin, out.u, count);
                    else if (arrayName == "vFace")
                        ok = readFloats(fin, out.v, count);
                    else if (arrayName == "pRaw")
                        ok = readFloats(fin, out.p, count);
                    else if (arrayName == "stateRho")
                        ok = readFloats(fin, out.stateRho, count);
                    else if (arrayName == "stateRhoU")
                        ok = readFloats(fin, out.stateRhoU, count);
                    else if (arrayName == "stateRhoV")
                        ok = readFloats(fin, out.stateRhoV, count);
                    else if (arrayName == "stateRhoW")
                        ok = readFloats(fin, out.stateRhoW, count);
                    else if (arrayName == "stateRhoE")
                        ok = readFloats(fin, out.stateRhoE, count);
                    else if (arrayName == "stateRhoY")
                        ok = readFloats(fin, out.stateRhoY, count);
                    else if (arrayName == "gridFaceX")
                        ok = readFloats(fin, out.gridFaceX, count);
                    else if (arrayName == "gridFaceY")
                        ok = readFloats(fin, out.gridFaceY, count);
                    else if (arrayName == "gridFaceZ")
                        ok = readFloats(fin, out.gridFaceZ, count);
                    else
                        ok = skipBytes(
                            fin, static_cast<std::streamoff>(count * 4));
                    if (!ok)
                        return fail(error, "Truncated FIELD " + arrayName);
                } else {
                    return fail(error,
                                "Unsupported FIELD type " + arrayType +
                                    " for " + arrayName);
                }
            }
        } else {
            return fail(error, "Unsupported VTK declaration: " + token);
        }
    }

    const size_t cells = static_cast<size_t>(out.nx) * out.ny * out.nz;
    if (cells == 0 || out.solid.size() != cells)
        return fail(error, "Frame carries no usable solid mask");

    // The configuration text is authoritative for everything except the grid,
    // which the file header already fixed. Unknown keys are skipped rather
    // than rejected so newer frames stay loadable by older builds.
    if (out.hasConfigText) {
        std::istringstream text(configText);
        std::string line;
        while (std::getline(text, line)) {
            line = trimCR(line);
            const size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0)
                continue;

            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);
            if (key == "restartTime")
                out.currentTime = std::strtod(value.c_str(), nullptr);
            else if (key == "restartStep")
                out.step = std::atoi(value.c_str());
            else if (key == "restartDt")
                out.dt = std::strtof(value.c_str(), nullptr);
            else if (key == "bodyState") {
                for (size_t pos = 0; pos < value.size();) {
                    size_t end = value.find(';', pos);
                    if (end == std::string::npos)
                        end = value.size();
                    const std::string entry = value.substr(pos, end - pos);
                    pos = end + 1;
                    if (entry.empty())
                        continue;
                    RestartData::BodyState state;
                    if (parseBodyState(entry, state))
                        out.bodies.push_back(state);
                }
            }
            else if (key == "formatVersion") {
                if (std::atoi(value.c_str()) > FRAME_FORMAT_VERSION)
                    std::cout << "  note: this frame was written by a newer "
                                 "build (frame format " << value
                              << ", this one reads " << FRAME_FORMAT_VERSION
                              << "). Whatever it added is ignored.\n";
            }
            else {
                std::string why;
                if (!out.cfg.setParam(key, value, why))
                    std::cerr << "Warning: the frame carries " << key << "="
                              << value << ", which this build refuses (" << why
                              << "). The default is used instead.\n";
            }
        }
    } else {
        // Nothing but the geometry can be recovered, so at least keep the
        // domain consistent with the grid that produced the frame
        out.cfg.Lx = out.dx * out.nx;
        out.cfg.Ly = out.dy * out.ny;
        out.cfg.Lz = out.dz * out.nz;

        // Frames of that vintage were always solution_<step>.vtk, so the
        // trailing number is the step count. The time behind it is gone.
        const std::string stem = file.stem().string();
        const size_t underscore = stem.find_last_of('_');
        if (underscore != std::string::npos)
            out.step = std::atoi(stem.c_str() + underscore + 1);
    }

    out.cfg.nx = out.nx;
    out.cfg.ny = out.ny;
    out.cfg.nz = out.nz;
    out.cfg.restart = true;

    // Frames written now leave the faces packed against the cell averages
    // instead of spelling them out, and drop the pressure array entirely
    // because the pressure the frame already shows is the same field scaled by
    // the density. Older frames arrive with all three written out and skip
    // both branches, so one rule covers every vintage from here on.
    if (out.u.empty() && !facePack.empty() &&
        cellVelocity.size() == cells * 3) {
        std::vector<float> uCell(cells), vCell(cells), wCell(cells);
        for (size_t id = 0; id < cells; ++id) {
            uCell[id] = cellVelocity[3 * id];
            vCell[id] = cellVelocity[3 * id + 1];
            wCell[id] = cellVelocity[3 * id + 2];
        }
        if (!unpackFaceVelocities(out.nx, out.ny, out.nz, facePack, uCell,
                                  vCell, wCell, out.u, out.v, out.w))
            std::cout << "  note: the packed face velocities in this frame did "
                         "not check out, so the\n"
                         "        state is rebuilt from the cell averages and "
                         "projected once.\n";
    }

    if (out.p.empty() && cellPressure.size() == cells && out.cfg.ro > 0.0f) {
        const float invRo = out.cfg.multiphase() ? 1.0f : 1.0f / out.cfg.ro;
        out.p.assign(cells, 0.0f);
        for (size_t id = 0; id < cells; ++id)
            out.p[id] = cellPressure[id] * invRo;
    }

    const size_t uCells = static_cast<size_t>(out.nx + 1) * out.ny * out.nz;
    const size_t vCells = static_cast<size_t>(out.nx) * (out.ny + 1) * out.nz;
    const size_t wCells = static_cast<size_t>(out.nx) * out.ny * (out.nz + 1);

    if (out.w.empty() && out.u.size() == uCells && out.v.size() == vCells)
        out.w.assign(wCells, 0.0f);

    out.exactState =
        out.u.size() == uCells &&
        out.v.size() == vCells &&
        out.w.size() == wCells &&
        out.p.size() == cells;

    if (!out.exactState) {
        if (cellVelocity.size() != cells * 3)
            return fail(error,
                        "Frame has neither a RestartData block nor a usable "
                        "velocity array");

        if (facePack.empty() && out.stateRho.empty())
            std::cout << "  note: this frame carries no face velocities, so "
                         "they are reconstructed from the\n"
                         "        cell averages and the state is projected "
                         "once.\n";

        // Cell centre -> face. Interior faces are the average of their two
        // neighbours, the inlet and outlet faces copy the cell they touch.
        out.u.assign(uCells, 0.0f);
        out.v.assign(vCells, 0.0f);
        out.w.assign(wCells, 0.0f);
        for (int k = 0; k < out.nz; ++k)
            for (int j = 0; j < out.ny; ++j) {
                const size_t rowC =
                    (static_cast<size_t>(k) * out.ny + j) * out.nx;
                const size_t rowU =
                    (static_cast<size_t>(k) * out.ny + j) * (out.nx + 1);
                out.u[rowU] = cellVelocity[3 * rowC];
                for (int i = 1; i < out.nx; ++i)
                    out.u[rowU + i] =
                        0.5f * (cellVelocity[3 * (rowC + i - 1)] +
                                cellVelocity[3 * (rowC + i)]);
                out.u[rowU + out.nx] = cellVelocity[3 * (rowC + out.nx - 1)];
            }
        for (int k = 0; k < out.nz; ++k)
            for (int j = 1; j < out.ny; ++j) {
                const size_t rowC =
                    (static_cast<size_t>(k) * out.ny + j) * out.nx;
                const size_t rowBot =
                    (static_cast<size_t>(k) * out.ny + j - 1) * out.nx;
                const size_t rowV =
                    (static_cast<size_t>(k) * (out.ny + 1) + j) * out.nx;
                for (int i = 0; i < out.nx; ++i)
                    out.v[rowV + i] =
                        0.5f * (cellVelocity[3 * (rowBot + i) + 1] +
                                cellVelocity[3 * (rowC + i) + 1]);
            }
        // Top and bottom rows stay zero, which is the wall condition anyway
        for (int k = 1; k < out.nz; ++k)
            for (int j = 0; j < out.ny; ++j) {
                const size_t rowC =
                    (static_cast<size_t>(k) * out.ny + j) * out.nx;
                const size_t rowBack =
                    (static_cast<size_t>(k - 1) * out.ny + j) * out.nx;
                for (int i = 0; i < out.nx; ++i)
                    out.w[rowC + i] =
                        0.5f * (cellVelocity[3 * (rowBack + i) + 2] +
                                cellVelocity[3 * (rowC + i) + 2]);
            }

        // Pressure is only the multigrid warm start, so an approximate one is
        // fine. It is stored in Pa in the frame and kinematic in the solver.
        if (out.p.size() != cells)
            out.p.assign(cells, 0.0f);
    }

    if (!out.hasConfigText) {
        std::cout << "  note: no configuration was stored in this frame. "
                     "Everything except the grid\n"
                     "        falls back to defaults, check it in the "
                     "confirmation screen.\n";
    }

    // Guard against a frame that came from a different domain than its own
    // configuration text claims
    const float expectedDx = out.cfg.Lx / out.nx;
    const float expectedDy = out.cfg.Ly / out.ny;
    const float expectedDz = out.cfg.Lz / out.nz;
    if (std::fabs(expectedDx - out.dx) > 1e-4f * out.dx ||
        std::fabs(expectedDy - out.dy) > 1e-4f * out.dy ||
        (pointNz > 1 &&
         std::fabs(expectedDz - out.dz) > 1e-4f * out.dz))
        return fail(error,
                    "Domain in the stored configuration does not match the "
                    "SPACING of the frame");

    return true;
}
