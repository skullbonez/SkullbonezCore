# Runtime Package Decomposition

Date: 2026-07-23
Status: IN PROGRESS — drafted from the 2026-07-23 from-source architecture review of
`nightrunner-22nd-JUL-26`. Registered in `MASTER-PLAN.md` on 2026-07-23 as
plan 3 of the Architecture Follow-Up Campaign Round 3; starts after
`ui-runtime-separation` closes. 2/5 phases complete.
Impact area: `SkullbonezSource/Runtime/` package structure, includes, project
files, intra-Runtime dependency rules
Owner: runtime
Priority: High — Runtime is the last structural god object; every future
feature lands somewhere in it, so its lack of internal layering compounds

## Problem And Evidence (measured 2026-07-23)

`Runtime/` is 242 files and ~108K lines — over half of the engine's ~212K
source lines — and it is the one package with no internal layering. The
class-level god objects were killed (the `Run` decomposition, `SceneWorld`,
the DX12 owner split all hold up under review), but the *package* absorbed
everything that didn't fit lower: input, interaction, automation, capture,
demo direction, stress, diagnostics, editor, picking, replay, startup, tools,
validation harness, view models, and UI text passes all share one namespace
and, for 81 files (~31K lines), one flat top-level directory.

Concrete symptoms:

- **81 loose top-level files (~31K lines)** sit directly in
  `SkullbonezSource/Runtime/` beside ten subdirectories, with no rule for what
  belongs at the top.
- **Input has five overlapping owners at top level**: `Input.cpp/h`,
  `InputController.cpp/h` (+ `.Bindings`), `InputFrame.cpp/h` (1,288 lines),
  `InputFrameExecution.cpp` (1,181 lines), and `InputRouter.cpp/h`, plus
  `InputRouter.Interactions.cpp` and
  `RuntimeInteractionController` beside them. The per-file headers each claim
  a boundary, but nothing states the call direction between the five, and
  nothing but convention stops a new file from reaching across them.
- **Subsystems exist as prefixes, not packages**: camera
  (`Camera`, `CameraCollection`, `AttachedCameraController`,
  `CameraControlState`, `RuntimeCameraMode`), capture (`CaptureController`,
  `CaptureSystem`, `GraphicsStressController`, `RuntimeStressController`),
  picking (`RuntimePickGeometry`, `RuntimePickService`), automation
  (`InteractionAutomationController` 3,300 lines,
  `InteractionAutomationInputDriver`, `InteractionAutomationReportWriter`),
  and interaction (`RuntimeInteractionController`,
  `RuntimeInteractionCommands`, `OperatorCommandApplier`) are all flat
  top-level name families.
- **No intra-Runtime dependency proofs exist.** `AGENTS.md` polices
  Core/Physics/Rendering/Gameplay edges and the Replay boundary with exact
  `rg` proofs, but inside Runtime any file may include any other. The
  layering rigor stops exactly at the package where most code lives.

Replay is explicitly out of scope (owner instruction): `Runtime/Replay/`
moves nowhere and its internals are not reorganized by this plan. Its
boundary is already policed by the Replay Boundary Rule.

## Goal

Runtime becomes a set of named sub-packages with a stated purpose, a stated
allowed-dependency direction, and mechanical `rg` proofs — the same regime
that already governs the lower layers — with (near-)zero loose files at the
package top level. Class ownership does not change; this is a packaging and
edge-declaration plan.

## Non-Goals

- No Replay reorganization, splitting, or interface change (owner
  instruction). `Runtime/Replay/` is frozen for this plan.
- No parameter-list reduction on frame views or load seams (owner
  instruction).
- No UI/Runtime dependency work — that is the `ui-runtime-separation` plan;
  this plan must not move `Runtime/UI/` contents or touch
  `SkullbonezSource/UI` includes.
- No behavior change: physics CSV byte-exact, DX12 baselines unchanged,
  automation reports unchanged.
- No merging of input owners' *behavior* — R4 clarifies and documents
  boundaries and may merge files only where two TUs already implement one
  authority; it must not redesign input semantics.
- No forwarding headers, compatibility aliases, `*Common.h` bags, or a new
  broad `RuntimeContext` — moves land in owning sub-packages directly, per
  the Dependency Direction Rule.

