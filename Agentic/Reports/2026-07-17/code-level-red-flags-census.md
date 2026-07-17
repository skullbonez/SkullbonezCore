# Code-Level Red-Flags Census And Proposed Owner Rulings

Date: 2026-07-17
Branch: `nightrunner-17th-july`
Source inspected: `9a8dfad1da7dbf92cb065b19e1cbf74b7e3d62b3`
Plan: `Agentic/Plans/TODO/code-level-red-flags-remediation.md`
Status: C0 owner-ratified evidence. The owner approved the rulings below on
2026-07-17 and directed the nightrunner to continue through plan completion.

## Census Result

Every Round-6 red-flag finding remains present at the current source tip.
The branch contains only the separately committed coverage-governance prose
change; no product source has moved since the reviewed `main` source.

| Finding | Current-tip evidence | Ownership conclusion |
|---|---|---|
| Text batch globals | `Rendering/Text.cpp:64-81` defines mutable file-scope text/quad arrays, vertex counts, and the orthographic projection. Mutating call sites run through `Text2d::Render2dText*`, `Batch*`, `Flush*`, and `RebuildProjection`. | `RuntimeRenderer` is already the process render owner that initializes, submits, and releases UI text resources (`Runtime/Render/RuntimeRenderer.h:117-151`). It is the narrow destination for a fixed-capacity `TextBatch` member. |
| Profiler singleton resolution | `Core/Profiler.cpp:43-46` owns the magic static. `ProfilerScope` and `GpuProfilerScope` resolve it independently in construction/destruction (`Core/Profiler.h:301-328`). Product `.cpp` files contain 29 `PROFILE_BEGIN`, 30 `PROFILE_END`, 18 GPU begin, 19 GPU end, 109 CPU scoped, and 4 worker-scoped uses. | The hot owners must receive a startup-bound profiler handle; changing only the singleton spelling would preserve the hidden dependency. Scope objects retain that handle for their lifetime. |
| Lock validator lookup | `Core/LockOrderValidator.cpp:206-230` performs four `Instance()` calls across construction/acquire/release paths. | `TrackedMutex` can capture the Debug validator once at construction. Profile/Release must compile acquisition/release instrumentation away rather than resolving a no-op singleton. |
| Engine log magic static | `Core/Log.cpp:42-46` owns the function-local `EngineLog`; it lazily owns Debug/test FILE handles and Release has no payload. | Retain and document this as the single allowed cold/fatal diagnostics static. It is not a frame service and avoids startup/shutdown dependency cycles on fatal worker paths. |
| Physics friend edge | `Physics/PhysicsScene.h:189` grants `PhysicsEngine` friendship. The only direct private access is the thirteen immutable query publications in `PhysicsEngine.cpp:357-418`; mutation already uses public `PhysicsScene` commands. | Publish one typed immutable `PhysicsSceneReadView` from `PhysicsScene`; `PhysicsEngine` projects its thirteen public read APIs from that value and the friend is deleted. No mutable store reference is added. |
| 32-tick clamp | `Physics/SimulationSystem.cpp:41,81-87` caps deterministic time-scale catch-up at 32 but retains all uncommitted whole ticks in the accumulator. Existing `TestSimulationSystem.cpp` covers interpolation/pause cadence, not clamp/drop accounting. | Select five committed ticks per frame and explicitly remove/report excess whole ticks. Add cumulative dropped-tick and hitch-event diagnostics to the result/owner plus focused arithmetic tests. |
| WPO/LTCG determinism | `SKULLBONEZ_CORE.vcxproj:35,51` enables WPO and `:154,233` enables LTCG in optimized configurations. The same policy exists in the test project. | Structurally narrow optimization for solver-critical physics TUs, then require paired performance evidence and byte-exact physics output across two consecutive clean optimized rebuilds. Do not accept a fresh physics baseline. |

## Proposed C0 Owner Rulings

1. Branch: ratify `nightrunner-17th-july`.
2. Text: `RuntimeRenderer` owns a fixed-capacity `TextBatch`; all mutating text
   submission receives it by reference. Static glyph measurement may remain
   read-only, but no mutable file-scope text state remains.
3. Profiler: choose explicit startup-bound handle plumbing through runtime,
   render, UI, and physics owners. Scope objects cache the supplied reference.
   Manual begin/end sites use the same owner handle; a different global accessor
   is not an acceptable substitute.
4. Lock validation: capture the Debug validator in each `TrackedMutex`; compile
   the instrumentation member and calls out of Profile/Release.
5. Logging: retain `EngineLog::Get()` only as the documented cold/fatal
   diagnostics static.
6. Clamp: select `5` ticks per frame. Excess whole fixed-step ticks are dropped
   deterministically and exposed through per-tick result facts plus cumulative
   owner counters.
7. LTO: select structural narrowing. Solver-critical physics TUs compile
   outside whole-program optimization; all other product code keeps WPO/LTCG.
   Performance budget and two-rebuild byte identity decide acceptance, never a
   regenerated oracle.

## Ledger Defect

The plan contains seven numbered tasks (`C0` through `C6`) while its title
status and MASTER ledger say `0/6`. The other plans contain seven (`T0-T6`) and
eight (`N0-N7`) tasks. The live denominator is therefore **22**, not 21.
The C0 commit must reconcile the red-flags row to `1/7` and the portfolio to
`1/22` (rounded 5%) after owner ratification.

## Validation

C0 is documentation-only. No repository validation is required; the final C0
diff must be proved documentation-only with `git diff --check` before commit.

## C1 Closure Evidence

Completed 2026-07-17 on `nightrunner-17th-july`. `RuntimeRenderer` now owns a
fixed-capacity `TextBatch` containing the text vertices, quad vertices,
counters, projection, and viewport extents. Every mutating text, UI, profiler,
and replay-overlay submission borrows that owner explicitly. `Window` no longer
mutates text projection; the late renderer pass refreshes the owner projection
from its viewport dimensions, with an unchanged-dim fast path. Font teardown
also clears the owning batch counters. No allocation-policy exception or
baseline/golden refresh was introduced.

The touched-source comment-style audit covered all 17 edited source-bearing
files and found the ownership/lifetime boundary documented at `TextBatch`,
`RuntimeRenderer`, `UIRenderContext`, and `UIDrawContext`. Formal evidence:

- `tools\validate_dx12_renderer.bat` — PASS in 83.145 seconds; Profile and
  Debug builds succeeded with zero warnings, DX12 InfoQueue reported 0 errors,
  and all three captures matched committed baselines. Manifest:
  `TestOutput/validation/dx12_renderer/20260717T113837Z/manifest.json`.
- `tools\run_graphics_stress.bat 1` — PASS in 61.558 seconds; PID 32092 ran
  for the bounded minute, reached frame 11,921 with 327 scene loads, accepted
  PID-scoped timeout shutdown, wrote its memory dump, produced empty stderr,
  and contained no fatal/crash/device-removed/assert signature.
