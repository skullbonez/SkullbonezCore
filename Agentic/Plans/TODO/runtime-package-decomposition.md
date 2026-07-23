# Runtime Package Decomposition

Date: 2026-07-23
Status: TODO — drafted from the 2026-07-23 from-source architecture review of
`nightrunner-22nd-JUL-26`. Not yet registered in `MASTER-PLAN.md` by owner
instruction ("just make the plans"); register rows there before the first
plan-runner commit executes any phase.
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
  `RunInput.cpp` (which *is* `class Run` phase code) and
  `RuntimeInteractionController` beside them. The per-file headers each claim
  a boundary, but nothing states the call direction between the five, and
  nothing but convention stops a new file from reaching across them.
- **Subsystems exist as prefixes, not packages**: camera
  (`Camera`, `CameraCollection`, `AttachedCameraController`,
  `RunCameraState`, `RuntimeCameraMode`), capture (`CaptureController`,
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

## Target Package Map (R1 refines this; current best assignment)

| Sub-package | Takes (from top level) |
|-------------|------------------------|
| `Runtime/App/` | `Run.h/.cpp`, `RunFrame.cpp`, `RunInput.cpp`, `RunRender.cpp`, `RunLaunchOptions*`, `RunStartupState.h`, `RunTimerState.h`, `RunDebugState.h`, `ApplicationExitState*`, `Window*`, `Init.cpp` |
| `Runtime/Input/` | `Input*`, `InputController*`, `InputFrame*`, `InputFrameExecution.cpp`, `InputRouter*` |
| `Runtime/Interaction/` | `RuntimeInteractionController*`, `RuntimeInteractionCommands.h`, `OperatorCommandApplier*`, `RuntimePickGeometry*`, `RuntimePickService*` |
| `Runtime/Camera/` | `Camera*`, `CameraCollection*`, `AttachedCameraController*`, `RunCameraState*`, `RuntimeCameraMode.h` |
| `Runtime/Capture/` | `CaptureController*`, `CaptureSystem*`, `GraphicsStressController.h`, `RuntimeStressController*` |
| `Runtime/Automation/` | `InteractionAutomationController*`, `InteractionAutomationInputDriver*`, `InteractionAutomationReportWriter*`, `RuntimeValidationHarness*` |
| `Runtime/Direction/` | `DemoDirector*`, `DemoDirectorPlayback*`, `LiveStyleController*` |
| `Runtime/Simulation/` | `SimulationSystem*` |
| existing `Runtime/Diagnostics/` | `DiagnosticsController*`, `RuntimeDiagnostics*`, `RuntimeOverlayDiagnostics*`, `RuntimeFileWriter*` |
| existing `Runtime/Render/` | `RenderDefaultsStore*`, `RuntimeViewModel*`, `UiTextPass.cpp` (or `Runtime/UI/` — decide in R1) |
| stay put | `Runtime/Scene/`, `Runtime/Startup/`, `Runtime/Editor/`, `Runtime/Debug/`, `Runtime/DevelopmentTools/`, `Runtime/Tools/`, `Runtime/UI/`, `Runtime/Replay/` (frozen) |
| top level (allowed residue) | `RuntimeFrameViews.h` (the frame borrow slices genuinely span sub-packages) |

## Phases

- [ ] **R1 — Census and ratified map.** Generate the complete top-level
  inventory with `git ls-files 'SkullbonezSource/Runtime/*.cpp'
  'SkullbonezSource/Runtime/*.h'` (not `rg`), reconcile it against the table
  above, and resolve every "decide in R1" row plus any file the table
  missed. Each row gets a one-line purpose statement for its sub-package.
  Acceptance: every tracked top-level Runtime file has exactly one assigned
  destination recorded in this plan, and the sub-package list is final.

- [ ] **R2 — Declare the intra-Runtime dependency direction.** Before moving
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
