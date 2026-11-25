#include "Process.h"
#include <gtest/gtest.h>
#include <sstream>

class ProcessTestAccess
{
public:
    static std::string pathForController(std::istream &stream, const std::string &controller)
    {
        return Process::parseCgroupPath(stream, controller);
    }
};

TEST(CgroupPathL1Test, FindsControllerAfterOtherEntries)
{
    std::istringstream cgroupFile(
        "10:gpu:/container\n"
        "9:pids:/system.slice/example.service\n"
        "3:cpuset:/container/example\n");

    EXPECT_EQ(ProcessTestAccess::pathForController(cgroupFile, "cpuset"), "container/example");
}

TEST(CgroupPathL1Test, DoesNotMatchSimilarControllerNames)
{
    std::istringstream cgroupFile(
        "3:cpuset2:/wrong\n"
        "4:cpu,cpuacct:/also-wrong\n");

    EXPECT_TRUE(ProcessTestAccess::pathForController(cgroupFile, "cpuset").empty());
}

TEST(CgroupPathL1Test, ReturnsEmptyForMissingControllerOrRootGroup)
{
    std::istringstream missingController("3:memory:/container\n");
    EXPECT_TRUE(ProcessTestAccess::pathForController(missingController, "cpuset").empty());

    std::istringstream rootGroup("3:cpuset:/\n");
    EXPECT_TRUE(ProcessTestAccess::pathForController(rootGroup, "cpuset").empty());
}

