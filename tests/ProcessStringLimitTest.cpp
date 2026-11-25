#include "Process.h"

#include <gtest/gtest.h>

#include <string>
#include <unistd.h>

TEST(ProcessStringLimitL1Test, TruncatesCachedNameAndCommandLine)
{
    constexpr std::string::size_type maxLength = 4;
    const Process process(getpid(), maxLength);

    EXPECT_EQ(process.name().size(), maxLength);
    EXPECT_EQ(process.cmdline().size(), maxLength);
    EXPECT_EQ(process.cmdline().find('\0'), std::string::npos);
}


