#include "HtmlEscape.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *templatePath = "templates/template.html";

nlohmann::json makeProcess(const std::string &payload)
{
    return {
        {"pid", 1},
        {"ppid", 0},
        {"name", payload},
        {"group", payload},
        {"systemdService", payload},
        {"container", payload},
        {"cmdline", payload},
        {"rss", {{"min", 0}, {"max", 0}, {"average", 0}}},
        {"pss", {{"min", 0}, {"max", 0}, {"average", 0}}},
        {"uss", {{"min", 0}, {"max", 0}, {"average", 0}}}
    };
}

nlohmann::json makeReport(const std::string &header, const std::string &gpuPayload,
                          const std::string &processPayload)
{
    const std::vector<std::string> columns = {
        header, "Process", "Command Line", "Container"
    };
    nlohmann::json row = {
        {header, gpuPayload},
        {"Process", gpuPayload},
        {"Command Line", gpuPayload},
        {"Container", gpuPayload}
    };
    nlohmann::json processes = nlohmann::json::array();
    for (int i = 0; i < 20; ++i) {
        processes.emplace_back(makeProcess(i == 0 ? processPayload : "process"));
    }

    return {
        {"metadata", {
            {"platform", "test"},
            {"image", "test-image"},
            {"mac", "00:00:00:00:00:00"},
            {"timestamp", "test-time"},
            {"duration", 1},
            {"swapEnabled", false}
        }},
        {"grandTotal", {{"linuxUsage", 0}, {"calculatedUsage", 0}}},
        {"processes", std::move(processes)},
        {"data", nlohmann::json::array({
            {
                {"name", "GPU Memory"},
                {"_columnOrder", columns},
                {"data", nlohmann::json::array({row})}
            }
        })},
        {"pssByGroup", nullptr},
        {"cpuIdleStats", nullptr}
    };
}

std::string renderReport(const nlohmann::json &report)
{
    std::ifstream templateFile(templatePath);
    if (!templateFile) {
        throw std::runtime_error("Unable to open report template");
    }
    const std::string templateText(
        (std::istreambuf_iterator<char>(templateFile)),
        std::istreambuf_iterator<char>());

    inja::Environment env;
    env.set_trim_blocks(true);
    env.set_lstrip_blocks(true);
    registerHtmlEscapeCallback(env);
    registerJsEscapeCallback(env);
    env.add_callback("objectToArray", 2, [](inja::Arguments &args)
    {
        const auto &row = *args.at(0);
        const auto &columns = *args.at(1);
        std::vector<nlohmann::json> values;
        for (const auto &column : columns) {
            values.emplace_back(row.at(column.get<std::string>()));
        }
        return values;
    });

    return env.render(templateText, report);
}

} // namespace

TEST(HtmlEscapeL1Test, EscapesHtmlMetacharactersAndPreservesOrdinaryText)
{
    EXPECT_EQ(htmlEscape(nlohmann::json("&<>\"'")), "&amp;&lt;&gt;&quot;&#39;");
    EXPECT_EQ(htmlEscape(nlohmann::json("GPU client_1-2")), "GPU client_1-2");
}

TEST(HtmlEscapeL1Test, SerializesNonStringJsonBeforeEscaping)
{
    EXPECT_EQ(htmlEscape(nlohmann::json(42)), "42");
}

TEST(GpuDatasetRenderL2Test, EscapesGpuDatasetHeadersAndCellsInActualTemplate)
{
    const std::string gpuPayload = "<gpu-client data='\"&'>";
    const std::string processPayload = "<process data='\"&'>";
    const std::string escapedGpuPayload = "&lt;gpu-client data=&#39;&quot;&amp;&#39;&gt;";
    const std::string escapedProcessPayload = "&lt;process data=&#39;&quot;&amp;&#39;&gt;";
    const std::string header = "GPU <Memory> & \"Usage\"";

    const std::string rendered = renderReport(makeReport(header, gpuPayload, processPayload));

    EXPECT_NE(rendered.find("GPU &lt;Memory&gt; &amp; &quot;Usage&quot;"), std::string::npos);
    EXPECT_EQ(rendered.find(header), std::string::npos);
    EXPECT_EQ(rendered.find(gpuPayload), std::string::npos);

    const size_t processTableStart = rendered.find("<table id=\"processes\"");
    ASSERT_NE(processTableStart, std::string::npos);
    const size_t processTableEnd = rendered.find("</table>", processTableStart);
    ASSERT_NE(processTableEnd, std::string::npos);
    const std::string processTable = rendered.substr(
        processTableStart, processTableEnd - processTableStart);
    EXPECT_EQ(processTable.find(processPayload), std::string::npos);

    size_t gpuOccurrences = 0;
    size_t position = 0;
    while ((position = rendered.find(escapedGpuPayload, position)) != std::string::npos) {
        ++gpuOccurrences;
        position += escapedGpuPayload.size();
    }
    EXPECT_EQ(gpuOccurrences, 4U);
    EXPECT_NE(rendered.find("<th>" + escapedGpuPayload + "</th>"), std::string::npos);

    size_t processOccurrences = 0;
    position = 0;
    while ((position = processTable.find(escapedProcessPayload, position)) != std::string::npos) {
        ++processOccurrences;
        position += escapedProcessPayload.size();
    }
    EXPECT_EQ(processOccurrences, 5U);
}

