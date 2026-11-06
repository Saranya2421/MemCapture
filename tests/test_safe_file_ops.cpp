/*
 * Security regression tests for SafeFileOps (RDKEMW-25861)
 *
 * Validates the security invariants introduced to prevent:
 * - MemCapture_High_001: Unvalidated -o output path via TR-181 injection
 * - MemCapture_High_002: Symlink-following ofstream trunc
 * - MemCapture_High_004: Fixed-path symlink-following write
 *
 * These tests run without root privileges where possible, using
 * path validation checks that don't require root ownership.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>

#include "SafeFileOps.h"

namespace fs = std::filesystem;

class SafeFileOpsTest : public ::testing::Test {
protected:
    fs::path testDir;

    void SetUp() override {
        // Create a temporary test directory
        testDir = fs::temp_directory_path() / "memcapture_test_safefileops";
        fs::create_directories(testDir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(testDir, ec);
    }
};

// --- validateOutputDirectory tests ---

// H001: Path outside allowed prefixes must be rejected
TEST_F(SafeFileOpsTest, RejectsPathOutsideAllowedPrefixes) {
    EXPECT_FALSE(validateOutputDirectory("/etc/evil"));
    EXPECT_FALSE(validateOutputDirectory("/home/attacker/output"));
    EXPECT_FALSE(validateOutputDirectory("/var/run/steal"));
    EXPECT_FALSE(validateOutputDirectory("/"));
    EXPECT_FALSE(validateOutputDirectory("/usr/local/bin"));
}

// H001: Allowed paths must be accepted
TEST_F(SafeFileOpsTest, AcceptsAllowedPrefixes) {
    EXPECT_TRUE(validateOutputDirectory("/tmp/memcapture_reports"));
    EXPECT_TRUE(validateOutputDirectory("/tmp/MemCaptureReport"));
    EXPECT_TRUE(validateOutputDirectory("/opt/logs/memcapture"));
    EXPECT_TRUE(validateOutputDirectory("/opt/memcapture/output"));
}

// H001: Path traversal via ../ must be caught after canonicalization
TEST_F(SafeFileOpsTest, RejectsPathTraversalAttempts) {
    // /tmp/../etc/passwd -> /etc/passwd after canonicalization
    EXPECT_FALSE(validateOutputDirectory("/tmp/../etc/passwd"));
    EXPECT_FALSE(validateOutputDirectory("/tmp/foo/../../etc"));
    EXPECT_FALSE(validateOutputDirectory("/opt/logs/../../../etc"));
}

// H001: Relative paths that resolve outside allowed prefixes must be rejected
TEST_F(SafeFileOpsTest, RejectsRelativePathsOutsidePrefixes) {
    // A relative path that resolves to the CWD (which is likely not under /tmp/)
    // weakly_canonical will resolve to CWD + the relative path
    auto cwd = fs::current_path().string();
    if (cwd.substr(0, 5) != "/tmp/" &&
        cwd.substr(0, 10) != "/opt/logs/" &&
        cwd.substr(0, 16) != "/opt/memcapture/") {
        EXPECT_FALSE(validateOutputDirectory("relative_output"));
        EXPECT_FALSE(validateOutputDirectory("./output"));
    }
}

// --- safeOpenForWrite tests ---

// H002/H004: Opening a symlink must fail (O_NOFOLLOW)
TEST_F(SafeFileOpsTest, RejectsSymlinkTarget) {
    fs::path realFile = testDir / "real_file.txt";
    fs::path symlinkFile = testDir / "symlink_file.txt";

    // Create a real file and a symlink to it
    std::ofstream(realFile) << "original content";
    fs::create_symlink(realFile, symlinkFile);

    // safeOpenForWrite should reject the symlink
    int fd = safeOpenForWrite(symlinkFile);
    EXPECT_EQ(fd, -1) << "safeOpenForWrite must reject symlinks (O_NOFOLLOW)";
    if (fd >= 0) close(fd);
}

// H004: Opening a symlink to a sensitive file must fail
TEST_F(SafeFileOpsTest, RejectsSymlinkToSensitiveTarget) {
    fs::path symlinkFile = testDir / "evil_symlink.txt";

    // Create symlink pointing to /etc/hostname (a real file, but we shouldn't follow it)
    fs::create_symlink("/etc/hostname", symlinkFile);

    int fd = safeOpenForWrite(symlinkFile);
    EXPECT_EQ(fd, -1) << "safeOpenForWrite must not follow symlink to sensitive file";
    if (fd >= 0) close(fd);
}

// Normal file open should succeed
TEST_F(SafeFileOpsTest, AcceptsRegularFile) {
    fs::path normalFile = testDir / "normal_file.txt";

    int fd = safeOpenForWrite(normalFile);
    EXPECT_GE(fd, 0) << "safeOpenForWrite should succeed on a regular path";
    if (fd >= 0) {
        // Verify we can write
        EXPECT_TRUE(writeToFd(fd, "test content"));
        close(fd);

        // Verify content was written
        std::ifstream ifs(normalFile);
        std::string content((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        EXPECT_EQ(content, "test content");
    }
}

// --- writeToFd tests ---

TEST_F(SafeFileOpsTest, WriteToFdHandlesLargeData) {
    fs::path file = testDir / "large_file.txt";
    int fd = safeOpenForWrite(file);
    ASSERT_GE(fd, 0);

    // Write 1MB of data
    std::string largeData(1024 * 1024, 'A');
    EXPECT_TRUE(writeToFd(fd, largeData));
    close(fd);

    // Verify size
    EXPECT_EQ(fs::file_size(file), 1024u * 1024u);
}

TEST_F(SafeFileOpsTest, WriteToFdRejectsInvalidFd) {
    EXPECT_FALSE(writeToFd(-1, "data"));
}
