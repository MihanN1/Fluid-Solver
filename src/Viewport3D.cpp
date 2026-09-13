#include "Viewport3D.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/OpenGL.hpp>
#include <SFML/Window/Context.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <utility>

#ifndef APIENTRY
#define APIENTRY
#endif

namespace maskui {
namespace {

// glVertexPointer's last argument means one of two things, and which one it is
// depends on state set somewhere else entirely: with no buffer bound it is a
// pointer into this program's memory, and with one bound it is an OFFSET into
// that buffer. SFML draws through vertex buffer objects, so whether a buffer
// is still bound when raw GL runs after it is not this code's to assume.
//
// Read as an offset, the address of a std::vector lands some two terabytes
// into a buffer a few kilobytes long. The driver answers that by faulting
// inside the vertex fetch code it generates at run time - which belongs to no
// module, so the crash names neither this program nor even a driver function,
// and on a machine whose GL does not use buffers here it never happens at all.
// Unbinding costs two calls a frame and takes the whole class of it away.
// There is a second reason to be rid of client arrays, and it is the one that
// actually killed the process: this driver runs its own worker thread and is
// free to read those arrays after the call that named them has returned. The
// vectors behind them are rebuilt between frames, so by then the memory is
// somewhere else. Copying each batch into a buffer object hands the driver
// memory it owns, and the question of when it reads it stops mattering.
constexpr GLenum ARRAY_BUFFER = 0x8892;
constexpr GLenum ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum STREAM_DRAW = 0x88E0;

struct BufferFunctions {
    void(APIENTRY* gen)(GLsizei, GLuint*) = nullptr;
    void(APIENTRY* bind)(GLenum, GLuint) = nullptr;
    void(APIENTRY* data)(GLenum, std::ptrdiff_t, const void*, GLenum) = nullptr;

