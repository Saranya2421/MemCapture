#include "GpuMeasurementPolicy.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <stdexcept>

namespace {

nlohmann::json loadFixture()
{
    std::ifstream fixture("test/5-gpu-measurement-capacity/input.json");
    if (!fixture) {
        throw std::runtime_error("Unable to open GPU-measurement fixture");
    }
    return nlohmann::json::parse(fixture);
}

struct FakeProcessInfo
{
    bool dead;
    bool updated = false;

    void updateAliveStatus() { updated = true; }
    bool isDead() const { return dead; }
};

struct FakeGpuMeasurement
{
    FakeProcessInfo ProcessInfo;
};

} // namespace

TEST(GpuMeasurementPolicyL1Test, RejectsInvalidAndNewMeasurementsPastTheLimit)
{
    const auto fixture = loadFixture();
    EXPECT_FALSE(gpu_measurement::shouldRecord(fixture.at("zeroPid").get<pid_t>(), false, 0));
    EXPECT_FALSE(gpu_measurement::shouldRecord(fixture.at("negativePid").get<pid_t>(), false, 0));
    EXPECT_TRUE(gpu_measurement::shouldRecord(
        1, false, fixture.at("availableSlots").get<size_t>()));
    EXPECT_FALSE(gpu_measurement::shouldRecord(
        1, false, fixture.at("capacity").get<size_t>()));
    EXPECT_TRUE(gpu_measurement::shouldRecord(
        1, true, fixture.at("capacity").get<size_t>()));
}

TEST(GpuMeasurementPolicyL1Test, RemovesDeadAndInvalidRecordsButKeepsLiveProcesses)
{
    std::map<pid_t, FakeGpuMeasurement> measurements = {
        {-1, {{false}}},
        {1, {{true}}},
        {2, {{false}}}
    };

    gpu_measurement::removeDeadProcesses(measurements);

    ASSERT_EQ(measurements.size(), 1U);
    EXPECT_EQ(measurements.begin()->first, 2);
    EXPECT_TRUE(measurements.begin()->second.ProcessInfo.updated);
}

