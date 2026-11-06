# Process Grouping Subsystem

## Group Definition Schema

### Facts - Group Schema

- Group file is loaded only when `-g/--groups` is provided and parse succeeds (`main.cpp:212` through `main.cpp:227`).
- Expected top-level arrays are `processes` and `containers` (`GroupManager.cpp:28`, `GroupManager.cpp:58`).
- Each process-group object expects:
  - `group` (string)
  - `processes` (array of regex strings)
- Each container-group object expects:
  - `group` (string)
  - `containers` (array of regex strings)
- Example schema instance is provided in `groups.example.json` (`groups.example.json:2`, `groups.example.json:10`).

## Matching and Resolution

### Facts - Matching

- `GroupManager` compiles configured patterns into RE2 expressions and stores them as `Group` entries (`GroupManager.cpp`).
- Matching uses RE2 partial matching on at most the first 4096 bytes of the input (`Group.h`).
- Pattern configuration is bounded to 128 patterns total, 64 patterns per group, 128 bytes per pattern, and 128 groups per type (`GroupManager.cpp`).
- Process-level group resolution order in `Process::group`:
  1. container match
  2. process basename
  3. full cmdline
  (`Process.cpp:148` through `Process.cpp:174`)

## Aggregation Impact on Output

### Facts - Aggregation Impact

- Per-process JSON includes `group` string in each process object (`JsonReportGenerator.cpp:121` to `JsonReportGenerator.cpp:127`).
- Group-level aggregate chart data `pssByGroup` is computed from process average PSS by resolved group (`JsonReportGenerator.cpp:151` onward).

## Inferences

- Grouping affects labeling and reporting aggregation only; it does not alter metric collection behavior.
