# Solar System Trajectory Planner Closure

Date: 2026-07-24
Branch: `nightrunner-23rd-JUL-26`
Plan: `solar-system-trajectory-planner`, SS0-SS6 complete
Status: COMPLETE — 7/7 phases

## Outcome

The authored four-body solar scene now has an opt-in Replay trajectory-planning
workflow:

- allocation-free orbital elements, propagation, Lambert, and Hohmann helpers
  live in `Maths`;
- Replay publishes an incremental closest-approach/intercept readout and cold
  analytic Earth/Mars guide arcs;
- the bounded shooting planner seeds from Lambert, judges candidates only with
  isolated engine prediction, retains fixed-capacity ghost witnesses, and
  applies a burn only through the normal typed velocity transaction;
- the 64×48 porkchop panel evaluates 96 cells per frame, records total
  departure-plus-arrival delta-v, and lets a valid cell seed planner
  time-of-flight; and
- all controls remain default-hidden. Unrelated scenes avoid orbital work and
  default intercept body/collider reads.

No Physics, Rendering, Scene, World, or Core implementation was changed. No
golden, screenshot baseline, physics CSV, config, schema, shader, or authored
baseline was refreshed.

## Independent Review And Remediation

The required single whole-feature review ran as
`/root/ss6_independent_review` over `9c9ddfad..868800a9`. It accepted the
ownership, `PhysicsSceneObjectId`, fixed-storage, draw/hit geometry, dependency,
and Replay-growth boundaries, then blocked closure on four concrete defects:

1. cancellation edges cleared the planner without restoring the already
   applied candidate velocity;
2. terminal planner states still accepted PLAN and time-of-flight mutations;
3. the default-off intercept update borrowed Physics stores before its enable
   predicate; and
4. porkchop wait advice remained relative to the sweep snapshot after the
   amortized build elapsed.

The closure remediation resolves all four:

- `CancelActivePlan()` emits the saved pre-plan velocity. Live advance, target
  loss, prediction cancellation, explicit cancel, and workspace exit apply it
  through `ApplyTripPlannerMutation()` before state reset. A rejected candidate
  receives a best-effort rollback through the same transaction; an unavailable
  or rejected owner is surfaced as a Lane R diagnostic.
- PLAN and time-of-flight commands are Idle-only in both the state owner and
  the shared draw/hit control surface. Converged/Failed states require Commit
  or Cancel, so re-planning cannot overwrite the original rollback value.
- `UpdateInterceptReadout()` now evaluates and clears its disabled packet before
  borrowing body/collider stores or resolving IDs.
- Porkchop publication carries its simulation epoch and sweep age. Hover and
  selected wait values subtract elapsed simulation time, the x-axis is labelled
  snapshot-relative, and the selected recommendation continues to count down
  after completion.

The review's non-blocking findings were also resolved: invalid target IDs are
retained to prevent repeated visible refreshes, all-failed grids and failed
hover cells say `NO SOLUTION`, total and worst-frame compute cost are separate,
and both stale source comments were corrected.

## Focused Behavioral Evidence

| Evidence | Result |
|---|---|
| Profile test build | PASS in 15.3 s; zero warnings/errors |
| Trip-planner focused tests | PASS; 5/5 cases, 108/108 assertions |
| Porkchop focused tests | PASS; 4/4 cases, 70/70 assertions |
| Profile engine build | PASS in 25.4 s; zero warnings/errors |
| Automation engine build | PASS in 26.9 s; zero warnings/errors |
| Final `solar_system_porkchop_probe.json` run | PASS in 3.2 s; report `ok=true` |
| Visual inspection | PASS; `TestOutput/interaction/solar_system_porkchop.bmp` has legible current-wait, total/max timing, and unchanged draw/hit alignment |

The final probe reports:

- minimum total delta-v `4.179023 u/s`;
- minimum snapshot departure offset `0.000000 s`;
- minimum time-of-flight `15.787234 s`;
- total sweep compute `15.467600 ms`;
- worst sweep frame `0.609200 ms`;
- end-to-end simulation age `0.345871 s`; and
- the clicked cell selected and seeded `15.787234 s` time-of-flight.

## Default-Off And Boundary Proofs

All five exact dependency proofs returned no rows:

