#pragma once
#include "Config.hpp"
#include "Mesh.hpp"
#include "Restart.hpp"
#include "RigidBody.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if defined(__CUDACC__)
#define CFD_HD __host__ __device__
#else
#define CFD_HD
#endif

struct GasModel {
    float gamma1 = 1.4f;
    float R1 = 287.05f;
    float gamma2 = 1.667f;
    float R2 = 2077.0f;
    bool species = false;
    bool active = true;

    float cv1 = 0.0f, cv2 = 0.0f;
    float cp1 = 0.0f, cp2 = 0.0f;

    void prepare();

    CFD_HD float gammaOf(float y) const {
        if (!species || !active)
            return gamma1;
        const float cp = cp1 + y * (cp2 - cp1);
        const float cv = cv1 + y * (cv2 - cv1);
        return cp / cv;
    }
    CFD_HD float gasConstantOf(float y) const {
        if (!species || !active)
            return R1;
        return R1 + y * (R2 - R1);
    }
};

struct Block {
    int nx = 0;
    int ny = 0;
    int nz = 1;
    int ghost = 2;
    int stride = 0;
    int rows = 0;
    float dx = 1.0f;
    float dy = 1.0f;
    float dz = 1.0f;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float z0 = 0.0f;

    float* rho = nullptr;
    float* rhou = nullptr;
    float* rhov = nullptr;
    float* rhow = nullptr;
    float* rhoE = nullptr;
    float* rhoY = nullptr;

    const uint8_t* solid = nullptr;
    const float* solidU = nullptr;
    const float* solidV = nullptr;
    const float* solidW = nullptr;

    const float* widths = nullptr;
    const float* heights = nullptr;
    const float* depths = nullptr;
    const float* centresX = nullptr;
    const float* centresY = nullptr;
    const float* centresZ = nullptr;

    CFD_HD bool spans() const { return nz > 1; }
    CFD_HD int ghostZ() const { return nz > 1 ? ghost : 0; }
    CFD_HD int plane() const { return stride * rows; }
    CFD_HD int layers() const { return nz + 2 * ghostZ(); }
    CFD_HD int index(int i, int j, int k = 0) const {
        return (k + ghostZ()) * plane() + (j + ghost) * stride + (i + ghost);
    }
    CFD_HD int cells() const { return plane() * layers(); }
    CFD_HD float widthAt(int i) const {
        return widths ? widths[i + ghost] : dx;
    }
    CFD_HD float heightAt(int j) const {
        return heights ? heights[j + ghost] : dy;
    }
    CFD_HD float depthAt(int k) const {
        return depths ? depths[k + ghost] : dz;
    }
    CFD_HD float cellX(int i) const {
        return centresX ? centresX[i + ghost] : x0 + (i + 0.5f) * dx;
    }
    CFD_HD float cellY(int j) const {
        return centresY ? centresY[j + ghost] : y0 + (j + 0.5f) * dy;
    }
    CFD_HD float cellZ(int k) const {
        return centresZ ? centresZ[k + ghost] : z0 + (k + 0.5f) * dz;
    }
    CFD_HD bool stretched() const { return widths != nullptr; }
};

struct SideState {
    BoundaryKind kind = BoundaryKind::Wall;
    bool noSlip = true;
    float speed = 0.0f;
    float from = 0.0f;
    float to = 1.0f;
    float from2 = 0.0f;
    float to2 = 1.0f;
    bool banded = false;
    bool interior = false;
};

struct BlockBoundaries {
    SideState left, right, bottom, top, front, back;
    float pInf = 101325.0f;
    float T0 = 288.15f;
    float mach = 0.5f;
    float inletY = 0.0f;
    float inletZ = 0.5f;
    int spanI0 = 0;
    int spanJ0 = 0;
    int spanK0 = 0;
    int spanNx = 0;
    int spanNy = 0;
    int spanNz = 0;
};

struct Workspace {
    std::vector<float> fluxX;
    std::vector<float> fluxY;
    std::vector<float> fluxZ;
    std::vector<float> primitive[7];
    void fit(const Block& block, int components);
};

void fillGhostCells(Block& block,
                    const BlockBoundaries& sides,
                    const GasModel& gas);

void fillSolidCells(Block& block, const GasModel& gas);

float blockTimeStep(const Block& block, const GasModel& gas, float cfl);

void advanceStage(Block& in,
                  const Block& keep,
                  Block& out,
                  const BlockBoundaries& sides,
                  const GasModel& gas,
                  float dt,
                  float a,
                  float b,
                  LimiterKind limiter,
                  float diffusivity,
                  Workspace& work);

