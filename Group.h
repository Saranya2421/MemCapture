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

#pragma once

#include <algorithm>
#include <memory>
#include <re2/re2.h>
#include <string>
#include <vector>

/**
 * Represents a group loaded from provided JSON file
 *
 * A group consists of a name and list of regex's that represent items belonging to that group. For example, all the
 * A/V processes could be grouped into a single "AV" group
 */
class Group
{
public:
    Group(std::string groupName, std::vector<std::shared_ptr<const re2::RE2>> toMatch)
        : mGroupName(std::move(groupName)), mToMatch(std::move(toMatch))
    {
    }

    /**
     * @return Name of the group
     */
    std::string name() const
    {
        return mGroupName;
    }

    /**
     * Check if the specified string belongs to the group. Will use a regex search
     *
     * @param name String to check if belongs to the group
     * @return True if the name is a member of the group
     */
    bool isMatch(const std::string &name) const
    {
        constexpr size_t maxSubjectLength = 4096;
        const std::string boundedName = name.substr(0, maxSubjectLength);
        return std::any_of(mToMatch.begin(), mToMatch.end(), [&](const auto &toMatch)
        {
            return re2::RE2::PartialMatch(boundedName, *toMatch);
        });
    }


private:
    const std::string mGroupName;
    const std::vector<std::shared_ptr<const re2::RE2>> mToMatch;
};
