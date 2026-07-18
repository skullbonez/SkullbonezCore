# ImGui + Tracy E10 Left Editor Workflow Evidence

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Plan task: E10 — deliver the left editor workflow from top-left downward

## Outcome

E10 is complete. The development ImGui editor's left rail now provides the
daily Scene & Modes, Hierarchy, and Assets/Create workflows without requiring
the legacy generic debug tabs. The UI consumes a bounded read-only editor view
and emits fixed typed scene/tool commands; scene, editor-history, and render
owners remain authoritative for mutations.

Scene & Modes exposes edit/place, pause/step, active-scene selection,
save/reset/defaults/create, and accurate clean/dirty plus undo/redo feedback.
Hierarchy publishes at most 512 stable `PhysicsSceneObjectId` rows with filter,
single-selection synchronization, independent session visibility and lock
state, and typed duplicate/delete/context actions. Assets/Create exposes all 37
existing primitive/object types plus the registered `assetlib.buildings`
category, bounded search/category filters, static and terrain-alignment options,
click placement, and a typed drag/drop payload with explicit unavailable state.

## Owner And Behavior Contracts

- Successful scene save is the only path that marks the current history cursor
  clean. New edits, undo/redo, branch replacement, overflow, and invalidation
  now produce accurate dirty state.
- Visibility and lock are session editor metadata, not authored schema fields.
  Visibility is mirrored into the fixed render-instance store and suppresses
  both main raster and shadow/bounds submission. Lock blocks duplicate/delete
  in both the ImGui surface and the owner command path.
- Duplicate uses the established fixed standalone-primitive recipe, assigns a
  new stable scene ID, offsets X/Z by two units, copies the display name, and
  records one history command. Reusable registered assets remain asset-library
  entries; E10 adds no hardcoded reusable asset recipe.
- Deferred scene/save/reset commands drain at an owner checkpoint independent
  of ImGui keyboard capture. The first live reset probe exposed that the drain
  was incorrectly nested under world-keyboard eligibility; moving the
  checkpoint outside that branch made reset/load work while an ImGui control
  owns keyboard focus.

No authored schema, baseline, replay golden, query golden, or physics CSV
changed.

## Live Interaction Matrix

Command:

`Debug\SKULLBONEZ_CORE.exe --scene stacking --interactive on --dev-ui imgui --vsync off --replay off`

The native development editor was exercised through the real Win32/DX12 app:

| Workflow | Observed result |
|---|---|
| Enter edit mode and select by hierarchy row | Stable selection published to the toolbar/status and hierarchy. |
| Duplicate selected object | Object count 3 → 4, dirty state set, undo depth 1. |
| Lock selected object | Row reported `locked`; duplicate and delete were disabled. |
| Unlock, delete, undo, redo | Counts moved 4 → 3 → 4 → 3 with correct history state. |
| Toggle visibility | Row reported `hidden`; render submission exclusion is owner-side. |
| Search assets for `house` | Registered Assets showed Brick house low/high entries. |
| Duplicate then reset | Count 4 → 3 and history returned clean/empty. |
| Load scene from combo | `asd.scene.json` loaded with 51 hierarchy rows and clean state. |

The final process exited cleanly and stderr was empty. An earlier launch used a
nonexistent `varied.scene.json`; correcting the scene name to the committed
`stacking` scene resolved it and is not a product blocker.

## Focused And Required Gates

| Gate | Result |
|---|---|
| Debug core and test builds | PASS with zero warnings and zero errors. |
| Focused doctest filters | PASS: Operator editor 5 cases/89 assertions; history 4 cases/102 assertions before two final clean-cursor assertions; scene entity store 3 cases/46 assertions; render instance store 4 cases/42 assertions. The final full suite reran the updated tests. |
| `tools\validate_ui.bat` | PASS. The first run stopped in 7.2s on five implementation-format findings and the second stopped on one header-alignment finding; narrow formatting fixes were applied. The final run completed all UI stages, with its final Debug build taking 20.28s and producing 0 warnings/0 errors. |
| `tools\validate_dx12_renderer.bat` | PASS in about 48s; 43 shader stages fresh, zero DX12 InfoQueue errors, and all three screenshots matched. Manifest: `TestOutput/validation/dx12_renderer/20260718T193610Z/manifest.json`. |
| `tools\run_graphics_stress.bat 1` | PASS in 60s; PID 45840 ran for the requested minute, remained crash-free, and was closed by the PID-scoped timeout. |
| `tools\validate_perf.bat` | COMPLETE in about 106s with no DX12 or physics-bench regression. |
| `tools\validate_full.bat` | PASS in about 135s; CPU umbrella and coverage, Automation/replay smoke, zero-error DX12 comparison, standalone physics smoke, and byte-exact 44,401-line physics regression passed. Final DX12 manifest: `TestOutput/validation/dx12_renderer/20260718T194138Z/manifest.json`. |
| `python tools\check_allocation_policy.py --repo .` | PASS in 9.0s; 404 files scanned and zero allowlist errors. |
| `tools\validate_build.bat Release` | PASS in 46.16s with zero warnings and zero errors. The Release core compile/link inventory contains no ImGui, ImGui owner, or Tracy client object. |
| Release executable/object audit | PASS; eight E10/development tokens are absent from `Release/SKULLBONEZ_CORE.exe`, and the Release core object directory contains no `imgui*`, `ImGuiEditorOwner*`, or `TracyClient*` object. |

## Comment Audit

The scoped inventory was taken from the 23 touched source-bearing files and
each was inspected against `Agentic/Skills/comment-style-audit/skill.md` and
the repository comment guide:

- [x] `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorCommandHistory.h`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorHistory.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.h`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.h`
- [x] `SkullbonezSource/UI/UICommands.h`
- [x] `SkullbonezTests/TestEditorCommandHistory.cpp`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`
- [x] `SkullbonezTests/TestSceneEntityStore.cpp`

Checked: 23. Deferred: 0. Unchecked: 0. Required file learning headers and
nearby visibility, lifetime, checkpoint, and clean-cursor invariants are
present.

## Unrelated Live Blocker

Physics body-count P1 remains blocked only on exact owner approval for replay
`causal.topologyCount: 199 -> 200` and the mechanically derived
`physics_query_varied.json`. E10 did not touch either artifact.
