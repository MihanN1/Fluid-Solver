#include "BodyRows.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
    std::cout << message << "\n";
    return 1;
}

bool near(double value, double expected) {
    return std::fabs(value - expected) <= 1e-9;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    using namespace maskui;

    {
        const std::vector<MotionEntry> entries =
            splitMotionEntries("1:rot=90,slideX=0.5;3:slip=1");
        if (entries.size() != 2)
            return fail("two objects did not split into two entries");
        if (entries[0].object != 1 || entries[1].object != 3)
            return fail("the object numbers did not survive the split");
        if (entries[1].settings != "slip=1")
            return fail("the second object's settings are wrong: " +
                        entries[1].settings);
        if (joinMotionEntries(entries) != "1:rot=90,slideX=0.5;3:slip=1")
            return fail("splitting and joining did not give the line back");
        if (motionSetting(entries[0].settings, "slidex") != 0.5)
            return fail("a setting is read case sensitively, and the solver "
                        "does not write it that way");
        if (motionSetting(entries[0].settings, "slidez", -1.0) != -1.0)
            return fail("a setting that is not there did not fall back");
    }

    {
        std::string wall;
        std::string body;
        BodyRowValues drag;
        drag.behaviour = BodyDrag;
        drag.rotation = 90.0;
        drag.rotationX = 45.0;
        drag.rotationY = -30.0;
        drag.slideX = 0.5;
        drag.slideZ = 0.25;
        writeBodyRows(wall, body, 2, drag);
        if (!contains(wall, "2:") || !contains(wall, "rot=90") ||
            !contains(wall, "rotX=45") || !contains(wall, "rotY=-30") ||
            !contains(wall, "slideX=0.5") || !contains(wall, "slideZ=0.25"))
            return fail("a dragging surface did not write every axis it was "
                        "given: " + wall);
        if (contains(wall, "slideY"))
            return fail("a slide of zero was written out, which is noise the "
                        "solver has to parse: " + wall);
        if (!body.empty())
            return fail("a dragging surface moved the body as well");

        const BodyRowValues back = readBodyRows(wall, body, 2);
        if (back.behaviour != BodyDrag)
            return fail("a dragging surface came back as something else");
        if (!near(back.rotation, 90.0) || !near(back.rotationX, 45.0) ||
            !near(back.rotationY, -30.0) || !near(back.slideX, 0.5) ||
            !near(back.slideZ, 0.25) || !near(back.slideY, 0.0))
            return fail("the dragging settings did not come back as they went "
                        "in");
    }

    {
        std::string wall;
        std::string body;
        BodyRowValues free;
        free.behaviour = BodyFree;
        free.velocityX = 1.5;
        free.velocityZ = -0.75;
        free.spin = 20.0;
        free.spinX = 5.0;
        free.spinY = -5.0;
        free.mass = 3.0;
        free.inertiaX = 0.125;
        free.inertiaY = 0.25;
        free.pins = PinSlideY | PinSlideZ | PinSpinX;
        writeBodyRows(wall, body, 1, free);
        for (const char* wanted : {"free=1", "vx=1.5", "vz=-0.75", "omega=20",
                                   "omegaX=5", "omegaY=-5", "mass=3",
                                   "inertiaX=0.125", "inertiaY=0.25",
                                   "pinY=1", "pinZ=1", "pinRotX=1"}) {
            if (!contains(body, wanted))
                return fail(std::string("a free body lost '") + wanted +
                            "' on the way out: " + body);
        }
        if (contains(body, "pinX=1") || contains(body, "pinRotY=1"))
            return fail("a pin that was not asked for was written: " + body);

        const BodyRowValues back = readBodyRows(wall, body, 1);
        if (back.behaviour != BodyFree)
            return fail("a free body came back as something else");
        if (!near(back.velocityZ, -0.75) || !near(back.spinX, 5.0) ||
            !near(back.spinY, -5.0) || !near(back.inertiaX, 0.125) ||
            !near(back.inertiaY, 0.25) || !near(back.mass, 3.0))
            return fail("the free body's third dimension did not come back");
        if (back.pins != (PinSlideY | PinSlideZ | PinSpinX))
            return fail("the pins did not come back as they went in");
    }

    {
        std::string wall;
        std::string body;
        BodyRowValues flat;
        flat.behaviour = BodyTravel;
        flat.velocityX = 2.0;
        flat.velocityY = -1.0;
        flat.spin = 45.0;
        writeBodyRows(wall, body, 1, flat);
        if (body != "1:vx=2,vy=-1,omega=45")
            return fail("a body that only moves in the plane is written "
                        "differently than it was before the third dimension: " +
                        body);
        if (!wall.empty())
            return fail("a travelling body wrote a wall entry");
    }

    {
        std::string wall = "1:rot=10;2:slip=1";
        std::string body = "2:free=1,mass=4";
        BodyRowValues stop;
        stop.behaviour = BodyStatic;
        writeBodyRows(wall, body, 2, stop);
        if (wall != "1:rot=10")
            return fail("making one object static disturbed another: " + wall);
        if (!body.empty())
            return fail("making the only moving body static left it moving: " +
                        body);
        const BodyRowValues first = readBodyRows(wall, body, 1);
        if (first.behaviour != BodyDrag || !near(first.rotation, 10.0))
            return fail("object 1 lost its own settings");
    }

    {
        const BodyRowValues values =
            readBodyRows(std::string(), "4:free=1,pinRot=1,pinRotY=1", 4);
        if ((values.pins & PinSpinZ) == 0)
            return fail("pinRot was not read");
        if ((values.pins & PinSpinY) == 0)
            return fail("pinRotY was not read");
        if ((values.pins & PinSpinX) != 0)
            return fail("pinRotX was read out of a line that does not have "
                        "it, so pinRot and pinRotX are being confused");
    }

    {
        const std::string line =
            "wing.stl@x=0.6,y=0.5,size=0.3;ball.obj@x=1.6,y=0.5,z=0.4";
        const std::vector<std::string> entries = splitProfileEntries(line);
        if (entries.size() != 2)
            return fail("a profiles line with two models split into " +
                        std::to_string(entries.size()));
        if (profileFileOf(entries[1]) != "ball.obj")
            return fail("the second model's file came back as '" +
                        profileFileOf(entries[1]) + "'");
        const BodyPlacement second = readPlacement(line, 2);
        if (!second.present || !near(second.x, 1.6) || !near(second.z, 0.4))
            return fail("body 2 read back at the wrong place");
        const BodyPlacement first = readPlacement(line, 1);
        if (!first.sized || !near(first.size, 0.3))
            return fail("body 1 lost its size");
        if (first.placed && !near(first.z, 0.0))
            return fail("a model with no z= came back somewhere other than 0");
        if (readPlacement(line, 3).present)
            return fail("a body with no entry in profiles reported one");
    }

    {
        // Moving one model has to leave everything else in the line exactly as
        // it was, including a setting these rows do not know about.
        std::string line =
            "wing.stl@x=0.6,y=0.5,size=0.3,invert=1;ball.obj@x=1.6,y=0.5";
        BodyPlacement place = readPlacement(line, 1);
        place.x += 0.25;
        place.placed = true;
        writePlacement(line, 1, place);
        if (!contains(line, "x=0.85"))
            return fail("the move did not land: " + line);
        if (!contains(line, "invert=1"))
            return fail("a setting the rows do not manage was dropped: " +
                        line);
        if (!contains(line, "ball.obj@x=1.6,y=0.5"))
            return fail("moving one model disturbed another: " + line);
        if (!near(readPlacement(line, 1).size, 0.3))
            return fail("the size was lost on the way through: " + line);
    }

    {
        std::string line = "wing.stl";
        BodyPlacement place = readPlacement(line, 1);
        if (!place.present)
            return fail("a bare file name is still a model");
        place.angleY = 15.0;
        writePlacement(line, 1, place);
        if (line != "wing.stl@ay=15")
            return fail("turning a model with no settings yet gave: " + line);
        place = readPlacement(line, 1);
        place.angleY = 0.0;
        writePlacement(line, 1, place);
        if (!contains(line, "ay=0"))
            return fail("turning a model back to zero dropped the setting "
                        "instead of writing it: " + line);
    }

    std::cout << "BodyRowsTests OK\n";
    return 0;
}