```powershell
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Maths
rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Core
rg -n '^#include[[:space:]]+.*(Gameplay|Runtime|UI)/' SkullbonezSource/Physics SkullbonezSource/Rendering
rg -n '^#include[[:space:]]+.*(Assets|Scene|World|Runtime|UI)/' SkullbonezSource/Gameplay
```

The full feature diff contains no `RuntimeReserveAllocator` registration or
Replay growth-privilege change. `python tools\check_allocation_policy.py
--repo .` passed in 9.415 s:

```text
scanned=440 direct_heap_findings=30 dynamic_stl_member_findings=129
stl_growth_findings=645 allowlist_errors=0
```

The unrelated `space_three_body` DX12 capture passed its committed comparison
inside `agent_validate`. The 200-box scene passed the sole SS6 Replay fidelity
invocation with default-hidden solar UI and unchanged presentation evidence.

## Final Validation

| Command | Time | Result |
|---|---:|---|
| `tools\validate_format.bat` | 14.0 s | PASS; all source formatted |
| `tools\validate_perf.bat` first sample | 130.1 s | Inconclusive; one DX12 `Frame.avg` sample was +13.4% against 12%; allocation and absolute physics budgets passed |
| `tools\validate_perf.bat` unchanged rerun | 82.0 s | PASS; allocation, DX12, selected-path, and physics-bench gates clean |
| `tools\validate_replay_visual_fidelity.bat` — sole SS6 invocation | 437.0 s | PASS; one process/generation/presentation, 17/17 cases, 75/75 assertions, 2,401 ticks, 200 causal nodes, every false-pass control |
| `tools\agent_validate.bat` | 250.1 s | PASS; final broad gate |

Final broad-gate highlights:

```text
Project filter summary: 0 errors, 759 project items, 759 filter items
[doctest] test cases: 369 | 369 passed | 0 failed
[doctest] assertions: 69490 | 69490 passed | 0 failed
DX12 validation errors: 0
PASS: physics_regression_varied.csv (44401 lines, byte-exact match)
VALIDATE_FULL: DEFAULT GATE PASSED
```

The final DX12 comparison summary is
`TestOutput/validation/dx12_renderer/20260723T173724Z/summary.json`.

## Touched-Source Comment Audit

Checklist source: this report. The final inventory combines tracked
source-bearing paths from `9c9ddfad` through the accepted SS0-SS5 commits with
the SS6 remediation worktree, then reconciles every path through
`git ls-files`.

Result: **47/47 checked, 0 deferred, 0 unchecked**. Each file has the required
File/Purpose/Summary/Glossary sections, applicable invariants, and nearby
ownership, lifetime, draw/hit, default-off, failure, or bounded-work comments.

- [x] `SkullbonezSource/Maths/OrbitalMechanics.cpp`
- [x] `SkullbonezSource/Maths/OrbitalMechanics.h`
- [x] `SkullbonezSource/Runtime/App/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp`
- [x] `SkullbonezSource/Runtime/App/Run.cpp`
- [x] `SkullbonezSource/Runtime/App/RunLaunchOptions.h`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorTracer.cpp`
- [x] `SkullbonezSource/Runtime/Input/InputController.Bindings.cpp`
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp`
- [x] `SkullbonezSource/Runtime/Input/InputController.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayCoordination.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayGuideArcs.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayGuideArcs.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayInterceptReadout.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayInterceptReadout.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayPackets.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPorkchopPanel.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPorkchopPanel.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPrediction.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionView.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayTripPlanner.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayTripPlanner.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- [x] `SkullbonezTests/TestOrbitalMechanics.cpp`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`
- [x] `SkullbonezTests/TestReplayGuideArcs.cpp`
- [x] `SkullbonezTests/TestReplayInterceptReadout.cpp`
- [x] `SkullbonezTests/TestReplayPorkchopPanel.cpp`
- [x] `SkullbonezTests/TestReplayTripPlanner.cpp`
- [x] `SkullbonezTests/TestRuntimeInputBindings.cpp`
- [x] `SkullbonezTests/TestStartup.cpp`
- [x] `tools/validate_project_filters.py`

## Retirement

SS0-SS6 are complete with evidence. The live plan is deleted under repository
inventory rule 4; git history is the plan archive. The active/future
denominator returns from 7 to 0.
