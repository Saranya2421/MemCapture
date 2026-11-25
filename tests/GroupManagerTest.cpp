#include "GroupManager.h"
#include <gtest/gtest.h>
#include <string>
#include <utility>

namespace {

nlohmann::json loadFixture()
{
    return {
        {"processes", {{{"group", "Logging"}, {"processes", {"syslog-ng", "systemd-journald"}}}}},
        {"containers", {{{"group", "Apps"}, {"containers", {"app-container"}}}}}
    };
}

nlohmann::json makeGroups(nlohmann::json processGroups, nlohmann::json containerGroups = nlohmann::json::array())
{
    return {
        {"processes", std::move(processGroups)},
        {"containers", std::move(containerGroups)}
    };
}

nlohmann::json makeGroup(const std::string &name, const nlohmann::json &patterns)
{
    return {{"group", name}, {"processes", patterns}};
}

} // namespace

TEST(GroupManagerL1Test, MatchesProcessAndContainerPatterns)
{
    GroupManager manager(loadFixture());

    EXPECT_EQ(manager.getGroup(GroupManager::groupType::PROCESS, "syslog-ng"),
              std::optional<std::string>("Logging"));
    EXPECT_EQ(manager.getGroup(GroupManager::groupType::CONTAINER, "app-container"),
              std::optional<std::string>("Apps"));
    EXPECT_FALSE(manager.getGroup(GroupManager::groupType::PROCESS, "unmatched").has_value());
}

TEST(GroupManagerL1Test, UsesPartialMatchingAndStandardRegexSyntax)
{
    GroupManager manager(makeGroups(nlohmann::json::array({
        makeGroup("Worker", nlohmann::json::array({"worker(?=-)"}))
    })));

    EXPECT_EQ(manager.getGroup(GroupManager::groupType::PROCESS, "/usr/bin/worker-service"),
              std::optional<std::string>("Worker"));
    EXPECT_FALSE(manager.getGroup(GroupManager::groupType::PROCESS, "worker_service").has_value());
}

TEST(GroupManagerL1Test, RejectsInvalidRegexAndNonStringPatterns)
{
    EXPECT_THROW(GroupManager(makeGroups(nlohmann::json::array({
        makeGroup("Invalid", nlohmann::json::array({"("}))
    }))), std::regex_error);

    EXPECT_THROW(GroupManager(makeGroups(nlohmann::json::array({
        makeGroup("Not a string", nlohmann::json::array({42}))
    }))), nlohmann::json::type_error);
}

TEST(GroupManagerL1Test, UsesFirstMatchingGroupAndSkipsMalformedEntries)
{
    GroupManager manager(makeGroups(nlohmann::json::array({
        {{"processes", {"worker"}}},
        {{"group", "Missing patterns"}},
        makeGroup("First", nlohmann::json::array({"worker"})),
        makeGroup("Second", nlohmann::json::array({"worker"}))
    })));
    EXPECT_EQ(manager.getGroup(GroupManager::groupType::PROCESS, "worker"),
              std::optional<std::string>("First"));
}

