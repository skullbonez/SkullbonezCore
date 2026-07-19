# ImGui + Tracy E15 Replay Workflow Checkpoint

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Campaign task: E15 — Anchor the complete replay workflow across the bottom

Result: implementation and independent validation complete; task acceptance is
retained as a checkpoint until the authorized Physics P1 replay-topology
transition `199 -> 200` is reconciled after E17

## Outcome

The ImGui editor now exposes the existing replay owners through a complete
bottom transport: record/stop, jump start/end, step backward/forward,
play/pause, return live, tick/count display, scrub position and live-present
marker, prediction state, reveal speed, prediction horizon, restore, save,
load, and selected-cause navigation. Compact labels keep the essential
transport and scrubber visible at the minimum tested 1024x720 client size;
secondary controls move into `More`.

The panel only emits fixed typed commands. `InputFrame` translates those
commands into the existing replay timeline, scrubber, prediction, and artifact
owners. Recording start/stop remains owned by `ReplayTimeline`; hash-log launch
policy locks recording as an explicit disabled state. The panel owns no replay
artifact, restore transaction, prediction archive, or timeline state.

`Jump to End` selects the end of the retained/predicted timeline. `Return Live`
is a distinct command that restores the live world. Cause links select the
existing replay-owned row by typed index. Invalid scrub positions, speeds,
horizons, and cause rows fail through the recoverable command-result path.

No authored scene, replay artifact, physics baseline, replay golden, or visual
golden changed.

## Exclusive surface contract

Legacy remains the selected development-build default. `--dev-ui legacy`
selects Legacy, `--dev-ui imgui` explicitly selects ImGui, and
`--dev-ui both` is a recoverable startup error. An omitted selector preserves
the established scene-authored Legacy visibility so ordinary captures and
tests remain stable while ImGui stays dormant.

`Ctrl+0` performs an atomic hot swap: the source is hidden before the target is
made visible. Plain `0` retains the Legacy minimize behavior. Escape and Legacy
visibility commands cannot activate the dormant Legacy surface while ImGui is
selected. Native probes confirmed both switch directions and never observed
both focus/input owners active in the same instant.

## Native and focused evidence

The final native captures are:

- `TestOutput/validation/imgui_e15_replay_initial.png` — explicit ImGui launch
  with the replay transport docked below the viewport.
- `TestOutput/validation/imgui_e15_replay_controls.png` — active transport and
  timeline state.
- `TestOutput/validation/imgui_e15_replay_more.png` — secondary replay controls
  and selected-cause detail.
- `TestOutput/validation/imgui_e15_minimum_width.png` — 1024x720 compact labels
  with all essential transport actions and the scrubber still visible.
- `TestOutput/validation/imgui_e15_ctrl0_legacy.png` and
  `TestOutput/validation/imgui_e15_ctrl0_imgui.png` — the final atomic hot-swap
  proof in each exclusive state.

All native probes used the explicit ImGui selector:

```text
Debug\SKULLBONEZ_CORE.exe --scene stacking --interactive on --dev-ui imgui --vsync off --replay on --replay-seconds 2
```

The three held native processes closed cleanly through `WM_CLOSE` by PID. No
engine process was left running.

Five focused cases passed with 91 assertions:

- startup development-UI selection: 2 cases / 22 assertions;
- selected-surface input routing: 1 case / 29 assertions;
- operator replay transport reduction: 1 case / 17 assertions;
- replay recording stop/resume ownership: 1 case / 23 assertions.

## Comment audit checklist

The final `git diff --name-only` source-bearing set was reconciled against
`git ls-files`: 27 tracked files, 27 inspected, 0 deferred, 0 unchecked. Each
meaningfully changed source file has the required learning-header coverage and
nearby ownership, lifetime, fixed-capacity, exclusive-surface, or replay
command invariants where the behavior is non-obvious.

- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/InputRouter.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayCoordination.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPrediction.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionView.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayTimeline.h`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunInput.cpp`
- [x] `SkullbonezSource/Runtime/RunLaunchOptions.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.h`
- [x] `SkullbonezTests/TestCoverageFloorContracts.cpp`
- [x] `SkullbonezTests/TestInputRouter.cpp`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`
- [x] `SkullbonezTests/TestStartup.cpp`

## Final-source gates

The Codex PTY was the available console for scripted gates, so output was
mirrored to the named logs. Native application probes were visible.

| Command | Result | Wall time | Evidence |
|---|---|---:|---|
| `tools\validate_ui.bat` | PASS from the final visibility/default contract | 57.02 s | `TestOutput/validation/imgui_e15_validate_ui_visibility_fix.log` |
| `tools\validate_interaction_clicks.bat` | Replay prediction scenario passed; the later pre-existing Legacy `editor_undo_redo` fixed-coordinate scenario failed on `editorSelectionHasTerrain` | 29.83 s | `TestOutput/validation/imgui_e15_validate_interaction_clicks.log` |
| `tools\validate_replay_visual_fidelity.bat` | Launcher and 16 control cases/72 assertions passed; the single permitted invocation then held only on `causal.topologyCount expected=199 actual=200`; no refresh or retry | 412.44 s | `TestOutput/validation/imgui_e15_validate_replay_visual_fidelity.log` |
| `tools\validate_full.bat` first run | Exposed that implicit Legacy selection was force-showing the scene-hidden panel in the water-ball capture; corrected without changing the default or any baseline | 121.66 s | `TestOutput/validation/imgui_e15_validate_full.log` |
| `tools\validate_full.bat` final run | PASS; 311 cases/21,850 assertions, all coverage floors, Automation/replay smoke, zero-error DX12 captures, and the byte-exact 44,401-line physics oracle | 141.93 s | `TestOutput/validation/imgui_e15_validate_full_visibility_fix.log` |
| `python tools\check_allocation_policy.py --repo .` | PASS; 405 files and zero allowlist errors | 9.16 s | `TestOutput/validation/imgui_e15_allocation_policy_final.log` |
| `tools\validate_build.bat Release` | PASS; zero warnings/errors | 39.71 s | `TestOutput/validation/imgui_e15_release_build_final.log` |
| Release exact-token scan | PASS; 7 development-tools tokens, zero hits | 0.10 s | `TestOutput/validation/imgui_e15_release_token_scan_final.log` |
| `git diff --check` | PASS | <1 s | final source inspection |

The final full gate produced the zero-error DX12 manifest at
`TestOutput/validation/dx12_renderer/20260718T234017Z/manifest.json`. The
water-ball, solver-smoke, and space-three-body captures all matched their
committed baselines. E15 changed no render-backend or shader source, so the
conditional direct graphics-stress gate did not apply. No SkullScope trace was
used.

## Held acceptance and continuation

E15 remains unchecked because its mandatory single-invocation replay gate
reached the Physics P1 topology transition already authorized by the binding
2026-07-19 Master Plan directive for reconciliation after E17. The approved
golden remains unchanged at this checkpoint. E16 proceeds now; after E17 the
runner loops back through the bounded P1 artifact assessment, closes P1, and
re-runs the held UI replay acceptance gates from the reconciled source.
