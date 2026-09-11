#include "ParameterInfo.hpp"

#include <cctype>
#include <iostream>
#include <set>
#include <string>

namespace {

int fail(const std::string& message) {
    std::cout << message << "\n";
    return 1;
}

} // namespace

int main() {
    using namespace maskui;

    std::set<std::string> keys;
    for (std::size_t index = 0; index < ParameterCount; ++index) {
        const std::string key = parameterKey(index);
        if (key.empty())
            return fail("parameter " + std::to_string(index) +
                        " has no solver key, so it can never be saved or "
                        "loaded");
        if (!keys.insert(key).second)
            return fail("two parameters answer to the key '" + key +
                        "', and a .cfdui file cannot tell them apart");
    }

    for (std::size_t index = 0; index < ParameterCount; ++index) {
        const std::string hint = parameterHint(index);
        if (hint.empty())
            return fail("parameter '" + std::string(parameterKey(index)) +
                        "' has no hover hint. Every row gets one - that is "
                        "the whole point of the tooltip");
        if (hint.size() < 20)
            return fail("the hint for '" + std::string(parameterKey(index)) +
                        "' is " + std::to_string(hint.size()) +
                        " characters, which is a label rather than an "
                        "explanation");
        if (hint.size() > 200)
            return fail("the hint for '" + std::string(parameterKey(index)) +
                        "' is " + std::to_string(hint.size()) +
                        " characters. A tooltip is one or two lines; the long "
                        "version belongs in parameterHelp");
        if (std::isspace(static_cast<unsigned char>(hint.front())) ||
            std::isspace(static_cast<unsigned char>(hint.back())))
            return fail("the hint for '" + std::string(parameterKey(index)) +
                        "' has whitespace on one end");
        const char last = hint.back();
        if (last != '.' && last != '!' && last != '?')
            return fail("the hint for '" + std::string(parameterKey(index)) +
                        "' does not end in a full stop");
        if (hint.find("  ") != std::string::npos)
            return fail("the hint for '" + std::string(parameterKey(index)) +
                        "' has a double space in it");
    }

    for (std::size_t index = 0; index < PARAMETER_GROUPS.size(); ++index) {
        if (PARAMETER_GROUPS[index].firstIndex >= ParameterCount)
            return fail("group '" + std::string(PARAMETER_GROUPS[index].label) +
                        "' starts past the end of the parameter list");
        if (index > 0 &&
            PARAMETER_GROUPS[index].firstIndex <=
                PARAMETER_GROUPS[index - 1].firstIndex)
            return fail("the parameter groups are not in ascending order, and "
                        "the panel walks them assuming they are");
    }

    for (std::size_t tab = 1; tab < PARAMETER_TABS.size(); ++tab)
        for (int group : PARAMETER_TABS[tab].groups) {
            if (group < 0)
                continue;
            if (static_cast<std::size_t>(group) >= PARAMETER_GROUPS.size())
                return fail("tab '" + std::string(PARAMETER_TABS[tab].label) +
                            "' points at a group that does not exist");
        }

    std::set<int> covered;
    for (std::size_t tab = 1; tab < PARAMETER_TABS.size(); ++tab)
        for (int group : PARAMETER_TABS[tab].groups)
            if (group >= 0 && !covered.insert(group).second)
                return fail("group " + std::to_string(group) +
                            " is on two tabs at once");
    for (std::size_t group = 0; group < PARAMETER_GROUPS.size(); ++group)
        if (covered.find(static_cast<int>(group)) == covered.end())
            return fail("group '" +
                        std::string(PARAMETER_GROUPS[group].label) +
                        "' is on no tab but All, so those rows are only "
                        "reachable one way");

    const auto groupOf = [](std::size_t index) {
        std::size_t found = 0;
        for (std::size_t group = 0; group < PARAMETER_GROUPS.size(); ++group)
            if (PARAMETER_GROUPS[group].firstIndex <= index)
                found = group;
        return std::string(PARAMETER_GROUPS[found].label);
    };

    const std::pair<const char*, const char*> volumeRows[] = {
        {"nz", "DOMAIN / GRID"}, {"Lz", "DOMAIN / GRID"},
        {"bcFront", "BOUNDARIES"}, {"bcBack", "BOUNDARIES"},
        {"bcFrontSpeed", "BOUNDARIES"}, {"bcBackSpeed", "BOUNDARIES"},
        {"inletFrom2", "BOUNDARIES"}, {"inletTo2", "BOUNDARIES"},
        {"gravityTilt", "FLUIDS"}, {"phaseZ", "FLUIDS"},
        {"sliceAngleY", "GEOMETRY"},
        {"uiBodyRotX", "BODIES"}, {"uiBodyRotY", "BODIES"},
        {"uiBodySlideZ", "BODIES"}, {"uiBodyVz", "BODIES"},
        {"uiBodySpinX", "BODIES"}, {"uiBodySpinY", "BODIES"},
        {"uiBodyInertiaX", "BODIES"}, {"uiBodyInertiaY", "BODIES"},
        {"uiBodyPinZ", "BODIES"}, {"uiBodyPinRotX", "BODIES"},
        {"uiBodyPinRotY", "BODIES"}, {"uiBodyPath", "BODIES"}
    };
    for (const auto& row : volumeRows) {
        const std::size_t index = parameterIndexForKey(row.first);
        if (index >= ParameterCount)
            return fail(std::string("there is no row for '") + row.first + "'");
        if (groupOf(index) != row.second)
            return fail(std::string("'") + row.first + "' sits in " +
                        groupOf(index) + " rather than beside its 2D sibling "
                        "in " + row.second);
        if (parameterHelp(index).empty())
            return fail(std::string("'") + row.first +
                        "' has no help text, and every row the panel gained "
                        "gets one");
        if (parameterHelp(index).size() < 40)
            return fail(std::string("the help for '") + row.first +
                        "' is a label rather than an explanation");
    }

    if (parameterIndexForKey("nx") != CellsX ||
        parameterIndexForKey("bcTop") != BcTop ||
        parameterIndexForKey("nowhere") != ParameterCount)
        return fail("a key does not find the row it names");

    if (parameterHelp(SourceLine).find("z=") == std::string::npos ||
        parameterHelp(SourceLine).find("elev=") == std::string::npos)
        return fail("the sources help does not cover z= and elev=, which the "
                    "solver now reads");
    if (parameterHelp(MicrophoneLine).find("z=") == std::string::npos)
        return fail("the microphones help does not cover the third "
                    "coordinate");
    if (parameterHelp(InletProfileKind).find("parabolicSpan") ==
        std::string::npos)
        return fail("the inlet profile help does not cover parabolicSpan");

    std::cout << "ParameterInfoTests OK\n";
    return 0;
}