## Ratified Target Package Map (R1, 2026-07-23)

The required execution-time `git ls-files` census finds exactly 81 tracked
top-level Runtime `.cpp`/`.h` files. The target package list is final:

| Sub-package | Single purpose |
|-------------|----------------|
| `Runtime/App/` | Process composition, startup/shutdown, OS message pumping, and top-level input/frame/render sequencing. |
| `Runtime/Input/` | Device sampling, bindings, and retained semantic routing policy/state. |
| `Runtime/Interaction/` | Owner-neutral interaction commands, picking, and synchronous application to scene/editor owners. |
| `Runtime/Camera/` | Camera values, collections, attachment policy, and camera-control state. |
| `Runtime/Capture/` | Screenshot/readback ownership plus deterministic UI/graphics stress controllers that exercise capture/resource lifetime. |
| `Runtime/Automation/` | CLI interaction drivers, reports, and validation-harness orchestration. |
| `Runtime/Direction/` | High-level demo playback and live style direction of already-owned Runtime systems. |
| `Runtime/Simulation/` | Deterministic simulation timing, accumulator, pause, and committed-tick policy. |
| `Runtime/Diagnostics/` | Runtime diagnostics, overlay presentation state/resources, and diagnostic command/status policy. |
| `Runtime/Render/` | Runtime-side render defaults, render sequencing support, and concrete UI/text render-pass implementation. |
| `Runtime/UI/` | Detached Runtime presentation view models and operator-editor frame composition. |
| `Runtime/Tools/` | Shared cold artifact path/file utilities and explicit runtime tool owners. |
| existing packages | `Scene`, `Startup`, `Editor`, `Debug`, and `DevelopmentTools` retain their current cohesive owners; `Replay` remains frozen. |
| top-level residue | `RuntimeFrameViews.h` alone defines cross-package synchronous frame borrows used to keep App from passing itself around. |

The four deliberate refinements from the draft map are ownership decisions:

- `OverlayDebugState.h` moves to `Diagnostics`, beside the owner that mutates
  and publishes it, rather than to App.
- `RuntimeFileWriter.*` moves to `Tools`; it owns generic cold artifact path
  policy and no diagnostic schema.
- `RuntimeViewModel.*` moves to existing `Runtime/UI`; it is a detached
  presentation snapshot, not render execution.
- `UiTextPass.cpp` moves to `Runtime/Render`; its class is declared in
  `Render/RuntimeRenderPasses.h` and scheduled as a concrete render-graph pass.

Stress remains in `Capture` because the paired controllers own deterministic
UI/graphics resource-lifetime churn and borrow Automation/App only for
execution. `InputRouter.Interactions.cpp` moves to App because that partial
implementation synchronously sequences Camera, Editor, Replay, Scene, Tools,
and overlay owners; it is intentionally not part of the lower Input package.
R2's projected-edge census also moves `InputFrame.cpp/.h` and
`InputFrameExecution.cpp` to App: those files apply a complete frame across
Scene, Replay, Render, UI, Editor, Diagnostics, and other owners. Keeping them
in lower Input would make the package-direction rule knowingly false.

### Complete top-level assignment

Counts reconcile to all 81 tracked files with no overlap.

