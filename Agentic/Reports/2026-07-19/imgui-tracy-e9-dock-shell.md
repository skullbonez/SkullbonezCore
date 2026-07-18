# ImGui + Tracy E9 Deterministic Dock Shell Evidence

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Plan task: E9 — build the deterministic dockspace, menus, toolbar, and reset
path

## Outcome

E9 is complete. The development ImGui editor now owns a stable-ID dock shell
whose first-run and version-mismatch topology is editor-left,
viewport-center, utility-right, replay-bottom, and status-bottommost. Layout
version 2 persists in `imgui_editor_layout_v2.ini`; missing, stale, or corrupt
settings rebuild from the same policy instead of entering a fatal path.

The root shell provides File/Edit/View/Debug menus and a compact toolbar for
mode, placement, undo/redo, play/pause/step, active-scene identity, and Tracy
viewer state. Actions enter the fixed typed editor command exchange from E8.
`Reset Editor Layout` is an operator action, not a business-state mutation.

## Deterministic Layout Contract

- The pure `ImGuiEditorLayoutPolicy` owns layout version 2, stable panel names,
  responsive size caps, and semantic fingerprint
  `9482475421528666861`.
- The left rail is capped to 260–420 px, the right utility rail to 280–460 px,
  the replay strip to 112–280 px, and the status strip to 48–68 px. The central
  viewport receives the residual width and height.
- DockBuilder runs only for a missing/version-mismatched layout or an explicit
  operator reset. Normal frames preserve the user's persisted arrangement.
- Both rebuild reasons produced fingerprint `9482475421528666861`. The observed
  reset logs were:
  - `reason=missing_or_version_mismatch ... viewport=964x650 left=392 right=428 replay=183 status=68`
  - `reason=operator_reset ... viewport=964x650 left=392 right=428 replay=183 status=68`
- The actual View → Reset Editor Layout menu path was exercised in a running
  Profile process. The process then exited cleanly with empty stderr.

## Responsive Visual Evidence

| Window | Observed dock envelope | Artifact |
|---|---|---|
| 1280 x 720 minimum | viewport 691 x 476; left 282; right 307; replay 134; status 50 | `TestOutput/validation/e9_layout_minimum_1280x720.bmp` |
| 1920 x 1080, 16:9 | viewport 1040 x 743; left 420; right 460; replay 209; status 68 | `TestOutput/validation/e9_layout_16x9_1920x1080.bmp` |
| 2560 x 1080 ultrawide | viewport 1680 x 743; left 420; right 460; replay 209; status 68 | `TestOutput/validation/e9_layout_ultrawide_2560x1080.bmp` |

All three captures were inspected. The central viewport remains usable, the
bottom replay strip remains visible, and the bottommost status region does not
overlap the editor or utility rails. Each Profile run reported a clean ImGui
shutdown, DX12 descriptor high-water of one, and empty stderr.

The pinned external Tracy viewer was not built on this machine. Its toolbar
affordance therefore reports the unavailable state and disables launch safely;
this is not a shell blocker and does not alter Tracy client ownership.

## Focused And Required Gates

| Gate | Result |
|---|---|
| Debug solution build | PASS in 15.571s with zero warnings and errors. |
| Two focused doctest cases | PASS in 3.813s with 43/43 assertions. |
| `tools\validate_ui.bat` | PASS in 62.054s; UI comparison, ready builds, and zero DX12 validation errors passed. |
| `tools\validate_ui_stress.bat` | PASS in 52.455s with zero warnings and zero DX12 validation errors. |
| `tools\validate_tests.bat` | The first 1.178s run exposed the missing test-project filter row. After adding the canonical Runtime/Editor entry, PASS in 7.811s with 304/304 cases and 21,615/21,615 assertions. |
| `tools\validate_fast.bat` | The first 15.538s run exposed a non-canonical production filter. After reconciling both project entries to Runtime/Editor, PASS in 56.054s. |
| Test project-filter checker | PASS, 97/97 entries. |
| Production project-filter checker | PASS, 761/761 entries. |
| `tools\validate_full.bat` | PASS in 147.406s; CPU umbrella and coverage floors, Automation/replay smoke, zero-error DX12 baseline comparison, physics standalone smoke, and byte-exact 44,401-line physics regression passed. DX12 manifest: `TestOutput/validation/dx12_renderer/20260718T184912Z/manifest.json`. |
| `python tools\check_allocation_policy.py --repo .` | PASS in 10.165s; 404 files scanned and zero allowlist errors. |
| Release x64 solution build | PASS in 27.373s. Core compile/link logs contain no ImGui or Tracy development objects. |
| Release executable token scan | PASS; `SkoreEditorDockspaceV2`, `Reset Editor Layout`, `imgui_editor_layout_v2.ini`, and `OPEN TRACY` are absent from `Release/SKULLBONEZ_CORE.exe`. |

No baseline, screenshot golden, replay golden, physics CSV, query golden, or
authored data changed.

## Comment Audit

The 13 touched source-bearing files were inspected against
`Agentic/Skills/comment-style-audit/skill.md` and the repository comment guide:

- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.h`
- [x] `SkullbonezSource/UI/UICommands.h`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`
- [x] `tools/validate_project_filters.py`

Checked: 13. Deferred: 0. Unchecked: 0. The touched project/filter files are
trivial build inventory entries and do not need source learning headers.

## Unrelated Live Blocker

Physics body-count P1 remains blocked only on exact owner approval for replay
`causal.topologyCount: 199 -> 200` and the mechanically derived
`physics_query_varied.json`. E9 did not touch either artifact.
