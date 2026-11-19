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

#include <sys/types.h>
#include <string>
#include "GroupManager.h"
#include <memory>
#include <optional>
#include "Measurement.h"

/**
 * Represent a running process on the system
 *
 * All info about the process is loaded in the constructor and cached to help avoid issues with short-lived processes.
 * As such, there is no guarantee the process actually exists in system any more when calling the various methods!
 *
 */
class Process
{
public:
    explicit Process(pid_t pid, std::string::size_type maxStoredStringLength = std::string::npos);

    bool operator==(const Process &rhs) const
    {
        // On long captures there is a small chance we loop around PIDs and re-use the same PID again
        return mPid == rhs.mPid && mCmdline == rhs.mCmdline;
    }

    pid_t pid() const;

    pid_t ppid() const;

    std::string name() const;

    std::string cmdline() const;

    std::optional<std::string> systemdService() const;

    std::optional<std::string> container() const;

    std::optional<std::string> group(const std::shared_ptr<GroupManager> &groupManager) const;

    bool isDead() const;

    void updateAliveStatus();

private:
    pid_t getParentPid() const;

    std::string getName(std::string::size_type maxLength) const;

    std::string getCmdline(std::string::size_type maxLength) const;

    std::string getSystemdService();

    std::string getContainer();

    std::string getNameWithoutPath() const;

    std::string GetCgroupPathByCgroupControllerAndPid(const std::string &cgroup_controller, pid_t pid);

    // UTF-8 boundary-aware string truncation helpers
    static bool isUtf8CharacterStart(unsigned char byte)
    {
        // UTF-8: single-byte (0xxxxxxx) or multi-byte start (11xxxxxx)
        // NOT continuation byte (10xxxxxx)
        return (byte & 0xC0) != 0x80;
    }

    static void resizeToUtf8Boundary(std::string &str, std::string::size_type maxLength)
    {
        if (str.size() > maxLength) {
            str.resize(maxLength);
        }
        if (str.empty()) {
            return;
        }
        auto pos = str.size() - 1;
        while (pos > 0 && !isUtf8CharacterStart(static_cast<unsigned char>(str[pos]))) {
            --pos;
        }
        const auto lead = static_cast<unsigned char>(str[pos]);
        const std::string::size_type expectedLength =
            lead < 0x80 ? 1 :
            (lead & 0xE0) == 0xC0 ? 2 :
            (lead & 0xF0) == 0xE0 ? 3 :
            (lead & 0xF8) == 0xF0 ? 4 : 1;
        if (str.size() - pos < expectedLength) {
            str.resize(pos);
        }
    }

private:
    pid_t mPid;
    pid_t mPpid;

    bool mDead;

    std::string mName;
    std::string mCmdline;
    std::string mSystemdService;
    std::string mContainer;
};