| Current top-level file/family | Count | Destination |
|---|---:|---|
| `ApplicationExitState.cpp/.h` | 2 | `App/` |
| `Init.cpp` | 1 | `App/` |
| `InputFrame.cpp/.h`, `InputFrameExecution.cpp` | 3 | `App/` |
| `InputRouter.Interactions.cpp` | 1 | `App/` |
| `Run.cpp/.h` | 2 | `App/` |
| `RunFrame.cpp` | 1 | `App/` |
| `RunLaunchOptions.h`, `RunLaunchOptions.Renderer.h` | 2 | `App/` |
| `RunRender.cpp` | 1 | `App/` |
| `RunStartupState.h`, `RunTimerState.h` | 2 | `App/` |
| `Window.cpp/.h` | 2 | `App/` |
| `Input.cpp/.h` | 2 | `Input/` |
| `InputController.cpp/.h`, `InputController.Bindings.cpp/.h` | 4 | `Input/` |
| `InputRouter.cpp/.h` | 2 | `Input/` |
| `OperatorCommandApplier.cpp/.h` | 2 | `Interaction/` |
| `RuntimeInteractionCommands.h` | 1 | `Interaction/` |
| `RuntimeInteractionController.cpp/.h` | 2 | `Interaction/` |
| `RuntimePickGeometry.cpp/.h` | 2 | `Interaction/` |
| `RuntimePickService.cpp/.h` | 2 | `Interaction/` |
| `AttachedCameraController.cpp/.h` | 2 | `Camera/` |
| `Camera.cpp/.h` | 2 | `Camera/` |
| `CameraCollection.cpp/.h` | 2 | `Camera/` |
| `CameraControlState.cpp/.h` | 2 | `Camera/` |
| `RuntimeCameraMode.h` | 1 | `Camera/` |
| `CaptureController.cpp/.h` | 2 | `Capture/` |
| `CaptureSystem.cpp/.h` | 2 | `Capture/` |
| `GraphicsStressController.h` | 1 | `Capture/` |
| `RuntimeStressController.cpp/.h` | 2 | `Capture/` |
| `InteractionAutomationController.cpp/.h` | 2 | `Automation/` |
| `InteractionAutomationInputDriver.cpp/.h` | 2 | `Automation/` |
| `InteractionAutomationReportWriter.cpp/.h` | 2 | `Automation/` |
| `RuntimeValidationHarness.cpp/.h` | 2 | `Automation/` |
| `DemoDirector.cpp/.h` | 2 | `Direction/` |
| `DemoDirectorPlayback.cpp/.h` | 2 | `Direction/` |
| `LiveStyleController.cpp/.h` | 2 | `Direction/` |
| `SimulationSystem.cpp/.h` | 2 | `Simulation/` |
| `DiagnosticsController.cpp/.h` | 2 | `Diagnostics/` |
| `OverlayDebugState.h` | 1 | `Diagnostics/` |
| `RuntimeDiagnostics.cpp/.h` | 2 | `Diagnostics/` |
| `RuntimeOverlayDiagnostics.cpp/.h` | 2 | `Diagnostics/` |
| `RenderDefaultsStore.cpp/.h` | 2 | `Render/` |
| `UiTextPass.cpp` | 1 | `Render/` |
| `RuntimeViewModel.cpp/.h` | 2 | `UI/` |
| `RuntimeFileWriter.cpp/.h` | 2 | `Tools/` |
| `RuntimeFrameViews.h` | 1 | top-level residue |
| **Total** | **81** | **81 assigned exactly once** |

## Ratified Intra-Runtime Edge Direction (R2, 2026-07-23)

This is an explicit per-source allowlist rather than a claim that preserved
Runtime owners form one strict total order. R3 is a physical decomposition:
it does not move authority, change Replay internals, or invent facade/context
types merely to manufacture an acyclic diagram. Each package may include
itself, lower engine layers allowed by the standing repository rules, and only
the Runtime targets listed here.

