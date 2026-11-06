# MemCapture OpenSpec Baseline Architecture

## Scope and Intent

This document is the primary architecture baseline for OpenSpec adoption in a brownfield embedded Linux/RDK repository.
It focuses on runtime behavior and subsystem responsibilities of the single runtime binary built from this codebase.

## Classification Legend

- Facts: directly verified from source or build files.
- Inferences: strongly implied by source structure and control flow.
- Assumptions: reasonable but not directly proven by source.
- Unknowns: require device-side or pipeline validation.

## System Purpose and Deployment Context

### Facts - System Purpose

- The repository builds a single executable target named `MemCapture` via CMake `add_executable(${PROJECT_NAME} ...)` where `project(MemCapture)` defines `PROJECT_NAME` (`CMakeLists.txt:19`, `CMakeLists.txt:35`).
- The binary captures memory telemetry and emits reports, described in repository docs as a memory capture/analysis tool for RDK (`README.md:1`, `README.md:3`).
- Runtime output includes `report.html` always and optional `report.json` when `-j` is provided (`main.cpp:327`, `main.cpp:332`, `main.cpp:344`).
- Build dependencies are `nlohmann_json`, `inja`, and `re2`; `incbin` is bundled in-tree (`CMakeLists.txt`, `main.cpp`, `README.md`).

### Inferences - System Purpose

- The intended operational role is one-shot telemetry capture under an external scheduler or maintenance manager, because `main` does not daemonize and exits immediately after writeout (`main.cpp:179` onward).
- The JSON output is intended for downstream automation while HTML is human-facing, based on write ordering and comments in `main.cpp`.

### Assumptions - System Purpose

- Fleet production invocation likely wraps `MemCapture` in a platform service (cron/systemd/telemetry agent), but wrapper logic is not in this repo.

### Unknowns - System Purpose

- Actual invocation cadence and orchestration contracts in production RDK images are not represented in this repository.

## High-Level Architecture

### Facts - High-Level Architecture

- `main.cpp` owns orchestration: argument parsing, signal handling, output path creation, metric lifecycle, report serialization/render (`main.cpp:83`, `main.cpp:179`).
- Metrics are concrete `IMetric` implementations: `ProcessMetric`, `MemoryMetric`, and optional `CpuIdleMetric` (`main.cpp:233`, `main.cpp:234`, `main.cpp:238`, `IMetric.h:28`).
- Parsing subsystem includes `/proc/meminfo` (`FileParsers/MemInfo.cpp`) and `/proc/<pid>/smaps(_rollup)` (`FileParsers/Smaps.cpp`, `Procrank.cpp:165`).
- Aggregation/reporting goes through `JsonReportGenerator` and then inja templating with embedded HTML template (`JsonReportGenerator.cpp`, `main.cpp:52`, `main.cpp:286`, `main.cpp:339`).

### Inferences - High-Level Architecture

- Layering is: kernel/procfs/sysfs data sources -> parsers -> metric accumulators (`Measurement`) -> report generator -> JSON/HTML artifacts.

### Assumptions - High-Level Architecture

- HTML is primarily for engineer triage, JSON for machine ingestion, based on template complexity and optional JSON flag.

## One-Shot Execution Model

### Facts - One-Shot Model

- Start flow: parse CLI -> register SIGTERM/SIGINT -> lower priority with `nice(10)` -> create output dir -> optional group config load (`main.cpp:181` through `main.cpp:230`).
- Collection flow: start metric threads (`ProcessMetric` and `MemoryMetric`) at 3s frequency; optional `CpuIdleMetric` starts when `-c` and build flag are both true (`main.cpp:243` to `main.cpp:251`).
- Main thread blocks via monotonic `ConditionVariable::wait_for` for duration or signal (`main.cpp:257`, `ConditionVariable.h:135`).
- Shutdown flow: stop metrics, join threads, run `SaveResults`, then write JSON/HTML and exit (`main.cpp:268` to `main.cpp:349`).

### Inferences - One-Shot Model

- This is deterministic one-shot capture with bounded collection window and synchronous finalization.

### Assumptions - One-Shot Model

- Collection period of 3s is tuned to balance cost vs fidelity for constrained devices.

## Platform Dispatch Summary

### Facts - Platform Dispatch

- CLI platform strings map to enum: `AMLOGIC`, `AMLOGIC_950D4`, `REALTEK`, `REALTEK64`, `BROADCOM`, `MEDIATEK` (`main.cpp:116` to `main.cpp:128`, `Platform.h:22`).
- `MemoryMetric` performs platform dispatch for GPU collection and some parsing rules (`MemoryMetric.cpp:468`, `MemoryMetric.cpp:722`).
- Amlogic/950D4 memory-bandwidth collection is gated by `/sys/class/aml_ddr/mode` and `/sys/class/aml_ddr/bandwidth` (`MemoryMetric.cpp:123`, `MemoryMetric.cpp:631`).

### Inferences - Platform Dispatch

- Portability risk is concentrated in `MemoryMetric` due to vendor-specific sysfs/debugfs formats and paths.

### Unknowns - Platform Dispatch

- Device-specific path presence/permissions across all RDK board variants must be validated on target hardware.

## Build Topology Summary

### Facts - Build Topology

- Desktop build path uses CMake + vcpkg toolchain (`README.md:23`, `vcpkg.json`).
- Device build guidance references Yocto recipe dependencies for json/inja (`README.md:29`).
- Breakpad is optional via `find_package(Breakpad QUIET)` and compile definition `USE_BREAKPAD` when found (`CMakeLists.txt:30`, `CMakeLists.txt:60`).
- CPU idle metric is compile-time optional via `ENABLE_CPU_IDLE_METRICS` (`CMakeLists.txt:83`).

### Unknowns - Build Topology

- Exact Yocto layer/recipe wiring and package names in production BSPs are not included in this repository.

## Supporting Documents

- Execution model: `runtime/execution-model.md`
- Measurement/statistics model: `runtime/measurement-model.md`
- Shutdown coordination: `runtime/shutdown-coordination.md`
- IMetric architecture: `subsystems/imetric-architecture.md`
- Memory metric details: `subsystems/memory-metric.md`
- Process metric details: `subsystems/process-metric.md`
- CPU idle metric details: `subsystems/cpu-idle-metric.md`
- Parser subsystem: `subsystems/file-parsers.md`
- Process grouping: `subsystems/group-manager.md`
- JSON schema: `report/json-schema.md`
- HTML rendering: `report/html-report.md`
- Metadata injection: `report/metadata.md`
- Platform portability: `platform/platform-portability.md`
- Known gaps and validation needs: `platform/known-gaps.md`
- Diagram set:
  - `diagrams/execution-flow.md`
  - `diagrams/collection-thread-lifecycle.md`
  - `diagrams/platform-dispatch.md`
  - `diagrams/report-pipeline.md`
  - `diagrams/data-flow.md`
