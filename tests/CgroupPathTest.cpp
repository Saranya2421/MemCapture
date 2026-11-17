#include "CgroupPath.h"
#include <gtest/gtest.h>
#include <sstream>

TEST(CgroupPathL1Test, FindsControllerAfterOtherEntries)
{
    std::istringstream cgroupFile(
        "10:gpu:/container\n"
        "9:pids:/system.slice/example.service\n"
        "3:cpuset:/container/example\n");

    EXPECT_EQ(cgroup::pathForController(cgroupFile, "cpuset"), "container/example");
}

TEST(CgroupPathL1Test, DoesNotMatchSimilarControllerNames)
{
    std::istringstream cgroupFile(
        "3:cpuset2:/wrong\n"
        "4:cpu,cpuacct:/also-wrong\n");

    EXPECT_TRUE(cgroup::pathForController(cgroupFile, "cpuset").empty());
}

TEST(CgroupPathL1Test, ReturnsEmptyForMissingControllerOrRootGroup)
{
    std::istringstream missingController("3:memory:/container\n");
    EXPECT_TRUE(cgroup::pathForController(missingController, "cpuset").empty());

    std::istringstream rootGroup("3:cpuset:/\n");
    EXPECT_TRUE(cgroup::pathForController(rootGroup, "cpuset").empty());
}
