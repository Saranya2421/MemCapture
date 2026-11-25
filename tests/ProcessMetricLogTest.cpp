#include "ProcessMetric.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <unistd.h>

class ProcessMetricTestAccess
{
public:
    static void addDeadDuplicate(ProcessMetric &metric, pid_t pid, const std::string &cmdline, int pss)
    {
        Process process(pid);
        process.mCmdline = cmdline;
        process.mPpid = 42;
        process.mDead = true;

        processMeasurement measurement(std::move(process));
        measurement.Pss.AddDataPoint(pss);
        metric.mMeasurements.emplace_back(std::move(measurement));
    }

    static void deduplicate(ProcessMetric &metric)
    {
        metric.DeduplicateData();
    }

    static size_t measurementCount(const ProcessMetric &metric)
    {
        return metric.mMeasurements.size();
    }
};

namespace {

std::string captureDeduplicationLog(ProcessMetric &metric)
{
    FILE *capture = std::tmpfile();
    if (capture == nullptr) {
        throw std::runtime_error("Unable to create temporary log capture");
    }

    std::fflush(stderr);
    const int savedStderr = dup(STDERR_FILENO);
    if (savedStderr < 0 || dup2(fileno(capture), STDERR_FILENO) < 0) {
        if (savedStderr >= 0) {
            close(savedStderr);
        }
        std::fclose(capture);
        throw std::runtime_error("Unable to redirect stderr for test");
    }

    ProcessMetricTestAccess::deduplicate(metric);
    std::fflush(stderr);

    const int restoreResult = dup2(savedStderr, STDERR_FILENO);
    close(savedStderr);
    if (restoreResult < 0) {
        std::fclose(capture);
        throw std::runtime_error("Unable to restore stderr after test");
    }

    std::rewind(capture);
    std::string output;
    char buffer[512];
    size_t bytesRead = 0;
    while ((bytesRead = std::fread(buffer, 1, sizeof(buffer), capture)) > 0) {
        output.append(buffer, bytesRead);
    }
    std::fclose(capture);
    return output;
}

} // namespace

TEST(ProcessMetricLogL1Test, DuplicateRemovalLogDoesNotExposeCommandLine)
{
    const std::string cmdline = "test-process --private-value=LOG_CANARY";
    ProcessMetric metric(nullptr);
    ProcessMetricTestAccess::addDeadDuplicate(metric, 1000001, cmdline, 10);
    ProcessMetricTestAccess::addDeadDuplicate(metric, 1000002, cmdline, 20);

    const std::string logOutput = captureDeduplicationLog(metric);

    EXPECT_EQ(logOutput.find(cmdline), std::string::npos);
    EXPECT_NE(logOutput.find("Removing 1 duplicate process records"), std::string::npos);
    EXPECT_EQ(ProcessMetricTestAccess::measurementCount(metric), 1U);
}


