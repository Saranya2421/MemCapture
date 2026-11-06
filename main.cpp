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

#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <csignal>
#include <fstream>
#include <optional>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>

#include "Platform.h"
#include "Log.h"
#include "SafeFileOps.h"
#include "ProcessMetric.h"
#include "MemoryMetric.h"
#include "Metadata.h"
#include "GroupManager.h"
#include "ConditionVariable.h"

#ifdef ENABLE_CPU_IDLE_METRICS
#include "CpuIdleMetric.h"
#endif

#include "inja/inja.hpp"

#ifdef USE_BREAKPAD
#include "breakpad_wrapper.h"
#endif

#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#define INCBIN_PREFIX g_

#include <incbin.h>

#include "JsonReportGenerator.h"

INCBIN(templateHtml, "./templates/template.html");

static int gDuration = 30;
static Platform gPlatform = Platform::AMLOGIC;

// Default to save in current directory if not specified
static std::filesystem::path gOutputDirectory = std::filesystem::current_path() / "MemCaptureReport";

static bool gJson = false;
static bool gCpuIdle = false;

bool gEnableGroups = false;
static std::filesystem::path gGroupsFile;

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

static nlohmann::json readGroupsFile(const std::filesystem::path &path)
{
    constexpr size_t maxGroupsFileSize = 1024 * 1024;
    const bool runningAsRoot = geteuid() == 0;
    if (runningAsRoot && path.lexically_normal() != std::filesystem::path("/etc/groups.json")) {
        throw std::runtime_error("groups file path is not allowed for privileged execution");
    }

    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        throw std::runtime_error("unable to open groups file");
    }
    const ScopedFileDescriptor file(fd);

    struct stat fileInfo {};
    if (fstat(file.get(), &fileInfo) != 0 || !S_ISREG(fileInfo.st_mode) ||
        (runningAsRoot && (fileInfo.st_uid != 0 || (fileInfo.st_mode & (S_IWGRP | S_IWOTH)) != 0))) {
        throw std::runtime_error("groups file is not a trusted regular file");
    }

    std::string contents;
    char buffer[4096];
    while (true) {
        const ssize_t bytesRead = read(file.get(), buffer, sizeof(buffer));
        if (bytesRead < 0) {
            throw std::runtime_error("unable to read groups file");
        }
        if (bytesRead == 0) {
            break;
        }
        const size_t count = static_cast<size_t>(bytesRead);
        if (count > maxGroupsFileSize - contents.size()) {
            throw std::runtime_error("groups file exceeds size limit");
        }
        contents.append(buffer, count);
    }

    return nlohmann::json::parse(contents);
}

ConditionVariable gStop;
std::mutex gLock;
bool gEarlyTermination = false;

static void displayUsage()
{
    printf("Usage: MemCapture <option(s)>\n");
    printf("    Utility to capture memory statistics\n\n");
    printf("    -h, --help          Print this help and exit\n");
    printf("    -o, --output-dir    Directory to save results in\n");
    printf("    -j, --json          Save data as JSON in addition to HTML report\n");
    printf("    -d, --duration      Amount of time (in seconds) to capture data for. Default 30 seconds\n");
    printf("    -p, --platform      Platform we're running on. Supported options = ['AMLOGIC', 'AMLOGIC_950D4', 'REALTEK', 'REALTEK64', 'BROADCOM', 'MEDIATEK']. Defaults to Amlogic\n");
    printf("    -g, --groups        Path to JSON file containing the group mappings (optional)\n");
    printf("    -c, --cpuidle       Enable CPU Idle metrics (default to false, requires kernel support)\n");
}