| Source package | Allowed Runtime package targets |
|---|---|
| `App` | Every Runtime package; App is the composition root. |
| `Automation` | `App`, `Camera`, `Capture`, `DevelopmentTools`, `Diagnostics`, `Direction`, `Editor`, `Input`, `Interaction`, `Replay`, top-level frame views, `Scene`, `Tools` |
| `Camera` | `App` process values, `Direction`, `Input`, `Interaction`, `Scene`; never `Render` |
| `Capture` | `App`, `Automation`, `Camera`, `Diagnostics`, `Input`, `Interaction`, `Render`, `Replay`, top-level frame views, `Scene`, `Simulation`, `Tools` |
| `DevelopmentTools` | `App`, `Input`, `Replay` |
| `Diagnostics` | `App`, `Automation`, `Capture`, `Debug`, `Input`, `Render`, `Replay`, `Scene` |
| `Direction` | `Camera`, `Capture`, `Scene`, `Tools` |
| `Editor` | `App`, `Camera`, `Capture`, `Diagnostics`, `Input`, `Interaction`, `Replay`, top-level frame views, `Scene`, `Tools` |
| `Input` | `App/Window` platform capability, `Camera`, `Interaction`, `ReplayEventCommand`, `SceneLifecycle`; never `Render`, `UI`, or `DevelopmentTools` |
| `Interaction` | `Camera`, `Diagnostics`, `Render`, `Scene`, `Simulation` |
| `Render` | `App`, `Camera`, `Debug`, `DevelopmentTools`, `Diagnostics`, `Input`, `Interaction`, `Replay`, top-level frame views, `Scene`, `Tools`, `UI` |
| `Replay` | `Camera`, `Diagnostics`, `Editor`, `Input`, `Interaction`, `Render`, `Scene`, `Simulation`, `Tools`, `UI`; Replay remains frozen and an upper Runtime consumer |
| `Scene` | `App`, `Automation`, `Camera`, `Debug`, `Diagnostics`, `Editor`, `Input`, `Interaction`, `Render`, `Replay`, `Simulation`, `Tools` |
| `Simulation` | `Interaction`, `Scene` |
| `Startup` | `App`, `Replay`, `Scene` |
| `Tools` | `Camera`, `Editor`, `Input`, `Interaction`, `Replay`, `Scene` |
| `UI` | `App`, `Automation`, `Capture`, `Diagnostics`, `Editor`, `Replay`, top-level frame views, `Scene` |
| `Debug` | No other Runtime package. |
| top-level frame views | No Runtime package; values/forward declarations only. |

The Input refinement preserves the five-family ownership model without
pretending frame orchestration is low-level input. `Input` samples devices,
`InputController` owns bindings/context, and `InputRouter` retains semantic
routing state in `Runtime/Input/`; App owns the `InputFrame` assembly/execution
TUs and `InputRouter.Interactions.cpp` because they synchronously sequence the
upper owners. Input's allowed Scene and Replay edges are the owner-defined
`SceneLifecycle` and `ReplayEventCommand` value/command seams, not reach-back
into their mutable storage.

`DevelopmentTools` has one additional incoming edge beyond the draft:
`Render/RuntimeRenderer.cpp` owns the render-graph callback that invokes the
development editor draw. Moving that call to App would split render-graph
transition authority or require a banned callback facade, so `Render` is an
explicit allowed source alongside App, Automation, and Editor. No other
package may add a DevelopmentTools include.

### Mechanical forbidden-edge proofs

After R3 moves, each command below must return no rows. Each pattern is the
complete complement of its source package's allowed set; a new Runtime edge
therefore fails until an owner updates this table and `AGENTS.md`.

