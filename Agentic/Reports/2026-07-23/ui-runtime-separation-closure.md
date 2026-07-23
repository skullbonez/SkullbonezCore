# UI / Runtime Separation Closure

Date: 2026-07-23
Branch: `nightrunner-23rd-JUL-26`
Plan: `Agentic/Plans/TODO/ui-runtime-separation.md`
Status: COMPLETE — 5/5 phases

## Outcome

UI is now a presentation library below Runtime. Runtime may include UI to
compose and draw it; UI consumes detached values, emits typed commands, and has
zero Runtime includes.

- U1 moved passive scene-navigation presentation values to
  `UI/UISceneNavigationModel.h`. The genuine load transaction remains in
  Runtime/Scene.
- U2 made `UIInputSnapshot` the detached keyboard/pointer boundary. Runtime
  constructs it from sampled device state.
- U3 replaced direct Physics debug-visualizer access with UI-owned command and
  status values. The focused `DiagnosticsPhysicsUI` Runtime owner applies
  commands and publishes status.
- U4 ratified the one-way direction and exact no-row proof in `AGENTS.md`.
- U5 ran the plan's one independent review, remediated its evidence finding,
  repeated the standing proofs, and closed the mapped gates.

Accepted implementation commits before this closing commit:

| Slice | Commit |
|---|---|
| U1 | `b7dd3c2b` |
| U2 | `120b1db7` |
| U3 | `c51d078a` |
| U4 | `22cbfa34` |
| U3 review remediation | `9ed2a9ea` |

## Independent Review

The required rubber-duck review ran once at whole-plan closure as
`/root/ui_runtime_u5_review` and completed in 3m0s.

It found no forwarding header, alias, callback pack, context bag, hidden
Runtime authority, allocation/replay/lifetime defect, or behavior regression.
Navigation values remained passive, the input snapshot remained cohesive, and
the UI direction proofs were honest.

The initial verdict was BLOCKED on one evidence defect: U3's focused test proved
all 13 UI rows produced the intended command, but did not invoke the Runtime
mapping or observe resulting status. It also noted one non-blocking stale
Runtime namespace import and “device frames” summary in
`UIWindowInteractionOwner.cpp`.

The finding reopened U3. Commit `9ed2a9ea` removes the stale residue and
extracts `DiagnosticsPhysicsUI` as the single Runtime owner for both directions:

```text
UIPhysicsCommands
  -> ApplyDiagnosticsPhysics*UICommand(s)
  -> OverlayDebugState
  -> BuildDiagnosticsPhysicsUIStatus
  -> UIPhysicsDebugStatus
```

The new owner-side test drives axes, contacts, sleep, pipeline, collision,
transparency, broadphase, terrain contact, previous-stage wrapping,
alpha/contact-linger clamping, and toggle-off behavior through that exact path.
Together with the 13-row policy case it passes 2 cases and 58 assertions.
Objective source, test, UI-gate, and full-gate evidence resolves the review
finding; the plan's single-review rule intentionally did not run a second
reviewer.

## Review Criteria

| Criterion | Closure evidence |
|---|---|
| No dependency laundering | No forwarding header, compatibility alias, callback pack, service bag, or broad context was added; the exact UI→Runtime proof is empty. |
| Single-purpose values | Navigation, input, physics command, and physics status records each describe one presentation boundary and retain no owner reference. |
| No Runtime authority moved into UI | UI navigation values contain no `SceneRuntime` reach-back, owner pointer, callback, or queue policy. |
| Behavior evidence | U2 input-policy/boundary cases, U3 13-row policy, the owner-side 58-assertion pair, `validate_ui`, and unchanged DX12 captures all pass. |

## Static Closure Proofs

All five exact commands returned zero rows in 0.22 s:

```powershell
rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Core
rg -n '^#include[[:space:]]+.*(Gameplay|Runtime|UI)/' SkullbonezSource/Physics SkullbonezSource/Rendering
rg -n '^#include[[:space:]]+.*(Assets|Scene|World|Runtime|UI)/' SkullbonezSource/Gameplay
rg -n '^#include[[:space:]]+.*Runtime/' SkullbonezSource/UI
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
```

UI also has zero `PhysicsDebugVisualizer` name rows. No downward Replay include
or new/expanded Replay growth privilege appeared.

## Final Validation

| Command | Time | Result |
|---|---:|---|
| Profile `SKULLBONEZ_CORE` focused build | 11.7 s | PASS; zero warnings/errors |
| Two focused Physics-UI owner cases | 2.2 s | PASS; 2/2 cases, 58/58 assertions |
| `tools\validate_ui.bat` | 42.3 s | PASS; Profile/Debug and UI interaction/capture lanes |
| production project-filter direct check | 2.9 s | PASS; 749/749 items |
| test project-filter direct check | 1.3 s | PASS; 102/102 items |
| `tools\validate_full.bat` | 101.8 s | PASS with exit 0; all CPU/coverage and runtime lanes |

Key final output:

```text
Project filter summary: ... (0 errors, 749 project items, 749 filter items across 3 production projects)
[doctest] test cases: 2 | 2 passed | 0 failed | 347 skipped
[doctest] assertions: 58 | 58 passed | 0 failed
DX12 validation errors: 0
PASS: physics_regression_varied.csv (44401 lines, byte-exact match; output runs=2, baseline runs=1)
VALIDATE_FULL: DEFAULT GATE PASSED
```

The first two broad attempts stopped honestly at missing production and test
filter-policy metadata for the new owner files. Both defects were corrected and
proved directly. A later run reached the pass marker but its shell wrapper
expired at 121.4 s; the final unchanged-source rerun completed in 101.8 s with
process exit 0.

The final DX12 comparison summary is
`TestOutput/validation/dx12_renderer/20260723T125427Z/summary.json`.
No baseline, golden, authored data, config, schema, or shader artifact changed.

## Comment Audit

All nine source-bearing files touched by the review remediation pass the
repository comment-style audit with zero deferrals:

- `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp`
- `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.h`
- `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`
- `SkullbonezSource/Runtime/InputFrame.cpp`
- `SkullbonezSource/Runtime/UiTextPass.cpp`
- `SkullbonezSource/UI/UIWindowInteractionOwner.cpp`
- `SkullbonezTests/TestOwnerRequestQueues.cpp`
- `tools/validate_project_filters.py`

Earlier phase audits remain recorded in git and the deleted plan: U1 23/23, U2
15/15, and original U3 12/12, all with zero deferrals.
