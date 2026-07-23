# UI / Runtime Separation

Date: 2026-07-23
Status: IN PROGRESS — drafted from the 2026-07-23 from-source architecture review of
`nightrunner-22nd-JUL-26`. Registered in `MASTER-PLAN.md` on 2026-07-23 as
plan 2 of the Architecture Follow-Up Campaign Round 3; starts after
`source-blemish-remediation` B3 renames land. 4/5 phases complete.
Impact area: `SkullbonezSource/UI/` includes, `Runtime/Scene` navigation
value ownership, input snapshot boundary, physics-debug-visualizer UI tab
Owner: runtime + UI
Priority: High — the UI↔Runtime cycle is the one dependency tangle the
repository's otherwise-strict layering regime does not police, and it grows
with every new tab

## Problem And Evidence (measured 2026-07-23)

UI and Runtime include each other. Ten UI files include Runtime headers while
ten Runtime files include UI headers, so the two top packages form one
mutually-dependent tangle: "UI" is not currently an extractable module, and
no `AGENTS.md` proof polices this edge the way Core/Physics/Rendering/
Gameplay edges are policed.

Complete UI→Runtime include census (2026-07-23, 10 rows):

| UI file | Runtime header pulled in |
|---------|--------------------------|
| `UI/UI.h:41` | `Runtime/Scene/SceneControllerState.h` |
| `UI/UIWindowInteractionOwner.h:50` | `Runtime/Scene/SceneControllerState.h` |
| `UI/UIFrameComposition.h:34` | `Runtime/InputRouter.h` |
| `UI/UIFrameComposition.h:41` | `Runtime/Debug/PhysicsDebugVisualizer.h` |
| `UI/UI.cpp:34` | `Runtime/InputRouter.h` |
| `UI/UI.cpp:41` | `Runtime/Debug/PhysicsDebugVisualizer.h` |
| `UI/UIInput.cpp:29` | `Runtime/Input.h` |
| `UI/UIInput.cpp:30` | `Runtime/InputRouter.h` |
| `UI/UITabPhysics.cpp:28` | `Runtime/Debug/PhysicsDebugVisualizer.h` |
| `UI/UIWindowInteractionOwner.cpp:29` | `Runtime/InputRouter.h` |

Three distinct edges hide behind those rows:

1. **A UI-owned model stored in Runtime.**
   `Runtime/Scene/SceneControllerState.h` says it itself: "Defines the
   UI-owned scene navigation model" — `RunSceneBrowserState`,
   `RunSceneUIOverrideState`, `SceneLoadNavigationState`,
   `SceneNavigationModel` (the latter even forward-declared as
   `namespace UI` by `SceneController.h:90`). The owning-layer rule already
   prescribes the fix: move the value to its owner instead of including
   upward.

2. **UI consuming Runtime input authority types for what are value reads.**
   `UIInput.cpp:39` builds `UIInputSnapshot` *from*
   `Runtime::DeviceInputFrame` and `Runtime::RuntimeMouseEdges`;
   `UIWindowInteractionOwner.cpp:534` unpacks the same two types from its
   frame input. UI only ever reads these as values, yet it includes
   `InputRouter.h` — the binding/edge-memory *authority* header — to get
   them.

3. **The gameplay UI mutating a Runtime debug subsystem directly.**
   `UITabPhysics.cpp`, `UI.cpp`, and `UIFrameComposition.h` reach
   `Runtime/Debug/PhysicsDebugVisualizer` as a concrete type, while the rest
   of the UI already speaks typed command values (`UICommands.h`,
   `OperatorEditorExchange`) that Runtime owners apply. The physics tab is
   the one surface that bypasses the UI's own command architecture.

The reverse direction (Runtime including UI from `RunRender.cpp`,
`RuntimeRenderer.cpp`, `UiTextPass.cpp`, `Runtime/UI/*`,
`InteractionAutomation*`, `ImGuiEditorOwner*`, `RuntimeOverlayDiagnostics`)
is fine under the target ruling: Runtime composes and draws the UI.

## Direction Ruling (the point of this plan)

