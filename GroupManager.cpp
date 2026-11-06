/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2023 Sky UK
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "GroupManager.h"
#include "Log.h"
#include <stdexcept>

namespace {
constexpr size_t maxGroupsPerType = 128;
constexpr size_t maxPatternsPerGroup = 64;
constexpr size_t maxTotalPatterns = 128;
constexpr size_t maxPatternLength = 128;

std::vector<std::shared_ptr<const re2::RE2>> compilePatterns(const nlohmann::json &patterns,
                                                            size_t &totalPatternCount)
{
    if (!patterns.is_array() || patterns.size() > maxPatternsPerGroup) {
        throw std::invalid_argument("invalid number of group patterns");
    }

    std::vector<std::shared_ptr<const re2::RE2>> compiledPatterns;
    compiledPatterns.reserve(patterns.size());
    for (const auto &patternJson : patterns) {
        if (!patternJson.is_string() || totalPatternCount >= maxTotalPatterns) {
            throw std::invalid_argument("invalid group pattern configuration");
        }

        const std::string pattern = patternJson.get<std::string>();
        if (pattern.size() > maxPatternLength) {
            throw std::invalid_argument("group pattern exceeds size limit");
        }

        auto compiledPattern = std::make_shared<re2::RE2>(pattern);
        if (!compiledPattern->ok()) {
            throw std::invalid_argument("invalid or unsupported group pattern");
        }
        compiledPatterns.emplace_back(std::move(compiledPattern));
        ++totalPatternCount;
    }
    return compiledPatterns;
}
}

GroupManager::GroupManager(nlohmann::json groupList) : mProcessGroups(), mContainerGroups()
{
    size_t totalPatternCount = 0;

    // Attempt to parse group JSON

    // Get process groups
    auto processGroups = groupList["processes"];
    if (!processGroups.is_array()) {
        LOG_ERROR("Process groups not a valid array - cannot map processes to groups");
    } else {
        if (processGroups.size() > maxGroupsPerType) {
            throw std::invalid_argument("too many process groups");
        }
        for (const auto &group: processGroups) {
            if (!group.contains("group")) {
                LOG_WARN("Found malformed process group - missing 'group' field");
                continue;
            }
            std::string groupName = group["group"];

            if (!group.contains("processes") || !group["processes"].is_array()) {
                LOG_WARN("Malformed group %s - no 'processes' array", groupName.c_str());
                continue;
            }
            auto processRegexes = compilePatterns(group["processes"], totalPatternCount);

            Group processGroup(groupName, processRegexes);
            mProcessGroups.emplace_back(processGroup);
        }

        LOG_INFO("Loaded %zu process groups", mProcessGroups.size());
    }

    // Get container groups
    auto containerGroups = groupList["containers"];
    if (!containerGroups.is_array()) {
        LOG_ERROR("Container groups not a valid array - cannot map containers to groups");
        return;
    } else {
        if (containerGroups.size() > maxGroupsPerType) {
            throw std::invalid_argument("too many container groups");
        }
        for (const auto &group: containerGroups) {
            if (!group.contains("group")) {
                LOG_WARN("Found malformed container group - missing 'group' field");
                continue;
            }
            std::string groupName = group["group"];

            if (!group.contains("containers") || !group["containers"].is_array()) {
                LOG_WARN("Malformed group %s - no 'containers' array", groupName.c_str());
                continue;
            }
            auto containerRegexes = compilePatterns(group["containers"], totalPatternCount);

            Group containerGroup(groupName, containerRegexes);
            mContainerGroups.emplace_back(containerGroup);
        }

        LOG_INFO("Loaded %zu container groups", mContainerGroups.size());
    }
}

/**
 * Work out which group a provided item belongs to based on it's name, based on the loaded JSON groups file
 *
 * @param type Type of group to check
 * @param name Name of the process/container to check the group for
 *
 * @return If the group is known, then return the name of the group. Otherwise return nullopt to indicate the process
 * does not belong to a known group
 */
std::optional<std::string> GroupManager::getGroup(groupType type, const std::string &name)
{
    switch (type) {
        case groupType::PROCESS: {
            for (const auto &group: mProcessGroups) {
                if (group.isMatch(name)) {
                    return group.name();
                }
            }
            return std::nullopt;
        }
        case groupType::CONTAINER: {
            for (const auto &group: mContainerGroups) {
                if (group.isMatch(name)) {
                    return group.name();
                }
            }
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}
