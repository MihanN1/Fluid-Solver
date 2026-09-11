#include "ConfigurationFile.hpp"

#include <array>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
    std::cout << message << "\n";
    return 1;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool listed(const std::vector<std::string>& names, const std::string& name) {
    for (const std::string& entry : names)
        if (entry == name)
            return true;
    return false;
}

} // namespace

int main() {
    using namespace maskui;

    std::array<std::string, ParameterCount> values;
    for (std::size_t index = 0; index < ParameterCount; ++index)
        values[index] = "value-" + std::to_string(index);

    const std::vector<std::pair<std::string, std::string>> extras{
        {"model", "/models/wing.obj"},
        {"outputRoot", "/runs"},
        {"solver", "/bin/Fluid Solver"},
        {"invertSection", "1"}
    };

    const std::string text = formatConfiguration(values, extras);

    {
        const ConfigurationDocument document = parseConfiguration(text);
        if (!document.recognised)
            return fail("the file this writer just wrote is not recognised by "
                        "its own reader");
        if (document.format != CONFIGURATION_FORMAT)
            return fail("the format marker did not survive the round trip");
        for (std::size_t index = 0; index < ParameterCount; ++index) {
            if (!document.present[index])
                return fail(std::string("'") + parameterKey(index) +
                            "' was written but did not come back");
            if (document.values[index] != values[index])
                return fail(std::string("'") + parameterKey(index) +
                            "' came back as '" + document.values[index] +
                            "' instead of '" + values[index] + "'");
        }
        if (!document.unknown.empty())
            return fail("the writer wrote a key its own reader does not "
                        "know: " + document.unknown.front());
        if (document.extra("model") != "/models/wing.obj" ||
            document.extra("outputRoot") != "/runs" ||
            document.extra("solver") != "/bin/Fluid Solver" ||
            document.extra("invertSection") != "1")
            return fail("the model, output root, solver or section flag did "
                        "not survive the round trip");
    }

    {
        const std::size_t banner = text.find(CONFIGURATION_UI_BANNER);
        if (banner == std::string::npos)
            return fail("the UI-only block is not marked, so nobody can tell "
                        "which lines the solver would refuse");
        const std::string solverPart = text.substr(0, banner);
        for (std::size_t index = 0; index < ParameterCount; ++index) {
            const std::string line =
                "\n" + std::string(parameterKey(index)) + "=";
            const bool above =
                contains("\n" + solverPart, line);
            if (above != parameterKeyIsSolverKey(index))
                return fail(std::string("'") + parameterKey(index) +
                            "' is on the wrong side of the UI-only marker");
        }
        if (contains(solverPart, "format=") ||
            contains(solverPart, "model=") ||
            contains(solverPart, "outputRoot=") ||
            contains(solverPart, "solver=") ||
            contains(solverPart, "useAvx2=") ||
            contains(solverPart, "useOpenMP="))
            return fail("a key the solver would refuse is above the marker");
        if (!contains(solverPart, "invertSection="))
            return fail("invertSection is a solver argument and belongs above "
                        "the marker with the rest of them");
    }

    {
        for (const char* key : {"nz", "Lz", "bcFront", "bcBack",
                                "bcFrontSpeed", "bcBackSpeed", "inletFrom2",
                                "inletTo2", "gravityTilt", "phaseZ",
                                "sliceAngleY"}) {
            const std::size_t index = parameterIndexForKey(key);
            if (index >= ParameterCount)
                return fail(std::string("the solver gained '") + key +
                            "' and no row here answers to it");
            if (!parameterKeyIsSolverKey(index))
                return fail(std::string("'") + key +
                            "' is treated as a UI-only key, so it would never "
                            "reach the solver");
        }
        for (const char* key : {"uiBodyVz", "uiBodySpinX", "uiBodySpinY",
                                "uiBodySlideZ", "uiBodyRotX", "uiBodyRotY",
                                "uiBodyInertiaX", "uiBodyInertiaY",
                                "uiBodyPinZ", "uiBodyPinRotX",
                                "uiBodyPinRotY", "uiBodyPath"}) {
            const std::size_t index = parameterIndexForKey(key);
            if (index >= ParameterCount)
                return fail(std::string("no row answers to '") + key + "'");
            if (parameterKeyIsSolverKey(index))
                return fail(std::string("'") + key +
                            "' would be sent to the solver, which has no such "
                            "argument");
        }
    }

    {
        const std::string header =
            "regime=incompressible\n"
            "Lx=2\nLy=1\nLz=0.5\nnx=128\nny=64\nnz=32\n"
            "U0=3\nnu=1e-06\nro=1.225\n"
            "gravityTilt=15\nphaseZ=0.25\nsliceAngleY=30\n"
            "bcFront=wall\nbcBack=slip\nbcFrontSpeed=0.5\n"
            "inletFrom2=0.1\ninletTo2=0.9\ninletProfile=parabolicSpan\n"
            "outputDir=/runs/2026-02-02\n"
            "geometryFile=/runs/2026-02-02/section-adapter.obj\n"
            "bodyIterations=4\ninitialPhaseFile=\n"
            "restart=0\nrestartFile=\n"
            "amrBuffer=2\namrMinPatch=8\namrMaxPatch=64\n"
            "wobble=7\n";
        const ConfigurationDocument document = parseConfiguration(header);
        if (!document.recognised)
            return fail("a frame's configText was not recognised as a "
                        "configuration");
        if (!document.format.empty())
            return fail("a solver frame header claims a UI format marker");
        if (document.values[parameterIndexForKey("nz")] != "32" ||
            document.values[parameterIndexForKey("Lz")] != "0.5" ||
            document.values[parameterIndexForKey("gravityTilt")] != "15" ||
            document.values[parameterIndexForKey("bcFront")] != "wall" ||
            document.values[parameterIndexForKey("inletProfile")] !=
                "parabolicSpan")
            return fail("the settings of the run did not come out of its own "
                        "frame header");
        for (const char* key : {"outputDir", "geometryFile", "bodyIterations",
                                "initialPhaseFile", "restart", "restartFile",
                                "amrBuffer", "amrMinPatch", "amrMaxPatch"}) {
            if (!listed(document.ignored, key))
                return fail(std::string("'") + key +
                            "' should be reported as a key the panel does not "
                            "carry, not as an unknown one");
        }
        if (document.unknown.size() != 1 || document.unknown.front() != "wobble")
            return fail("the one key nothing knows was not the one reported");
    }

    {
        const ConfigurationDocument document = parseConfiguration(
            "# a comment\n\n   \nnot-a-setting\nnx=64\n");
        if (!document.present[parameterIndexForKey("nx")])
            return fail("a file with a comment in it lost its settings");
        if (document.unknown.size() != 1 ||
            document.unknown.front() != "not-a-setting")
            return fail("a line with no '=' in it was not reported");
    }

    {
        const ConfigurationDocument document =
            parseConfiguration("hello\nworld\n");
        if (document.recognised)
            return fail("a file with nothing of ours in it was accepted");
    }

    {
        const ConfigurationDocument document = parseConfiguration(
            "format=CFDMaskUI-1\nsurfaceTension=72\n");
        if (document.format != "CFDMaskUI-1")
            return fail("a file written by the older UI lost its format "
                        "marker, and its surface tension is in the other unit");
    }

    std::cout << "ConfigurationFileTests OK\n";
    return 0;
}
