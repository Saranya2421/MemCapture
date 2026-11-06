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

#include "Process.h"
#include <climits>
#include "Log.h"
#include <fstream>
#include <sys/stat.h>

Process::Process(pid_t pid, std::string::size_type maxStoredStringLength) : mPid(pid), mDead(false)
{
    // Get and cache details about the process
    mName = getName(maxStoredStringLength);
    mCmdline = getCmdline(maxStoredStringLength);
    mPpid = getParentPid();

    mContainer = getContainer();
    mSystemdService = getSystemdService();
}

/**
 *
 * @return Cached PID of the process
 */
pid_t Process::pid() const
{
    return mPid;
}

/**
 *
 * @return Parent PID
 */
pid_t Process::ppid() const
{
    return mPpid;
}

/**
 *
 * @return Cached name of the process (not including any arguments)
 */
std::string Process::name() const
{
    if (!mName.empty()) {
        return mName;
    }

    // Perhaps the process died whilst getting its name?
    return {};
}

/**
 *
 * @return Full cmdline (including args) of the process
 */
std::string Process::cmdline() const
{
    if (!mCmdline.empty()) {
        return mCmdline;
    }

    // Perhaps the process died whilst getting its name?
    return {};
}

/**
 *
 * @return If the processes is running as a systemd service, return the name of that service. If not, nullopt.
 */
std::optional<std::string> Process::systemdService() const
{
    if (mSystemdService.empty()) {
        return std::nullopt;
    }

    return mSystemdService;
}

/**
 *
 * @return If the process is running in a container, return the name of the container. If not containerised, return
 * nullopt
 */
std::optional<std::string> Process::container() const
{
    if (mContainer.empty()) {
        return std::nullopt;
    }

    return mContainer;
}

/**
 *
 * @return True if the process has died
 */
bool Process::isDead() const
{
    return mDead;
}

/**
 * Check if the process is alive and update the dead/alive status
 */
void Process::updateAliveStatus()
{
    // Once dead, stay dead
    if (mDead) {
        return;
    }

    struct stat s{};

    char procDir[PATH_MAX];
    snprintf(procDir, PATH_MAX, "/proc/%d", mPid);

    if (stat(procDir, &s) == -1 && errno == ENOENT) {
        mDead = true;
    } else {
        mDead = false;
    }
}


/**
 * Attempt to work out which group the process belongs to, using the provided groupmanager to resolve names -> groups
 *
 * @param groupManager Group manager containing loaded group definitions
 * @return If the group can be resolved, return the name of the group. Otherwise returns nullopt
 */
std::optional<std::string> Process::group(const std::shared_ptr<GroupManager> &groupManager) const
{
    // WARNING:: Container is intentionally prioritised over everything else to allow a "WPEWebProcess" rule to capture JSPP
    // processes without capturing containerised browsers
    if (!mContainer.empty()) {
        auto group = groupManager->getGroup(GroupManager::groupType::CONTAINER, mContainer);

        if (group.has_value()) {
            return group.value();
        }
    }

    auto name = getNameWithoutPath();
    auto group = groupManager->getGroup(GroupManager::groupType::PROCESS, name);

    if (group.has_value()) {
        return group.value();
    }

    // Didn't get group by name or container, try cmdline
    group = groupManager->getGroup(GroupManager::groupType::PROCESS, mCmdline);

    if (group.has_value()) {
        return group.value();
    }

    // Couldn't work out what the group is
    return std::nullopt;
}

pid_t Process::getParentPid() const
{
    char procPath[PATH_MAX];
    sprintf(procPath, "/proc/%u/status", mPid);

    std::ifstream statusFile(procPath);

    if (!statusFile) {
        return {};
    }

    std::string line;
    pid_t ppid;
    while (std::getline(statusFile, line)) {
        if (sscanf(line.c_str(), "PPid:\t%d", &ppid) == 1) {
            return ppid;
        }
    }

    return -1;
}


/**
 * Attempts to work out the name of the process
 *
 * @return Process name (empty string if failed to get name - e.g. if process was very short-lived and died)
 */
std::string Process::getName(std::string::size_type maxLength) const
{
    char procPath[PATH_MAX];
    sprintf(procPath, "/proc/%u/cmdline", mPid);

    std::ifstream cmdFile(procPath);

    if (!cmdFile) {
        return {};
    }

    std::string name;
    char character;
    while (name.size() < maxLength && cmdFile.get(character) && character != '\0') {
        name.push_back(character);
    }

    return name;
}

/**
 * Attempts to work out the cmdline of the process
 *
 * @return Process cmdline (empty string if failed - e.g. if process was very short-lived and died)
 */