```powershell
rg -n '^#include[[:space:]]+.*(Debug|Render|Simulation|Startup|UI)/' SkullbonezSource/Runtime/Automation
rg -n '^#include[[:space:]]+.*(Automation|Capture|Debug|DevelopmentTools|Diagnostics|Editor|Render|Replay|Simulation|Startup|Tools|UI)/' SkullbonezSource/Runtime/Camera
rg -n '^#include[[:space:]]+.*(Debug|DevelopmentTools|Direction|Editor|Startup|UI)/' SkullbonezSource/Runtime/Capture
rg -n '^#include[[:space:]]+.*(Automation|Camera|Capture|Debug|Diagnostics|Direction|Editor|Interaction|Render|Scene|Simulation|Startup|Tools|UI)/' SkullbonezSource/Runtime/DevelopmentTools
rg -n '^#include[[:space:]]+.*(Camera|DevelopmentTools|Direction|Editor|Interaction|Simulation|Startup|Tools|UI)/' SkullbonezSource/Runtime/Diagnostics
rg -n '^#include[[:space:]]+.*(App|Automation|Debug|DevelopmentTools|Diagnostics|Editor|Input|Interaction|Render|Replay|Simulation|Startup|UI)/' SkullbonezSource/Runtime/Direction
rg -n '^#include[[:space:]]+.*(Automation|Debug|DevelopmentTools|Direction|Render|Simulation|Startup|UI)/' SkullbonezSource/Runtime/Editor
rg -n '^#include[[:space:]]+.*(Automation|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Editor|Render|Simulation|Startup|Tools|UI)/' SkullbonezSource/Runtime/Input
rg -n '^#include[[:space:]]+.*(App|Automation|Capture|Debug|DevelopmentTools|Direction|Editor|Input|Replay|Startup|Tools|UI)/' SkullbonezSource/Runtime/Interaction
rg -n '^#include[[:space:]]+.*(Automation|Capture|Direction|Editor|Simulation|Startup)/' SkullbonezSource/Runtime/Render
rg -n '^#include[[:space:]]+.*(App|Automation|Capture|Debug|DevelopmentTools|Direction|Startup)/' SkullbonezSource/Runtime/Replay
rg -n '^#include[[:space:]]+.*(Capture|DevelopmentTools|Direction|Startup|UI)/' SkullbonezSource/Runtime/Scene
rg -n '^#include[[:space:]]+.*(App|Automation|Camera|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Editor|Input|Render|Replay|Startup|Tools|UI)/' SkullbonezSource/Runtime/Simulation
rg -n '^#include[[:space:]]+.*(Automation|Camera|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Editor|Input|Interaction|Render|Simulation|Tools|UI)/' SkullbonezSource/Runtime/Startup
rg -n '^#include[[:space:]]+.*(App|Automation|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Render|Simulation|Startup|UI)/' SkullbonezSource/Runtime/Tools
rg -n '^#include[[:space:]]+.*(Camera|Debug|DevelopmentTools|Direction|Input|Interaction|Render|Simulation|Startup|Tools)/' SkullbonezSource/Runtime/UI
rg -n '^#include[[:space:]]+.*(App|Automation|Camera|Capture|DevelopmentTools|Diagnostics|Direction|Editor|Input|Interaction|Render|Replay|Scene|Simulation|Startup|Tools|UI)/' SkullbonezSource/Runtime/Debug
rg -n '^#include[[:space:]]+.*(App|Automation|Camera|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Editor|Input|Interaction|Render|Replay|Scene|Simulation|Startup|Tools|UI)/' SkullbonezSource/Runtime/RuntimeFrameViews.h
```

R2 projected every current quoted include onto the ratified future package
paths: 463 cross-package include rows across 135 directed package pairs.
The projection found no unrecorded forbidden edge after the InputFrame
correction. R3 owns the 81 physical moves, path rewrites, project/filter
updates, and execution of these proofs against the real layout.

## Phases

- [x] **R1 — Census and ratified map.** Generate the complete top-level
  inventory with `git ls-files 'SkullbonezSource/Runtime/*.cpp'
  'SkullbonezSource/Runtime/*.h'` (not `rg`), reconcile it against the table
  above, and resolve every "decide in R1" row plus any file the table
  missed. Each row gets a one-line purpose statement for its sub-package.
  Acceptance: every tracked top-level Runtime file has exactly one assigned
  destination recorded in this plan, and the sub-package list is final.

  Completed 2026-07-23. The required tracked-file census returns exactly 81
  rows, and the complete assignment table above reconciles all 81 exactly
  once. Every target package has one stated purpose. The prior
  `UiTextPass.cpp` decision is resolved to `Render`; the other three ownership
  refinements are recorded explicitly. `RuntimeFrameViews.h` is the sole
  allowed residue. This phase changes documentation only, so repository
  validation is not required.

- [x] **R2 — Declare the intra-Runtime dependency direction.** Before moving
  anything, write the allowed edges into this plan and `AGENTS.md` (same
  commit as R5 closure if preferred). Starting ruling, refined during R1:
  `App` may include everything; `Automation` may include `Input`,
  `Interaction`, `Scene`, `Diagnostics`; `Interaction` may include `Input`,
  `Camera`, `Scene`; `Input` may include only `Core`/value headers — never
  `Render`, `Scene`, `Replay`, or `UI`; `Camera` must not include `Render`;
  `Replay` remains an upper consumer per the existing Replay Boundary Rule;
  nothing but `App`, `Automation`, and `Editor` may include
  `DevelopmentTools`. Each allowed edge gets a matching `rg` proof of the
  forbidden direction, in the style of the existing rules, e.g.:
  `rg -n '^#include[[:space:]]+.*(Render|Scene|Replay|UI)/' SkullbonezSource/Runtime/Input`
  must return no rows. Acceptance: a complete edge table plus one proof
  command per forbidden edge, all returning no rows against the *planned*
  layout (violations found here become explicit R3 work items, not silent
  allowances).

  Completed 2026-07-23. The projected include graph covers every tracked
  Runtime source-bearing file and all 81 future moves. The exact allowed-target
  table and 18 complementary proof commands above are final. The projection
  corrected `InputFrame*` into App and then found zero unrecorded forbidden
  edges across 463 cross-package rows / 135 directed pairs. R3 must make the
  projected paths real and execute every command; R5 copies the ratified table
  and proofs into `AGENTS.md`. This phase changes documentation only, so
  repository validation is not required.

