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

#include <filesystem>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace groups_file {

constexpr size_t maxFileSize = 1024 * 1024;

class ScopedFileDescriptor
{
public:
    explicit ScopedFileDescriptor(const int fd) : fd_(fd) {}
    ~ScopedFileDescriptor() { if (fd_ >= 0) close(fd_); }
    int get() const { return fd_; }

    ScopedFileDescriptor(const ScopedFileDescriptor &) = delete;
    ScopedFileDescriptor &operator=(const ScopedFileDescriptor &) = delete;

private:
    int fd_;
};

inline bool isAllowedPath(const std::filesystem::path &path, bool runningAsRoot)
{
    if (!runningAsRoot) {
        return true;
    }
    static const char *const allowedPaths[] = {
        "/etc/groups.json",
        "/tmp/memcapture/etc/groups.json",
        "/media/apps/memcapture/etc/groups.json",
    };
    const auto normalized = path.lexically_normal();
    for (const auto *allowed : allowedPaths) {
        if (normalized == std::filesystem::path(allowed)) {
            return true;
        }
    }
    return false;
}

// Sticky shared directories such as /tmp are allowed, but their descendants
// must still be real root-owned directories that others cannot modify.
inline bool hasTrustedParentChain(const std::filesystem::path &path)
{
    std::filesystem::path current;
    for (const auto &component : path.lexically_normal().parent_path()) {
        current /= component;
        struct stat dirInfo {};
        if (lstat(current.c_str(), &dirInfo) != 0 || !S_ISDIR(dirInfo.st_mode) ||
            dirInfo.st_uid != 0 ||
            ((dirInfo.st_mode & (S_IWGRP | S_IWOTH)) != 0 && (dirInfo.st_mode & S_ISVTX) == 0)) {
            return false;
        }
    }
    return true;
}

inline bool isTrustedRegularFile(const struct stat &fileInfo, bool runningAsRoot)
{
    return S_ISREG(fileInfo.st_mode) &&
           (!runningAsRoot || (fileInfo.st_uid == 0 && (fileInfo.st_mode & (S_IWGRP | S_IWOTH)) == 0));
}

inline nlohmann::json read(const std::filesystem::path &path, bool runningAsRoot)
{
    if (!isAllowedPath(path, runningAsRoot)) {
        throw std::runtime_error("groups file path is not allowed for privileged execution");
    }
    if (runningAsRoot && !hasTrustedParentChain(path)) {
        throw std::runtime_error("groups file directory is not trusted for privileged execution");
    }
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        throw std::runtime_error("unable to open groups file");
    }
    const ScopedFileDescriptor file(fd);

    struct stat fileInfo {};
    if (fstat(file.get(), &fileInfo) != 0 || !isTrustedRegularFile(fileInfo, runningAsRoot)) {
        throw std::runtime_error("groups file is not a trusted regular file");
    }

    std::string contents;
    char buffer[4096];
    while (true) {
        const ssize_t bytesRead = ::read(file.get(), buffer, sizeof(buffer));
        if (bytesRead < 0) {
            throw std::runtime_error("unable to read groups file");
        }
        if (bytesRead == 0) {
            break;
        }
        const size_t count = static_cast<size_t>(bytesRead);
        if (count > maxFileSize - contents.size()) {
            throw std::runtime_error("groups file exceeds size limit");
        }
        contents.append(buffer, count);
    }

    return nlohmann::json::parse(contents);
}

} // namespace groups_file

