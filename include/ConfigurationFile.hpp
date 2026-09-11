#pragma once

#include "ParameterInfo.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace maskui {

extern const char* const CONFIGURATION_FORMAT;
extern const char* const CONFIGURATION_UI_BANNER;

bool parameterKeyIsSolverKey(std::size_t index);

std::string formatConfiguration(
    const std::array<std::string, ParameterCount>& values,
    const std::vector<std::pair<std::string, std::string>>& extras);

struct ConfigurationDocument {
    std::array<std::string, ParameterCount> values;
    std::array<bool, ParameterCount> present{};
    std::vector<std::pair<std::string, std::string>> extras;
    std::vector<std::string> ignored;
    std::vector<std::string> unknown;
    std::string format;
    bool recognised = false;

    std::string extra(const std::string& key) const;
};

ConfigurationDocument parseConfiguration(const std::string& text);

} // namespace maskui