- [ ] **R3 — Execute the moves.** Move files per the R1 map, update every
  include, `SKULLBONEZ_CORE.vcxproj`, and filters. No forwarding headers, no
  compatibility includes, no namespace changes required (namespace stays
  `SkullbonezCore::Runtime`; only physical layout moves). Where a move
  exposes a genuine misplaced-ownership edge (a file that must reach into a
  package it shouldn't), record it in the R2 edge table with owner, reason,
  and deletion condition rather than hiding it with a re-export. Sequence
  after the `source-blemish-remediation` B3 renames to avoid double
  project-file churn. Acceptance: top-level residue is only the files the R1
  map explicitly allows; build clean at `/W4`; all R2 proofs return no rows.

- [ ] **R4 — Input ownership statement.** With `Runtime/Input/` assembled,
  write one package-level statement (in the package's most central header)
  of who owns what across the five input families: device sampling
  (`Input`), binding/context enforcement (`InputRouter`), frame assembly
  (`InputFrame`), frame execution (`InputFrameExecution`), and higher-level
  bindings (`InputController`). If two TUs turn out to implement one
  authority (candidate: `InputFrame.cpp` + `InputFrameExecution.cpp`), they
  may merge or re-split along the stated boundary — but semantics, key
  bindings, and edge behavior must not change. Acceptance: the ownership
  statement exists, names each file's single responsibility, and the
  independent review (R5) finds no input state reachable through two owners.

- [ ] **R5 — Independent ownership review and closure.** A fresh review
  checks: (a) no move recreated a bag/forwarding shape banned by the
  God-Object Closure Rule; (b) the R2 edge table matches reality (all proofs
  no-rows); (c) `Runtime/Replay/` is untouched (`git diff --stat` for that
  path is empty); (d) the standing `AGENTS.md` dependency and replay proofs
  still return no rows. Findings reopen R3/R4. Acceptance: review recorded,
  edge table and proofs land in `AGENTS.md`, gates below green with output
  pasted.

## Dependencies And Decisions

- Sequence after `source-blemish-remediation` B3 (file renames) — both
  rewrite project files, and R1's census should see final names.
- Coordinate with `ui-runtime-separation`: that plan moves
  `Runtime/Scene/SceneControllerState.h` content into `SkullbonezSource/UI`.
  Whichever plan lands second re-runs the other's proofs. Do not interleave
  the two in one commit.
- `RuntimeFrameViews.h` staying top-level is a deliberate decision: the frame
  borrow slices are the one legitimately cross-package surface (they exist to
  keep `Run` from passing itself around). Revisit only if a sub-package for
  frame sequencing emerges naturally in R1.
- This plan deliberately does not renumber or rename namespaces; physical
  layout first, namespace alignment (if ever) is a separate future decision.

## Acceptance

Runtime has a ratified sub-package map with ≤ a handful of explicitly-allowed
top-level files; every sub-package has a stated purpose and a policed edge
list with `rg` proofs in `AGENTS.md`; input ownership is written down; Replay
is untouched; no behavior change anywhere.

## Validation

`tools\validate_full.bat` at closure (the `Runtime/*` mapping row), with
output pasted. Because moves touch automation and validation-harness files,
also confirm the automation lane inside the full gate reports the same
scenario results as before the moves. If any profiling marker file moves,
run `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` as well.
Physics CSV must remain byte-exact; no baseline refresh is authorized by
this plan.
