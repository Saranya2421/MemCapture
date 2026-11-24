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

GroupManager::GroupManager(nlohmann::json groupList) : mProcessGroups(), mContainerGroups()
{

    // Attempt to parse group JSON

    // Get process groups
    auto processGroups = groupList["processes"];
    if (!processGroups.is_array()) {
        LOG_ERROR("Process groups not a valid array - cannot map processes to groups");
    } else {
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

	    std::vector<std::string> processList = group["processes"];

            // Got valid group info, parse process names into regexes and add to list
            std::vector<std::regex> processRegexes{};
            std::for_each(processList.begin(), processList.end(), [&](const std::string &name)
            {
                processRegexes.emplace_back(name);
            });

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
            std::vector<std::string> containerList = group["containers"];

            std::vector<std::regex> containerRegexes{};
            std::for_each(containerList.begin(), containerList.end(), [&](const std::string &name)
            {
                containerRegexes.emplace_back(name);
            });

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
