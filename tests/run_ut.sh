#!/bin/sh
##########################################################################
# Copyright 2023 Sky UK
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
REPO_DIR=$(dirname "$SCRIPT_DIR")
TEST_BIN=${MEMCAPTURE_TEST_BIN:-"$REPO_DIR/build/MemCaptureTests"}

if [ ! -x "$TEST_BIN" ] && [ -x "$TEST_BIN.exe" ]; then
    TEST_BIN="$TEST_BIN.exe"
fi

if [ ! -x "$TEST_BIN" ]; then
    echo "ERROR: MemCaptureTests not found at $TEST_BIN"
    echo "Build with -DBUILD_TESTS=ON or set MEMCAPTURE_TEST_BIN."
    exit 1
fi

cd "$REPO_DIR" || exit 1

TEST_FAILED=0
TC_RESULTS=""

record_tc_result()
{
    tc_id="$1"
    tc_summary="$2"
    tc_status="$3"
    if [ -z "$TC_RESULTS" ]; then
        TC_RESULTS="${tc_id}|${tc_summary}|${tc_status}"
    else
        TC_RESULTS=$(printf '%s\n%s|%s|%s' "$TC_RESULTS" "$tc_id" "$tc_summary" "$tc_status")
    fi
}

run_test_case()
{
    tc_id="$1"
    tc_summary="$2"
    gtest_filter="$3"

    echo "------------------------------------------"
    echo "$tc_id: $tc_summary"
    echo "------------------------------------------"

    if ! test_list=$("$TEST_BIN" "--gtest_filter=$gtest_filter" --gtest_list_tests); then
        echo "ERROR: Unable to list tests for $gtest_filter"
        TEST_FAILED=$((TEST_FAILED + 1))
        record_tc_result "$tc_id" "$tc_summary" "FAILURE"
        return
    fi
    if ! printf '%s\n' "$test_list" | grep -q '^  [^ ]'; then
        echo "ERROR: No tests matched $gtest_filter"
        TEST_FAILED=$((TEST_FAILED + 1))
        record_tc_result "$tc_id" "$tc_summary" "FAILURE"
        return
    fi

    if "$TEST_BIN" "--gtest_filter=$gtest_filter"; then
        record_tc_result "$tc_id" "$tc_summary" "SUCCESS"
    else
        TEST_FAILED=$((TEST_FAILED + 1))
        record_tc_result "$tc_id" "$tc_summary" "FAILURE"
    fi
}

print_tc_summary()
{
    printf '+-%-7s-+-%-52s-+-%-15s-+\n' '-------' '----------------------------------------------------' '---------------'
    printf '| %-7s | %-52s | %-15s |\n' "TC ID" "Small summary" "Success/Failure"
    printf '+-%-7s-+-%-52s-+-%-15s-+\n' '-------' '----------------------------------------------------' '---------------'
    printf '%s\n' "$TC_RESULTS" | while IFS='|' read -r tc_id tc_summary tc_status; do
        [ -z "$tc_id" ] && continue
        printf '| %-7.7s | %-52.52s | %-15.15s |\n' "$tc_id" "$tc_summary" "$tc_status"
    done
    printf '+-%-7s-+-%-52s-+-%-15s-+\n' '-------' '----------------------------------------------------' '---------------'
}

echo "=========================================="
echo "Running MemCapture Unit Tests"
echo "=========================================="
echo "Using test binary: $TEST_BIN"

run_test_case "1" "HTML metacharacter escaping" "HtmlEscapeL1Test.*"
run_test_case "2" "GPU dataset report rendering" "GpuDatasetRenderL2Test.*"
run_test_case "3" "Group matching and regex validation" "GroupManagerL1Test.*"
run_test_case "4" "Process string length bounds" "ProcessStringLimitL1Test.*"
run_test_case "5" "GPU measurement capacity and pruning" "GpuMeasurementPolicyL1Test.*"
run_test_case "6" "Groups-file trust validation" "GroupsFileTest.*"
run_test_case "7" "Duplicate process log redaction" "ProcessMetricLogL1Test.*"
run_test_case "8" "Cgroup controller path parsing" "CgroupPathL1Test.*"

echo
print_tc_summary

if [ "$TEST_FAILED" -ne 0 ]; then
    exit 1
fi

exit 0


