# ImGui/Tracy E4 Owner-Boundary Instrumentation

Date: 2026-07-19
Branch: `nightrunner-18th-july`
Plan task: E4 — instrument Tracy at owner boundaries, not every function

## Outcome

E4 is complete. Tracy now mirrors the engine's established profiler owner
paths into focused zones, publishes bounded capacity plots, and provides an
explicit heavy capture mode for call stacks and global C++ heap events. The
reference capture correlates frame, replay, physics, rendering, UI, and DX12
work without changing a baseline, screenshot, authored file, or behavioral
oracle.

Tracy is compiled only in development configurations and is off at process
startup unless `SKORE_TRACY_MODE=standard` or `SKORE_TRACY_MODE=heavy` is set.
This keeps ordinary and performance runs free of Tracy's vendor transport-queue
working-set cost while retaining an attachable on-demand client for named
profiling launches.

## Owner Boundary

- The existing `Profiler` hierarchy remains the vocabulary owner. Main-thread
  and worker scopes register their complete owner path in a fixed 256-row Tracy
  source-location table, then emit only while a viewer is connected.
- A 16-row thread-local direct-mapped lookup cache keeps recurring worker scope
  registration off the global mutex. Names are borrowed process-lifetime
  profiler strings; no runtime string growth was introduced.
- Zone tokens retain the Tracy connection generation. An end event is discarded
  after disconnect/reconnect so it cannot close a zone in a later session.
- Direct zones cover replay restore and cold artifact IO plus DX12 command-list
  close, command submission, swap-chain present, and frame-owner present.
- Existing owner paths provide frame sequencing, replay record/prediction,
  physics stages, render passes, and UI build/render coverage without adding
  markers to every function.

## Capacity Plots

The connected standard capture publishes 32 distinct capacity plots:

| Family | Distinct plots | Examples |
|---|---:|---|
| `Counter/Workers` | 4 | active workers, jobs, core milliseconds, utilization |
| `Counter/Physics` | 5 | bodies, awake bodies, persistent contact rows, reinserts, hot bytes |
| `Counter/Render` | 9 | draw calls, upload use/high-water, descriptor use/high-water |
| `Counter/Replay` | 10 | retained samples/capacities, events, reserve bytes/capacity |
| `Counter/DevelopmentTools` | 4 | ImGui/Tracy active and high-water bytes |

Value snapshots that exist only to feed Tracy are skipped when no viewer is
connected. Replay reserve high-water sampling is amortized to once every 60
solver frames.

## Capture Modes And Cost

| Process setting | Behavior |
|---|---|
| unset or any unrecognized value | client remains off; named default/perf configuration |
| `SKORE_TRACY_MODE=standard` | owner zones, submitted-frame marks, thread names, and capacity plots |
| `SKORE_TRACY_MODE=heavy` | standard data plus depth-16 zone/allocation call stacks and named global C++ heap events |

Final-source standard reference capture:

```text
SKORE_TRACY_MODE=standard
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step \
  --audio off --automation-hidden-window --frames 300 --replay on \
  --replay-seconds 1 --scene SkullbonezData/scenes/physics_roll.scene.json
```

- 303 submitted frames, 22,519 zones, 1.04-second captured span.
- Trace: `TestOutput/validation/tracy_e4_final_explicit_standard.tracy`,
  108,917 bytes; engine and capture stderr were both empty.
- Export: 9,931-byte zone summary and 2,438,195-byte unwrapped
  zone/plot CSV; exporter stderr was empty.
- Required zone families were all present: `Frame` 86 unique / 22,519 events,
  `Frame/Replay` 7 / 2,100, `Frame/Physics` 36 / 6,317,
  `Frame/Render` 10 / 3,000, `Frame/UI` 5 / 1,500, and
  `Frame/DX12` 4 / 1,202.
- Capture plus save completed in 3.445 seconds; both exports completed in
  2.079 seconds.

Final-source heavy proof used the same executable and an ordinary ten-frame
`physics_roll` launch with `SKORE_TRACY_MODE=heavy`:

- Banner confirmed `callstacks=depth-16 allocations=global-cpp-heap`.
- 13 frames and 742 zones produced a 23,376,355-byte trace.
- The connected capture and shutdown flush took 37.153 seconds; stderr was
  empty. This measured cost is why heavy mode is explicit and is not a perf
  comparison configuration.
- A deliberately adverse screenshot/cold-IO heavy probe produced 19.63 MiB in
  14 frames and exceeded its initial 30-second flush bound. Only that exact PID
  was stopped. The bounded ordinary-scene heavy proof above then exited cleanly.

## Validation

Final-source evidence:

- Targeted `Profile|x64` build: 10.616 seconds, zero warnings/errors.
- Disabled macro seam: 1 test case, 4 assertions; argument expressions were
  not evaluated when Tracy was not compiled.
- `tools\validate_perf.bat`: passed in 108.013 seconds. The first run exposed
  Tracy's always-started transport queues as a +19–24 MiB memory regression.
  Requiring an explicit capture mode removed that regression without changing
  a perf baseline; Physics Bench start/restart/end became
  142.74/207.11/207.11 MiB versus the approved
  173.54/239.75/239.49 MiB baseline.
- `tools\validate_full.bat`: passed in 138.440 seconds with 295 tests and
  21,474 assertions, every coverage floor, Automation replay/prediction smoke,
  zero DX12 validation errors, matching screenshots, and the 44,401-line core
  physics regression byte-exact.
- `tools\run_graphics_stress.bat 1`: passed in 60.864 seconds and closed its
  exact PID at the bounded timeout.
- `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers`: ran for 10.483
  seconds and closed normally through `CloseMainWindow`.
- `tools\validate_replay_visual_fidelity.bat`: reached its sole pre-existing,
  owner-gated Physics P1 mismatch after 406.345 seconds:
  `causal.topologyCount: expected=199 actual=200`. No replay golden changed.

Two formatting preflights initially identified only five touched `.cpp` files
and then `TracyClientOwner.h`; targeted clang-format/header alignment corrected
those files before the passing full gate. A forced v143 targeted build also
failed because this machine has v145 only; the project-selected toolset builds
above passed. The pinned Tracy capture/export utilities were built with Visual
Studio's bundled CMake because `cmake` is not on this shell's PATH.

## Comment Audit

Checklist inventory was regenerated from the ten touched tracked
source-bearing files and audited against
`Agentic/Skills/comment-style-audit/skill.md`:

- [x] `SkullbonezSource/Core/Profiler.cpp`
- [x] `SkullbonezSource/Core/Profiler.h`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- [x] `SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [x] `SkullbonezTests/TestRuntimeContracts.cpp`

Checked: 10. Deferred: 0. Unchecked: 0.

## Remaining External Blockers

Physics P1 remains unchecked pending exact owner authority for the replay
topology golden transition `199 -> 200` and the mechanically derived
`physics_query_varied.json` update. Neither artifact is part of E4 and neither
was changed here.
