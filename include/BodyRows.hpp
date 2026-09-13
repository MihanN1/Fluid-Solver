#pragma once

#include <string>
#include <vector>

namespace maskui {

struct MotionEntry {
    int object = 0;
    std::string settings;
};

std::vector<MotionEntry> splitMotionEntries(const std::string& line);

std::string joinMotionEntries(const std::vector<MotionEntry>& entries);

std::string motionEntryOf(const std::string& line, int object);

void setMotionEntry(std::string& line, int object,
                    const std::string& settings);

double motionSetting(const std::string& settings,
                     const std::string& name,
                     double fallback = 0.0);

enum BodyPinFlag {
    PinSlideX = 1,
    PinSlideY = 2,
    PinSpinZ = 4,
    PinSlideZ = 8,
    PinSpinX = 16,
    PinSpinY = 32
};

enum BodyBehaviourKind {
    BodyStatic = 0,
    BodyDrag = 1,
    BodySlip = 2,
    BodyTravel = 3,
    BodyFree = 4
};

struct BodyRowValues {
    int behaviour = BodyStatic;
    double rotation = 0.0;
    double rotationX = 0.0;
    double rotationY = 0.0;
    double slideX = 0.0;
    double slideY = 0.0;
    double slideZ = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double velocityZ = 0.0;
    double spin = 0.0;
    double spinX = 0.0;
    double spinY = 0.0;
    double mass = 0.0;
    double density = 0.0;
    double inertiaX = 0.0;
    double inertiaY = 0.0;
    int pins = 0;
};

std::vector<std::string> splitProfileEntries(const std::string& line);

std::string joinProfileEntries(const std::vector<std::string>& entries);

std::string profileFileOf(const std::string& entry);

double profileSetting(const std::string& entry,
                      const std::string& name,
                      double fallback = 0.0,
                      bool* found = nullptr);

void setProfileSetting(std::string& entry,
                       const std::string& name,
                       double value);

struct BodyPlacement {
    bool present = false;
    std::string file;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool placed = false;
    double size = 0.0;
    bool sized = false;
    double rot = 0.0;
    double angleX = 0.0;
    double angleY = 0.0;
    double angleZ = 0.0;
};

BodyPlacement readPlacement(const std::string& profiles, int object);

void writePlacement(std::string& profiles, int object,
                    const BodyPlacement& values);

BodyRowValues readBodyRows(const std::string& wallMotion,
                           const std::string& bodyMotion,
                           int object);

void writeBodyRows(std::string& wallMotion,
                   std::string& bodyMotion,
                   int object,
                   const BodyRowValues& values);

} // namespace maskui
