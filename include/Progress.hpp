#pragma once
#include <string>

// Where a long run reports from when nobody is looking at the console.
//
// On Windows that is a real tray icon plus the taskbar progress bar: the
// tooltip says how many of the simulated seconds are done, the taskbar button
// fills up, and the console window can be sent to the tray and brought back.
// The tray menu can also ask the run to stop, which finishes the current step,
// writes a frame and returns - a killed process would leave the last frame
// half written.
//
// Everywhere else there is no tray a static console binary can reach without
// dragging in a desktop toolkit, so the same information goes into the
// terminal's title, which is what the taskbar or the Dock shows for that
// window. Same numbers, one line up.

namespace progress {

// Starts reporting. total is the simulated time the run is heading for and
// startAt is where it begins, which is not zero for a continuation. outputDir
// is what the tray menu's "Open the output folder" opens.
void begin(const std::string& title, double startAt, double total,
           const std::string& outputDir);

// Called once per step. Cheap: it only redraws when the percentage or the
// displayed second actually moved.
void update(double current);

// "0.0125 / 0.019 s (66%)" - where the run has got to, for the step line the
// solver prints. Empty before begin() and after finish(), so a solver that
// prints it unconditionally still reads correctly.
std::string statusLine();

// Reached the end, or gave up. Leaves a balloon on Windows and restores the
// title elsewhere.
void finish(bool ok);

// True once the tray menu (or a console Ctrl+C, which is handled the same way)
// has asked for the run to stop. The solve loop checks it once per step.
bool stopRequested();

// Ask for that stop from code - the Ctrl+C handler uses it.

// Takes the icon down and puts the console back the way it was found. Safe to
// call twice, and called automatically at exit.
void shutdown();

}   // namespace progress