**UI is a presentation library below Runtime.** Runtime may include UI; UI
must not include Runtime. UI consumes value snapshots and emits typed command
values; Runtime owners build the snapshots, apply the commands, and own every
subsystem the commands target. This mirrors the existing rule style
("Gameplay may include stable Physics/Rendering value or registration
seams"), and it is the direction the code is already 90% shaped for — the
ten include rows above are the entire residue.

UI→Rendering/DX12 includes (`UI.h` pulling `ShaderDX12.h`,
`Dx12Diagnostics.h`; `UIRenderContext` carrying concrete DX12 owner
pointers) are **out of scope** here — that is a UI→lower-layer edge, legal
under the ruling. Narrowing it to a smaller seam is recorded as a follow-up
candidate, not part of this plan.

## Goal

`rg -n '^#include[[:space:]]+.*Runtime/' SkullbonezSource/UI` returns no
rows, the ruling and that proof are added to the `AGENTS.md` dependency
rules, and no behavior, layout, binding, or rendering output changes.

## Non-Goals

- No Replay changes (owner instruction).
- No parameter-list reduction work (owner instruction) — where a value
  snapshot replaces an authority include, the snapshot is a single typed
  struct, not a new wide signature.
- No UI→Rendering narrowing (recorded follow-up, see ruling above).
- No widget, tab, layout, input-feel, or visual behavior change.
- No new context bags: the input snapshot and physics-debug command/status
  values must be small, single-purpose types, not a `UIRuntimeContext`.

## Phases

- [x] **U1 — Move the scene navigation model to its owner.** Relocate the
  UI-owned value types in `Runtime/Scene/SceneControllerState.h`
  (`RunSceneBrowserState`, `RunSceneUIOverrideState`, `SceneNavigationModel`,
  and `SceneLoadNavigationState` if the census confirms it is UI-owned
  rather than a load-transaction value — decide and record here) into a
  value-only header under `SkullbonezSource/UI/` (e.g.
  `UI/UISceneNavigationModel.h`). Runtime consumers (`SceneController.h`,
  `SceneRuntimeLoad`, `SceneRuntimeUiOptions`, `SceneController.Load.cpp`, …) include
  the UI header — legal under the ruling. If `SceneLoadNavigationState`
  turns out to be a genuine load-transaction value, it stays in
  `Runtime/Scene` and UI stops needing it once U1's split is done; record
  the outcome either way. Delete `SceneControllerState.h` or reduce it to
  the Runtime-owned remainder; no forwarding header. Acceptance: `UI.h` and
  `UIWindowInteractionOwner.h` include no Runtime header for navigation
  state; scene browser and override behavior unchanged.

  Completed 2026-07-23. `UISceneNavigationModel.h` now physically owns
  `RunSceneBrowserState`, `RunSceneUIOverrideState`, and the passive
  `SceneNavigationModel` under `SkullbonezCore::UI`. The new header has no
  Runtime include, forward declaration, pointer, callback, or policy method.
  Runtime queue decisions are free functions in the reduced
  `SceneControllerState.h`/`SceneNavigationModel.cpp` seam; every cross-layer
  reference is fully qualified to avoid the unrelated nested `Runtime::UI`
  helper namespace. `UI.h` and `UIWindowInteractionOwner.h` now include the
  UI-owned value header and no Runtime navigation header.

  Execution-time ownership decision: `SceneLoadNavigationState` remains in
  `Runtime/Scene`. It is a detached, cold load-transaction value that owns
  copied browser paths plus the selected cinematic row and override values,
  and its load operations synchronously borrow the concrete `SceneRuntime`
  queue owner. It is not retained presentation state. The former mixed header
  is therefore reduced to this Runtime-owned snapshot and queue policy rather
  than deleted. Existing type spellings remain unchanged as required.

  The focused Profile build passes in 14.42 s with zero warnings/errors after
  correcting the first build's `Runtime::UI` namespace lookup collision. Five
  navigation tests pass with 43 assertions. All 23 touched source-bearing files
  pass the comment audit with zero deferrals. The current UI→Runtime include
  census drops from ten to eight rows, exactly removing the two navigation
  includes; no authored data, baseline, golden, config, schema, or shader
  changes.

  Final `tools\validate_full.bat` passes in 156.50 s after the first preflight
  correctly requested the repository header-alignment pass: 747/747
  project/filter items, zero-warning builds, CPU/coverage and five runtime
  lanes, zero DX12 validation errors, accepted captures, and the byte-exact
  44,401-line physics CSV. The cumulative Replay mapping is satisfied by one
  `tools\validate_replay_visual_fidelity.bat` invocation in 434.52 s: one
  engine process/generation/presentation, the full 2,401-tick durable/causal
  oracle, and all negative controls pass.

- [x] **U2 — Input crosses as a UI-owned value snapshot.** UI already
  defines `UIInputSnapshot`; make it the boundary. Move snapshot
  construction to the Runtime side of the seam (the caller that already owns
  `DeviceInputFrame`/`RuntimeMouseEdges` — `InputRouter.Interactions.cpp` /
  `OperatorEditorFrameComposer` territory) so UI receives a fully-built
  `UIInputSnapshot` (extended with whatever fields
  `UIWindowInteractionOwner.cpp:534` currently unpacks from the raw device
  frame). Remove `Runtime/Input.h` and `Runtime/InputRouter.h` includes from
  all five UI files in the census. Keyboard/pointer edge semantics must be
  bit-identical — the same sampled values cross, only the packaging moves.
  Acceptance: no UI file includes Runtime input headers; interactive UI
  behavior (drag, capture, hover, shortcuts) unchanged.

  Completed 2026-07-23. `UIInputSnapshot` is now an owning UI value: four
  64-bit keyboard words plus pointer position, wheel delta, button level, and
  press/release edges. It retains no Runtime reference or pointer. Runtime's
  `BuildUIInputSnapshot` copies those facts from the already-sampled
  `DeviceInputFrame` and `RuntimeMouseEdges`; the small `UIPointerOverride`
  carries only UI-owned automation coordinates back to that construction
  site. UI key helpers and the Scene tab now consume the detached snapshot.
  All `Runtime/Input.h`, `Runtime/InputRouter.h`, `DeviceInputFrame`,
  `RuntimeMouseEdges`, and `InputKeySnapshot` dependencies are absent from
  `SkullbonezSource/UI/`.

  The first focused compile exposed three stale unqualified `keys` references,
  unused namespace imports that had depended on transitive Runtime visibility,
  and the standalone test target's missing pure-UI input implementation. Those
  issues were corrected by using the snapshot parameter, removing the unused
  imports, and adding `UIInput.cpp` to the test project and filter. No
  forwarding header or compatibility alias was introduced.

  The final focused Profile build passes in 4.85 s with zero warnings/errors.
  The input-policy selection passes 28 cases / 1,170 assertions in 1.73 s, and
  the exact detached-boundary case passes 1 case / 16 assertions in 0.07 s,
  including source-mutation independence and pointer override behavior. All 15
  touched source-bearing files pass the comment audit with zero deferrals.
  `tools\validate_ui.bat` passes in 53.91 s, exercising visible UI states and
  DX12 presentation while the CPU input tests cover capture, focus, pointer
  edges, and shortcut routing.

  Final `tools\validate_full.bat` passes in 124.96 s: all CPU/coverage floors,
  747/747 project/filter entries, zero-warning Profile and Debug builds, zero
  DX12 validation errors with accepted committed images, and the byte-exact
  44,401-line physics CSV. The remaining UI→Runtime source include census is
  exactly the three `PhysicsDebugVisualizer.h` rows assigned to U3. No authored
  data, baseline, golden, config, schema, or shader changed.

- [x] **U3 — Physics debug visualizer goes behind typed commands.** Replace
  UI's direct `PhysicsDebugVisualizer` access with the existing command
  pattern: `UITabPhysics` emits typed toggle/value commands (extend
  `UICommands.h`), a Runtime owner applies them to the visualizer, and the
  tab renders from a small value status struct built by that owner (current
  toggle states, counts). `UIFrameComposition.h` and `UI.cpp` drop the
  include. Acceptance: no UI file names `PhysicsDebugVisualizer`; every
  physics-tab toggle behaves identically, verified by driving each toggle in
  an interactive smoke pass.

  Completed 2026-07-23. UI now owns `UIPhysicsDebugOverlay`, a typed four-value
  toggle vocabulary, and `UIPhysicsDebugStatus`, a detached status record with
  decoded overlay states, pipeline name/index/count, alpha/linger values, and
  the related collision/transparent/broadphase presentation toggles.
  `DiagnosticsRuntime` explicitly maps each UI overlay value to its
  Physics-owned flag; unknown shared-editor values are rejected before
  projection. `UiTextPass` performs the inverse owner-side decode into the
  status record. Legacy and ImGui surfaces both submit the same UI enum rather
  than passing Physics masks through UI.

  The Physics tab's hit handler and the focused test share one pure 13-row
  toggle-to-command policy. The focused case drives all 13 toggle rows plus
  both invalid bounds and passes 1 case / 28 assertions in 1.77 s. Eight
  operator-editor normalization, validation, arbitration, and projection cases
  pass 207 assertions. The first attempted focused test build correctly failed
  because the lightweight test target does not link the full tab draw
  implementation; the final policy remained inline and value-only instead of
  expanding that target, whose rebuild passes in 6.45 s. The final Profile
  solution build passes in 15.89 s with zero warnings/errors.

  All 12 touched source-bearing files pass the comment audit with zero
  deferrals. `tools\validate_ui.bat` passes in 45.73 s, including the
  `physics_toggles` visible state, all accepted UI captures, and zero DX12
  validation errors. Final `tools\validate_full.bat` passes in 127.12 s: all
  CPU/coverage floors, 747/747 project/filter entries, zero-warning
  Profile/Debug builds, accepted committed DX12 images, and the byte-exact
  44,401-line physics CSV. The UI Runtime-include proof returns zero rows, UI
  never names `PhysicsDebugVisualizer`, and the retired raw UI debug-mask
  command has zero source/test rows. No authored data, baseline, golden,
  config, schema, or shader changed.

  Independent closure review reopened U3 because the original focused case
  proved the 13-row UI command policy but stopped before Runtime applied the
  command or published the resulting status. The remediation extracted the
  cohesive `DiagnosticsPhysicsUI` Runtime owner: it is now the single mapping
  point from `UIPhysicsDebugOverlay` to Physics flags and the single builder of
  `UIPhysicsDebugStatus`. `UiTextPass` consumes that builder instead of carrying
  a second inline decode.

  The new owner-side case drives axes, contacts, sleep, pipeline, collision,
  transparency, broadphase, terrain-contact, pipeline wrapping, alpha/linger
  clamping, and toggle-off behavior through the exact command-application and
  status-publication functions. Together with the original row-policy case it
  passes 2 cases / 58 assertions in 2.2 s. The focused Profile executable build
  passes in 11.7 s with zero warnings/errors; all nine remediation-touched
  source-bearing files pass the comment audit with zero deferrals.
  `tools\validate_ui.bat` passes in 42.3 s. The final clean-exit
  `tools\validate_full.bat` run passes in 101.8 s with 749/749 production
  project/filter items, all mandatory CPU/coverage lanes, zero DX12 validation
  errors, accepted committed captures, and the byte-exact 44,401-line physics
  CSV. Two earlier full-gate attempts stopped honestly at missing production
  and test filter-policy metadata; both metadata defects were corrected and
  their direct checks pass before the clean full rerun.

- [x] **U4 — Proof, rule, and guard.** Add the ruling and its proof to
  `AGENTS.md` beside the existing dependency proofs:
  `rg -n '^#include[[:space:]]+.*Runtime/' SkullbonezSource/UI` must return
  no rows. Re-run all standing dependency and replay proofs. Acceptance: all
  proofs no-rows; `AGENTS.md` documents the UI direction rule.

  Completed 2026-07-23. `AGENTS.md` now states that UI is a presentation
  library below Runtime: Runtime may include UI for composition, while UI must
  consume detached values, emit typed commands, and never include Runtime.
  The rule explicitly rejects forwarding headers, aliases, callback packs,
  service bags, and broad contexts as dependency laundering.

  All five exact standing proofs pass in 0.19 s with zero rows: Core has no
  upward engine includes; Physics/Rendering have no Gameplay/Runtime/UI
  includes; Gameplay has no Assets/Scene/World/Runtime/UI includes; UI has no
  Runtime includes; and Physics/Rendering/Scene/World/Core have no downward
  Replay includes. This slice changes documentation only, so repository
  validation is not required.

- [ ] **U5 — Independent review and gates.** Fresh review checks: (a) no
  forwarding header, alias, or callback pack was introduced to launder a
  Runtime include; (b) the snapshot/command types are single-purpose values,
  not context bags; (c) moved navigation types did not drag Runtime
  authority (e.g. `SceneRuntime` reach-back) into UI; (d) behavior evidence
  from U2/U3 smoke passes is recorded. Findings reopen the owning phase.
  Run the gates below and paste output.

## Dependencies And Decisions

- Coordinate with `runtime-package-decomposition`: U1 deletes/shrinks a file
  in `Runtime/Scene/` that plan's census will list. Whichever plan lands
  second re-runs the other's proofs; do not interleave in one commit.
- Independent of `source-blemish-remediation` except for shared
  project-file churn; land as separate commits.
- The `RunSceneBrowserState`/`RunSceneUIOverrideState` *type names* keep
  their current spelling in U1 (renames are the blemish plan's territory if
  the owner wants them); only ownership and physical location move here.
- Decision recorded up front: Runtime→UI includes remain legal. Anyone
  tempted to also forbid that direction is redesigning the presentation
  architecture, which this plan explicitly does not do.

## Acceptance

Zero UI→Runtime includes, proven by the `rg` proof and enforced in
`AGENTS.md`; scene browser, UI input feel, and physics-tab behavior
unchanged; independent review clear; gates green with output pasted.

## Validation

`tools\validate_full.bat` at closure (multiple areas: UI + Runtime). The
full gate's DX12 renderer process must show unchanged visual baselines —
UI draw output is part of captured screenshots, so a baseline diff means U1–
U3 changed presentation and is a defect, not a refresh cue. Physics CSV
byte-exact as always. Record the U3 interactive toggle smoke evidence
(commands driven, resulting visualizer states) in the closing commit body.
