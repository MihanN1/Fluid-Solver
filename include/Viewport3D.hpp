#pragma once

#include "VtkFrame.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {
class RenderWindow;
}

namespace maskui {

sf::Color scalarColor(double value, double minimum, double maximum);

struct Camera3D {
    float targetX = 0.5f;
    float targetY = 0.5f;
    float targetZ = 0.5f;
    float distance = 3.0f;
    float yaw = 0.6f;
    float pitch = 0.4f;
    bool orthographic = false;
};

enum class VolumeField {
    Pressure,
    Speed,
    VelocityX,
    VelocityY,
    VelocityZ,
    Vorticity,
    QCriterion,
    Scalar
};

struct Viewport3DSettings {
    bool showBox = true;
    bool showGrid = false;
    bool showSolid = true;
    bool showSlices = true;
    bool sliceX = false;
    bool sliceY = false;
    bool sliceZ = true;
    std::size_t sliceIndexX = 0;
    std::size_t sliceIndexY = 0;
    std::size_t sliceIndexZ = 0;
    bool showIsosurface = false;
    VolumeField isoField = VolumeField::Pressure;
    float isoLevel = 0.5f;
    bool showVortices = false;
    float vortexLevel = 0.15f;
    bool showStreamlines = false;
    int streamlineSeeds = 400;
    int streamlineSteps = 400;
    bool animateTracers = false;
    VolumeField colourBy = VolumeField::Speed;
    std::string colourScalar;
    bool trimmedRange = true;
    bool wireframeSolid = false;
    bool showMicrophones = true;
    std::vector<std::array<float, 3>> microphones;
};

struct ScalarVolume {
    std::size_t nx = 0;
    std::size_t ny = 0;
    std::size_t nz = 0;
    std::vector<float> values;
    DataRange range;
    DataRange trimmedRange;

    std::size_t index(std::size_t i, std::size_t j, std::size_t k) const {
        return (k * ny + j) * nx + i;
    }
    float at(std::size_t i, std::size_t j, std::size_t k) const {
        return values[index(i, j, k)];
    }
    bool empty() const { return values.empty(); }
};

ScalarVolume sampleVolumeField(
    const VtkFrame& frame,
    VolumeField field,
    const std::string& scalarName);

std::vector<float> cellCentresX(const VtkFrame& frame);
std::vector<float> cellCentresY(const VtkFrame& frame);
std::vector<float> cellCentresZ(const VtkFrame& frame);

struct SurfaceMesh {
    std::vector<float> positions;
    std::vector<float> normals;

    std::size_t triangleCount() const { return positions.size() / 9u; }
    void clear();
};

SurfaceMesh marchingCubes(
    const ScalarVolume& volume,
    const std::vector<float>& xs,
    const std::vector<float>& ys,
    const std::vector<float>& zs,
    float level);

double surfaceArea(const SurfaceMesh& mesh);
bool surfaceIsClosed(const SurfaceMesh& mesh, float tolerance);

struct StreamlineOptions {
    int steps = 400;
    float stepTime = 0.0f;
    bool stopAtSolid = true;
};

struct Streamline {
    std::vector<float> points;
    std::vector<float> speeds;

    std::size_t pointCount() const { return points.size() / 3u; }
    double length() const;
};

bool sampleVelocityAt(
    const VtkFrame& frame,
    const std::array<float, 3>& position,
    std::array<float, 3>& velocity);

Streamline traceStreamline(
    const VtkFrame& frame,
    const std::array<float, 3>& seed,
    const StreamlineOptions& options);

std::vector<std::array<float, 3>> streamlineSeedPoints(
    const VtkFrame& frame,
    int count);

struct VortexCore {
    std::vector<float> segments;
};

VortexCore vortexCoreLines(
    const VtkFrame& frame,
    const ScalarVolume& criterion,
    float level);

class Viewport3D {
public:
    struct Pick {
        bool hit = false;
        std::size_t i = 0;
        std::size_t j = 0;
        std::size_t k = 0;
        int face = -1;
        bool solidHit = false;
        int objectId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    void setFrame(std::shared_ptr<const VtkFrame> frame);
    const std::shared_ptr<const VtkFrame>& frame() const;
    void setSettings(const Viewport3DSettings& settings);
    const Viewport3DSettings& settings() const;
    Camera3D& camera();
    const Camera3D& camera() const;

    void frameAll();
    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float amount);
    void setView(int axis, bool negative);

    void draw(sf::RenderWindow& window, const sf::FloatRect& area);
    void advance(float seconds);

    Pick pickAt(const sf::FloatRect& area, float screenX, float screenY) const;

    std::size_t triangleCount() const;
    std::size_t lineCount() const;

private:
    struct Batch {
        std::vector<float> positions;
        std::vector<std::uint8_t> colours;
        std::array<float, 3> lowest{{0.0f, 0.0f, 0.0f}};
        std::array<float, 3> highest{{0.0f, 0.0f, 0.0f}};

        void clear();
        void reserve(std::size_t vertices);
        void add(float x, float y, float z, const sf::Color& colour);
        std::size_t vertexCount() const { return positions.size() / 3u; }
        bool empty() const { return positions.empty(); }
    };

    struct Bounds {
        float lowX = 0.0f;
        float lowY = 0.0f;
        float lowZ = 0.0f;
        float highX = 1.0f;
        float highY = 1.0f;
        float highZ = 1.0f;
    };

    void rebuildAll();
    void rebuildBox();
    void rebuildSolid();
    void rebuildSlices();
    void rebuildIsosurface();
    void rebuildVortices();
    void rebuildStreamlines();
    void rebuildTracers();
    void rebuildMarkers();
    Bounds bounds() const;
    DataRange colourRange(const ScalarVolume& volume) const;
    void appendSurface(
        Batch& batch,
        const SurfaceMesh& mesh,
        const ScalarVolume& colourField,
        const DataRange& range);

    std::shared_ptr<const VtkFrame> frame_;
    Viewport3DSettings settings_;
    Camera3D camera_;
    float phase_ = 0.0f;

    Batch box_;
    std::array<Batch, 6> grid_;
    Batch solid_;
    Batch slices_;
    Batch isosurface_;
    Batch vortexSurface_;
    Batch vortexLines_;
    Batch streamlines_;
    Batch tracers_;
    Batch markers_;
    std::vector<Streamline> paths_;
    unsigned int positionBuffer_ = 0;
    unsigned int colourBuffer_ = 0;
};

} // namespace maskui