// The five conserved variables of a frame that does not spell them out,
// rebuilt from density, the velocity vector and pressure - the three things
// every frame shows anyway. Returns false when the frame is missing one of
// them; a frame that already carries the conserved arrays is left alone and
// returns true.
bool rebuildConservedState(RestartData& state,
                           const GasModel& gas,
                           std::size_t cells,
                           bool wantSpecies);

#ifdef USE_CUDA
struct CompressibleDevice;

bool compressibleCudaAvailable();
CompressibleDevice* compressibleCudaCreate(int nx, int ny, int nz, int ghost,
                                           bool species);
void compressibleCudaDestroy(CompressibleDevice* device);
void compressibleCudaUploadSolid(CompressibleDevice* device,
                                 const uint8_t* mask,
                                 const float* velX,
                                 const float* velY,
                                 const float* velZ);
void compressibleCudaUpload(CompressibleDevice* device, int set,
                            const float* const* host);
void compressibleCudaDownload(CompressibleDevice* device, int set,
                              float* const* host);
float compressibleCudaTimeStep(CompressibleDevice* device, const Block& shape,
                               const GasModel& gas, float cfl);
void compressibleCudaStage(CompressibleDevice* device, const Block& shape,
                           int inSet, int keepSet, int outSet,
                           const GasModel& gas, const BlockBoundaries& sides,
                           float dt, float a, float b, int limiter,
                           float diffusivity);
#endif

class AmrHierarchy;
class AmrDriver;
struct AmrSettings;

class CompressibleRun {
public:
    CompressibleRun(const Config& cfg, Mesh& mesh);
    ~CompressibleRun();

    bool setInitialState(RestartData&& state, const std::string& framePrefix);
    void reportCore() const;
    void seedVortices();
    void run();

private:
    const Config& cfg;
    Mesh& mesh;

    int nx = 0, ny = 0, nz = 1, ghost = 2;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    bool volumetric = false;
    double currentTime = 0.0;
    int step = 0;
    float dt = 0.0f;
    std::string framePrefix = "solution";
    std::filesystem::path outputPath;
    bool hasRestartState = false;

    GasModel gas;
    BlockBoundaries sides;
    std::vector<uint8_t> solidMask;
    std::vector<float> faceX, faceY, faceZ;
    std::vector<float> cellWidths, cellHeights, cellDepths;
    std::vector<float> cellCentresX, cellCentresY, cellCentresZ;
    bool stretched = false;
    std::vector<float> solidVelX;
    std::vector<float> solidVelY;
    std::vector<float> solidVelZ;

    std::vector<RigidBody> bodies;
    std::vector<RestartData::BodyState> restartBodies;
    bool bodiesMove = false;
    bool bodiesFree = false;
    bool bodyCollisions = false;
    int contactsReported = 0;

    std::vector<float> rho, rhou, rhov, rhow, rhoE, rhoY;
    std::vector<float> rho1, rhou1, rhov1, rhow1, rhoE1, rhoY1;
    std::vector<float> rho2, rhou2, rhov2, rhow2, rhoE2, rhoY2;

    std::vector<float> pressureMean;
    std::vector<float> pressureFast;
    std::vector<float> pressureRms;
    std::vector<float> crossingRate;
    std::vector<int8_t> lastSign;
    bool acousticsReady = false;

    std::vector<Microphone> mics;
    std::vector<std::vector<float>> micSamples;
    std::vector<float> micTimes;
    Workspace work;
    std::unique_ptr<AmrHierarchy> tree;
    std::unique_ptr<AmrSettings> amr;
    std::unique_ptr<AmrDriver> driver;
    int sinceRegrid = 0;
    bool onDevice = false;
#ifdef USE_CUDA
    CompressibleDevice* device = nullptr;
#endif

    Block view(std::vector<float>& r,
               std::vector<float>& ru,
               std::vector<float>& rv,
               std::vector<float>& rw,
               std::vector<float>& re,
               std::vector<float>& ry);

    void allocate();
    void setUpAmr();
    void regridIfDue();
    void reportAmr() const;
    void writeAmrFrame(int stepNumber) const;
    void buildGrid();
    void applyGridToBlock(Block& block) const;
    int columnAt(float x) const;
    int rowAt(float y) const;
    int planeAt(float z) const;
    void initialise();
    void computeStep();
    float timeStep(const Block& block);
    void syncFromDevice();
    void syncToDevice();
    void updateAcoustics(float stepDt);
    void sampleMicrophones();
    void writeMicrophones() const;
    void writeMicrophoneAudio() const;

    void resolveBodyMotion();
    void reportBodies() const;
    void bodyForces();
    void advanceBodies(float stepDt);
    void applyBodyPoses();
    void refreshSolidMask();
    void saveVTK(int stepNumber) const;
    void reportStep() const;

    std::vector<float> primitive(const char* what) const;
};
