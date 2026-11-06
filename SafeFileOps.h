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

#include <string>
#include <vector>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include "Log.h"

// Allowed output directory prefixes (canonicalized)
inline const std::vector<std::string> kAllowedOutputPrefixes = {
    "/tmp/",
    "/opt/logs/",
    "/opt/memcapture/"
};

/**
 * @brief Validate that the output directory path is under an allowed prefix.
 *
 * @return true if the path is under an allowed prefix
 */
inline bool validateOutputDirectory(const std::filesystem::path &dir)
{
    std::error_code ec;

    // Resolve to canonical path; use weakly_canonical so the leaf need not exist yet
    auto canonical = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        LOG_ERROR("Failed to canonicalize output path %s: %s", dir.string().c_str(), ec.message().c_str());
        return false;
    }

    std::string canonStr = canonical.string();
    if (canonStr.back() != '/') {
        canonStr += '/';
    }

    bool prefixOk = false;
    for (const auto &prefix : kAllowedOutputPrefixes) {
        if (canonStr.substr(0, prefix.size()) == prefix) {
            prefixOk = true;
            break;
        }
    }

    if (!prefixOk) {
        LOG_ERROR("Output directory %s is not under an allowed prefix", canonical.string().c_str());
        return false;
    }

    return true;
}

/**
 * @brief Create the output directory safely: verify it is not a symlink and
 * is owned by root after creation. Sets mode 0700.
 *
 * @return true on success
 */
inline bool createOutputDirectorySafe(const std::filesystem::path &dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        LOG_ERROR("Failed to create directory %s: %s", dir.string().c_str(), ec.message().c_str());
        return false;
    }

    // Verify the directory is not a symlink
    auto status = std::filesystem::symlink_status(dir, ec);
    if (ec || status.type() == std::filesystem::file_type::symlink) {
        LOG_ERROR("Output directory %s is a symlink — refusing to write", dir.string().c_str());
        return false;
    }

    // Verify ownership is root (uid 0) and no group/other write bits
    struct stat st{};
    if (stat(dir.string().c_str(), &st) != 0) {
        LOG_ERROR("Failed to stat output directory %s", dir.string().c_str());
        return false;
    }

    if (st.st_uid != 0) {
        LOG_ERROR("Output directory %s is not owned by root (uid=%d)", dir.string().c_str(), st.st_uid);
        return false;
    }

    if (st.st_mode & (S_IWGRP | S_IWOTH)) {
        LOG_ERROR("Output directory %s has group/other write permissions (mode=%o)", dir.string().c_str(), st.st_mode);
        return false;
    }

    // Tighten permissions to 0700
    chmod(dir.string().c_str(), S_IRWXU);

    return true;
}

/**
 * @brief Open a file safely with O_NOFOLLOW to prevent symlink-following.
 * Returns a valid file descriptor on success, or -1 on failure.
 * Caller is responsible for closing the fd.
 */
inline int safeOpenForWrite(const std::filesystem::path &filepath)
{
    // Open with O_CREAT|O_WRONLY|O_TRUNC|O_NOFOLLOW to prevent symlink attacks
    int fd = open(filepath.string().c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        LOG_ERROR("Failed to safely open %s: %s", filepath.string().c_str(), strerror(errno));
        return -1;
    }

    // Verify after open that the file is a regular file
    struct stat st{};
    if (fstat(fd, &st) != 0) {
        LOG_ERROR("fstat failed for %s", filepath.string().c_str());
        close(fd);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        LOG_ERROR("Opened path %s is not a regular file", filepath.string().c_str());
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * @brief Write a string to a file descriptor. Returns true on success.
 */
inline bool writeToFd(int fd, const std::string &data)
{
    const char *ptr = data.c_str();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}
