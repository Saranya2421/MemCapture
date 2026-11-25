#include "MemoryMetric.h"
#include <gtest/gtest.h>
#include <unistd.h>

class MemoryMetricTestAccess
{
public:
    static void record(MemoryMetric &metric, pid_t pid, long double usage)
    {
        metric.recordGpuMeasurement(pid, usage);
    }

    static size_t activeCount(const MemoryMetric &metric)
    {
        return metric.mGpuMeasurements.size();
    }

    static long double average(const MemoryMetric &metric, pid_t pid)
    {
        return metric.mGpuMeasurements.at(pid).Used.GetAverage();
    }

    static void fillCompleted(MemoryMetric &metric, size_t count)
    {
        const Process process(getpid());
        for (size_t i = 0; i < count; ++i) {
            metric.mGpuCompletedProcesses.emplace_back(process, Measurement("Memory_Usage_KB"));
        }
    }

    static void addDeadRecord(MemoryMetric &metric, pid_t pid)
    {
        Process process(getpid());
        process.mPid = pid;
        process.mDead = true;
        metric.mGpuMeasurements.emplace(pid, MemoryMetric::gpuMeasurement(
            std::move(process), Measurement("Memory_Usage_KB")));
    }

    static void prune(MemoryMetric &metric)
    {
        metric.pruneGpuMeasurements();
    }

    static size_t completedCount(const MemoryMetric &metric)
    {
        return metric.mGpuCompletedProcesses.size();
    }
};

TEST(GpuMeasurementPolicyL1Test, RejectsInvalidAndNewMeasurementsPastTheLimit)
{
    MemoryMetric metric(Platform::BROADCOM, nullptr);
    MemoryMetricTestAccess::record(metric, 0, 10);
    MemoryMetricTestAccess::record(metric, -1, 10);
    EXPECT_EQ(MemoryMetricTestAccess::activeCount(metric), 0U);

    MemoryMetricTestAccess::fillCompleted(metric, 255);
    MemoryMetricTestAccess::record(metric, getpid(), 10);
    ASSERT_EQ(MemoryMetricTestAccess::activeCount(metric), 1U);

    MemoryMetricTestAccess::record(metric, getpid() + 1, 10);
    EXPECT_EQ(MemoryMetricTestAccess::activeCount(metric), 1U);
    MemoryMetricTestAccess::record(metric, getpid(), 20);
    EXPECT_EQ(MemoryMetricTestAccess::average(metric, getpid()), 15);
}

TEST(GpuMeasurementPolicyL1Test, PreservesDeadRecordsButKeepsLiveProcessesActive)
{
    MemoryMetric metric(Platform::BROADCOM, nullptr);
    MemoryMetricTestAccess::record(metric, getpid(), 10);
    MemoryMetricTestAccess::addDeadRecord(metric, -1);
    MemoryMetricTestAccess::addDeadRecord(metric, getpid() + 1);

    MemoryMetricTestAccess::prune(metric);
    EXPECT_EQ(MemoryMetricTestAccess::activeCount(metric), 1U);
    EXPECT_EQ(MemoryMetricTestAccess::completedCount(metric), 2U);
    EXPECT_EQ(MemoryMetricTestAccess::average(metric, getpid()), 10);
}


