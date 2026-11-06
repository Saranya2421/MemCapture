---
applyTo: "**/CMakeLists.txt,**/cmake/**,**/vcpkg.json"
---

# Build System Standards (CMake + vcpkg)

MemCapture uses CMake 3.10+ with vcpkg for desktop builds and Yocto recipes for device builds.
The build produces a single executable `MemCapture` with the HTML template embedded at compile time.

## CMakeLists.txt Best Practices

### Project and Standard
```cmake
# GOOD: Declare minimum version and C++ standard explicitly
cmake_minimum_required(VERSION 3.10)
project(MemCapture)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
```

### Compiler Warnings
```cmake
# GOOD: Enable all warnings; treat as errors in CI
add_compile_options(-Wall -Wextra -Wno-psabi)

# Do NOT use -Werror globally in CMakeLists.txt — use it in CI flags only
# to avoid breaking end-user builds on newer compilers
```

### Finding Dependencies
```cmake
# GOOD: Use find_package with CONFIG for vcpkg packages
find_package(nlohmann_json CONFIG REQUIRED)
find_package(inja CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

# GOOD: Optional dependency with graceful fallback
find_package(Breakpad QUIET)
if (BREAKPAD_FOUND)
    message(STATUS "Enabling Breakpad crash reporter")
    add_definitions(-DUSE_BREAKPAD)
    target_link_libraries(${PROJECT_NAME} breakpad_client)
endif()
```

### Source Files
```cmake
# GOOD: List all sources explicitly — no globbing
add_executable(${PROJECT_NAME}
    main.cpp
    Measurement.cpp
    Procrank.cpp
    GroupManager.cpp
    Process.cpp
    Metadata.cpp
    FileParsers/MemInfo.cpp
    FileParsers/Smaps.cpp
    JsonReportGenerator.cpp
    ProcessMetric.cpp
    MemoryMetric.cpp
    CpuIdleMetric.cpp
)

# BAD: Do not use file(GLOB ...) — it misses new files until cmake re-runs
```

### incbin Template Embedding
```cmake
# GOOD: Declare that main.cpp depends on the template so it recompiles when template changes
set_property(SOURCE main.cpp
    APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/templates/template.html")
```

### Include Paths
```cmake
# GOOD: Use target_include_directories — not include_directories()
target_include_directories(${PROJECT_NAME}
    PRIVATE
    3rdparty    # incbin.h and other bundled headers
    .           # project root for all component headers
)
```

### Linking
```cmake
# GOOD: Link only what is needed
target_link_libraries(${PROJECT_NAME}
    Threads::Threads
    nlohmann_json::nlohmann_json
    pantor::inja
    re2::re2
)

# Always find Threads before linking
find_package(Threads REQUIRED)
```

## vcpkg Dependency Management

### vcpkg.json
Keep `vcpkg.json` minimal — only declare direct dependencies:
```json
{
  "name": "memcapture",
  "version-string": "2.0.1",
  "dependencies": [
    "nlohmann-json",
    "inja",
    "re2"
  ]
}
```

Rules:
- Do not pin transitive dependencies
- Prefer `baseline` over per-package version constraints unless required for a bug fix
- Run `vcpkg install` from the build directory using the toolchain file

### Desktop Build
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake \
      ..
cmake --build . --parallel $(nproc)
```

### Yocto / Cross-Compilation Build
- Add `nlohmann-json`, `inja`, and `re2` as recipe dependencies in the Yocto layer
- Do NOT use the vcpkg toolchain file for Yocto builds
- Use the Yocto SDK sysroot; CMake will find packages via `PKG_CONFIG_PATH`

## Custom CMake Modules

The `cmake/FindBreakpad.cmake` module uses standard Find-module conventions:
```cmake
# GOOD: Set BREAKPAD_FOUND, BREAKPAD_INCLUDE_DIRS, BREAKPAD_LIBRARIES
find_path(BREAKPAD_INCLUDE_DIR client/linux/handler/exception_handler.h ...)
find_library(BREAKPAD_LIBRARY breakpad_client ...)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Breakpad DEFAULT_MSG
    BREAKPAD_LIBRARY BREAKPAD_INCLUDE_DIR)
```

## Build Performance

- Use `cmake --build . --parallel $(nproc)` to exploit parallel compilation
- Do not add unnecessary `add_subdirectory()` calls — MemCapture is a single-target project
- Keep `template.html` dependency tracking accurate to avoid stale binary embeds

## Platform-Specific Notes

| Platform | Build method | Notes |
|----------|-------------|-------|
| Linux desktop | CMake + vcpkg | Full feature set |
| RDK-E / RDK-V devices (Amlogic, Realtek, Broadcom, Mediatek, etc.) | Yocto recipe | No vcpkg; `nlohmann_json` + `inja` + `re2` from sysroot |
| RDK-E / RDK-V with Breakpad | Yocto recipe + Breakpad lib | Set `USE_BREAKPAD` define |

```makefile
# Test targets
check-local:
	@echo "Running memory leak tests..."
	@for test in $(TESTS); do \
		valgrind --leak-check=full \
		         --error-exitcode=1 \
		         ./$$test || exit 1; \
	done

# Code coverage
if ENABLE_COVERAGE
AM_CFLAGS += --coverage
AM_LDFLAGS += --coverage
endif

coverage: check
	$(LCOV) --capture --directory . --output-file coverage.info
	$(GENHTML) coverage.info --output-directory coverage
```