static void parseArgs(const int argc, char **argv)
{
    struct option longopts[] = {
            {"help",       no_argument,       nullptr, (int) 'h'},
            {"duration",   required_argument, nullptr, (int) 'd'},
            {"platform",   required_argument, nullptr, (int) 'p'},
            {"output-dir", required_argument, nullptr, (int) 'o'},
            {"json",       no_argument,       nullptr, (int) 'j'},
            {"groups",     required_argument, nullptr, (int) 'g'},
            {"cpuidle",     no_argument, nullptr, (int) 'c'},
            {nullptr, 0,                      nullptr, 0}
    };

    opterr = 0;

    int option;
    int longindex;

    while ((option = getopt_long(argc, argv, "hd:p:o:jg:c", longopts, &longindex)) != -1) {
        switch (option) {
            case 'h':
                displayUsage();
                exit(EXIT_SUCCESS);
                break;
            case 'd':
                gDuration = std::atoi(optarg);
                if (gDuration < 0) {
                    fprintf(stderr, "Error: duration (s) must be > 0\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p': {
                std::string platform(optarg);

                if (platform == "AMLOGIC") {
                    gPlatform = Platform::AMLOGIC;
                } else if (platform == "AMLOGIC_950D4") {
                    gPlatform = Platform::AMLOGIC_950D4;
                } else if (platform == "REALTEK") {
                    gPlatform = Platform::REALTEK;
                } else if (platform == "REALTEK64") {
                    gPlatform = Platform::REALTEK64;
                } else if (platform == "BROADCOM") {
                    gPlatform = Platform::BROADCOM;
                } else if (platform == "MEDIATEK") {
                    gPlatform = Platform::MEDIATEK;
                } else {
                    fprintf(stderr, "Warning: Unsupported platform %s\n", platform.c_str());
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 'o': {
                gOutputDirectory = std::filesystem::path(optarg);
                break;
            }
            case 'j': {
                gJson = true;
                break;
            }
            case 'g': {
                gEnableGroups = true;
                gGroupsFile = std::filesystem::path(optarg);
                break;
            }
            case 'c': {
                gCpuIdle = true;
                break;
            }
            case '?':
                if (optopt == 'c')
                    fprintf(stderr, "Warning: Option -%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    fprintf(stderr, "Warning: Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr, "Warning: Unknown option character `\\x%x'.\n", optopt);

                exit(EXIT_FAILURE);
                break;
            default:
                exit(EXIT_FAILURE);
                break;
        }
    }
}

void signalHandler(int signal)
{
    // On SIGTERM, we should stop capturing and save the report
    LOG_INFO("Signal %d (%s) received. Stopping and saving report!", signal, strsignal(signal));
    gEarlyTermination = true;
    gStop.notify_all();
    LOG_INFO("Waiting for in-progress data collection to complete");
}


int main(int argc, char *argv[])
{
    parseArgs(argc, argv);

    // Get start time
    auto start = std::chrono::steady_clock::now();

    // Configure signals to stop and clean up
#ifdef USE_BREAKPAD
    // Breakpad will handle SIGILL, SIGABRT, SIGFPE and SIGSEGV
    LOG_INFO("Breakpad support enabled");
    breakpad_ExceptionHandler();
#endif

    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);

    // Lower our priority to avoid getting in the way
    if (nice(10) < 0) {
        LOG_WARN("Failed to set nice value");
    }

    // Validate the output directory path before any filesystem operations
    if (!validateOutputDirectory(gOutputDirectory)) {
        return EXIT_FAILURE;
    }

    if (!createOutputDirectorySafe(gOutputDirectory)) {
        return EXIT_FAILURE;
    }

    LOG_INFO("** About to start memory capture for %d seconds **", gDuration);
    LOG_INFO("Will save report to %s", gOutputDirectory.string().c_str());

    // Load groups JSON if provided
    std::optional<std::shared_ptr<GroupManager>> groupManager = std::nullopt;
    if (gEnableGroups) {
        try {
            auto groupsJson = readGroupsFile(gGroupsFile);
            groupManager = std::make_shared<GroupManager>(groupsJson);
        } catch (const std::exception &) {
            LOG_ERROR("Failed to load groups file; verify its path, permissions, JSON, and group patterns");
            return EXIT_FAILURE;
        }
    }

    auto metadata = std::make_shared<Metadata>();
    auto reportGenerator = std::make_shared<JsonReportGenerator>(metadata, groupManager);

    // Create all our metrics
    ProcessMetric processMetric(reportGenerator);
    MemoryMetric memoryMetric(gPlatform, reportGenerator);

#ifdef ENABLE_CPU_IDLE_METRICS
    CpuIdleMetric cpuIdleMetric(reportGenerator);
#endif

    // Start data collection
    processMetric.StartCollection(std::chrono::seconds(3));
    memoryMetric.StartCollection(std::chrono::seconds(3));

    if (gCpuIdle) {
#ifdef ENABLE_CPU_IDLE_METRICS
        // The frequency does not affect this metric
        cpuIdleMetric.StartCollection(std::chrono::seconds(0));
#else
        LOG_ERROR("Cannot retrieve CPU idle stats - not built with ENABLE_CPU_IDLE_METRICS set");
#endif
    }

    // Block main thread for the collection duration or until SIGTERM
    std::unique_lock<std::mutex> locker(gLock);
    gStop.wait_for(locker, std::chrono::seconds(gDuration));

    if (!gEarlyTermination) {
        LOG_INFO("Stopping after %d seconds - completed full capture", gDuration);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    metadata->SetDuration(duration);

    // Done! Stop data collection
    processMetric.StopCollection();
    memoryMetric.StopCollection();
#ifdef ENABLE_CPU_IDLE_METRICS
    if (gCpuIdle) {
        cpuIdleMetric.StopCollection();
    }
#endif

    // Save results
    processMetric.SaveResults();
    memoryMetric.SaveResults();
#ifdef ENABLE_CPU_IDLE_METRICS
    if (gCpuIdle) {
        cpuIdleMetric.SaveResults();
    }
#endif

    // Build report
    inja::Environment env;
    // Make the output a bit tidier
    env.set_trim_blocks(true);
    env.set_lstrip_blocks(true);

    env.add_callback("htmlEscape", 1, [](inja::Arguments &args)
    {
        const auto &value = *args.at(0);
        const std::string text = value.is_string() ? value.get<std::string>() : value.dump();
        std::string escaped;
        escaped.reserve(text.size());

        for (const char character : text) {
            switch (character) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped += character; break;
            }
        }

        return escaped;
    });

    // Convert the values in an object into an array that we can loop over (used for generating rows)
    // Order the values using the _columnOrder data given in the second argument
    env.add_callback("objectToArray", 2, [](inja::Arguments &args)
    {
        std::vector<nlohmann::json> values;

        assert(args.at(0)->is_object());
        assert(args.at(1)->is_array());

        std::map<std::string, nlohmann::json> flattenedData;

        // Flatten the data (one level deep only)
        for (const auto &element: args.at(0)->items()) {
            if (element.value().is_object()) {
                // This is a very janky way to handle min/max/average & column ordering. The key name corresponds to "<Measurement Name> (Min/Max/Average)"
                // which matches the _columnOrder values set in JsonReportGenerator
                for (const auto &child: element.value().items()) {
                    std::string keyName = element.key() + " (" + child.key() + ")";
                    flattenedData.emplace(keyName, child.value());
                }
            } else {
                values.emplace_back(element.value());
                flattenedData.emplace(element.key(), element.value());
            }
        }

        // Put the data into the order specified by _columnOrder
        std::vector<nlohmann::json> ordered;
        for (const auto &column: args.at(1)->items()) {
            auto item = flattenedData.at(column.value());
            ordered.emplace_back(item);
        }

        return ordered;
    });

    // Escape a string for safe embedding in a JavaScript single-quoted string literal.
    // Prevents XSS by escaping backslash, single quote, </script> breakout sequences,
    // and control characters.
    env.add_callback("jsEscape", 1, [](inja::Arguments &args)
    {
        std::string input = args.at(0)->get<std::string>();
        std::string escaped;
        escaped.reserve(input.size() + input.size() / 4);

        for (char c : input) {
            switch (c) {
                case '\\': escaped += "\\\\"; break;
                case '\'': escaped += "\\'"; break;
                case '"':  escaped += "\\\""; break;
                case '<':  escaped += "\\x3c"; break;
                case '>':  escaped += "\\x3e"; break;
                case '/':  escaped += "\\/"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\0': escaped += "\\0"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(c));
                        escaped += buf;
                    } else {
                        escaped += c;
                    }
                    break;
            }
        }

        return escaped;
    });

    // Write the JSON first - this is safer and is the report automation need, so if we crash
    // after this point we'll still get some data
    if (gJson) {
        std::filesystem::path jsonFilepath = gOutputDirectory / "report.json";

	int jsonFd = safeOpenForWrite(jsonFilepath);
        if (jsonFd < 0) {
            LOG_ERROR("Failed to safely open %s for writing", jsonFilepath.string().c_str());
            return EXIT_FAILURE;
        }
        std::string jsonData = reportGenerator->getJson().dump(4);
        if (!writeToFd(jsonFd, jsonData)) {
            LOG_ERROR("Failed to write JSON data to %s", jsonFilepath.string().c_str());
            close(jsonFd);
            return EXIT_FAILURE;
        }
        close(jsonFd);
        LOG_INFO("Saved JSON data to %s", jsonFilepath.string().c_str());
    }

    try {
        auto htmlTemplateString = std::string(g_templateHtml_data, g_templateHtml_data + g_templateHtml_size);
        std::string result = env.render(htmlTemplateString, reportGenerator->getJson());

        std::filesystem::path htmlFilepath = gOutputDirectory / "report.html";
        int htmlFd = safeOpenForWrite(htmlFilepath);
        if (htmlFd < 0) {
            LOG_ERROR("Failed to safely open %s for writing", htmlFilepath.string().c_str());
            return EXIT_FAILURE;
        }
        if (!writeToFd(htmlFd, result)) {
            LOG_ERROR("Failed to write HTML report to %s", htmlFilepath.string().c_str());
            close(htmlFd);
            return EXIT_FAILURE;
        }
        close(htmlFd);
        LOG_INFO("Saved report to %s", htmlFilepath.string().c_str());
    } catch (const std::exception &e) {
        LOG_ERROR("Failed to save HTML report with exception %s", e.what());
        throw;
    }

    return EXIT_SUCCESS;
}
