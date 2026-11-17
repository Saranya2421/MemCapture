#include "GroupManager.h"
#include <gtest/gtest.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

nlohmann::json loadFixture()
{
    std::ifstream fixture("test/3-group-pattern-validation/input.json");
    if (!fixture) {
        throw std::runtime_error("Unable to open group-pattern fixture");
    }
    return nlohmann::json::parse(fixture);
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

TEST(GroupManagerL1Test, LimitsRegexMatchingToFirst4096Bytes)
{
    GroupManager manager(makeGroups(nlohmann::json::array({
        makeGroup("Suffix", nlohmann::json::array({"needle"}))
    })));

    EXPECT_FALSE(manager.getGroup(GroupManager::groupType::PROCESS, std::string(4096, 'x') + "needle").has_value());
    EXPECT_EQ(manager.getGroup(GroupManager::groupType::PROCESS, std::string(4090, 'x') + "needle"),
              std::optional<std::string>("Suffix"));
}

TEST(GroupManagerL1Test, RejectsInvalidAndOversizedPatterns)
{
    EXPECT_THROW(GroupManager(makeGroups(nlohmann::json::array({
        makeGroup("Invalid", nlohmann::json::array({"("}))
    }))), std::invalid_argument);

    EXPECT_THROW(GroupManager(makeGroups(nlohmann::json::array({
        makeGroup("Not a string", nlohmann::json::array({42}))
    }))), std::invalid_argument);

    EXPECT_THROW(GroupManager(makeGroups(nlohmann::json::array({
        makeGroup("Long", nlohmann::json::array({std::string(129, 'a')}))
    }))), std::invalid_argument);
}

TEST(GroupManagerL1Test, RejectsPerGroupTotalPatternAndGroupCountLimits)
{
    nlohmann::json tooManyPatterns = nlohmann::json::array();
    for (size_t i = 0; i < 65; ++i) {
        tooManyPatterns.emplace_back("pattern");
    }
    EXPECT_THROW(GroupManager(makeGroups(nlohmann::json::array({
        makeGroup("Too many", tooManyPatterns)
    }))), std::invalid_argument);

    nlohmann::json processGroups = nlohmann::json::array({
        makeGroup("First", nlohmann::json::array()),
        makeGroup("Second", nlohmann::json::array())
    });
    for (size_t i = 0; i < 64; ++i) {
        processGroups[0]["processes"].emplace_back("first");
        processGroups[1]["processes"].emplace_back("second");
    }
    const nlohmann::json containerGroups = nlohmann::json::array({
        {{"group", "Third"}, {"containers", nlohmann::json::array({"third"})}}
    });
    EXPECT_THROW(GroupManager(makeGroups(processGroups, containerGroups)), std::invalid_argument);

    nlohmann::json tooManyGroups = nlohmann::json::array();
    for (size_t i = 0; i < 129; ++i) {
        tooManyGroups.emplace_back(makeGroup("Group", nlohmann::json::array()));
    }
    EXPECT_THROW(GroupManager(makeGroups(tooManyGroups)), std::invalid_argument);

    nlohmann::json tooManyContainerGroups = nlohmann::json::array();
    for (size_t i = 0; i < 129; ++i) {
        tooManyContainerGroups.emplace_back(
            nlohmann::json{{"group", "Container"}, {"containers", nlohmann::json::array()}});
    }
    EXPECT_THROW(GroupManager(makeGroups(nlohmann::json::array(), tooManyContainerGroups)),
                 std::invalid_argument);
}

TEST(GroupManagerL1Test, AcceptsConfiguredPatternAndGroupBoundaries)
{
    nlohmann::json processGroups = nlohmann::json::array();
    for (size_t i = 0; i < 128; ++i) {
        processGroups.emplace_back(makeGroup("Group", nlohmann::json::array()));
    }

    nlohmann::json processPatterns = nlohmann::json::array();
    nlohmann::json containerPatterns = nlohmann::json::array();
    for (size_t i = 0; i < 64; ++i) {
        processPatterns.emplace_back("process");
        containerPatterns.emplace_back("container");
    }
    processPatterns[0] = std::string(128, 'a');
    processGroups[0]["processes"] = std::move(processPatterns);
    const nlohmann::json containerGroups = nlohmann::json::array({
        {{"group", "Containers"}, {"containers", containerPatterns}}
    });

    EXPECT_NO_THROW(GroupManager(makeGroups(processGroups, containerGroups)));
}
