# ImGui + Tracy E3 — Minimal Client Lifecycle

Date: 2026-07-18 to 2026-07-19 (Australia/Brisbane)
Branch: `nightrunner-18th-july`
Plan task: E3
Result: Complete

## Outcome

The development configurations now own Tracy through an explicit manual,
on-demand lifetime at the application boundary. The owner starts before the
engine worker pool, stops only after every worker joins, and stops before the
logging, window, COM, and platform services are torn down. Debug, Profile, and
Automation compile the client; Release compiles neither its client nor its
engine owner.

One Tracy frame mark is emitted only after a successful DX12 `Present` for a
submitted game frame. Failed presentation, capture-only continuation, and
attempted render turns therefore cannot inflate the frame count.

The editor's existing profiler tab shows fixed Tracy build/lifetime/connection
state (`connected`, `waiting for viewer`, or `stopped`). The runtime copies
three booleans from the Tracy owner; UI drawing does not scan processes, open a
socket, retain the owner, grow storage, or allocate a per-frame status string.

## Thread topology and names

The source inventory command

```powershell
rg -n "std::thread|CreateThread\(|_beginthread" SkullbonezSource -g "*.cpp" -g "*.h"
```

found one engine thread owner: `WorkerPool`. The parser schema also reads
`hardware_concurrency` but does not construct a thread. Rendering,
replay/prediction coordination, and cold IO currently run on the main thread,
so E3 names the real topology instead of inventing lanes:

- main: `Skore Main + Render + Replay + IO`;
- persistent workers: `Skore Worker 00` through `Skore Worker 62`.

A live Windows thread-description probe observed one composite main name and
63 unique worker names while the engine was running. This is the same
`tracy::SetThreadName` / `SetThreadNameWithHint` path used by the viewer.

## External viewer and lifetime evidence

The pinned Tracy 0.13.1 headless capture client was configured and built under
ignored validation output (about 151 seconds configure plus 33 seconds build):

`TestOutput/validation/tracy_capture_tool/Release/tracy-capture.exe`

The client connected to `127.0.0.1:8086` while the Debug engine ran a bounded
hidden DX12 scene. It captured 101 frame marks over 2.23 seconds and saved
`TestOutput/validation/tracy_e3_capture.tracy` (982 bytes). The engine stdout
artifact is 810 bytes, stderr is 0 bytes, and the lifecycle order is:

```text
[tracy] Manual on-demand client started. viewer=waiting
[workers] Worker pool initialized with 63 thread(s).
...
[runtime-exit] Exiting because scene screenshot capture completed and no next scene is queued.
[tracy] Client stopped after engine worker shutdown.
```

A repeated connection probe captured 101 frame marks over 2.39 seconds in a
968-byte trace with 182 bytes of capture stdout and 0 bytes of capture stderr.
The no-viewer `Debug\SKULLBONEZ_CORE.exe --worker-self-test` run also exited 0
in 3.83 seconds and showed the same start → worker shutdown → client shutdown
order. Disconnecting the capture client did not disturb the engine, and both
bounded game runs exited cleanly.

## Disabled-build contract

`SKORE_TRACY_MARK_SUBMITTED_FRAME()` and
`SKORE_TRACY_NAME_WORKER_THREAD(argument)` discard their tokens when Tracy is
not compiled. The focused doctest proves that a side-effecting worker-name
argument is not evaluated; it passed 1/1 test and 1/1 assertion in a 6.9-second
focused Debug build/run.

`tools\validate_build.bat Release` passed in 47.28 seconds with zero warnings
and zero errors. Its compile/link inventory contains neither
`TracyClient.cpp` nor `TracyClientOwner.cpp`; a follow-up executable/object scan
found no Tracy owner object, runtime symbol, label, or diagnostic string.

## Validation

- `tools\validate_project_filters.bat` — passed: 752 project items and 752
  filter items, zero errors (3.0 seconds).
- Focused Debug test — passed: 1/1 Tracy disabled-seam test, 1/1 assertion
  (6.9 seconds). An initial explicitly forced `v143` lookup failed in 0.8
  seconds because this machine has the repository's `v145` toolset; rerunning
  with the project-selected toolset passed and required no source change.
- `tools\validate_build.bat Release` — passed: zero warnings/errors, 47.28
  seconds; Release artifact exclusion scan passed.
- `tools\validate_fast.bat` — passed after the scoped header-alignment
  post-pass; Profile and Debug builds produced zero warnings/errors and the
  full unit suite passed. The first invocation correctly rejected the touched
  profiler header until the repository's header pipeline was applied only to
  that file.
- `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` — exact command
  launched as PID 39592; its identity/command line were checked, then the
  native window received `WM_CLOSE` and the process exited cleanly.
- `tools\validate_full.bat` — passed in about 2 minutes 37 seconds. Evidence:
  295/295 doctests and 21,471/21,471 assertions; all ratified coverage floors;
  interaction, scene-parser, and DX12 architecture suites; Automation
  replay/prediction smoke; zero DX12 validation errors and all three screenshot
  baseline comparisons; standalone physics smoke; and a 44,401-line byte-exact
  core physics CSV match.

No behavioral baseline, replay golden, screenshot baseline, physics CSV, or
coverage floor changed.

## Comment-quality checklist

Checklist path: this report.

Checked: 11. Deferred: 0. Unchecked: none.

- [x] `SkullbonezSource/Core/WorkerPool.cpp`
- [x] `SkullbonezSource/Runtime/Init.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/UiTextPass.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.cpp`
- [x] `SkullbonezSource/UI/UIFrameComposition.cpp`
- [x] `SkullbonezSource/UI/UITabProfiler.h`
- [x] `SkullbonezSource/UI/UITabProfiler.cpp`
- [x] `SkullbonezTests/TestRuntimeContracts.cpp`
- [x] `tools/validate_project_filters.py`

Every touched source-bearing file has a learning header. The audit added or
confirmed local definitions and nearby `Invariant:`, `Lifetime:`, `Why:`, and
`Concept:` comments for manual profiler lifetime, submitted-frame semantics,
stable thread labels, fixed connection snapshots, disabled macro evaluation,
and allocation-owner scope. No term remains for human-approved wording.
