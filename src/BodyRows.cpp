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

namespace {

std::string trimEnds(const std::string& text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last &&
           std::isspace(static_cast<unsigned char>(text[first])))
        ++first;
    while (last > first &&
           std::isspace(static_cast<unsigned char>(text[last - 1])))
        --last;
    return text.substr(first, last - first);
}

// The profiles grammar spells the three slice angles both ways, so a line the
// user typed by hand reads the same as one this panel wrote.
std::string canonicalProfileKey(const std::string& name) {
    if (name == "angleX")
        return "ax";
    if (name == "angleY")
        return "ay";
    if (name == "angleZ")
        return "az";
    return name;
}

} // namespace

std::vector<std::string> splitProfileEntries(const std::string& line) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= line.size()) {
        const std::size_t mark = line.find(';', pos);
        const std::string token = trimEnds(
            line.substr(pos, mark == std::string::npos
                                 ? std::string::npos
                                 : mark - pos));
        if (!token.empty())
            out.push_back(token);
        if (mark == std::string::npos)
            break;
        pos = mark + 1;
    }
    return out;
}

std::string joinProfileEntries(const std::vector<std::string>& entries) {
    std::string out;
    for (const std::string& entry : entries) {
        if (entry.empty())
            continue;
        if (!out.empty())
            out += ';';
        out += entry;
    }
    return out;
}

std::string profileFileOf(const std::string& entry) {
    const std::size_t at = entry.find('@');
    return trimEnds(at == std::string::npos ? entry : entry.substr(0, at));
}

double profileSetting(const std::string& entry,
                      const std::string& name,
                      double fallback,
                      bool* found) {
    if (found)
        *found = false;
    const std::size_t at = entry.find('@');
    if (at == std::string::npos)
        return fallback;
    const std::string wanted = canonicalProfileKey(name);
    const std::string settings = entry.substr(at + 1);
    std::size_t pos = 0;
    while (pos <= settings.size()) {
        const std::size_t mark = settings.find(',', pos);
        const std::string token = trimEnds(
            settings.substr(pos, mark == std::string::npos
                                     ? std::string::npos
                                     : mark - pos));
        const std::size_t equals = token.find('=');
        if (equals != std::string::npos &&
            canonicalProfileKey(trimEnds(token.substr(0, equals))) == wanted) {
            if (found)
                *found = true;
            return std::atof(token.substr(equals + 1).c_str());
        }
        if (mark == std::string::npos)
            break;
        pos = mark + 1;
    }
    return fallback;
}

// Replaces the setting where it already is, so the order the user wrote stays
// the order it keeps, and appends it otherwise. Everything this panel does not
// know about - invert=, a spelling it does not use - is carried across
// untouched rather than rewritten from the fields it does know.
void setProfileSetting(std::string& entry,
                       const std::string& name,
                       double value) {
    const std::string wanted = canonicalProfileKey(name);
    const std::string written = wanted + "=" + formatNumber(value);
    const std::size_t at = entry.find('@');
    if (at == std::string::npos) {
        entry = trimEnds(entry) + "@" + written;
        return;
    }
    const std::string file = entry.substr(0, at + 1);
    const std::string settings = entry.substr(at + 1);
    std::string rebuilt;
    bool replaced = false;
    std::size_t pos = 0;
    while (pos <= settings.size()) {
        const std::size_t mark = settings.find(',', pos);
        const std::string token = trimEnds(
            settings.substr(pos, mark == std::string::npos
                                     ? std::string::npos
                                     : mark - pos));
        if (!token.empty()) {
            const std::size_t equals = token.find('=');
            const bool match =
                equals != std::string::npos &&
                canonicalProfileKey(trimEnds(token.substr(0, equals))) ==
                    wanted;
            if (!rebuilt.empty())
                rebuilt += ',';
            if (match) {
                rebuilt += written;
                replaced = true;
            } else {
                rebuilt += token;
            }
        }
        if (mark == std::string::npos)
            break;
        pos = mark + 1;
    }
    if (!replaced) {
        if (!rebuilt.empty())
            rebuilt += ',';
        rebuilt += written;
    }
    entry = file + rebuilt;
}

BodyPlacement readPlacement(const std::string& profiles, int object) {
    BodyPlacement out;
    if (object < 1)
        return out;
    const std::vector<std::string> entries = splitProfileEntries(profiles);
    const std::size_t index = static_cast<std::size_t>(object - 1);
    if (index >= entries.size())
        return out;

    const std::string& entry = entries[index];
    out.present = true;
    out.file = profileFileOf(entry);

    bool hasX = false;
    bool hasY = false;
    bool hasZ = false;
    out.x = profileSetting(entry, "x", 0.0, &hasX);
    out.y = profileSetting(entry, "y", 0.0, &hasY);
    out.z = profileSetting(entry, "z", 0.0, &hasZ);
    out.placed = hasX || hasY || hasZ;

    bool hasSize = false;
    out.size = profileSetting(entry, "size", 0.0, &hasSize);
    out.sized = hasSize;

    out.rot = profileSetting(entry, "rot");
    out.angleX = profileSetting(entry, "ax");
    out.angleY = profileSetting(entry, "ay");
    out.angleZ = profileSetting(entry, "az");
    return out;
}

void writePlacement(std::string& profiles, int object,
                    const BodyPlacement& values) {
    if (object < 1)
        return;
    std::vector<std::string> entries = splitProfileEntries(profiles);
    const std::size_t index = static_cast<std::size_t>(object - 1);
    if (index >= entries.size()) {
        if (values.file.empty())
            return;
        while (entries.size() <= index)
            entries.push_back(values.file);
    }

    std::string& entry = entries[index];
    if (values.placed) {
        setProfileSetting(entry, "x", values.x);
        setProfileSetting(entry, "y", values.y);
        setProfileSetting(entry, "z", values.z);
    }
    if (values.sized)
        setProfileSetting(entry, "size", values.size);
    if (values.rot != 0.0 || profileSetting(entry, "rot") != 0.0)
        setProfileSetting(entry, "rot", values.rot);
    if (values.angleX != 0.0 || profileSetting(entry, "ax") != 0.0)
        setProfileSetting(entry, "ax", values.angleX);
    if (values.angleY != 0.0 || profileSetting(entry, "ay") != 0.0)
        setProfileSetting(entry, "ay", values.angleY);
    if (values.angleZ != 0.0 || profileSetting(entry, "az") != 0.0)
        setProfileSetting(entry, "az", values.angleZ);

    profiles = joinProfileEntries(entries);
}

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
