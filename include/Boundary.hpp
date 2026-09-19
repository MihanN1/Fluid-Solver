#pragma once
#include <string>
#include <vector>

enum class BoundaryKind {
    Inlet,
    Outlet,
    Wall,
    MovingWall,
    Slip
};

enum class InletProfile {
    Uniform,
    Parabolic,
    ParabolicSpan
};

enum class BoundarySide {
    Left = 0,
    Right = 1,
    Bottom = 2,
    Top = 3,
    Front = 4,
    Back = 5
};

constexpr int kBoundarySides = 6;

struct BoundarySpec {
    BoundaryKind kind = BoundaryKind::Slip;
    InletProfile profile = InletProfile::Uniform;

    float speed = 0.0f;

    bool speedSet = false;

    float from = 0.0f;
    float to = 1.0f;

    float from2 = 0.0f;
    float to2 = 1.0f;
};

struct BoundarySet {
    BoundarySpec side[kBoundarySides];

    const BoundarySpec& operator[](BoundarySide s) const {
        return side[static_cast<int>(s)];
    }
    BoundarySpec& operator[](BoundarySide s) {
        return side[static_cast<int>(s)];
    }
};

bool parseBoundaryKind(const std::string& text,
                       BoundaryKind& out,
                       std::string& error);

bool parseInletProfile(const std::string& text,
                       InletProfile& out,
                       std::string& error);

const char* boundaryKindName(BoundaryKind kind);
const char* inletProfileName(InletProfile profile);
const char* boundarySideName(BoundarySide side);


void inletBandCells(const BoundarySpec& spec,
                    int cellsAlongSide,
                    int& first,
                    int& last);

// The same band along the second tangential axis of the face. Left and right
// span (y, z), bottom and top span (x, z), front and back span (x, y), so the
// second axis is z everywhere except on front and back.
void inletBandCellsSpan(const BoundarySpec& spec,
                        int cellsAlongSide,
                        int& first,
                        int& last);

float inletVelocityAt(const BoundarySpec& spec, float t);

// t runs along the first tangential axis, s along the second. spanResolved is
// false when that second axis holds a single cell, which is what makes a
// nz = 1 run reproduce the plane case exactly instead of picking up the peak
// of a parabola it cannot resolve.
float inletVelocityAt(const BoundarySpec& spec,
                      float t,
                      float s,
                      bool spanResolved);

enum class CaseType {
    Channel,
    Cavity,
    ShockTube
};

bool parseCaseType(const std::string& text, CaseType& out, std::string& error);
const char* caseTypeName(CaseType type);

BoundarySet defaultChannelBoundaries();

BoundarySet cavityBoundaries(float lidSpeed);
BoundarySet closedBoundaries();

struct DomainExtent {
    float Lx = 1.0f;
    float Ly = 1.0f;
    float Lz = 1.0f;
    int nx = 0;
    int ny = 0;
    int nz = 1;
};

bool checkBoundaryMassBalance(const BoundarySet& sides,
                              float defaultSpeed,
                              const DomainExtent& domain,
                              const std::vector<int>& solid,
                              std::string& error,
                              double extraInflow = 0.0);

std::string boundaryHelp();
