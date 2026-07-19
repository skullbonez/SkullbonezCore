# ImGui + Tracy E14 Causality Checkpoint

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Campaign task: E14 — Reduce Causality to a useful contextual right-side tool

Result: implementation complete; task acceptance remains blocked by the
already owner-gated replay topology transition `199 -> 200`

## Outcome

The development editor now projects one compact contextual cause summary from
the existing immutable replay overlay publication. It shows the selected body,
retained replay tick, prediction state, immediate cause/effect, and at most
eight relevant links. Empty, stale, truncated, and capacity-limited states are
explicit.

`Open Detail` reveals a separate dockable window over the replay-owned rows.
The table uses `ImGuiListClipper`, keeps only presentation-local row selection,
and exposes the existing numeric contact, solver, pipeline, vector, and
hierarchy details. The legacy cause renderer and data path remain compiled and
reachable. `RunFrame` publishes the overlay view once per frame for both
surfaces; E14 does not build a second cause tree.

The compact projection scans a fixed 512-row neighborhood and retains at most
eight pointers into owner storage. The expanded table virtualizes visible rows.
No authored scene, replay artifact, physics baseline, replay golden, or visual
golden changed.

## Native and focused evidence

The production-owner prediction probe used
`nbody_prediction_mode_fidelity.json` with the ImGui surface. It enabled
prediction, set the three-second horizon and `chaos_a` target, completed the
full horizon, and published a ready trajectory fingerprint. Report:
`TestOutput/validation/imgui_e14_native_report.json` (`ok=true`).

Visible native captures:

- `TestOutput/validation/imgui_e14_compact_populated.png` — compact right-rail
  summary at replay tick 4 with prediction ready and selected `chaos_a`.
- `TestOutput/validation/imgui_e14_detail.png` — separate detail window over
  five published rows with full selected-row facts.

The held native process closed through `WM_CLOSE` by PID after 19,833 ImGui
frames and 400,561 draws. It reported one viewport resource recreation,
descriptor high-water 2/16, and clean ImGui/DX12 shutdown.

The focused causality projection test passed 1/1 case and 16/16 assertions,
covering empty, stale, ready, capacity-limited, prediction, immediate-cause,
tick, and link-truncation states.

The replay-prediction click scenario passed in the cumulative interaction
probe. The later unrelated `editor_undo_redo.json` scenario reproduced the
transition-sensitive legacy fixed-coordinate/history failure already recorded
at E0 (`editorSelectionHasTerrain expected=true actual=false`). E14 does not
change that harness or legacy selection/history ownership.

## Comment audit

The scoped source-bearing inventory contains seven touched files: the new
causality projection, layout policy, ImGui owner source/header, `RunFrame`, the
focused test file, and the project-filter policy script. All 7/7 were inspected
against the comment guide; 0 were deferred. The new projection has a complete
learning header and nearby bounded-scan/lifetime/capacity comments. The detail
selection and shared replay-publication lifetime invariants are documented at
their use sites. The filter-policy change is a self-explanatory single catalog
entry.

## Final-source gates

The Codex PTY was the available console for scripted gates, so output was
mirrored to the named logs. The native application probe itself was visible.

| Command | Result | Wall time | Evidence |
|---|---|---:|---|
| `tools\validate_ui.bat` | PASS; clean format, Profile/Debug builds, UI checks | 63.65 s | `TestOutput/validation/imgui_e14_validate_ui.log` |
| `tools\validate_interaction_clicks.bat` | Replay prediction scenarios passed; final pre-existing legacy editor-history scenario failed as noted above | recorded in log | `TestOutput/validation/imgui_e14_validate_interaction_clicks.log` |
| `tools\validate_replay_visual_fidelity.bat` | BLOCKED only on `causal.topologyCount expected=199 actual=200`; launcher shape and 16 control cases/72 assertions passed; exactly one invocation; no golden refresh | 417.95 s | `TestOutput/validation/imgui_e14_validate_replay_visual_fidelity.log` |
| `tools\validate_full.bat` first run | Found missing project-filter prefix for the new header; corrected before checkpoint | 16.03 s | `TestOutput/validation/imgui_e14_validate_full.log` |
| `python tools\validate_project_filters.py --repo .` | PASS; 763/763 project/filter items, zero errors | 2.8 s | console evidence |
| `tools\validate_full.bat` rerun | PASS; 310 cases/21,828 assertions, coverage floors, Automation/replay smoke, zero-error DX12 comparisons, byte-exact 44,401-line physics oracle | 144.92 s | `TestOutput/validation/imgui_e14_validate_full_rerun.log` |
| `python tools\check_allocation_policy.py --repo .` | PASS; 405 files, zero allowlist errors | 9.30 s | `TestOutput/validation/imgui_e14_allocation_policy.log` |
| `tools\validate_build.bat Release` | PASS; zero warnings/errors | 42.61 s | `TestOutput/validation/imgui_e14_release_build.log` |
| Release exact-token scan | PASS; ImGui/Tracy/E14 causality tokens absent | 0.3 s | command output retained in the task transcript |

`validate_full` produced the zero-error DX12 manifest at
`TestOutput/validation/dx12_renderer/20260718T222000Z/manifest.json`. E14 did
not change render-backend or shader source, so the conditional direct graphics
stress gate did not apply. No SkullScope trace was used.

## Blocker and continuation

E14 remains unchecked because its mandatory, single-invocation replay visual
gate reached the same P1 transition artifact that already awaits exact owner
authority. The approved golden remains unchanged. Under the owner's explicit
continue-on-blocker direction, E15 implementation proceeds while E14 stays
open; resolving the P1 topology approval will unblock both acceptance rows.
