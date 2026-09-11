#include "BodyRows.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace maskui {
namespace {

std::string formatNumber(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

void add(std::string& into, const std::string& text) {
    if (!into.empty())
        into += ',';
    into += text;
}

void addIf(std::string& into, bool wanted, const char* key, double value) {
    if (wanted)
        add(into, std::string(key) + "=" + formatNumber(value));
}

} // namespace

std::vector<MotionEntry> splitMotionEntries(const std::string& line) {
    std::vector<MotionEntry> out;
    std::size_t pos = 0;
    while (pos < line.size()) {
        std::size_t colon = line.find(':', pos);
        if (colon == std::string::npos)
            break;
        const std::string number = line.substr(pos, colon - pos);
        int object = std::atoi(number.c_str());
        std::size_t next = colon + 1;
        while (next < line.size()) {
            const std::size_t mark = line.find(';', next);
            if (mark == std::string::npos) {
                next = line.size();
                break;
            }
            next = mark;
            break;
        }
        if (object >= 1) {
            MotionEntry entry;
            entry.object = object;
            entry.settings = line.substr(colon + 1, next - colon - 1);
            out.push_back(entry);
        }
        pos = next + 1;
    }
    return out;
}

std::string joinMotionEntries(const std::vector<MotionEntry>& entries) {
    std::string out;
    for (const MotionEntry& entry : entries) {
        if (entry.settings.empty())
            continue;
        if (!out.empty())
            out += ';';
        out += std::to_string(entry.object) + ':' + entry.settings;
    }
    return out;
}

std::string motionEntryOf(const std::string& line, int object) {
    for (const MotionEntry& entry : splitMotionEntries(line))
        if (entry.object == object)
            return entry.settings;
    return std::string();
}

void setMotionEntry(std::string& line, int object,
                    const std::string& settings) {
    std::vector<MotionEntry> entries = splitMotionEntries(line);
    bool found = false;
    for (MotionEntry& entry : entries)
        if (entry.object == object) {
            entry.settings = settings;
            found = true;
        }
    if (!found && !settings.empty()) {
        MotionEntry entry;
        entry.object = object;
        entry.settings = settings;
        entries.push_back(entry);
    }
    line = joinMotionEntries(entries);
}

double motionSetting(const std::string& settings,
                     const std::string& name,
                     double fallback) {
    std::size_t pos = 0;
    while (pos < settings.size()) {
        std::size_t end = settings.find(',', pos);
        if (end == std::string::npos)
            end = settings.size();
        const std::string token = settings.substr(pos, end - pos);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos) {
            std::string key = token.substr(0, eq);
            while (!key.empty() && key.front() == ' ')
                key.erase(key.begin());
            while (!key.empty() && key.back() == ' ')
                key.pop_back();
            std::string lower;
            for (char c : key)
                lower += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            if (lower == name)
                return std::atof(token.c_str() + eq + 1);
        }
        pos = end + 1;
    }
    return fallback;
}

BodyRowValues readBodyRows(const std::string& wallMotion,
                           const std::string& bodyMotion,
                           int object) {
    const std::string wall = motionEntryOf(wallMotion, object);
    const std::string travel = motionEntryOf(bodyMotion, object);

    BodyRowValues values;
    if (!travel.empty())
        values.behaviour =
            motionSetting(travel, "free", 0.0) >= 0.5 ? BodyFree : BodyTravel;
    else if (!wall.empty())
        values.behaviour =
            motionSetting(wall, "slip", 0.0) >= 0.5 ? BodySlip : BodyDrag;

    values.rotation = motionSetting(wall, "rot");
    values.rotationX = motionSetting(wall, "rotx");
    values.rotationY = motionSetting(wall, "roty");
    values.slideX = motionSetting(wall, "slidex");
    values.slideY = motionSetting(wall, "slidey");
    values.slideZ = motionSetting(wall, "slidez");
    values.velocityX = motionSetting(travel, "vx");
    values.velocityY = motionSetting(travel, "vy");
    values.velocityZ = motionSetting(travel, "vz");
    values.spin = motionSetting(travel, "omega");
    values.spinX = motionSetting(travel, "omegax");
    values.spinY = motionSetting(travel, "omegay");
    values.mass = motionSetting(travel, "mass");
    values.density = motionSetting(travel, "density");
    values.inertiaX = motionSetting(travel, "inertiax");
    values.inertiaY = motionSetting(travel, "inertiay");
    values.pins =
        (motionSetting(travel, "pinx") >= 0.5 ? PinSlideX : 0) |
        (motionSetting(travel, "piny") >= 0.5 ? PinSlideY : 0) |
        (motionSetting(travel, "pinrot") >= 0.5 ? PinSpinZ : 0) |
        (motionSetting(travel, "pinz") >= 0.5 ? PinSlideZ : 0) |
        (motionSetting(travel, "pinrotx") >= 0.5 ? PinSpinX : 0) |
        (motionSetting(travel, "pinroty") >= 0.5 ? PinSpinY : 0);
    return values;
}

void writeBodyRows(std::string& wallMotion,
                   std::string& bodyMotion,
                   int object,
                   const BodyRowValues& values) {
    std::string wall;
    std::string travel;

    if (values.behaviour == BodyDrag) {
        addIf(wall, values.rotation != 0.0, "rot", values.rotation);
        addIf(wall, values.rotationX != 0.0, "rotX", values.rotationX);
        addIf(wall, values.rotationY != 0.0, "rotY", values.rotationY);
        addIf(wall, values.slideX != 0.0, "slideX", values.slideX);
        addIf(wall, values.slideY != 0.0, "slideY", values.slideY);
        addIf(wall, values.slideZ != 0.0, "slideZ", values.slideZ);
        if (wall.empty())
            wall = "rot=0";
    } else if (values.behaviour == BodySlip) {
        wall = "slip=1";
    } else if (values.behaviour == BodyTravel ||
               values.behaviour == BodyFree) {
        if (values.behaviour == BodyFree)
            add(travel, "free=1");
        addIf(travel, values.velocityX != 0.0, "vx", values.velocityX);
        addIf(travel, values.velocityY != 0.0, "vy", values.velocityY);
        addIf(travel, values.velocityZ != 0.0, "vz", values.velocityZ);
        addIf(travel, values.spin != 0.0, "omega", values.spin);
        addIf(travel, values.spinX != 0.0, "omegaX", values.spinX);
        addIf(travel, values.spinY != 0.0, "omegaY", values.spinY);
        if (values.behaviour == BodyFree) {
            if (values.density > 0.0)
                add(travel, "density=" + formatNumber(values.density));
            else if (values.mass > 0.0)
                add(travel, "mass=" + formatNumber(values.mass));
            addIf(travel, values.inertiaX > 0.0, "inertiaX", values.inertiaX);
            addIf(travel, values.inertiaY > 0.0, "inertiaY", values.inertiaY);
            if (values.pins & PinSlideX) add(travel, "pinX=1");
            if (values.pins & PinSlideY) add(travel, "pinY=1");
            if (values.pins & PinSpinZ) add(travel, "pinRot=1");
            if (values.pins & PinSlideZ) add(travel, "pinZ=1");
            if (values.pins & PinSpinX) add(travel, "pinRotX=1");
            if (values.pins & PinSpinY) add(travel, "pinRotY=1");
        }
        if (travel.empty())
            travel = "vx=0";
    }

    setMotionEntry(wallMotion, object, wall);
    setMotionEntry(bodyMotion, object, travel);
}

} // namespace maskui
