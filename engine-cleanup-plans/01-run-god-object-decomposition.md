# 01 — Run God-Object Decomposition

Date: 2026-07-08
Status: Proposed
Priority: P0
Owner: Runtime
Source issue: audit iss-01 (severity 5)

## Problem

`Run` is one class that owns and implements ~40 subsystems — window, cameras,
physics, replay, UI, editor, audio, stress fuzzer, launch options, debug toggles
— and is only kept openable by splitting `Run::` across ~16 translation units
(`Run.cpp`, `RunInput.cpp`, `RunFrame.cpp`, `RunRender.cpp`, `RunScene.cpp`,
`RunStress.cpp`, `RunCapture.cpp`, `RunLiveStyle.cpp`, plus `.inl` includes).

Verified evidence:

- [`Run::TakeInput()`](../SkullbonezSource/Runtime/RunInput.cpp:1576) spans
  L1576→~3240 (**~1,664 lines**; next member `DrainRuntimeCommands` at L3241),
  hand-branching every key and poking 25+ subsystem members with no
  keybinding/command table.
- [`RunState.h`](../SkullbonezSource/Runtime/RunState.h) is a shared "state
  shelf" whose own header (L9-10) concedes it is *"a staging boundary, not a
  destination,"* aggregating 250+ mutable public fields reached directly from
  across the Run files (e.g. `m_camera.autoCycleAccum += simulationDt` in
  `RunFrame.cpp`).

This is the flagship amateur symptom of the codebase.

## Goal

`Run` becomes a thin launcher + frame coordinator. Lifecycle, input, scene,
camera, capture, diagnostics, editor, and render policy move to real owners that
hold their own state behind narrow APIs.

## Approach

- [ ] **Phase 0 — Inventory.** List every `Run` member and method; classify each
  by owner (input / scene / camera / capture / diagnostics / replay / editor /
  stress / render-policy). Output a one-page ownership map.
- [ ] **Phase 1 — Kill `TakeInput()`.** Replace hand-branching with a data-driven
  binding table: `struct KeyBinding { Key key; InputAction action; ContextMask
  contexts; }`. A single dispatch loop maps pressed keys → actions; each action
  handler lives in its owning subsystem. This one change removes the 1,664-line
  function.
- [ ] **Phase 2 — Move state shelves out of `RunState`.** Relocate each shelf's
  fields into the owner that mutates them; `RunState` shrinks toward empty.
  Delete cross-subsystem field pokes.
- [ ] **Phase 3 — Shrink `Run`.** Reduce it to `Initialise` / `Run` / `Shutdown`
  plus per-frame tick coordination that calls owners.

## Risks

- Input is entangled with editor/replay/camera modes; migrate one context at a
  time. Interaction-automation reports are the behavior guard — they must stay
  green across every phase.

## Step-by-step implementation

This is the largest plan — go slowly, one action-group at a time. The
**interaction-automation suite is your behavior guard**: it must stay green after
every step. Commit per step.

### Phase 0 — Inventory

- [ ] **0.1** Produce the `Run` ownership map: list every `Run` member and method
  and classify each by owner (input / scene / camera / capture / diagnostics /
  replay / editor / stress / render-policy). Save it here as a sub-list. No code
  change.

### Phase 1 — Kill `TakeInput()` (the flagship)

- [ ] **1.1** Define the binding types: `enum class InputAction` covering the ~38
  keys `TakeInput()` branches on, and `struct KeyBinding { Key key; InputAction
  action; ContextMask contexts; }`. No behavior yet. Build. Commit.
- [ ] **1.2** Build the static binding table (data) that reproduces the current
  key→action mapping **exactly**, including context conditions (fly/launcher/
  director/replay modes). No dispatch yet. Build. Commit.
- [ ] **1.3** Replace `TakeInput()`'s hand-branching with a dispatch loop over
  the table, calling one handler per action. Move each action's body into its
  **owning subsystem's** handler — do this **one action-group at a time**,
  keeping behavior identical. Gate: interaction-automation suite +
  `validate_full`. Commit per group. Repeat until `TakeInput()` is only the loop.
- [ ] **1.4** Confirm `TakeInput()` is under ~200 lines (setup + dispatch loop).

### Phase 2 — Move state shelves out of `RunState`

- [ ] **2.1** For **one shelf at a time**, relocate its fields into the owner
  that mutates them and remove cross-file pokes (e.g. `m_camera.autoCycleAccum`
  from `RunFrame`). Gate: `validate_full`. Commit per shelf.

### Phase 3 — Shrink `Run`

- [ ] **3.1** Reduce `Run` to `Initialise` / `Run` / `Shutdown` plus per-frame
  tick coordination that calls owners. It should no longer implement subsystem
  logic. Gate: `validate_full`. Commit.

## Validation

`tools\validate_full.bat` (Run/Runtime changes). Interaction-automation suite
after each phase.

## Acceptance (structural)

- [ ] No single `Run` function exceeds ~200 lines; `TakeInput` is a table +
  dispatch loop.
- [ ] `Run` public-method and owned-member counts drop materially from the
  audit baseline (~60 public methods, ~40 members).
- [ ] `RunState` field count is measurably reduced; no external file mutates a
  `RunState` sub-field directly.
- [ ] `Run` no longer implements subsystem logic — it coordinates owners.
