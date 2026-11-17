#include "Process.h"

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

nlohmann::json loadFixture()
{
    std::ifstream fixture("test/4-process-string-limit/input.json");
    if (!fixture) {
        throw std::runtime_error("Unable to open process-string fixture");
    }
    return nlohmann::json::parse(fixture);
}

} // namespace

TEST(ProcessStringLimitL1Test, TruncatesCachedNameAndCommandLine)
{
    const auto fixture = loadFixture();
    const auto maxLength = fixture.at("maxStoredLength").get<std::string::size_type>();
    const Process process(getpid(), maxLength);

    EXPECT_EQ(process.name().size(), maxLength);
    EXPECT_EQ(process.cmdline().size(), maxLength);
    EXPECT_EQ(process.cmdline().find('\0'), std::string::npos);
}

