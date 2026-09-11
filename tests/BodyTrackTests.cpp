#include "BodyTrack.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
    std::cout << message << "\n";
    return 1;
}

bool near(double value, double expected, double tolerance) {
    return std::fabs(value - expected) <= tolerance;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    using namespace maskui;

    {
        const std::vector<BodyPose> empty = parseBodyTrack("");
        if (!empty.empty())
            return fail("an empty track parsed into something");
        if (!bodyTrackToMotion(empty).empty())
            return fail("an empty track wrote a bodyMotion entry");
        const BodyPose origin = bodyPoseAt(empty, 0.5);
        if (origin.x != 0.0 || origin.y != 0.0 || origin.rot != 0.0)
            return fail("an empty track did not sample as the origin");
    }

    {
        std::vector<BodyPose> track;
        BodyPose first;
        first.time = 0.0;
        first.x = 0.0;
        first.y = 0.0;
        dropBodyPose(track, first);

        BodyPose second;
        second.time = 2.0;
        second.x = 1.0;
        second.y = -0.5;
        second.rot = 90.0;
        dropBodyPose(track, second);

        if (track.size() != 2)
            return fail("two keyframes did not make a track of two");

        const BodyPose middle = bodyPoseAt(track, 1.0);
        if (!near(middle.x, 0.5, 1e-12) || !near(middle.y, -0.25, 1e-12) ||
            !near(middle.rot, 45.0, 1e-12))
            return fail("the halfway pose is not halfway");

        const std::string motion = bodyTrackToMotion(track);
        if (!contains(motion, "@0,vx=0.5") || !contains(motion, "vy=-0.25") ||
            !contains(motion, "omega=45"))
            return fail("the velocities written out do not carry the body "
                        "from one pose to the next: " + motion);
        if (!contains(motion, "@2,vx=0,vy=0,omega=0"))
            return fail("the track does not stop at its last keyframe: " +
                        motion);
    }

    {
        std::vector<BodyPose> track;
        BodyPose pose;
        pose.time = 0.0;
        pose.x = 0.25;
        pose.interp = "bezier";
        pose.ease = "out";
        dropBodyPose(track, pose);

        BodyPose replacement = pose;
        replacement.x = 0.75;
        dropBodyPose(track, replacement);
        if (track.size() != 1)
            return fail("dropping a keyframe at the same time made a second "
                        "one instead of replacing it");
        if (!near(track.front().x, 0.75, 1e-12))
            return fail("the replacement keyframe did not take");

        BodyPose later;
        later.time = 1.0;
        later.x = 1.0;
        dropBodyPose(track, later);

        const std::string text = formatBodyTrack(track);
        const std::vector<BodyPose> round = parseBodyTrack(text);
        if (round.size() != track.size())
            return fail("the track did not survive a round trip: " + text);
        if (round.front().interp != "bezier" || round.front().ease != "out")
            return fail("the interpolation did not survive a round trip");
        if (!near(round.front().x, 0.75, 1e-9) ||
            !near(round.back().x, 1.0, 1e-9))
            return fail("the positions did not survive a round trip");

        const std::string motion = bodyTrackToMotion(track);
        if (!contains(motion, "interp=bezier") ||
            !contains(motion, "ease=out"))
            return fail("a non-linear keyframe lost its interpolation on the "
                        "way to bodyMotion: " + motion);
    }

    {
        std::vector<BodyPose> track = parseBodyTrack(
            "@t=1,x=1,y=0,rot=0,interp=linear,ease=inout"
            "@t=0,x=0,y=0,rot=0,interp=linear,ease=inout");
        if (track.size() != 2 || track.front().time != 0.0)
            return fail("keyframes given out of order were not sorted");

        if (!removeBodyPose(track, 1.0))
            return fail("removing a keyframe that exists reported nothing "
                        "removed");
        if (track.size() != 1)
            return fail("removing one keyframe removed something else too");
        if (removeBodyPose(track, 5.0))
            return fail("removing a keyframe that is not there reported a "
                        "removal");
        if (!bodyTrackToMotion(track).empty())
            return fail("a single keyframe wrote a bodyMotion entry, and one "
                        "pose is not a motion");
    }

    {
        std::vector<BodyPose> track;
        BodyPose a;
        a.time = 0.0;
        BodyPose b;
        b.time = 0.0;
        b.x = 1.0;
        track.push_back(a);
        track.push_back(b);
        if (!bodyTrackToMotion(track).empty())
            return fail("two keyframes at the same instant produced a "
                        "velocity, and that is a division by zero");
    }

    {
        std::vector<BodyPose> track = parseBodyTrack(
            "@t=0,x=0,y=0,rot=0@t=1,x=2,y=0,rot=0");
        const BodyPose before = bodyPoseAt(track, -5.0);
        const BodyPose after = bodyPoseAt(track, 5.0);
        if (!near(before.x, 0.0, 1e-12))
            return fail("sampling before the first keyframe moved the body");
        if (!near(after.x, 2.0, 1e-12))
            return fail("sampling after the last keyframe moved the body");
    }

    {
        std::vector<BodyPose> track;
        for (int step = 0; step < 8; ++step) {
            const double angle = 2.0 * 3.14159265358979323846 * step / 8.0;
            BodyPose pose;
            pose.time = step * 0.25;
            pose.x = std::cos(angle);
            pose.y = std::sin(angle);
            track.push_back(pose);
        }
        BodyPose closing = track.front();
        closing.time = 2.0;
        track.push_back(closing);

        if (!bodyTrackIsClosed(track))
            return fail("a track whose last pose sits on its first is not "
                        "seen as a loop, so the curve has a corner at the "
                        "join");

        const BodyPose end = bodyPoseOnCurve(track, 2.0);
        if (!near(end.x, track.front().x, 1e-9) ||
            !near(end.y, track.front().y, 1e-9))
            return fail("a closed loop does not come back to where it "
                        "started");

        for (double when = 0.0; when <= 2.0; when += 1.0 / 64.0) {
            const BodyPose sample = bodyPoseOnCurve(track, when);
            const double radius =
                std::sqrt(sample.x * sample.x + sample.y * sample.y);
            if (!near(radius, 1.0, 0.01))
                return fail("the curve through eight points on a circle "
                            "leaves the circle by more than a percent at t=" +
                            std::to_string(when) + ", radius " +
                            std::to_string(radius));
        }

        const double length = bodyCurveLength(track, 256);
        const double circumference = 2.0 * 3.14159265358979323846;
        if (!near(length, circumference, circumference * 0.005))
            return fail("the arc length of a circle of radius 1 came out as " +
                        std::to_string(length) + " instead of " +
                        std::to_string(circumference));

        const double straight = 8.0 * 2.0 * std::sin(
            3.14159265358979323846 / 8.0);
        if (near(length, straight, circumference * 0.005))
            return fail("the curve is as long as the straight legs through "
                        "the same points, so it is not curving at all");
    }

    {
        std::vector<BodyPose> track;
        for (int step = 0; step < 5; ++step) {
            BodyPose pose;
            pose.time = step * 0.5;
            pose.x = 0.2 * step;
            pose.y = 0.3 * std::sin(step);
            pose.z = 0.1 * step * step;
            pose.rot = 12.0 * step;
            track.push_back(pose);
        }

        const std::string motion = bodyCurveToMotion(track, 8);
        if (motion.empty())
            return fail("a five-pose curve wrote no bodyMotion at all");
        if (!contains(motion, "interp=constant"))
            return fail("the emitted keyframes let the solver ramp the "
                        "velocity between them, which does not reproduce the "
                        "curve: " + motion);
        if (!contains(motion, "vz="))
            return fail("a curve that leaves the plane wrote no vz: " +
                        motion);

        struct Key {
            double time = 0.0;
            double vx = 0.0;
            double vy = 0.0;
            double vz = 0.0;
            double omega = 0.0;
        };
        std::vector<Key> keys;
        for (std::size_t at = motion.find('@'); at != std::string::npos;
             at = motion.find('@', at + 1)) {
            const std::size_t end = motion.find('@', at + 1);
            const std::string block = motion.substr(
                at + 1, end == std::string::npos ? std::string::npos
                                                 : end - at - 1);
            Key key;
            key.time = std::atof(block.c_str());
            const auto valueOf = [&block](const std::string& name) {
                const std::size_t found = block.find("," + name + "=");
                return found == std::string::npos
                    ? 0.0
                    : std::atof(block.c_str() + found + name.size() + 2);
            };
            key.vx = valueOf("vx");
            key.vy = valueOf("vy");
            key.vz = valueOf("vz");
            key.omega = valueOf("omega");
            keys.push_back(key);
        }
        if (keys.size() < 8)
            return fail("the curve was written as too few keyframes to be "
                        "anything but straight legs");

        double x = track.front().x;
        double y = track.front().y;
        double z = track.front().z;
        double rot = track.front().rot;
        for (std::size_t k = 0; k + 1 < keys.size(); ++k) {
            const double leg = keys[k + 1].time - keys[k].time;
            x += keys[k].vx * leg;
            y += keys[k].vy * leg;
            z += keys[k].vz * leg;
            rot += keys[k].omega * leg;
            const BodyPose wanted = bodyPoseOnCurve(track, keys[k + 1].time);
            if (!near(x, wanted.x, 1e-6) || !near(y, wanted.y, 1e-6) ||
                !near(z, wanted.z, 1e-6) || !near(rot, wanted.rot, 1e-6))
                return fail("integrating the emitted keyframes leaves the "
                            "curve at t=" +
                            std::to_string(keys[k + 1].time));
        }

        const BodyPose last = bodyPoseOnCurve(track, track.back().time);
        if (!near(x, last.x, 1e-6) || !near(y, last.y, 1e-6) ||
            !near(z, last.z, 1e-6) || !near(rot, last.rot, 1e-6))
            return fail("the emitted keyframes do not end where the curve "
                        "ends");
    }

    {
        std::vector<BodyPose> track = parseBodyTrack(
            "@t=0,x=0,y=0,z=0.5,rot=0@t=1,x=1,y=0,z=1.5,rot=0");
        if (!near(track.front().z, 0.5, 1e-12) ||
            !near(track.back().z, 1.5, 1e-12))
            return fail("a keyframe's z did not parse");
        const BodyPose middle = bodyPoseAt(track, 0.5);
        if (!near(middle.z, 1.0, 1e-12))
            return fail("the halfway pose is not halfway in z");
        const std::string motion = bodyTrackToMotion(track);
        if (!contains(motion, "vz=1"))
            return fail("a straight leg that changes z wrote no vz: " +
                        motion);
        const std::vector<BodyPose> round =
            parseBodyTrack(formatBodyTrack(track));
        if (!near(round.back().z, 1.5, 1e-9))
            return fail("z did not survive a track round trip");
    }

    std::cout << "BodyTrackTests OK\n";
    return 0;
}