    bool ready() const {
        return gen != nullptr && bind != nullptr && data != nullptr;
    }
};

const BufferFunctions& bufferFunctions() {
    static const BufferFunctions loaded = [] {
        BufferFunctions out;
        out.gen = reinterpret_cast<decltype(out.gen)>(
            sf::Context::getFunction("glGenBuffers"));
        out.bind = reinterpret_cast<decltype(out.bind)>(
            sf::Context::getFunction("glBindBuffer"));
        out.data = reinterpret_cast<decltype(out.data)>(
            sf::Context::getFunction("glBufferData"));
        return out;
    }();
    return loaded;
}

void unbindBuffers() {
    const BufferFunctions& gl = bufferFunctions();
    if (gl.bind == nullptr) {
        return;
    }
    gl.bind(ARRAY_BUFFER, 0);
    gl.bind(ELEMENT_ARRAY_BUFFER, 0);
}

const sf::Color VIEW_BACKGROUND{4, 6, 5};
const sf::Color INVALID_COLOR{255, 0, 180};
const sf::Color BOX_COLOR{86, 96, 90};
const sf::Color GRID_COLOR{34, 40, 36};
const sf::Color AXIS_X{220, 43, 43};
const sf::Color AXIS_Y{68, 214, 44};
const sf::Color AXIS_Z{38, 92, 214};
const sf::Color SOLID_SURFACE{150, 156, 152};
const sf::Color VORTEX_CORE{255, 157, 46};
const sf::Color TRACER_HEAD{255, 250, 230};

constexpr float FIELD_OF_VIEW = 0.7853981634f;
constexpr float PITCH_LIMIT = 1.5697963268f;
constexpr float ORBIT_RADIANS_PER_PIXEL = 0.0100f;
constexpr float PAN_FRACTION_PER_PIXEL = 0.0020f;
constexpr float ZOOM_PER_NOTCH = 0.1200f;
constexpr float AMBIENT = 0.35f;
constexpr std::size_t MAX_SLICE_CELLS = 4194304u;

const int MARCHING_CUBE_EDGES[256] = {
    0x000, 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
    0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x099, 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
    0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x033, 0x13a, 0x636, 0x73f, 0x435, 0x53c,
    0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0x0aa, 0x7a6, 0x6af, 0x5a5, 0x4ac,
    0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x066, 0x16f, 0x265, 0x36c,
    0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0x0ff, 0x3f5, 0x2fc,
    0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x055, 0x15c,
    0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0x0cc,
    0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
    0x0cc, 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
    0x15c, 0x055, 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
    0x2fc, 0x3f5, 0x0ff, 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
    0x36c, 0x265, 0x16f, 0x066, 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
    0x4ac, 0x5a5, 0x6af, 0x7a6, 0x0aa, 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
    0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x033, 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
    0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x099, 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
    0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x000,
};

const int MARCHING_CUBE_TRIANGLES[256][16] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 9, 1, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 3, 8, 0, 2, 3, 0, 10, 2, 0, 1, 10, -1, -1, -1, -1},
    {0, 10, 2, 0, 9, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 9, 10, 2, 8, 9, 2, 3, 8, -1, -1, -1, -1, -1, -1, -1},
    {2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 8, 0, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 3, 0, 2, 11, 0, 1, 2, 0, 9, 1, -1, -1, -1, -1},
    {1, 8, 9, 1, 11, 8, 1, 2, 11, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 3, 1, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 8, 0, 10, 11, 0, 1, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 3, 0, 10, 11, 0, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {8, 10, 11, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 4, 0, 3, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 9, 1, 0, 4, 9, 0, 7, 4, 0, 8, 7, -1, -1, -1, -1},
    {1, 4, 9, 1, 7, 4, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 10, 2, 4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 4, 0, 3, 7, 0, 2, 3, 0, 10, 2, 0, 1, 10, -1},
    {0, 10, 2, 0, 9, 10, 0, 4, 9, 0, 7, 4, 0, 8, 7, -1},
    {2, 9, 10, 2, 4, 9, 2, 7, 4, 2, 3, 7, -1, -1, -1, -1},
    {2, 8, 3, 2, 4, 8, 2, 7, 4, 2, 11, 7, -1, -1, -1, -1},
    {0, 7, 4, 0, 11, 7, 0, 2, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 4, 9, 1, 7, 4, 1, 11, 7, 1, 2, 11, -1},
    {1, 4, 9, 1, 7, 4, 1, 11, 7, 1, 2, 11, -1, -1, -1, -1},
    {1, 8, 3, 1, 4, 8, 1, 7, 4, 1, 11, 7, 1, 10, 11, -1},
    {0, 7, 4, 0, 11, 7, 0, 10, 11, 0, 1, 10, -1, -1, -1, -1},
    {0, 8, 3, 4, 11, 7, 4, 10, 11, 4, 9, 10, -1, -1, -1, -1},
    {4, 11, 7, 4, 10, 11, 4, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {4, 5, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 9, 0, 4, 5, 0, 8, 4, 0, 3, 8, -1, -1, -1, -1},
    {0, 5, 1, 0, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 4, 5, 1, 8, 4, 1, 3, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 10, 2, 1, 5, 10, 1, 4, 5, 1, 9, 4, -1, -1, -1, -1},
    {0, 1, 9, 2, 5, 10, 2, 4, 5, 2, 8, 4, 2, 3, 8, -1},
    {0, 10, 2, 0, 5, 10, 0, 4, 5, -1, -1, -1, -1, -1, -1, -1},
    {2, 5, 10, 2, 4, 5, 2, 8, 4, 2, 3, 8, -1, -1, -1, -1},
    {2, 11, 3, 4, 5, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 9, 0, 4, 5, 0, 8, 4, 0, 11, 8, 0, 2, 11, -1},
    {0, 11, 3, 0, 2, 11, 0, 1, 2, 0, 5, 1, 0, 4, 5, -1},
    {1, 4, 5, 1, 8, 4, 1, 11, 8, 1, 2, 11, -1, -1, -1, -1},
    {1, 11, 3, 1, 10, 11, 1, 5, 10, 1, 4, 5, 1, 9, 4, -1},
    {0, 1, 9, 4, 11, 8, 4, 10, 11, 4, 5, 10, -1, -1, -1, -1},
    {0, 11, 3, 0, 10, 11, 0, 5, 10, 0, 4, 5, -1, -1, -1, -1},
    {4, 11, 8, 4, 10, 11, 4, 5, 10, -1, -1, -1, -1, -1, -1, -1},
    {5, 8, 7, 5, 9, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 9, 0, 7, 5, 0, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 1, 0, 7, 5, 0, 8, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 7, 5, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 10, 2, 1, 5, 10, 1, 7, 5, 1, 8, 7, 1, 9, 8, -1},
    {0, 1, 9, 2, 5, 10, 2, 7, 5, 2, 3, 7, -1, -1, -1, -1},
    {0, 10, 2, 0, 5, 10, 0, 7, 5, 0, 8, 7, -1, -1, -1, -1},
    {2, 5, 10, 2, 7, 5, 2, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 9, 8, 2, 5, 9, 2, 7, 5, 2, 11, 7, -1},
    {0, 5, 9, 0, 7, 5, 0, 11, 7, 0, 2, 11, -1, -1, -1, -1},
    {0, 8, 3, 1, 7, 5, 1, 11, 7, 1, 2, 11, -1, -1, -1, -1},
    {1, 7, 5, 1, 11, 7, 1, 2, 11, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, 5, 11, 7, 5, 10, 11, -1, -1, -1, -1},
    {0, 1, 9, 5, 11, 7, 5, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 5, 11, 7, 5, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {5, 11, 7, 5, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 1, 0, 6, 10, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
    {1, 6, 10, 1, 5, 6, 1, 9, 5, 1, 8, 9, 1, 3, 8, -1},
    {1, 6, 2, 1, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 3, 8, 0, 2, 3, 0, 6, 2, 0, 5, 6, 0, 1, 5, -1},
    {0, 6, 2, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {2, 5, 6, 2, 9, 5, 2, 8, 9, 2, 3, 8, -1, -1, -1, -1},
    {2, 11, 3, 2, 6, 11, 2, 5, 6, 2, 10, 5, -1, -1, -1, -1},
    {0, 11, 8, 0, 6, 11, 0, 5, 6, 0, 10, 5, 0, 2, 10, -1},
    {0, 11, 3, 0, 6, 11, 0, 5, 6, 0, 9, 5, 1, 2, 10, -1},
    {1, 2, 10, 5, 8, 9, 5, 11, 8, 5, 6, 11, -1, -1, -1, -1},
    {1, 11, 3, 1, 6, 11, 1, 5, 6, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 8, 0, 6, 11, 0, 5, 6, 0, 1, 5, -1, -1, -1, -1},
    {0, 11, 3, 0, 6, 11, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
    {5, 8, 9, 5, 11, 8, 5, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 5, 4, 6, 10, 4, 7, 6, 4, 8, 7, -1, -1, -1, -1},
    {0, 5, 4, 0, 10, 5, 0, 6, 10, 0, 7, 6, 0, 3, 7, -1},
    {0, 10, 1, 0, 6, 10, 0, 7, 6, 0, 8, 7, 4, 9, 5, -1},
    {1, 6, 10, 1, 7, 6, 1, 3, 7, 4, 9, 5, -1, -1, -1, -1},
    {1, 6, 2, 1, 7, 6, 1, 8, 7, 1, 4, 8, 1, 5, 4, -1},
    {0, 5, 4, 0, 1, 5, 2, 7, 6, 2, 3, 7, -1, -1, -1, -1},
    {0, 6, 2, 0, 7, 6, 0, 8, 7, 4, 9, 5, -1, -1, -1, -1},
    {2, 7, 6, 2, 3, 7, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 4, 8, 2, 5, 4, 2, 10, 5, 6, 11, 7, -1},
    {0, 5, 4, 0, 10, 5, 0, 2, 10, 6, 11, 7, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, 4, 9, 5, 6, 11, 7, -1, -1, -1, -1},
    {1, 2, 10, 4, 9, 5, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 4, 8, 1, 5, 4, 6, 11, 7, -1, -1, -1, -1},
    {0, 5, 4, 0, 1, 5, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 5, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 9, 4, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 9, 0, 6, 10, 0, 4, 6, 0, 8, 4, 0, 3, 8, -1},
    {0, 10, 1, 0, 6, 10, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 10, 1, 4, 6, 1, 8, 4, 1, 3, 8, -1, -1, -1, -1},
    {1, 6, 2, 1, 4, 6, 1, 9, 4, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 2, 4, 6, 2, 8, 4, 2, 3, 8, -1, -1, -1, -1},
    {0, 6, 2, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 4, 6, 2, 8, 4, 2, 3, 8, -1, -1, -1, -1, -1, -1, -1},
    {2, 11, 3, 2, 6, 11, 2, 4, 6, 2, 9, 4, 2, 10, 9, -1},
    {0, 10, 9, 0, 2, 10, 4, 11, 8, 4, 6, 11, -1, -1, -1, -1},
    {0, 11, 3, 0, 6, 11, 0, 4, 6, 1, 2, 10, -1, -1, -1, -1},
    {1, 2, 10, 4, 11, 8, 4, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 3, 1, 6, 11, 1, 4, 6, 1, 9, 4, -1, -1, -1, -1},
    {0, 1, 9, 4, 11, 8, 4, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 3, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {4, 11, 8, 4, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {6, 8, 7, 6, 9, 8, 6, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 9, 0, 6, 10, 0, 7, 6, 0, 3, 7, -1, -1, -1, -1},
    {0, 10, 1, 0, 6, 10, 0, 7, 6, 0, 8, 7, -1, -1, -1, -1},
    {1, 6, 10, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 2, 1, 7, 6, 1, 8, 7, 1, 9, 8, -1, -1, -1, -1},
    {0, 1, 9, 2, 7, 6, 2, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {0, 6, 2, 0, 7, 6, 0, 8, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 7, 6, 2, 3, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 9, 8, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1},
    {0, 10, 9, 0, 2, 10, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {6, 7, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 6, 7, 0, 11, 6, 0, 3, 11, -1, -1, -1, -1},
    {0, 9, 1, 6, 7, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 9, 1, 7, 8, 1, 6, 7, 1, 11, 6, 1, 3, 11, -1},
    {1, 11, 2, 1, 7, 11, 1, 6, 7, 1, 10, 6, -1, -1, -1, -1},
    {0, 7, 8, 0, 6, 7, 0, 10, 6, 0, 1, 10, 2, 3, 11, -1},
    {0, 11, 2, 0, 7, 11, 0, 6, 7, 0, 10, 6, 0, 9, 10, -1},
    {2, 3, 11, 6, 9, 10, 6, 8, 9, 6, 7, 8, -1, -1, -1, -1},
    {2, 7, 3, 2, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 6, 7, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 6, 7, 0, 2, 6, 0, 1, 2, 0, 9, 1, -1},
    {1, 8, 9, 1, 7, 8, 1, 6, 7, 1, 2, 6, -1, -1, -1, -1},
    {1, 7, 3, 1, 6, 7, 1, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 6, 7, 0, 10, 6, 0, 1, 10, -1, -1, -1, -1},
    {0, 7, 3, 0, 6, 7, 0, 10, 6, 0, 9, 10, -1, -1, -1, -1},
    {6, 9, 10, 6, 8, 9, 6, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {4, 11, 6, 4, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 6, 4, 0, 11, 6, 0, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 9, 1, 0, 4, 9, 0, 6, 4, 0, 11, 6, 0, 8, 11, -1},
    {1, 4, 9, 1, 6, 4, 1, 11, 6, 1, 3, 11, -1, -1, -1, -1},
    {1, 11, 2, 1, 8, 11, 1, 4, 8, 1, 6, 4, 1, 10, 6, -1},
    {0, 6, 4, 0, 10, 6, 0, 1, 10, 2, 3, 11, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, 4, 10, 6, 4, 9, 10, -1, -1, -1, -1},
    {2, 3, 11, 4, 10, 6, 4, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 4, 8, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {0, 6, 4, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 4, 9, 1, 6, 4, 1, 2, 6, -1, -1, -1, -1},
    {1, 4, 9, 1, 6, 4, 1, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 4, 8, 1, 6, 4, 1, 10, 6, -1, -1, -1, -1},
    {0, 6, 4, 0, 10, 6, 0, 1, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 10, 6, 4, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 6, 4, 9, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 5, 9, 4, 6, 5, 4, 11, 6, 4, 7, 11, -1, -1, -1, -1},
    {0, 5, 9, 0, 6, 5, 0, 11, 6, 0, 3, 11, 4, 7, 8, -1},
    {0, 5, 1, 0, 6, 5, 0, 11, 6, 0, 7, 11, 0, 4, 7, -1},
    {1, 6, 5, 1, 11, 6, 1, 3, 11, 4, 7, 8, -1, -1, -1, -1},
    {1, 11, 2, 1, 7, 11, 1, 4, 7, 1, 9, 4, 5, 10, 6, -1},
    {0, 1, 9, 2, 3, 11, 4, 7, 8, 5, 10, 6, -1, -1, -1, -1},
    {0, 11, 2, 0, 7, 11, 0, 4, 7, 5, 10, 6, -1, -1, -1, -1},
    {2, 3, 11, 4, 7, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {2, 7, 3, 2, 4, 7, 2, 9, 4, 2, 5, 9, 2, 6, 5, -1},
    {0, 5, 9, 0, 6, 5, 0, 2, 6, 4, 7, 8, -1, -1, -1, -1},
    {0, 7, 3, 0, 4, 7, 1, 6, 5, 1, 2, 6, -1, -1, -1, -1},
    {1, 6, 5, 1, 2, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 7, 3, 1, 4, 7, 1, 9, 4, 5, 10, 6, -1, -1, -1, -1},
    {0, 1, 9, 4, 7, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 4, 7, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {5, 11, 6, 5, 8, 11, 5, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 9, 0, 6, 5, 0, 11, 6, 0, 3, 11, -1, -1, -1, -1},
    {0, 5, 1, 0, 6, 5, 0, 11, 6, 0, 8, 11, -1, -1, -1, -1},
    {1, 6, 5, 1, 11, 6, 1, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 8, 11, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1},
    {0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 9, 8, 2, 5, 9, 2, 6, 5, -1, -1, -1, -1},
    {0, 5, 9, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 6, 5, 1, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 1, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {5, 11, 10, 5, 7, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 5, 7, 0, 10, 5, 0, 11, 10, 0, 3, 11, -1},
    {0, 10, 1, 0, 11, 10, 0, 7, 11, 0, 5, 7, 0, 9, 5, -1},
    {1, 11, 10, 1, 3, 11, 5, 8, 9, 5, 7, 8, -1, -1, -1, -1},
    {1, 11, 2, 1, 7, 11, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 5, 7, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1},
    {0, 11, 2, 0, 7, 11, 0, 5, 7, 0, 9, 5, -1, -1, -1, -1},
    {2, 3, 11, 5, 8, 9, 5, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {2, 7, 3, 2, 5, 7, 2, 10, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 5, 7, 0, 10, 5, 0, 2, 10, -1, -1, -1, -1},
    {0, 7, 3, 0, 5, 7, 0, 9, 5, 1, 2, 10, -1, -1, -1, -1},
    {1, 2, 10, 5, 8, 9, 5, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 7, 3, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 5, 7, 0, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 5, 7, 0, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 8, 9, 5, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 5, 4, 11, 10, 4, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 0, 10, 5, 0, 11, 10, 0, 3, 11, -1, -1, -1, -1},
    {0, 10, 1, 0, 11, 10, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1},
    {1, 11, 10, 1, 3, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 8, 11, 1, 4, 8, 1, 5, 4, -1, -1, -1, -1},
    {0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 4, 8, 2, 5, 4, 2, 10, 5, -1, -1, -1, -1},
    {0, 5, 4, 0, 10, 5, 0, 2, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 4, 8, 1, 5, 4, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 0, 1, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 9, 4, 11, 10, 4, 7, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 9, 0, 11, 10, 0, 3, 11, 4, 7, 8, -1, -1, -1, -1},
    {0, 10, 1, 0, 11, 10, 0, 7, 11, 0, 4, 7, -1, -1, -1, -1},
    {1, 11, 10, 1, 3, 11, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 7, 11, 1, 4, 7, 1, 9, 4, -1, -1, -1, -1},
    {0, 1, 9, 2, 3, 11, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 7, 11, 0, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 7, 3, 2, 4, 7, 2, 9, 4, 2, 10, 9, -1, -1, -1, -1},
    {0, 10, 9, 0, 2, 10, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 4, 7, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 7, 3, 1, 4, 7, 1, 9, 4, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 10, 9, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 9, 0, 11, 10, 0, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 1, 0, 11, 10, 0, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 10, 1, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 8, 11, 1, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 9, 8, 2, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 9, 0, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

const int MARCHING_CUBE_CORNERS[8][3] = {
    {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
    {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}
};

const int MARCHING_CUBE_EDGE_ENDS[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
};

float clampFloat(float value, float low, float high) {
    return std::clamp(value, low, high);
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

DataRange trimmedOf(std::vector<float>& values) {
    DataRange range;
    if (values.empty()) {
        return range;
    }
    const std::size_t last = values.size() - 1u;
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

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vector3 operator+(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 operator-(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 operator*(const Vector3& a, float scale) {
    return {a.x * scale, a.y * scale, a.z * scale};
}

float dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 cross(const Vector3& a, const Vector3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

Vector3 normalise(const Vector3& value) {
    const float size = std::sqrt(dot(value, value));
    if (!(size > 0.0f)) {
        return {0.0f, 0.0f, 1.0f};
    }
    return value * (1.0f / size);
}

using Matrix4 = std::array<float, 16>;

Matrix4 perspectiveMatrix(float aspect, float nearPlane, float farPlane) {
    Matrix4 matrix{};
    const float focal = 1.0f / std::tan(0.5f * FIELD_OF_VIEW);
    matrix[0] = focal / std::max(aspect, 1.0e-4f);
    matrix[5] = focal;
    matrix[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    matrix[11] = -1.0f;
    matrix[14] = 2.0f * farPlane * nearPlane / (nearPlane - farPlane);
    return matrix;
}

Matrix4 orthographicMatrix(
    float halfHeight,
    float aspect,
    float nearPlane,
    float farPlane) {
    Matrix4 matrix{};
    const float height = std::max(halfHeight, 1.0e-6f);
    matrix[0] = 1.0f / (height * std::max(aspect, 1.0e-4f));
    matrix[5] = 1.0f / height;
    matrix[10] = -2.0f / (farPlane - nearPlane);
    matrix[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    matrix[15] = 1.0f;
    return matrix;
}

Matrix4 lookAtMatrix(
    const Vector3& eye,
    const Vector3& target,
    const Vector3& up) {
    const Vector3 backward = normalise(eye - target);
    Vector3 right = cross(up, backward);
    if (dot(right, right) < 1.0e-12f) {
        right = cross(Vector3{0.0f, 0.0f, 1.0f}, backward);
    }
    right = normalise(right);
    const Vector3 above = cross(backward, right);
    Matrix4 matrix{};
    matrix[0] = right.x;
    matrix[1] = above.x;
    matrix[2] = backward.x;
    matrix[4] = right.y;
    matrix[5] = above.y;
    matrix[6] = backward.y;
    matrix[8] = right.z;
    matrix[9] = above.z;
    matrix[10] = backward.z;
    matrix[12] = -dot(right, eye);
    matrix[13] = -dot(above, eye);
    matrix[14] = -dot(backward, eye);
    matrix[15] = 1.0f;
    return matrix;
}

Matrix4 multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float total = 0.0f;
            for (int inner = 0; inner < 4; ++inner) {
                total += a[static_cast<std::size_t>(inner * 4 + row)] *
                         b[static_cast<std::size_t>(column * 4 + inner)];
            }
            result[static_cast<std::size_t>(column * 4 + row)] = total;
        }
    }
    return result;
}

struct CameraBasis {
    Vector3 eye;
    Vector3 target;
    Vector3 right;
    Vector3 up;
    Vector3 forward;
};

CameraBasis basisOf(const Camera3D& camera) {
    CameraBasis basis;
    const float pitch = clampFloat(camera.pitch, -PITCH_LIMIT, PITCH_LIMIT);
    const Vector3 backward{
        std::cos(pitch) * std::sin(camera.yaw),
        std::sin(pitch),
        std::cos(pitch) * std::cos(camera.yaw)
    };
    basis.target = {camera.targetX, camera.targetY, camera.targetZ};
    basis.eye = basis.target + backward * std::max(camera.distance, 1.0e-6f);
    basis.forward = backward * -1.0f;
    basis.right = normalise(cross(Vector3{0.0f, 1.0f, 0.0f}, backward));
    basis.up = cross(backward, basis.right);
    return basis;
}

float shadeOf(const Vector3& normal) {
    static const Vector3 light = normalise(Vector3{0.45f, 0.82f, 0.35f});
    return AMBIENT + (1.0f - AMBIENT) * std::fabs(dot(normal, light));
}

sf::Color shaded(const sf::Color& colour, float shade) {
    const auto scale = [shade](std::uint8_t channel) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(static_cast<float>(channel) * shade), 0L, 255L));
    };
    return {scale(colour.r), scale(colour.g), scale(colour.b), colour.a};
}

float axisDerivativeStep(const std::vector<float>& centres, std::size_t low, std::size_t high) {
    if (high <= low) {
        return 0.0f;
    }
    return centres[high] - centres[low];
}

void interpolationWeights(
    const std::vector<float>& centres,
    float value,
    std::size_t& low,
    std::size_t& high,
    float& blend) {
    low = 0;
    high = 0;
    blend = 0.0f;
    if (centres.empty()) {
        return;
    }
    if (centres.size() == 1u || value <= centres.front()) {
        low = high = 0;
        return;
    }
    if (value >= centres.back()) {
        low = high = centres.size() - 1u;
        return;
    }
    const auto found = std::upper_bound(centres.begin(), centres.end(), value);
    high = static_cast<std::size_t>(found - centres.begin());
    low = high - 1u;
    const float width = centres[high] - centres[low];
    blend = width > 0.0f ? (value - centres[low]) / width : 0.0f;
}

} // namespace

sf::Color scalarColor(double value, double minimum, double maximum) {
    if (!std::isfinite(value)) {
        return INVALID_COLOR;
    }
    double normalized = 0.5;
    if (maximum > minimum) {
        normalized = std::clamp(
            (value - minimum) / (maximum - minimum),
            0.0,
            1.0);
    }

    struct Stop {
        double position;
        sf::Color color;
    };
    const std::array<Stop, 5> stops{{
        {0.00, {91, 33, 182}},
        {0.25, {38, 92, 214}},
        {0.50, {34, 201, 173}},
        {0.75, {247, 177, 48}},
        {1.00, {220, 43, 43}}
    }};

    for (std::size_t index = 1; index < stops.size(); ++index) {
        if (normalized <= stops[index].position) {
            const Stop& first = stops[index - 1];
            const Stop& second = stops[index];
            const double local =
                (normalized - first.position) /
                (second.position - first.position);
            const auto interpolate = [local](std::uint8_t a, std::uint8_t b) {
                return static_cast<std::uint8_t>(
                    std::lround(
                        static_cast<double>(a) +
                        local *
                            (static_cast<double>(b) -
                             static_cast<double>(a))));
            };
            return {
                interpolate(first.color.r, second.color.r),
                interpolate(first.color.g, second.color.g),
                interpolate(first.color.b, second.color.b)
            };
        }
    }
    return stops.back().color;
}

std::vector<float> cellCentresX(const VtkFrame& frame) {
    std::vector<float> centres(frame.nx);
    for (std::size_t i = 0; i < frame.nx; ++i) {
        centres[i] = static_cast<float>(frame.cellCentreX(i));
    }
    return centres;
}

std::vector<float> cellCentresY(const VtkFrame& frame) {
    std::vector<float> centres(frame.ny);
    for (std::size_t j = 0; j < frame.ny; ++j) {
        centres[j] = static_cast<float>(frame.cellCentreY(j));
    }
    return centres;
}

std::vector<float> cellCentresZ(const VtkFrame& frame) {
    std::vector<float> centres(frame.nz);
    for (std::size_t k = 0; k < frame.nz; ++k) {
        centres[k] = static_cast<float>(frame.cellCentreZ(k));
    }
    return centres;
}

namespace {

struct VelocityGradient {
    float value[3][3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
};

VelocityGradient gradientAt(
    const VtkFrame& frame,
    const std::vector<float>& xs,
    const std::vector<float>& ys,
    const std::vector<float>& zs,
    std::size_t i,
    std::size_t j,
    std::size_t k) {
    VelocityGradient gradient;
    const std::size_t iLow = i > 0 ? i - 1u : 0u;
    const std::size_t iHigh = std::min(i + 1u, frame.nx - 1u);
    const std::size_t jLow = j > 0 ? j - 1u : 0u;
    const std::size_t jHigh = std::min(j + 1u, frame.ny - 1u);
    const std::size_t kLow = k > 0 ? k - 1u : 0u;
    const std::size_t kHigh = std::min(k + 1u, frame.nz - 1u);

    const float stepX = axisDerivativeStep(xs, iLow, iHigh);
    const float stepY = axisDerivativeStep(ys, jLow, jHigh);
    const float stepZ = axisDerivativeStep(zs, kLow, kHigh);

    const Velocity& xMinus = frame.velocity[frame.cellIndex(iLow, j, k)];
    const Velocity& xPlus = frame.velocity[frame.cellIndex(iHigh, j, k)];
    const Velocity& yMinus = frame.velocity[frame.cellIndex(i, jLow, k)];
    const Velocity& yPlus = frame.velocity[frame.cellIndex(i, jHigh, k)];
    const Velocity& zMinus = frame.velocity[frame.cellIndex(i, j, kLow)];
    const Velocity& zPlus = frame.velocity[frame.cellIndex(i, j, kHigh)];

    if (stepX > 0.0f) {
        gradient.value[0][0] = (xPlus.x - xMinus.x) / stepX;
        gradient.value[1][0] = (xPlus.y - xMinus.y) / stepX;
        gradient.value[2][0] = (xPlus.z - xMinus.z) / stepX;
    }
    if (stepY > 0.0f) {
        gradient.value[0][1] = (yPlus.x - yMinus.x) / stepY;
        gradient.value[1][1] = (yPlus.y - yMinus.y) / stepY;
        gradient.value[2][1] = (yPlus.z - yMinus.z) / stepY;
    }
    if (stepZ > 0.0f) {
        gradient.value[0][2] = (zPlus.x - zMinus.x) / stepZ;
        gradient.value[1][2] = (zPlus.y - zMinus.y) / stepZ;
        gradient.value[2][2] = (zPlus.z - zMinus.z) / stepZ;
    }
    return gradient;
}

float qCriterionOf(const VelocityGradient& gradient) {
    float rotation = 0.0f;
    float strain = 0.0f;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const float symmetric =
                0.5f * (gradient.value[row][column] + gradient.value[column][row]);
            const float antisymmetric =
                0.5f * (gradient.value[row][column] - gradient.value[column][row]);
            strain += symmetric * symmetric;
            rotation += antisymmetric * antisymmetric;
        }
    }
    return 0.5f * (rotation - strain);
}

Vector3 vorticityOf(const VelocityGradient& gradient) {
    return {
        gradient.value[2][1] - gradient.value[1][2],
        gradient.value[0][2] - gradient.value[2][0],
        gradient.value[1][0] - gradient.value[0][1]
    };
}

} // namespace

ScalarVolume sampleVolumeField(
    const VtkFrame& frame,
    VolumeField field,
    const std::string& scalarName) {
    ScalarVolume volume;
    volume.nx = frame.nx;
    volume.ny = frame.ny;
    volume.nz = frame.nz;
    const std::size_t count = frame.nx * frame.ny * frame.nz;
    if (count == 0 || frame.velocity.size() < count) {
        return volume;
    }
    volume.values.assign(count, 0.0f);

    switch (field) {
    case VolumeField::Pressure:
        if (frame.pressure.size() >= count) {
            std::copy(
                frame.pressure.begin(), frame.pressure.begin() + count,
                volume.values.begin());
        }
        break;
    case VolumeField::Speed:
        if (frame.velocityMagnitude.size() >= count) {
            std::copy(
                frame.velocityMagnitude.begin(),
                frame.velocityMagnitude.begin() + count,
                volume.values.begin());
        }
        break;
    case VolumeField::VelocityX:
        for (std::size_t index = 0; index < count; ++index) {
            volume.values[index] = frame.velocity[index].x;
        }
        break;
    case VolumeField::VelocityY:
        for (std::size_t index = 0; index < count; ++index) {
            volume.values[index] = frame.velocity[index].y;
        }
        break;
    case VolumeField::VelocityZ:
        for (std::size_t index = 0; index < count; ++index) {
            volume.values[index] = frame.velocity[index].z;
        }
        break;
    case VolumeField::Vorticity:
    case VolumeField::QCriterion: {
        const std::vector<float> xs = cellCentresX(frame);
        const std::vector<float> ys = cellCentresY(frame);
        const std::vector<float> zs = cellCentresZ(frame);
        for (std::size_t k = 0; k < frame.nz; ++k) {
            for (std::size_t j = 0; j < frame.ny; ++j) {
                for (std::size_t i = 0; i < frame.nx; ++i) {
                    const VelocityGradient gradient =
                        gradientAt(frame, xs, ys, zs, i, j, k);
                    const std::size_t index = frame.cellIndex(i, j, k);
                    if (field == VolumeField::QCriterion) {
                        volume.values[index] = qCriterionOf(gradient);
                    } else {
                        const Vector3 curl = vorticityOf(gradient);
                        volume.values[index] = std::sqrt(dot(curl, curl));
                    }
                }
            }
        }
        break;
    }
    case VolumeField::Scalar: {
        const auto found = frame.scalars.find(scalarName);
        if (found != frame.scalars.end() && found->second.size() >= count) {
            std::copy(
                found->second.begin(), found->second.begin() + count,
                volume.values.begin());
        }
        break;
    }
    }

    if (field == VolumeField::Pressure) {
        volume.range = frame.pressureRange;
        volume.trimmedRange = frame.pressureTrimmedRange;
        return volume;
    }
    if (field == VolumeField::Speed) {
        volume.range = frame.velocityMagnitudeRange;
        volume.trimmedRange = frame.velocityMagnitudeTrimmedRange;
        return volume;
    }
    if (field == VolumeField::Scalar) {
        const auto full = frame.scalarRanges.find(scalarName);
        const auto trimmed = frame.scalarTrimmedRanges.find(scalarName);
        if (full != frame.scalarRanges.end()) {
            volume.range = full->second;
        }
        if (trimmed != frame.scalarTrimmedRanges.end()) {
            volume.trimmedRange = trimmed->second;
        }
        if (volume.range.available) {
            return volume;
        }
    }

    std::vector<float> finite;
    finite.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (index < frame.solid.size() && frame.solid[index] != 0) {
            continue;
        }
        const float value = volume.values[index];
        if (!std::isfinite(value)) {
            continue;
        }
        includeValue(volume.range, value);
        finite.push_back(value);
    }
    volume.trimmedRange = trimmedOf(finite);
    return volume;
}

void SurfaceMesh::clear() {
    positions.clear();
    normals.clear();
}

SurfaceMesh marchingCubes(
    const ScalarVolume& volume,
    const std::vector<float>& xs,
    const std::vector<float>& ys,
    const std::vector<float>& zs,
    float level) {
    SurfaceMesh mesh;
    if (volume.nx < 2 || volume.ny < 2 || volume.nz < 2) {
        return mesh;
    }
    if (xs.size() < volume.nx || ys.size() < volume.ny || zs.size() < volume.nz) {
        return mesh;
    }

    std::array<float, 3> corner[8];
    float value[8];
    std::array<float, 3> vertex[12];

    for (std::size_t k = 0; k + 1u < volume.nz; ++k) {
        for (std::size_t j = 0; j + 1u < volume.ny; ++j) {
            for (std::size_t i = 0; i + 1u < volume.nx; ++i) {
                int cubeIndex = 0;
                bool finite = true;
                for (int slot = 0; slot < 8; ++slot) {
                    const std::size_t ci = i + static_cast<std::size_t>(
                        MARCHING_CUBE_CORNERS[slot][0]);
                    const std::size_t cj = j + static_cast<std::size_t>(
                        MARCHING_CUBE_CORNERS[slot][1]);
                    const std::size_t ck = k + static_cast<std::size_t>(
                        MARCHING_CUBE_CORNERS[slot][2]);
                    corner[slot] = {xs[ci], ys[cj], zs[ck]};
                    value[slot] = volume.at(ci, cj, ck);
                    if (!std::isfinite(value[slot])) {
                        finite = false;
                    }
                    if (value[slot] < level) {
                        cubeIndex |= 1 << slot;
                    }
                }
                if (!finite) {
                    continue;
                }
                const int cut = MARCHING_CUBE_EDGES[cubeIndex];
                if (cut == 0) {
                    continue;
                }
                for (int edge = 0; edge < 12; ++edge) {
                    if ((cut & (1 << edge)) == 0) {
                        continue;
                    }
                    int first = MARCHING_CUBE_EDGE_ENDS[edge][0];
                    int second = MARCHING_CUBE_EDGE_ENDS[edge][1];
                    if (corner[second] < corner[first]) {
                        std::swap(first, second);
                    }
                    const float low = value[first];
                    const float high = value[second];
                    const float span = high - low;
                    const float blend =
                        std::fabs(span) > 0.0f ? (level - low) / span : 0.5f;
                    for (int axis = 0; axis < 3; ++axis) {
                        vertex[edge][static_cast<std::size_t>(axis)] =
                            corner[first][static_cast<std::size_t>(axis)] +
                            blend * (corner[second][static_cast<std::size_t>(axis)] -
                                     corner[first][static_cast<std::size_t>(axis)]);
                    }
                }
                const int* row = MARCHING_CUBE_TRIANGLES[cubeIndex];
                for (int slot = 0; row[slot] >= 0; slot += 3) {
                    const std::array<float, 3>& a = vertex[row[slot]];
                    const std::array<float, 3>& b = vertex[row[slot + 1]];
                    const std::array<float, 3>& c = vertex[row[slot + 2]];
                    const Vector3 edge1{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                    const Vector3 edge2{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    const Vector3 normal = normalise(cross(edge1, edge2));
                    for (const std::array<float, 3>* point : {&a, &b, &c}) {
                        mesh.positions.push_back((*point)[0]);
                        mesh.positions.push_back((*point)[1]);
                        mesh.positions.push_back((*point)[2]);
                        mesh.normals.push_back(normal.x);
                        mesh.normals.push_back(normal.y);
                        mesh.normals.push_back(normal.z);
                    }
                }
            }
        }
    }
    return mesh;
}

double surfaceArea(const SurfaceMesh& mesh) {
    double total = 0.0;
    for (std::size_t triangle = 0; triangle < mesh.triangleCount(); ++triangle) {
        const std::size_t base = triangle * 9u;
        const Vector3 a{
            mesh.positions[base] , mesh.positions[base + 1], mesh.positions[base + 2]};
        const Vector3 b{
            mesh.positions[base + 3], mesh.positions[base + 4], mesh.positions[base + 5]};
        const Vector3 c{
            mesh.positions[base + 6], mesh.positions[base + 7], mesh.positions[base + 8]};
        const Vector3 normal = cross(b - a, c - a);
        total += 0.5 * std::sqrt(static_cast<double>(dot(normal, normal)));
    }
    return total;
}

bool surfaceIsClosed(const SurfaceMesh& mesh, float tolerance) {
    if (mesh.triangleCount() == 0) {
        return false;
    }
    const float scale = tolerance > 0.0f ? 1.0f / tolerance : 1.0f;
    const auto key = [scale, &mesh](std::size_t vertex) {
        const std::size_t base = vertex * 3u;
        const long long qx = std::llround(mesh.positions[base] * scale);
        const long long qy = std::llround(mesh.positions[base + 1] * scale);
        const long long qz = std::llround(mesh.positions[base + 2] * scale);
        return (static_cast<std::uint64_t>(qx) * 0x9e3779b97f4a7c15ull) ^
               (static_cast<std::uint64_t>(qy) * 0xc2b2ae3d27d4eb4full) ^
               (static_cast<std::uint64_t>(qz) * 0x165667b19e3779f9ull);
    };
    std::unordered_map<std::uint64_t, int> directed;
    for (std::size_t triangle = 0; triangle < mesh.triangleCount(); ++triangle) {
        const std::size_t base = triangle * 3u;
        const std::uint64_t point[3] = {
            key(base), key(base + 1u), key(base + 2u)
        };
        for (std::size_t slot = 0; slot < 3u; ++slot) {
            const std::uint64_t from = point[slot];
            const std::uint64_t to = point[(slot + 1u) % 3u];
            directed[from * 0x100000001b3ull + to] += 1;
            directed[to * 0x100000001b3ull + from] -= 1;
        }
    }
    for (const auto& entry : directed) {
        if (entry.second != 0) {
            return false;
        }
    }
    return true;
}

double Streamline::length() const {
    double total = 0.0;
    for (std::size_t point = 1; point < pointCount(); ++point) {
        const std::size_t base = point * 3u;
        const double dx = points[base] - points[base - 3u];
        const double dy = points[base + 1u] - points[base - 2u];
        const double dz = points[base + 2u] - points[base - 1u];
        total += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return total;
}

namespace {

bool sampleVelocityOn(
    const VtkFrame& frame,
    const std::vector<float>& xs,
    const std::vector<float>& ys,
    const std::vector<float>& zs,
    const std::array<float, 3>& position,
    std::array<float, 3>& velocity) {
    velocity = {0.0f, 0.0f, 0.0f};
    if (frame.nx == 0 || frame.ny == 0 || frame.nz == 0) {
        return false;
    }
    if (position[0] < frame.cellLeft(0) ||
        position[0] > frame.cellRight(frame.nx - 1u) ||
        position[1] < frame.cellBottom(0) ||
        position[1] > frame.cellTop(frame.ny - 1u) ||
        position[2] < frame.cellFront(0) ||
        position[2] > frame.cellBack(frame.nz - 1u)) {
        return false;
    }

    std::size_t i0 = 0;
    std::size_t i1 = 0;
    std::size_t j0 = 0;
    std::size_t j1 = 0;
    std::size_t k0 = 0;
    std::size_t k1 = 0;
    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    interpolationWeights(xs, position[0], i0, i1, tx);
    interpolationWeights(ys, position[1], j0, j1, ty);
    interpolationWeights(zs, position[2], k0, k1, tz);

    const auto sample = [&](std::size_t i, std::size_t j, std::size_t k) {
        return frame.velocity[frame.cellIndex(i, j, k)];
    };
    const Velocity c000 = sample(i0, j0, k0);
    const Velocity c100 = sample(i1, j0, k0);
    const Velocity c010 = sample(i0, j1, k0);
    const Velocity c110 = sample(i1, j1, k0);
    const Velocity c001 = sample(i0, j0, k1);
    const Velocity c101 = sample(i1, j0, k1);
    const Velocity c011 = sample(i0, j1, k1);
    const Velocity c111 = sample(i1, j1, k1);

    const auto blend = [](float a, float b, float t) {
        return a + t * (b - a);
    };
    const float components[3][8] = {
        {c000.x, c100.x, c010.x, c110.x, c001.x, c101.x, c011.x, c111.x},
        {c000.y, c100.y, c010.y, c110.y, c001.y, c101.y, c011.y, c111.y},
        {c000.z, c100.z, c010.z, c110.z, c001.z, c101.z, c011.z, c111.z}
    };
    for (int axis = 0; axis < 3; ++axis) {
        const float* value = components[axis];
        const float x00 = blend(value[0], value[1], tx);
        const float x10 = blend(value[2], value[3], tx);
        const float x01 = blend(value[4], value[5], tx);
        const float x11 = blend(value[6], value[7], tx);
        const float y0 = blend(x00, x10, ty);
        const float y1 = blend(x01, x11, ty);
        velocity[static_cast<std::size_t>(axis)] = blend(y0, y1, tz);
    }
    return true;
}

bool solidAt(const VtkFrame& frame, const std::array<float, 3>& position) {
    if (frame.solid.empty()) {
        return false;
    }
    const std::size_t i = frame.columnAt(position[0]);
    const std::size_t j = frame.rowAt(position[1]);
    const std::size_t k = frame.planeAt(position[2]);
    return frame.solid[frame.cellIndex(i, j, k)] != 0;
}

float smallestCellSize(const VtkFrame& frame) {
    float smallest = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < frame.nx; ++i) {
        smallest = std::min(
            smallest, static_cast<float>(frame.cellRight(i) - frame.cellLeft(i)));
    }
    for (std::size_t j = 0; j < frame.ny; ++j) {
        smallest = std::min(
            smallest, static_cast<float>(frame.cellTop(j) - frame.cellBottom(j)));
    }
    if (frame.nz > 1) {
        for (std::size_t k = 0; k < frame.nz; ++k) {
            smallest = std::min(
                smallest, static_cast<float>(frame.cellBack(k) - frame.cellFront(k)));
        }
    }
    if (!(smallest > 0.0f) || !std::isfinite(smallest)) {
        return 1.0f;
    }
    return smallest;
}

} // namespace

bool sampleVelocityAt(
    const VtkFrame& frame,
    const std::array<float, 3>& position,
    std::array<float, 3>& velocity) {
    return sampleVelocityOn(
        frame,
        cellCentresX(frame),
        cellCentresY(frame),
        cellCentresZ(frame),
        position,
        velocity);
}

Streamline traceStreamline(
    const VtkFrame& frame,
    const std::array<float, 3>& seed,
    const StreamlineOptions& options) {
    Streamline line;
    if (frame.nx == 0 || frame.ny == 0 || frame.nz == 0 || options.steps <= 0) {
        return line;
    }
    const std::vector<float> xs = cellCentresX(frame);
    const std::vector<float> ys = cellCentresY(frame);
    const std::vector<float> zs = cellCentresZ(frame);
    const auto velocityAt = [&](
        const std::array<float, 3>& point,
        std::array<float, 3>& value) {
        return sampleVelocityOn(frame, xs, ys, zs, point, value);
    };

    float step = options.stepTime;
    if (!(step > 0.0f)) {
        double reference = frame.velocityMagnitudeRange.available
            ? std::fabs(frame.velocityMagnitudeRange.maximum)
            : 0.0;
        if (!(reference > 0.0)) {
            reference = 1.0;
        }
        step = 0.5f * smallestCellSize(frame) / static_cast<float>(reference);
    }

    std::array<float, 3> position = seed;
    std::array<float, 3> velocity{};
    if (!velocityAt(position, velocity)) {
        return line;
    }
    line.points.reserve(static_cast<std::size_t>(options.steps + 1) * 3u);
    line.speeds.reserve(static_cast<std::size_t>(options.steps + 1));

    const auto record = [&line](
        const std::array<float, 3>& point,
        const std::array<float, 3>& value) {
        line.points.push_back(point[0]);
        line.points.push_back(point[1]);
        line.points.push_back(point[2]);
        line.speeds.push_back(std::sqrt(
            value[0] * value[0] + value[1] * value[1] + value[2] * value[2]));
    };
    record(position, velocity);

    for (int taken = 0; taken < options.steps; ++taken) {
        std::array<float, 3> k1{};
        std::array<float, 3> k2{};
        std::array<float, 3> k3{};
        std::array<float, 3> k4{};
        if (!velocityAt(position, k1)) {
            break;
        }
        std::array<float, 3> probe{
            position[0] + 0.5f * step * k1[0],
            position[1] + 0.5f * step * k1[1],
            position[2] + 0.5f * step * k1[2]
        };
        if (!velocityAt(probe, k2)) {
            break;
        }
        probe = {
            position[0] + 0.5f * step * k2[0],
            position[1] + 0.5f * step * k2[1],
            position[2] + 0.5f * step * k2[2]
        };
        if (!velocityAt(probe, k3)) {
            break;
        }
        probe = {
            position[0] + step * k3[0],
            position[1] + step * k3[1],
            position[2] + step * k3[2]
        };
        if (!velocityAt(probe, k4)) {
            break;
        }
        std::array<float, 3> advanced{};
        for (std::size_t axis = 0; axis < 3u; ++axis) {
            advanced[axis] = position[axis] + step / 6.0f *
                (k1[axis] + 2.0f * k2[axis] + 2.0f * k3[axis] + k4[axis]);
        }
        std::array<float, 3> value{};
        if (!velocityAt(advanced, value)) {
            break;
        }
        if (options.stopAtSolid && solidAt(frame, advanced)) {
            break;
        }
        position = advanced;
        record(position, value);
    }
    return line;
}

std::vector<std::array<float, 3>> streamlineSeedPoints(
    const VtkFrame& frame,
    int count) {
    std::vector<std::array<float, 3>> seeds;
    if (count <= 0 || frame.nx == 0 || frame.ny == 0 || frame.nz == 0) {
        return seeds;
    }
    const double spanX = std::max(frame.spanX(), 1.0e-9);
    const double spanY = std::max(frame.spanY(), 1.0e-9);
    const double spanZ = frame.nz > 1 ? std::max(frame.spanZ(), 1.0e-9) : 0.0;

    const double volume = spanX * spanY * (spanZ > 0.0 ? spanZ : 1.0);
    const double density = std::cbrt(static_cast<double>(count) / volume);
    const auto axisCount = [&](double span) {
        const long long value = std::llround(std::ceil(density * span));
        return static_cast<std::size_t>(std::max<long long>(1, value));
    };
    const std::size_t alongX = axisCount(spanX);
    const std::size_t alongY = axisCount(spanY);
    const std::size_t alongZ = spanZ > 0.0 ? axisCount(spanZ) : 1u;

    seeds.reserve(alongX * alongY * alongZ);
    for (std::size_t k = 0; k < alongZ; ++k) {
        for (std::size_t j = 0; j < alongY; ++j) {
            for (std::size_t i = 0; i < alongX; ++i) {
                const float x = static_cast<float>(
                    frame.cellLeft(0) +
                    spanX * (static_cast<double>(i) + 0.5) /
                        static_cast<double>(alongX));
                const float y = static_cast<float>(
                    frame.cellBottom(0) +
                    spanY * (static_cast<double>(j) + 0.5) /
                        static_cast<double>(alongY));
                const float z = static_cast<float>(
                    frame.cellFront(0) +
                    (spanZ > 0.0 ? spanZ : frame.cellBack(0) - frame.cellFront(0)) *
                        (static_cast<double>(k) + 0.5) /
                        static_cast<double>(alongZ));
                const std::array<float, 3> point{x, y, z};
                if (solidAt(frame, point)) {
                    continue;
                }
                seeds.push_back(point);
            }
        }
    }
    return seeds;
}

VortexCore vortexCoreLines(
    const VtkFrame& frame,
    const ScalarVolume& criterion,
    float level) {
    VortexCore core;
    if (criterion.empty() || frame.nx < 3 || frame.ny < 3 || frame.nz < 3) {
        return core;
    }
    const std::vector<float> xs = cellCentresX(frame);
    const std::vector<float> ys = cellCentresY(frame);
    const std::vector<float> zs = cellCentresZ(frame);
    const float probe = 0.75f * smallestCellSize(frame);

    const auto valueAt = [&](const Vector3& point) {
        const std::size_t i = frame.columnAt(point.x);
        const std::size_t j = frame.rowAt(point.y);
        const std::size_t k = frame.planeAt(point.z);
        return criterion.at(i, j, k);
    };

    for (std::size_t k = 1; k + 1u < frame.nz; ++k) {
        for (std::size_t j = 1; j + 1u < frame.ny; ++j) {
            for (std::size_t i = 1; i + 1u < frame.nx; ++i) {
                const float centre = criterion.at(i, j, k);
                if (!(centre > level)) {
                    continue;
                }
                if (!frame.solid.empty() &&
                    frame.solid[frame.cellIndex(i, j, k)] != 0) {
                    continue;
                }
                const VelocityGradient gradient =
                    gradientAt(frame, xs, ys, zs, i, j, k);
                const Vector3 curl = vorticityOf(gradient);
                if (dot(curl, curl) <= 0.0f) {
                    continue;
                }
                const Vector3 axis = normalise(curl);
                Vector3 helper{1.0f, 0.0f, 0.0f};
                if (std::fabs(axis.x) > 0.9f) {
                    helper = {0.0f, 1.0f, 0.0f};
                }
                const Vector3 first = normalise(cross(axis, helper));
                const Vector3 second = normalise(cross(axis, first));
                const Vector3 point{xs[i], ys[j], zs[k]};
                bool maximum = true;
                for (const Vector3& direction : {first, second}) {
                    if (valueAt(point + direction * probe) >= centre ||
                        valueAt(point - direction * probe) >= centre) {
                        maximum = false;
                        break;
                    }
                }
                if (!maximum) {
                    continue;
                }
                const Vector3 tail = point - axis * (0.5f * probe);
                const Vector3 head = point + axis * (0.5f * probe);
                core.segments.push_back(tail.x);
                core.segments.push_back(tail.y);
                core.segments.push_back(tail.z);
                core.segments.push_back(head.x);
                core.segments.push_back(head.y);
                core.segments.push_back(head.z);
            }
        }
    }
    return core;
}

void Viewport3D::Batch::clear() {
    positions.clear();
    colours.clear();
    lowest = {0.0f, 0.0f, 0.0f};
    highest = {0.0f, 0.0f, 0.0f};
}

void Viewport3D::Batch::reserve(std::size_t vertices) {
    positions.reserve(vertices * 3u);
    colours.reserve(vertices * 4u);
}

void Viewport3D::Batch::add(float x, float y, float z, const sf::Color& colour) {
    if (positions.empty()) {
        lowest = {x, y, z};
        highest = {x, y, z};
    } else {
        lowest[0] = std::min(lowest[0], x);
        lowest[1] = std::min(lowest[1], y);
        lowest[2] = std::min(lowest[2], z);
        highest[0] = std::max(highest[0], x);
        highest[1] = std::max(highest[1], y);
        highest[2] = std::max(highest[2], z);
    }
    positions.push_back(x);
    positions.push_back(y);
    positions.push_back(z);
    colours.push_back(colour.r);
    colours.push_back(colour.g);
    colours.push_back(colour.b);
    colours.push_back(colour.a);
}

void Viewport3D::setFrame(std::shared_ptr<const VtkFrame> frame) {
    frame_ = std::move(frame);
    if (frame_) {
        settings_.sliceIndexX =
            std::min(settings_.sliceIndexX, frame_->nx ? frame_->nx - 1u : 0u);
        settings_.sliceIndexY =
            std::min(settings_.sliceIndexY, frame_->ny ? frame_->ny - 1u : 0u);
        settings_.sliceIndexZ =
            std::min(settings_.sliceIndexZ, frame_->nz ? frame_->nz - 1u : 0u);
    }
    rebuildAll();
}

const std::shared_ptr<const VtkFrame>& Viewport3D::frame() const {
    return frame_;
}

void Viewport3D::setSettings(const Viewport3DSettings& settings) {
    const Viewport3DSettings previous = settings_;
    settings_ = settings;
    if (frame_) {
        settings_.sliceIndexX =
            std::min(settings_.sliceIndexX, frame_->nx ? frame_->nx - 1u : 0u);
        settings_.sliceIndexY =
            std::min(settings_.sliceIndexY, frame_->ny ? frame_->ny - 1u : 0u);
        settings_.sliceIndexZ =
            std::min(settings_.sliceIndexZ, frame_->nz ? frame_->nz - 1u : 0u);
    }

    const bool colourChanged =
        previous.colourBy != settings_.colourBy ||
        previous.colourScalar != settings_.colourScalar ||
        previous.trimmedRange != settings_.trimmedRange;

    if (previous.showBox != settings_.showBox ||
        previous.showGrid != settings_.showGrid) {
        rebuildBox();
    }
    if (previous.showSolid != settings_.showSolid ||
        previous.wireframeSolid != settings_.wireframeSolid) {
        rebuildSolid();
    }
    if (colourChanged ||
        previous.showSlices != settings_.showSlices ||
        previous.sliceX != settings_.sliceX ||
        previous.sliceY != settings_.sliceY ||
        previous.sliceZ != settings_.sliceZ ||
        previous.sliceIndexX != settings_.sliceIndexX ||
        previous.sliceIndexY != settings_.sliceIndexY ||
        previous.sliceIndexZ != settings_.sliceIndexZ) {
        rebuildSlices();
    }
    if (previous.showMicrophones != settings_.showMicrophones ||
        previous.microphones != settings_.microphones) {
        rebuildMarkers();
    }
    if (colourChanged ||
        previous.showIsosurface != settings_.showIsosurface ||
        previous.isoField != settings_.isoField ||
        previous.isoLevel != settings_.isoLevel) {
        rebuildIsosurface();
    }
    if (colourChanged ||
        previous.showVortices != settings_.showVortices ||
        previous.vortexLevel != settings_.vortexLevel) {
        rebuildVortices();
    }
    if (previous.showStreamlines != settings_.showStreamlines ||
        previous.streamlineSeeds != settings_.streamlineSeeds ||
        previous.streamlineSteps != settings_.streamlineSteps) {
        rebuildStreamlines();
    }
    if (previous.animateTracers != settings_.animateTracers) {
        rebuildTracers();
    }
}

const Viewport3DSettings& Viewport3D::settings() const {
    return settings_;
}

Camera3D& Viewport3D::camera() {
    return camera_;
}

const Camera3D& Viewport3D::camera() const {
    return camera_;
}

Viewport3D::Bounds Viewport3D::bounds() const {
    Bounds box;
    if (!frame_ || frame_->nx == 0 || frame_->ny == 0 || frame_->nz == 0) {
        return box;
    }
    box.lowX = static_cast<float>(frame_->cellLeft(0));
    box.highX = static_cast<float>(frame_->cellRight(frame_->nx - 1u));
    box.lowY = static_cast<float>(frame_->cellBottom(0));
    box.highY = static_cast<float>(frame_->cellTop(frame_->ny - 1u));
    box.lowZ = static_cast<float>(frame_->cellFront(0));
    box.highZ = static_cast<float>(frame_->cellBack(frame_->nz - 1u));
    return box;
}

DataRange Viewport3D::colourRange(const ScalarVolume& volume) const {
    if (settings_.trimmedRange && volume.trimmedRange.available) {
        return volume.trimmedRange;
    }
    return volume.range;
}

void Viewport3D::frameAll() {
    const Bounds box = bounds();
    camera_.targetX = 0.5f * (box.lowX + box.highX);
    camera_.targetY = 0.5f * (box.lowY + box.highY);
    camera_.targetZ = 0.5f * (box.lowZ + box.highZ);
    const float dx = box.highX - box.lowX;
    const float dy = box.highY - box.lowY;
    const float dz = box.highZ - box.lowZ;
    const float radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    camera_.distance =
        std::max(radius, 1.0e-4f) / std::sin(0.5f * FIELD_OF_VIEW) * 1.08f;
}

void Viewport3D::orbit(float dx, float dy) {
    camera_.yaw -= dx * ORBIT_RADIANS_PER_PIXEL;
    camera_.pitch = clampFloat(
        camera_.pitch + dy * ORBIT_RADIANS_PER_PIXEL,
        -PITCH_LIMIT,
        PITCH_LIMIT);
    const float turn = 6.2831853072f;
    while (camera_.yaw > turn) {
        camera_.yaw -= turn;
    }
    while (camera_.yaw < -turn) {
        camera_.yaw += turn;
    }
}

void Viewport3D::pan(float dx, float dy) {
    const CameraBasis basis = basisOf(camera_);
    const float scale = camera_.distance * PAN_FRACTION_PER_PIXEL;
    const Vector3 target =
        Vector3{camera_.targetX, camera_.targetY, camera_.targetZ} -
        basis.right * (dx * scale) + basis.up * (dy * scale);
    camera_.targetX = target.x;
    camera_.targetY = target.y;
    camera_.targetZ = target.z;
}

void Viewport3D::zoom(float amount) {
    camera_.distance *= std::exp(-amount * ZOOM_PER_NOTCH);
    const Bounds box = bounds();
    const float dx = box.highX - box.lowX;
    const float dy = box.highY - box.lowY;
    const float dz = box.highZ - box.lowZ;
    const float radius =
        std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 1.0e-4f);
    camera_.distance = clampFloat(camera_.distance, radius * 0.02f, radius * 200.0f);
}

void Viewport3D::setView(int axis, bool negative) {
    const float quarter = 1.5707963268f;
    switch (axis) {
    case 0:
        camera_.yaw = negative ? -quarter : quarter;
        camera_.pitch = 0.0f;
        break;
    case 1:
        camera_.yaw = 0.0f;
        camera_.pitch = negative ? -PITCH_LIMIT : PITCH_LIMIT;
        break;
    default:
        camera_.yaw = negative ? 3.1415926536f : 0.0f;
        camera_.pitch = 0.0f;
        break;
    }
}

void Viewport3D::advance(float seconds) {
    if (!settings_.animateTracers || paths_.empty()) {
        return;
    }
    phase_ += seconds * 0.25f;
    while (phase_ >= 1.0f) {
        phase_ -= 1.0f;
    }
    rebuildTracers();
}

std::size_t Viewport3D::triangleCount() const {
    return (solid_.vertexCount() + slices_.vertexCount() +
            isosurface_.vertexCount() + vortexSurface_.vertexCount()) / 3u;
}

std::size_t Viewport3D::lineCount() const {
    std::size_t vertices = box_.vertexCount() + vortexLines_.vertexCount() +
                           streamlines_.vertexCount();
    for (const Batch& face : grid_) {
        vertices += face.vertexCount();
    }
    return vertices / 2u;
}

void Viewport3D::rebuildAll() {
    rebuildBox();
    rebuildSolid();
    rebuildSlices();
    rebuildIsosurface();
    rebuildVortices();
    rebuildStreamlines();
    rebuildTracers();
    rebuildMarkers();
}

// A microphone is a coordinate in a text row and nothing on screen, which
// makes "is it in the wake or beside it" a question answered by arithmetic
// rather than by looking. Each one is drawn as a three-axis cross sized
// against the box, so it reads at any zoom without being mistaken for data.
void Viewport3D::rebuildMarkers() {
    markers_.clear();
    if (!frame_ || !settings_.showMicrophones ||
        settings_.microphones.empty()) {
        return;
    }
    const Bounds box = bounds();
    const float dx = box.highX - box.lowX;
    const float dy = box.highY - box.lowY;
    const float dz = box.highZ - box.lowZ;
    const float arm = 0.02f * std::max(std::max(dx, dy), dz);
    if (!(arm > 0.0f)) {
        return;
    }
    const sf::Color colour(255, 210, 60);
    markers_.reserve(settings_.microphones.size() * 6u);
    for (const std::array<float, 3>& point : settings_.microphones) {
        for (int axis = 0; axis < 3; ++axis) {
            for (int end = 0; end < 2; ++end) {
                float corner[3] = {point[0], point[1], point[2]};
                corner[axis] += end != 0 ? arm : -arm;
                markers_.add(corner[0], corner[1], corner[2], colour);
            }
        }
    }
}

void Viewport3D::rebuildBox() {
    box_.clear();
    for (Batch& face : grid_) {
        face.clear();
    }
    if (!frame_) {
        return;
    }
    const Bounds box = bounds();
    const float corner[8][3] = {
        {box.lowX, box.lowY, box.lowZ},
        {box.highX, box.lowY, box.lowZ},
        {box.highX, box.highY, box.lowZ},
        {box.lowX, box.highY, box.lowZ},
        {box.lowX, box.lowY, box.highZ},
        {box.highX, box.lowY, box.highZ},
        {box.highX, box.highY, box.highZ},
        {box.lowX, box.highY, box.highZ}
    };
    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    const sf::Color edgeColour[12] = {
        AXIS_X, BOX_COLOR, BOX_COLOR, AXIS_Y,
        BOX_COLOR, BOX_COLOR, BOX_COLOR, BOX_COLOR,
        AXIS_Z, BOX_COLOR, BOX_COLOR, BOX_COLOR
    };
    if (settings_.showBox) {
        box_.reserve(24);
        for (int edge = 0; edge < 12; ++edge) {
            for (int end = 0; end < 2; ++end) {
                const float* point = corner[edges[edge][end]];
                box_.add(point[0], point[1], point[2], edgeColour[edge]);
            }
        }
    }

    if (!settings_.showGrid) {
        return;
    }
    const auto gridFace = [&](Batch& target, int fixedAxis, bool high) {
        const int axisA = fixedAxis == 0 ? 1 : 0;
        const int axisB = fixedAxis == 2 ? 1 : 2;
        const float low[3] = {box.lowX, box.lowY, box.lowZ};
        const float top[3] = {box.highX, box.highY, box.highZ};
        const std::size_t countA = axisA == 0
            ? frame_->nx
            : (axisA == 1 ? frame_->ny : frame_->nz);
        const std::size_t countB = axisB == 0
            ? frame_->nx
            : (axisB == 1 ? frame_->ny : frame_->nz);
        const auto coordinate = [&](int which, std::size_t cell) {
            if (which == 0) {
                return static_cast<float>(
                    cell < frame_->nx ? frame_->cellLeft(cell)
                                      : frame_->cellRight(frame_->nx - 1u));
            }
            if (which == 1) {
                return static_cast<float>(
                    cell < frame_->ny ? frame_->cellBottom(cell)
                                      : frame_->cellTop(frame_->ny - 1u));
            }
            return static_cast<float>(
                cell < frame_->nz ? frame_->cellFront(cell)
                                  : frame_->cellBack(frame_->nz - 1u));
        };
        target.reserve(2u * (countA + countB + 2u));
        for (std::size_t cell = 0; cell <= countA; ++cell) {
            float first[3];
            float second[3];
            for (int axis = 0; axis < 3; ++axis) {
                first[axis] = high ? top[axis] : low[axis];
                second[axis] = first[axis];
            }
            first[axisA] = coordinate(axisA, cell);
            second[axisA] = first[axisA];
            first[axisB] = low[axisB];
            second[axisB] = top[axisB];
            target.add(first[0], first[1], first[2], GRID_COLOR);
            target.add(second[0], second[1], second[2], GRID_COLOR);
        }
        for (std::size_t cell = 0; cell <= countB; ++cell) {
            float first[3];
            float second[3];
            for (int axis = 0; axis < 3; ++axis) {
                first[axis] = high ? top[axis] : low[axis];
                second[axis] = first[axis];
            }
            first[axisB] = coordinate(axisB, cell);
            second[axisB] = first[axisB];
            first[axisA] = low[axisA];
            second[axisA] = top[axisA];
            target.add(first[0], first[1], first[2], GRID_COLOR);
            target.add(second[0], second[1], second[2], GRID_COLOR);
        }
    };
    gridFace(grid_[0], 0, false);
    gridFace(grid_[1], 0, true);
    gridFace(grid_[2], 1, false);
    gridFace(grid_[3], 1, true);
    gridFace(grid_[4], 2, false);
    gridFace(grid_[5], 2, true);
}

void Viewport3D::rebuildSolid() {
    solid_.clear();
    if (!frame_ || !settings_.showSolid || frame_->solid.empty()) {
        return;
    }
    const VtkFrame& frame = *frame_;
    const int offsets[6][3] = {
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
    };
    std::size_t exposed = 0;
    for (std::size_t k = 0; k < frame.nz; ++k) {
        for (std::size_t j = 0; j < frame.ny; ++j) {
            for (std::size_t i = 0; i < frame.nx; ++i) {
                if (frame.solid[frame.cellIndex(i, j, k)] == 0) {
                    continue;
                }
                for (const auto& offset : offsets) {
                    const long long ni = static_cast<long long>(i) + offset[0];
                    const long long nj = static_cast<long long>(j) + offset[1];
                    const long long nk = static_cast<long long>(k) + offset[2];
                    const bool inside =
                        ni >= 0 && nj >= 0 && nk >= 0 &&
                        ni < static_cast<long long>(frame.nx) &&
                        nj < static_cast<long long>(frame.ny) &&
                        nk < static_cast<long long>(frame.nz);
                    if (inside && frame.solid[frame.cellIndex(
                            static_cast<std::size_t>(ni),
                            static_cast<std::size_t>(nj),
                            static_cast<std::size_t>(nk))] != 0) {
                        continue;
                    }
                    ++exposed;
                }
            }
        }
    }
    solid_.reserve(exposed * 6u);

    for (std::size_t k = 0; k < frame.nz; ++k) {
        for (std::size_t j = 0; j < frame.ny; ++j) {
            for (std::size_t i = 0; i < frame.nx; ++i) {
                if (frame.solid[frame.cellIndex(i, j, k)] == 0) {
                    continue;
                }
                const float low[3] = {
                    static_cast<float>(frame.cellLeft(i)),
                    static_cast<float>(frame.cellBottom(j)),
                    static_cast<float>(frame.cellFront(k))
                };
                const float high[3] = {
                    static_cast<float>(frame.cellRight(i)),
                    static_cast<float>(frame.cellTop(j)),
                    static_cast<float>(frame.cellBack(k))
                };
                for (int face = 0; face < 6; ++face) {
                    const int* offset = offsets[face];
                    const long long ni = static_cast<long long>(i) + offset[0];
                    const long long nj = static_cast<long long>(j) + offset[1];
                    const long long nk = static_cast<long long>(k) + offset[2];
                    const bool inside =
                        ni >= 0 && nj >= 0 && nk >= 0 &&
                        ni < static_cast<long long>(frame.nx) &&
                        nj < static_cast<long long>(frame.ny) &&
                        nk < static_cast<long long>(frame.nz);
                    if (inside && frame.solid[frame.cellIndex(
                            static_cast<std::size_t>(ni),
                            static_cast<std::size_t>(nj),
                            static_cast<std::size_t>(nk))] != 0) {
                        continue;
                    }
                    const int fixedAxis = face / 2;
                    const bool atHigh = (face % 2) == 1;
                    const int axisA = fixedAxis == 0 ? 1 : 0;
                    const int axisB = fixedAxis == 2 ? 1 : 2;
                    float normalAxis[3] = {0.0f, 0.0f, 0.0f};
                    normalAxis[fixedAxis] = atHigh ? 1.0f : -1.0f;
                    const Vector3 normal{
                        normalAxis[0], normalAxis[1], normalAxis[2]};
                    const sf::Color colour =
                        shaded(SOLID_SURFACE, shadeOf(normal));
                    float base[3];
                    for (int axis = 0; axis < 3; ++axis) {
                        base[axis] = low[axis];
                    }
                    base[fixedAxis] = atHigh ? high[fixedAxis] : low[fixedAxis];
                    const int forward[6][2] = {
                        {0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}
                    };
                    const int reversed[6][2] = {
                        {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {0, 0}
                    };
                    const bool flip =
                        fixedAxis == 1 ? atHigh : !atHigh;
                    for (int corner = 0; corner < 6; ++corner) {
                        const int* step =
                            flip ? reversed[corner] : forward[corner];
                        float point[3] = {base[0], base[1], base[2]};
                        point[axisA] = step[0] != 0 ? high[axisA] : low[axisA];
                        point[axisB] = step[1] != 0 ? high[axisB] : low[axisB];
                        solid_.add(point[0], point[1], point[2], colour);
                    }
                }
            }
        }
    }
}

void Viewport3D::rebuildSlices() {
    slices_.clear();
    if (!frame_ || !settings_.showSlices) {
        return;
    }
    const VtkFrame& frame = *frame_;
    const ScalarVolume field =
        sampleVolumeField(frame, settings_.colourBy, settings_.colourScalar);
    if (field.empty()) {
        return;
    }
    const DataRange range = colourRange(field);

    const auto emit = [&](int fixedAxis, std::size_t index) {
        const int axisA = fixedAxis == 0 ? 1 : 0;
        const int axisB = fixedAxis == 2 ? 1 : 2;
        const std::size_t countA = axisA == 0
            ? frame.nx
            : (axisA == 1 ? frame.ny : frame.nz);
        const std::size_t countB = axisB == 0
            ? frame.nx
            : (axisB == 1 ? frame.ny : frame.nz);
        if (countA * countB > MAX_SLICE_CELLS) {
            return;
        }
        const auto lowOf = [&](int axis, std::size_t cell) {
            if (axis == 0) {
                return static_cast<float>(frame.cellLeft(cell));
            }
            if (axis == 1) {
                return static_cast<float>(frame.cellBottom(cell));
            }
            return static_cast<float>(frame.cellFront(cell));
        };
        const auto highOf = [&](int axis, std::size_t cell) {
            if (axis == 0) {
                return static_cast<float>(frame.cellRight(cell));
            }
            if (axis == 1) {
                return static_cast<float>(frame.cellTop(cell));
            }
            return static_cast<float>(frame.cellBack(cell));
        };
        const float centre = 0.5f *
            (lowOf(fixedAxis, index) + highOf(fixedAxis, index));
        slices_.reserve(slices_.vertexCount() + countA * countB * 6u);
        for (std::size_t b = 0; b < countB; ++b) {
            for (std::size_t a = 0; a < countA; ++a) {
                std::size_t cell[3] = {0, 0, 0};
                cell[static_cast<std::size_t>(axisA)] = a;
                cell[static_cast<std::size_t>(axisB)] = b;
                cell[static_cast<std::size_t>(fixedAxis)] = index;
                const float value =
                    field.at(cell[0], cell[1], cell[2]);
                const sf::Color colour = std::isfinite(value)
                    ? scalarColor(value, range.minimum, range.maximum)
                    : INVALID_COLOR;
                const int order[6][2] = {
                    {0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}
                };
                for (const auto& step : order) {
                    float point[3];
                    point[fixedAxis] = centre;
                    point[axisA] = step[0] != 0 ? highOf(axisA, a) : lowOf(axisA, a);
                    point[axisB] = step[1] != 0 ? highOf(axisB, b) : lowOf(axisB, b);
                    slices_.add(point[0], point[1], point[2], colour);
                }
            }
        }
    };

    if (settings_.sliceX && frame.nx > 0) {
        emit(0, std::min(settings_.sliceIndexX, frame.nx - 1u));
    }
    if (settings_.sliceY && frame.ny > 0) {
        emit(1, std::min(settings_.sliceIndexY, frame.ny - 1u));
    }
    if (settings_.sliceZ && frame.nz > 0) {
        emit(2, std::min(settings_.sliceIndexZ, frame.nz - 1u));
    }
}

void Viewport3D::appendSurface(
    Batch& batch,
    const SurfaceMesh& mesh,
    const ScalarVolume& colourField,
    const DataRange& range) {
    if (mesh.triangleCount() == 0 || !frame_) {
        return;
    }
    const VtkFrame& frame = *frame_;
    batch.reserve(batch.vertexCount() + mesh.triangleCount() * 3u);
    for (std::size_t vertex = 0; vertex < mesh.triangleCount() * 3u; ++vertex) {
        const std::size_t base = vertex * 3u;
        const float x = mesh.positions[base];
        const float y = mesh.positions[base + 1u];
        const float z = mesh.positions[base + 2u];
        const Vector3 normal{
            mesh.normals[base], mesh.normals[base + 1u], mesh.normals[base + 2u]};
        sf::Color colour = SOLID_SURFACE;
        if (!colourField.empty()) {
            const std::size_t i = frame.columnAt(x);
            const std::size_t j = frame.rowAt(y);
            const std::size_t k = frame.planeAt(z);
            const float value = colourField.at(i, j, k);
            colour = std::isfinite(value)
                ? scalarColor(value, range.minimum, range.maximum)
                : INVALID_COLOR;
        }
        batch.add(x, y, z, shaded(colour, shadeOf(normal)));
    }
}

void Viewport3D::rebuildIsosurface() {
    isosurface_.clear();
    if (!frame_ || !settings_.showIsosurface) {
        return;
    }
    const VtkFrame& frame = *frame_;
    const ScalarVolume field =
        sampleVolumeField(frame, settings_.isoField, settings_.colourScalar);
    if (field.empty() || !field.range.available) {
        return;
    }
    const DataRange levelRange =
        settings_.trimmedRange && field.trimmedRange.available
            ? field.trimmedRange
            : field.range;
    const float level = static_cast<float>(
        levelRange.minimum +
        static_cast<double>(clampFloat(settings_.isoLevel, 0.0f, 1.0f)) *
            (levelRange.maximum - levelRange.minimum));

    const SurfaceMesh mesh = marchingCubes(
        field,
        cellCentresX(frame),
        cellCentresY(frame),
        cellCentresZ(frame),
        level);

    const ScalarVolume colourField =
        settings_.colourBy == settings_.isoField
            ? field
            : sampleVolumeField(frame, settings_.colourBy, settings_.colourScalar);
    appendSurface(isosurface_, mesh, colourField, colourRange(colourField));
}

void Viewport3D::rebuildVortices() {
    vortexSurface_.clear();
    vortexLines_.clear();
    if (!frame_ || !settings_.showVortices) {
        return;
    }
    const VtkFrame& frame = *frame_;
    const ScalarVolume criterion =
        sampleVolumeField(frame, VolumeField::QCriterion, std::string());
    if (criterion.empty() || !criterion.range.available) {
        return;
    }
    const double highest = std::max(criterion.range.maximum, 0.0);
    if (!(highest > 0.0)) {
        return;
    }
    const float level = static_cast<float>(
        highest * static_cast<double>(clampFloat(settings_.vortexLevel, 0.0f, 1.0f)));

    const SurfaceMesh mesh = marchingCubes(
        criterion,
        cellCentresX(frame),
        cellCentresY(frame),
        cellCentresZ(frame),
        level);
    const ScalarVolume colourField =
        sampleVolumeField(frame, settings_.colourBy, settings_.colourScalar);
    appendSurface(vortexSurface_, mesh, colourField, colourRange(colourField));

    const VortexCore core = vortexCoreLines(frame, criterion, level);
    vortexLines_.reserve(core.segments.size() / 3u);
    for (std::size_t index = 0; index + 2u < core.segments.size(); index += 3u) {
        vortexLines_.add(
            core.segments[index],
            core.segments[index + 1u],
            core.segments[index + 2u],
            VORTEX_CORE);
    }
}

void Viewport3D::rebuildStreamlines() {
    streamlines_.clear();
    paths_.clear();
    if (!frame_ || !settings_.showStreamlines) {
        rebuildTracers();
        return;
    }
    const VtkFrame& frame = *frame_;
    StreamlineOptions options;
    options.steps = std::max(1, settings_.streamlineSteps);
    const std::vector<std::array<float, 3>> seeds =
        streamlineSeedPoints(frame, std::max(1, settings_.streamlineSeeds));

    DataRange speed = frame.velocityMagnitudeRange;
    if (settings_.trimmedRange && frame.velocityMagnitudeTrimmedRange.available) {
        speed = frame.velocityMagnitudeTrimmedRange;
    }

    paths_.reserve(seeds.size());
    std::size_t vertices = 0;
    for (const std::array<float, 3>& seed : seeds) {
        Streamline line = traceStreamline(frame, seed, options);
        if (line.pointCount() < 2u) {
            continue;
        }
        vertices += (line.pointCount() - 1u) * 2u;
        paths_.push_back(std::move(line));
    }

    streamlines_.reserve(vertices);
    for (const Streamline& line : paths_) {
        for (std::size_t point = 1; point < line.pointCount(); ++point) {
            for (std::size_t end = point - 1u; end <= point; ++end) {
                const std::size_t base = end * 3u;
                const sf::Color colour = scalarColor(
                    line.speeds[end], speed.minimum, speed.maximum);
                streamlines_.add(
                    line.points[base],
                    line.points[base + 1u],
                    line.points[base + 2u],
                    colour);
            }
        }
    }
    rebuildTracers();
}

void Viewport3D::rebuildTracers() {
    tracers_.clear();
    if (!settings_.animateTracers || paths_.empty()) {
        return;
    }
    tracers_.reserve(paths_.size() * 2u);
    for (const Streamline& line : paths_) {
        const std::size_t count = line.pointCount();
        if (count < 2u) {
            continue;
        }
        const float exact =
            phase_ * static_cast<float>(count - 1u);
        std::size_t head = static_cast<std::size_t>(exact);
        if (head + 1u >= count) {
            head = count - 2u;
        }
        for (std::size_t end = head; end <= head + 1u; ++end) {
            const std::size_t base = end * 3u;
            tracers_.add(
                line.points[base],
                line.points[base + 1u],
                line.points[base + 2u],
                TRACER_HEAD);
        }
    }
}

namespace {

struct FrustumPlanes {
    std::array<std::array<float, 4>, 6> plane{};
};

FrustumPlanes planesOf(const Matrix4& viewProjection) {
    const auto at = [&viewProjection](int row, int column) {
        return viewProjection[static_cast<std::size_t>(column * 4 + row)];
    };
    FrustumPlanes frustum;
    for (int index = 0; index < 6; ++index) {
        const int row = index / 2;
        const float sign = (index % 2) == 0 ? 1.0f : -1.0f;
        for (int column = 0; column < 4; ++column) {
            frustum.plane[static_cast<std::size_t>(index)]
                         [static_cast<std::size_t>(column)] =
                at(3, column) + sign * at(row, column);
        }
    }
    return frustum;
}

bool boxIsVisible(
    const FrustumPlanes& frustum,
    const std::array<float, 3>& lowest,
    const std::array<float, 3>& highest) {
    for (const std::array<float, 4>& plane : frustum.plane) {
        const float x = plane[0] >= 0.0f ? highest[0] : lowest[0];
        const float y = plane[1] >= 0.0f ? highest[1] : lowest[1];
        const float z = plane[2] >= 0.0f ? highest[2] : lowest[2];
        if (plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace

void Viewport3D::draw(sf::RenderWindow& window, const sf::FloatRect& area) {
    if (!frame_ || area.size.x < 2.0f || area.size.y < 2.0f) {
        return;
    }

    const Bounds box = bounds();
    const float dx = box.highX - box.lowX;
    const float dy = box.highY - box.lowY;
    const float dz = box.highZ - box.lowZ;
    const float radius =
        std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 1.0e-4f);
    const CameraBasis basis = basisOf(camera_);
    const float aspect = area.size.x / area.size.y;
    const float nearPlane = std::max(camera_.distance * 0.01f, 1.0e-5f);
    const float farPlane = camera_.distance + 6.0f * radius;
    const Matrix4 projection = camera_.orthographic
        ? orthographicMatrix(
              camera_.distance * std::tan(0.5f * FIELD_OF_VIEW),
              aspect,
              -6.0f * radius,
              camera_.distance + 6.0f * radius)
        : perspectiveMatrix(aspect, nearPlane, farPlane);
    const Matrix4 view = lookAtMatrix(
        basis.eye, basis.target, Vector3{0.0f, 1.0f, 0.0f});
    const FrustumPlanes frustum = planesOf(multiply(projection, view));

    window.pushGLStates();

    const sf::Vector2u windowSize = window.getSize();
    const GLint left = static_cast<GLint>(std::lround(area.position.x));
    const GLsizei width = static_cast<GLsizei>(std::lround(area.size.x));
    const GLsizei height = static_cast<GLsizei>(std::lround(area.size.y));
    const GLint bottom = static_cast<GLint>(windowSize.y) -
        static_cast<GLint>(std::lround(area.position.y)) -
        static_cast<GLint>(height);

    glViewport(left, bottom, width, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, bottom, width, height);
    glClearColor(
        static_cast<GLfloat>(VIEW_BACKGROUND.r) / 255.0f,
        static_cast<GLfloat>(VIEW_BACKGROUND.g) / 255.0f,
        static_cast<GLfloat>(VIEW_BACKGROUND.b) / 255.0f,
        1.0f);

    const bool depthAvailable = window.getSettings().depthBits > 0;
    glClear(
        GL_COLOR_BUFFER_BIT |
        (depthAvailable ? GL_DEPTH_BUFFER_BIT : 0u));
    if (depthAvailable) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.data());
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.data());

    unbindBuffers();
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    const BufferFunctions& gl = bufferFunctions();
    if (gl.ready() && positionBuffer_ == 0) {
        GLuint made[2] = {0, 0};
        gl.gen(2, made);
        positionBuffer_ = made[0];
        colourBuffer_ = made[1];
    }
    const bool throughBuffers = gl.ready() && positionBuffer_ != 0;

    const auto submit = [&](const Batch& batch, GLenum mode) {
        if (batch.empty() ||
            !boxIsVisible(frustum, batch.lowest, batch.highest)) {
            return;
        }
        if (throughBuffers) {
            // The binding in force when glVertexPointer runs is the one that
            // call remembers, so each array is bound, filled and named in turn
            // and the offset is zero rather than an address.
            gl.bind(ARRAY_BUFFER, positionBuffer_);
            gl.data(ARRAY_BUFFER,
                    static_cast<std::ptrdiff_t>(batch.positions.size() *
                                                sizeof(float)),
                    batch.positions.data(), STREAM_DRAW);
            glVertexPointer(3, GL_FLOAT, 0, nullptr);

            gl.bind(ARRAY_BUFFER, colourBuffer_);
            gl.data(ARRAY_BUFFER,
                    static_cast<std::ptrdiff_t>(batch.colours.size()),
                    batch.colours.data(), STREAM_DRAW);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, nullptr);
        } else {
            glVertexPointer(3, GL_FLOAT, 0, batch.positions.data());
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, batch.colours.data());
        }
        glDrawArrays(
            mode, 0, static_cast<GLsizei>(batch.vertexCount()));
    };

    if (settings_.wireframeSolid) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    submit(solid_, GL_TRIANGLES);
    if (settings_.wireframeSolid) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    submit(slices_, GL_TRIANGLES);
    submit(isosurface_, GL_TRIANGLES);
    submit(vortexSurface_, GL_TRIANGLES);

    glShadeModel(GL_SMOOTH);
    glLineWidth(1.0f);
    if (settings_.showGrid) {
        const float lowest[3] = {box.lowX, box.lowY, box.lowZ};
        const float highest[3] = {box.highX, box.highY, box.highZ};
        for (int face = 0; face < 6; ++face) {
            const int fixedAxis = face / 2;
            const bool atHigh = (face % 2) == 1;
            float normalAxis[3] = {0.0f, 0.0f, 0.0f};
            normalAxis[fixedAxis] = atHigh ? 1.0f : -1.0f;
            float centreAxis[3];
            for (int axis = 0; axis < 3; ++axis) {
                centreAxis[axis] = 0.5f * (lowest[axis] + highest[axis]);
            }
            centreAxis[fixedAxis] =
                atHigh ? highest[fixedAxis] : lowest[fixedAxis];
            const Vector3 normal{
                normalAxis[0], normalAxis[1], normalAxis[2]};
            const Vector3 centre{
                centreAxis[0], centreAxis[1], centreAxis[2]};
            if (dot(normal, basis.eye - centre) < 0.0f) {
                submit(grid_[static_cast<std::size_t>(face)], GL_LINES);
            }
        }
    }
    glLineWidth(2.5f);
    submit(markers_, GL_LINES);
    glLineWidth(1.5f);
    submit(box_, GL_LINES);
    submit(streamlines_, GL_LINES);
    glLineWidth(3.0f);
    submit(vortexLines_, GL_LINES);
    submit(tracers_, GL_LINES);
    glLineWidth(1.0f);

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    unbindBuffers();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);

    window.popGLStates();
    window.resetGLStates();
}

Viewport3D::Pick Viewport3D::pickAt(
    const sf::FloatRect& area,
    float screenX,
    float screenY) const {
    Pick pick;
    if (!frame_ || area.size.x < 1.0f || area.size.y < 1.0f) {
        return pick;
    }
    const VtkFrame& frame = *frame_;
    if (frame.nx == 0 || frame.ny == 0 || frame.nz == 0) {
        return pick;
    }

    const CameraBasis basis = basisOf(camera_);
    const float aspect = area.size.x / area.size.y;
    const float ndcX =
        2.0f * (screenX - area.position.x) / area.size.x - 1.0f;
    const float ndcY =
        1.0f - 2.0f * (screenY - area.position.y) / area.size.y;
    const float tangent = std::tan(0.5f * FIELD_OF_VIEW);

    Vector3 origin = basis.eye;
    Vector3 direction;
    if (camera_.orthographic) {
        const float halfHeight = camera_.distance * tangent;
        origin = basis.eye +
            basis.right * (ndcX * halfHeight * aspect) +
            basis.up * (ndcY * halfHeight);
        direction = basis.forward;
    } else {
        direction = normalise(
            basis.forward +
            basis.right * (ndcX * tangent * aspect) +
            basis.up * (ndcY * tangent));
    }

    const Bounds box = bounds();
    const float low[3] = {box.lowX, box.lowY, box.lowZ};
    const float high[3] = {box.highX, box.highY, box.highZ};
    const float start[3] = {origin.x, origin.y, origin.z};
    const float step[3] = {direction.x, direction.y, direction.z};

    float enter = 0.0f;
    float leave = std::numeric_limits<float>::max();
    int entryAxis = -1;
    bool entryHigh = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(step[axis]) < 1.0e-12f) {
            if (start[axis] < low[axis] || start[axis] > high[axis]) {
                return pick;
            }
            continue;
        }
        float first = (low[axis] - start[axis]) / step[axis];
        float second = (high[axis] - start[axis]) / step[axis];
        bool hitHigh = false;
        if (first > second) {
            std::swap(first, second);
            hitHigh = true;
        }
        if (first > enter) {
            enter = first;
            entryAxis = axis;
            entryHigh = hitHigh;
        }
        leave = std::min(leave, second);
        if (enter > leave) {
            return pick;
        }
    }
    if (leave < 0.0f) {
        return pick;
    }
    enter = std::max(enter, 0.0f);

    const auto lowOf = [&frame](int axis, std::size_t cell) {
        if (axis == 0) {
            return static_cast<float>(frame.cellLeft(cell));
        }
        if (axis == 1) {
            return static_cast<float>(frame.cellBottom(cell));
        }
        return static_cast<float>(frame.cellFront(cell));
    };
    const auto highOf = [&frame](int axis, std::size_t cell) {
        if (axis == 0) {
            return static_cast<float>(frame.cellRight(cell));
        }
        if (axis == 1) {
            return static_cast<float>(frame.cellTop(cell));
        }
        return static_cast<float>(frame.cellBack(cell));
    };
    const std::size_t counts[3] = {frame.nx, frame.ny, frame.nz};

    float point[3];
    for (int axis = 0; axis < 3; ++axis) {
        point[axis] = start[axis] + enter * step[axis];
    }
    long long cell[3] = {
        static_cast<long long>(frame.columnAt(point[0])),
        static_cast<long long>(frame.rowAt(point[1])),
        static_cast<long long>(frame.planeAt(point[2]))
    };

    pick.hit = true;
    pick.i = static_cast<std::size_t>(cell[0]);
    pick.j = static_cast<std::size_t>(cell[1]);
    pick.k = static_cast<std::size_t>(cell[2]);
    pick.x = point[0];
    pick.y = point[1];
    pick.z = point[2];
    if (entryAxis >= 0) {
        pick.face = entryAxis * 2 + (entryHigh ? 1 : 0);
    }

    int direction3[3];
    float nextCrossing[3];
    for (int axis = 0; axis < 3; ++axis) {
        if (step[axis] > 0.0f) {
            direction3[axis] = 1;
            nextCrossing[axis] =
                (highOf(axis, static_cast<std::size_t>(cell[axis])) -
                 start[axis]) / step[axis];
        } else if (step[axis] < 0.0f) {
            direction3[axis] = -1;
            nextCrossing[axis] =
                (lowOf(axis, static_cast<std::size_t>(cell[axis])) -
                 start[axis]) / step[axis];
        } else {
            direction3[axis] = 0;
            nextCrossing[axis] = std::numeric_limits<float>::max();
        }
    }

    const std::size_t limit = frame.nx + frame.ny + frame.nz + 3u;
    for (std::size_t visited = 0; visited < limit; ++visited) {
        const std::size_t index = frame.cellIndex(
            static_cast<std::size_t>(cell[0]),
            static_cast<std::size_t>(cell[1]),
            static_cast<std::size_t>(cell[2]));
        if (!frame.solid.empty() && frame.solid[index] != 0) {
            pick.solidHit = true;
            pick.i = static_cast<std::size_t>(cell[0]);
            pick.j = static_cast<std::size_t>(cell[1]);
            pick.k = static_cast<std::size_t>(cell[2]);
            pick.x = static_cast<float>(frame.cellCentreX(pick.i));
            pick.y = static_cast<float>(frame.cellCentreY(pick.j));
            pick.z = static_cast<float>(frame.cellCentreZ(pick.k));
            const auto found = frame.scalars.find("objectId");
            if (found != frame.scalars.end() && index < found->second.size()) {
                pick.objectId = static_cast<int>(std::lround(found->second[index]));
            }
            return pick;
        }
        int axis = 0;
        if (nextCrossing[1] < nextCrossing[axis]) {
            axis = 1;
        }
        if (nextCrossing[2] < nextCrossing[axis]) {
            axis = 2;
        }
        if (direction3[axis] == 0 ||
            nextCrossing[axis] > leave ||
            nextCrossing[axis] == std::numeric_limits<float>::max()) {
            break;
        }
        cell[axis] += direction3[axis];
        if (cell[axis] < 0 ||
            cell[axis] >= static_cast<long long>(counts[axis])) {
            break;
        }
        nextCrossing[axis] = direction3[axis] > 0
            ? (highOf(axis, static_cast<std::size_t>(cell[axis])) -
               start[axis]) / step[axis]
            : (lowOf(axis, static_cast<std::size_t>(cell[axis])) -
               start[axis]) / step[axis];
    }
    return pick;
}

} // namespace maskui
