#include "BodyTrack.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace maskui {
namespace {

std::string lower(std::string text) {
    for (char& character : text)
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    return text;
}

std::string itemValue(const std::string& block, const std::string& key) {
    std::size_t at = 0;
    while (at <= block.size()) {
        std::size_t comma = block.find(',', at);
        if (comma == std::string::npos)
            comma = block.size();
        const std::string item = block.substr(at, comma - at);
        const std::size_t equals = item.find('=');
        if (equals != std::string::npos &&
            lower(item.substr(0, equals)) == key)
            return item.substr(equals + 1);
        if (comma == block.size())
            break;
        at = comma + 1;
    }
    return std::string();
}

double numberValue(const std::string& block,
                   const std::string& key,
                   double fallback) {
    const std::string text = itemValue(block, key);
    if (text.empty())
        return fallback;
    try {
        const double parsed = std::stod(text);
        return std::isfinite(parsed) ? parsed : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

std::string trimmed(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

bool usesDepth(const std::vector<BodyPose>& track) {
    for (const BodyPose& pose : track)
        if (pose.z != 0.0)
            return true;
    return false;
}

BodyPose neighbourPose(const std::vector<BodyPose>& track,
                       std::ptrdiff_t index,
                       bool closed) {
    const std::ptrdiff_t last =
        static_cast<std::ptrdiff_t>(track.size()) - 1;
    if (index >= 0 && index <= last)
        return track[static_cast<std::size_t>(index)];
    const BodyPose& edge = index < 0 ? track.front() : track.back();
    const BodyPose& inward =
        index < 0 ? track[1] : track[static_cast<std::size_t>(last - 1)];
    BodyPose outer = edge;
    outer.x = 2.0 * edge.x - inward.x;
    outer.y = 2.0 * edge.y - inward.y;
    outer.z = 2.0 * edge.z - inward.z;
    outer.rot = 2.0 * edge.rot - inward.rot;
    if (closed && last >= 2) {
        const BodyPose& across =
            index < 0 ? track[static_cast<std::size_t>(last - 1)] : track[1];
        outer.x = across.x;
        outer.y = across.y;
        outer.z = across.z;
    }
    return outer;
}

double catmullRom(double p0, double p1, double p2, double p3, double t) {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
                  (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

} // namespace

std::vector<BodyPose> parseBodyTrack(const std::string& entry) {
    std::vector<BodyPose> track;
    std::size_t at = 0;
    while (at < entry.size()) {
        const std::size_t open = entry.find('@', at);
        if (open == std::string::npos)
            break;
        std::size_t close = entry.find('@', open + 1);
        if (close == std::string::npos)
            close = entry.size();
        const std::string block =
            trimmed(entry.substr(open + 1, close - open - 1));
        BodyPose pose;
        pose.time = numberValue(block, "t", 0.0);
        pose.x = numberValue(block, "x", 0.0);
        pose.y = numberValue(block, "y", 0.0);
        pose.z = numberValue(block, "z", 0.0);
        pose.rot = numberValue(block, "rot", 0.0);
        const std::string interp = itemValue(block, "interp");
        const std::string ease = itemValue(block, "ease");
        if (!interp.empty())
            pose.interp = interp;
        if (!ease.empty())
            pose.ease = ease;
        track.push_back(pose);
        at = close;
    }
    std::sort(track.begin(), track.end(),
              [](const BodyPose& a, const BodyPose& b) {
                  return a.time < b.time;
              });
    return track;
}

std::string formatBodyTrack(const std::vector<BodyPose>& track) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const BodyPose& pose : track)
        out << "@t=" << pose.time << ",x=" << pose.x << ",y=" << pose.y
            << ",z=" << pose.z
            << ",rot=" << pose.rot << ",interp=" << pose.interp
            << ",ease=" << pose.ease;
    return out.str();
}

std::string bodyTrackToMotion(const std::vector<BodyPose>& track) {
    if (track.size() < 2)
        return std::string();

    const bool spatial = usesDepth(track);
    std::ostringstream out;
    out << std::setprecision(9);
    bool wrote = false;
    for (std::size_t k = 0; k + 1 < track.size(); ++k) {
        const BodyPose& from = track[k];
        const BodyPose& to = track[k + 1];
        const double span = to.time - from.time;
        if (!(span > 1e-9))
            continue;
        if (wrote)
            out << ',';
        wrote = true;
        out << '@' << from.time;
        if (from.interp != "linear")
            out << ",interp=" << from.interp << ",ease=" << from.ease;
        out << ",vx=" << (to.x - from.x) / span
            << ",vy=" << (to.y - from.y) / span;
        if (spatial)
            out << ",vz=" << (to.z - from.z) / span;
        out << ",omega=" << (to.rot - from.rot) / span;
    }
    if (!wrote)
        return std::string();
    out << ",@" << track.back().time << ",vx=0,vy=0"
        << (spatial ? ",vz=0" : "") << ",omega=0";
    return out.str();
}

BodyPose bodyPoseAt(const std::vector<BodyPose>& track, double when) {
    BodyPose current;
    current.time = when;
    if (track.empty())
        return current;
    if (when <= track.front().time) {
        current = track.front();
        current.time = when;
        return current;
    }
    if (when >= track.back().time) {
        current = track.back();
        current.time = when;
        return current;
    }
    for (std::size_t k = 0; k + 1 < track.size(); ++k) {
        if (when < track[k].time || when > track[k + 1].time)
            continue;
        const double span = track[k + 1].time - track[k].time;
        const double t = span > 1e-12 ? (when - track[k].time) / span : 0.0;
        current = track[k];
        current.time = when;
        current.x += (track[k + 1].x - track[k].x) * t;
        current.y += (track[k + 1].y - track[k].y) * t;
        current.z += (track[k + 1].z - track[k].z) * t;
        current.rot += (track[k + 1].rot - track[k].rot) * t;
        return current;
    }
    current = track.back();
    current.time = when;
    return current;
}

bool bodyTrackIsClosed(const std::vector<BodyPose>& track) {
    if (track.size() < 3)
        return false;
    const BodyPose& first = track.front();
    const BodyPose& last = track.back();
    double span = 0.0;
    for (const BodyPose& pose : track)
        span = std::max(span,
                        std::fabs(pose.x - first.x) +
                            std::fabs(pose.y - first.y) +
                            std::fabs(pose.z - first.z));
    const double tolerance = std::max(1e-9, span * 1e-6);
    return std::fabs(last.x - first.x) <= tolerance &&
           std::fabs(last.y - first.y) <= tolerance &&
           std::fabs(last.z - first.z) <= tolerance;
}

BodyPose bodyPoseOnCurve(const std::vector<BodyPose>& track, double when) {
    if (track.size() < 3)
        return bodyPoseAt(track, when);
    BodyPose current;
    current.time = when;
    if (when <= track.front().time) {
        current = track.front();
        current.time = when;
        return current;
    }
    if (when >= track.back().time) {
        current = track.back();
        current.time = when;
        return current;
    }
    const bool closed = bodyTrackIsClosed(track);
    for (std::size_t k = 0; k + 1 < track.size(); ++k) {
        if (when < track[k].time || when > track[k + 1].time)
            continue;
        const double span = track[k + 1].time - track[k].time;
        const double t = span > 1e-12 ? (when - track[k].time) / span : 0.0;
        const BodyPose before = neighbourPose(
            track, static_cast<std::ptrdiff_t>(k) - 1, closed);
        const BodyPose after = neighbourPose(
            track, static_cast<std::ptrdiff_t>(k) + 2, closed);
        current = track[k];
        current.time = when;
        current.x = catmullRom(before.x, track[k].x, track[k + 1].x, after.x, t);
        current.y = catmullRom(before.y, track[k].y, track[k + 1].y, after.y, t);
        current.z = catmullRom(before.z, track[k].z, track[k + 1].z, after.z, t);
        current.rot =
            catmullRom(before.rot, track[k].rot, track[k + 1].rot, after.rot, t);
        return current;
    }
    current = track.back();
    current.time = when;
    return current;
}

double bodyCurveLength(const std::vector<BodyPose>& track,
                       int samplesPerLeg) {
    if (track.size() < 2)
        return 0.0;
    const int steps = std::max(1, samplesPerLeg);
    double length = 0.0;
    BodyPose previous = bodyPoseOnCurve(track, track.front().time);
    for (std::size_t k = 0; k + 1 < track.size(); ++k) {
        const double span = track[k + 1].time - track[k].time;
        if (!(span > 0.0))
            continue;
        for (int step = 1; step <= steps; ++step) {
            const BodyPose sample = bodyPoseOnCurve(
                track,
                track[k].time + span * static_cast<double>(step) / steps);
            const double dx = sample.x - previous.x;
            const double dy = sample.y - previous.y;
            const double dz = sample.z - previous.z;
            length += std::sqrt(dx * dx + dy * dy + dz * dz);
            previous = sample;
        }
    }
    return length;
}

std::string bodyCurveToMotion(const std::vector<BodyPose>& track,
                              int stepsPerLeg) {
    if (track.size() < 2)
        return std::string();
    if (track.size() == 2)
        return bodyTrackToMotion(track);

    const int steps = std::max(1, stepsPerLeg);
    const bool spatial = usesDepth(track);
    std::ostringstream out;
    out << std::setprecision(9);
    bool wrote = false;
    BodyPose previous = bodyPoseOnCurve(track, track.front().time);
    for (std::size_t k = 0; k + 1 < track.size(); ++k) {
        const double span = track[k + 1].time - track[k].time;
        if (!(span > 1e-9))
            continue;
        for (int step = 1; step <= steps; ++step) {
            const double when =
                track[k].time + span * static_cast<double>(step) / steps;
            const BodyPose sample = bodyPoseOnCurve(track, when);
            const double leg = when - previous.time;
            if (!(leg > 1e-12)) {
                previous = sample;
                continue;
            }
            if (wrote)
                out << ',';
            wrote = true;
            out << '@' << previous.time << ",interp=constant"
                << ",vx=" << (sample.x - previous.x) / leg
                << ",vy=" << (sample.y - previous.y) / leg;
            if (spatial)
                out << ",vz=" << (sample.z - previous.z) / leg;
            out << ",omega=" << (sample.rot - previous.rot) / leg;
            previous = sample;
        }
    }
    if (!wrote)
        return std::string();
    out << ",@" << track.back().time << ",interp=constant,vx=0,vy=0"
        << (spatial ? ",vz=0" : "") << ",omega=0";
    return out.str();
}

void dropBodyPose(std::vector<BodyPose>& track, const BodyPose& pose) {
    bool replaced = false;
    for (BodyPose& existing : track)
        if (std::fabs(existing.time - pose.time) < 1e-6) {
            existing = pose;
            replaced = true;
        }
    if (!replaced)
        track.push_back(pose);
    std::sort(track.begin(), track.end(),
              [](const BodyPose& a, const BodyPose& b) {
                  return a.time < b.time;
              });
}

bool removeBodyPose(std::vector<BodyPose>& track, double when) {
    const std::size_t before = track.size();
    track.erase(std::remove_if(track.begin(), track.end(),
                               [&](const BodyPose& pose) {
                                   return std::fabs(pose.time - when) < 1e-6;
                               }),
                track.end());
    return track.size() != before;
}

} // namespace maskui
