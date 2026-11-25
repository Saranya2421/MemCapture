#include "GroupsFile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

class GroupsFileTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::string pattern = (std::filesystem::temp_directory_path() /
                               "memcapture_groups_test_XXXXXX").string();
        const char *created = mkdtemp(pattern.data());
        ASSERT_NE(created, nullptr);
        tempDir = created;
    }

    void TearDown() override
    {
        std::filesystem::remove_all(tempDir);
    }

    std::filesystem::path tempDir;
};

} // namespace

TEST_F(GroupsFileTest, AllowsReadableNonRootFixtureAndParsesJson)
{
    const auto path = tempDir / "valid.json";
    {
        std::ofstream file(path);
        file << R"({"processes":[],"containers":[]})";
    }
    const auto json = groups_file::read(path, false);
    EXPECT_TRUE(json.at("processes").is_array());
    EXPECT_TRUE(json.at("containers").is_array());
}

TEST_F(GroupsFileTest, RejectsUnapprovedPathForRootAndValidatesTrustedFileMetadata)
{
    const auto path = tempDir / "valid.json";
    EXPECT_FALSE(groups_file::isAllowedPath(path, true));
    EXPECT_TRUE(groups_file::isAllowedPath("/etc/groups.json", true));
    EXPECT_TRUE(groups_file::isAllowedPath("/tmp/memcapture/etc/groups.json", true));
    EXPECT_TRUE(groups_file::isAllowedPath("/media/apps/memcapture/etc/groups.json", true));
    EXPECT_THROW(groups_file::read(path, true), std::runtime_error);

    struct stat fileInfo {};
    fileInfo.st_mode = S_IFREG | 0644;
    fileInfo.st_uid = 0;
    EXPECT_TRUE(groups_file::isTrustedRegularFile(fileInfo, true));

    fileInfo.st_uid = 1000;
    EXPECT_FALSE(groups_file::isTrustedRegularFile(fileInfo, true));
    fileInfo.st_uid = 0;
    fileInfo.st_mode |= S_IWOTH;
    EXPECT_FALSE(groups_file::isTrustedRegularFile(fileInfo, true));
    fileInfo.st_mode = S_IFDIR | 0755;
    EXPECT_FALSE(groups_file::isTrustedRegularFile(fileInfo, false));
}

TEST_F(GroupsFileTest, RejectsSymlinksDirectoriesInvalidJsonAndOversizedFiles)
{
    const auto target = tempDir / "target.json";
    {
        std::ofstream file(target);
        file << R"({"processes":[],"containers":[]})";
    }

    const auto symlink = tempDir / "groups-link.json";
    std::filesystem::create_symlink(target, symlink);
    EXPECT_THROW(groups_file::read(symlink, false), std::runtime_error);
    EXPECT_THROW(groups_file::read(tempDir, false), std::runtime_error);

    const auto invalidJson = tempDir / "invalid.json";
    {
        std::ofstream file(invalidJson);
        file << "{invalid";
    }
    EXPECT_THROW(groups_file::read(invalidJson, false), nlohmann::json::parse_error);

    const auto oversized = tempDir / "oversized.json";
    {
        std::ofstream file(oversized);
        file << std::string(groups_file::maxFileSize + 1, ' ');
    }
    EXPECT_THROW(groups_file::read(oversized, false), std::runtime_error);
}