std::string Process::getCmdline(std::string::size_type maxLength) const
{
    char procPath[PATH_MAX];
    sprintf(procPath, "/proc/%u/cmdline", mPid);

    std::ifstream cmdFile(procPath);

    if (!cmdFile) {
        return {};
    }

    std::string cmdline;
    if (maxLength == std::string::npos) {
        cmdline.assign((std::istreambuf_iterator<char>(cmdFile)),
                       (std::istreambuf_iterator<char>()));
    } else {
        cmdline.resize(maxLength);
        cmdFile.read(cmdline.data(), static_cast<std::streamsize>(cmdline.size()));
        cmdline.resize(static_cast<std::string::size_type>(cmdFile.gcount()));
    }

    if (cmdline.empty()) {
        return cmdline;
    }

    // Replace null chars with spaces
    std::replace(cmdline.begin(), std::prev(cmdline.end()), '\0', ' ');
    cmdline.erase(std::remove(std::prev(cmdline.end()), cmdline.end(), '\0'), cmdline.end());

    return cmdline;
}

/**
 *
 * @return the name of the process without the leading directory if present
 */
std::string Process::getNameWithoutPath() const
{
    auto lastSlash = mName.find_last_of('/');

    std::string tmpName;
    if (lastSlash == std::string::npos) {
        return mName;
    } else {
        return mName.substr(lastSlash + 1);
    }
}

/**
 * Attempt to work out the container name of the process
 * @return
 */
std::string Process::getContainer()
{
    // cpuset seems reliable, since systemd doesn't add services to it, and other cgroups (such as gpu) are sometimes
    // used by other processes (such as appsserviced) to track their gpu allocations for debugging
    return GetCgroupPathByCgroupControllerAndPid("cpuset", mPid);
}

std::string Process::getSystemdService()
{
    // systemd services will always add themselves to pids cgroup controller
    std::string systemdSlice = GetCgroupPathByCgroupControllerAndPid("pids", mPid);

    if (systemdSlice.empty()) {
        return {};
    }

    // Remove the leading system.slice string
    auto pos = systemdSlice.find("system.slice/");
    if (pos == std::string::npos) {
        // Maybe we're in a container?
        return "Unknown";
    }

    return systemdSlice.substr(pos + 13);
}

/**
 * Extract cgroup name from /proc/<pid>/cgroup (if any) for specified cgroup_controller and pid and return. Otherwise
 * return empty string.
 *
 * Example of process which is part of gpu cgroup. Here /proc/<pid>/cgroup will have a 'gpu' entry followed by name of
 * cgroup which is also the name of the container:
 *
 * root@xione-sercomm:~# cat /proc/8619/cgroup
 * 10:gpu:/com.sky.as.apps_com.bskyb.epgui
 * 9:pids:/com.sky.as.apps_com.bskyb.epgui
 * 8:cpu,cpuacct:/com.sky.as.apps_com.bskyb.epgui
 * 7:freezer:/com.sky.as.apps_com.bskyb.epgui
 * 6:memory:/com.sky.as.apps_com.bskyb.epgui
 * 5:blkio:/com.sky.as.apps_com.bskyb.epgui
 * 4:devices:/com.sky.as.apps_com.bskyb.epgui
 * 3:cpuset:/com.sky.as.apps_com.bskyb.epgui
 * 2:debug:/com.sky.as.apps_com.bskyb.epgui
 * 1:name=systemd:/com.sky.as.apps_com.bskyb.epgui
 * root@xione-sercomm:~#
 *
 * Example of process which is not part of gpu cgroup and therefore not part of a container. Here the 'gpu' entry is
 * not followed by anything:
 *
 * root@xione-sercomm:~# cat /proc/7539/cgroup
 * 10:gpu:/
 * 9:pids:/system.slice/sky-appsservice.service
 * 8:cpu,cpuacct:/system.slice/sky-appsservice.service
 * 7:freezer:/
 * 6:memory:/system.slice/sky-appsservice.service
 * 5:blkio:/
 * 4:devices:/system.slice/sky-appsservice.service
 * 3:cpuset:/
 * 2:debug:/
 * 1:name=systemd:/system.slice/sky-appsservice.service
 * root@xione-sercomm:~#
 *
 * @param cgroup_controller name of cgroup controller e.g. 'gpu'
 * @param pid pid of process
 * @return name of cgroup (which is also name of container)
*/
std::string Process::GetCgroupPathByCgroupControllerAndPid(const std::string &cgroup_controller, pid_t pid)
{
    char cgroupFilePath[PATH_MAX];
    snprintf(cgroupFilePath, sizeof(cgroupFilePath), "/proc/%d/cgroup", pid);

    std::string cgrp_path;

    // Match pattern: "<hierarchy_id>:<controller>:/<path>"
    // e.g. "3:cpuset:/com.sky.as.apps_com.bskyb.epgui"
    std::string needle = ":" + cgroup_controller + ":/";

    std::ifstream cgrp_strm(cgroupFilePath);
    if (cgrp_strm) {
        std::string cgrp_line;

        while (std::getline(cgrp_strm, cgrp_line)) {
            auto pos = cgrp_line.find(needle);
	    if (pos != std::string::npos) {
                // Extract everything after ":<controller>:/"
                size_t pathStart = pos + needle.size();
                if (pathStart < cgrp_line.size()) {
                    cgrp_path = cgrp_line.substr(pathStart);
                }
                break;
            }
        }
    } else {
        // Expected, process might have died in the meantime
        LOG_DEBUG("Could not open process cgroup file '%s'", cgroupFilePath);
    }

    return cgrp_path;
}
