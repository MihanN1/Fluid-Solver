#include "ConfigurationFile.hpp"

#include <cctype>
#include <iterator>
#include <sstream>

namespace maskui {
namespace {

const char* const IGNORED_SOLVER_KEYS[] = {
    "outputDir", "geometryFile", "initialPhaseFile", "bodyIterations",
    "amrBuffer", "amrMinPatch", "amrMaxPatch", "restart", "restartFile"
};

const char* const EXTRA_KEYS[] = {
    "format", "model", "outputRoot", "solver", "invertSection"
};

bool listed(const char* const* names, std::size_t count,
            const std::string& key) {
    for (std::size_t index = 0; index < count; ++index)
        if (key == names[index])
            return true;
    return false;
}

std::string trimmed(std::string text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

} // namespace

const char* const CONFIGURATION_FORMAT = "CFDMaskUI-2";

const char* const CONFIGURATION_UI_BANNER =
    "# --- below here is the UI's own and is not a solver argument ---";

bool parameterKeyIsSolverKey(std::size_t index) {
    const std::string key = parameterKey(index);
    return key.rfind("ui", 0) != 0 && key != "useAvx2" && key != "useOpenMP";
}

std::string formatConfiguration(
    const std::array<std::string, ParameterCount>& values,
    const std::vector<std::pair<std::string, std::string>>& extras) {
    std::ostringstream out;
    for (std::size_t index = 0; index < ParameterCount; ++index)
        if (parameterKeyIsSolverKey(index))
            out << parameterKey(index) << '=' << values[index] << '\n';
    for (const auto& extra : extras)
        if (extra.first == "invertSection")
            out << extra.first << '=' << extra.second << '\n';
    out << CONFIGURATION_UI_BANNER << '\n';
    out << "format=" << CONFIGURATION_FORMAT << '\n';
    for (const auto& extra : extras)
        if (extra.first != "format" && extra.first != "invertSection")
            out << extra.first << '=' << extra.second << '\n';
    for (std::size_t index = 0; index < ParameterCount; ++index)
        if (!parameterKeyIsSolverKey(index))
            out << parameterKey(index) << '=' << values[index] << '\n';
    return out.str();
}

std::string ConfigurationDocument::extra(const std::string& key) const {
    for (const auto& entry : extras)
        if (entry.first == key)
            return entry.second;
    return std::string();
}

ConfigurationDocument parseConfiguration(const std::string& text) {
    ConfigurationDocument document;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::string stripped = trimmed(line);
        if (stripped.empty() || stripped.front() == '#')
            continue;
        const std::size_t separator = stripped.find('=');
        if (separator == std::string::npos || separator == 0) {
            document.unknown.push_back(stripped);
            continue;
        }
        const std::string key = trimmed(stripped.substr(0, separator));
        const std::string value = stripped.substr(separator + 1);
        const std::size_t index = parameterIndexForKey(key);
        if (index < ParameterCount) {
            document.values[index] = value;
            document.present[index] = true;
            document.recognised = true;
            continue;
        }
        if (listed(EXTRA_KEYS, std::size(EXTRA_KEYS), key)) {
            if (key == "format")
                document.format = value;
            document.extras.emplace_back(key, value);
            document.recognised = true;
            continue;
        }
        if (listed(IGNORED_SOLVER_KEYS, std::size(IGNORED_SOLVER_KEYS), key)) {
            document.ignored.push_back(key);
            document.recognised = true;
            continue;
        }
        document.unknown.push_back(key);
    }
    return document;
}

} // namespace maskui
