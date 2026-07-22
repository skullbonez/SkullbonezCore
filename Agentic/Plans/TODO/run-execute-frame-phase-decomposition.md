# Run::Execute Frame-Phase Decomposition

Date: 2026-07-22
Owner: skullbonez
State: Registered, not started
Ledger tasks: 4 (RX0-RX3)

## Problem And Evidence (2026-07-22, main tip 0c5263e1)

`Run`'s state decomposition is complete (`run-execute-deaccretion` closed
X0-X2 on 2026-07-20; `run-member-and-include-shrink` closed earlier), but the
control flow did not follow: `Run::Execute` in
`SkullbonezSource/Runtime/RunFrame.cpp:177` runs to roughly line 748 — a
~570-line single function containing the Win32 message pump, automation
pre-input, development-UI capture intent assembly, input turn, simulation
tick, capture/screenshot/auto-cycle/scene-advance policy application, render
entry, and present bookkeeping, threaded with a
`SKULLBONEZ_AUTOMATION_DIAGNOSTICS` × `SKULLBONEZ_DEVELOPMENT_TOOLS` `#ifdef`
lattice. The four frame-view structs already exist as phase boundaries
(`RuntimeFrameHostView`, `RuntimeFrameInteractionView`,
`RuntimeFrameSceneView`, `RuntimeFramePresentationView`), but the phases
themselves are inline prose inside one function body.

This is a god *function*: every frame-order invariant the repo's validation
depends on lives in one place that must be read top-to-bottom to change
safely.

## Goal

`Run::Execute` reads as a short, stable sequence of named frame phases, each
a private `Run` method (or existing-owner call) taking the established frame
views. The frame order contract becomes visible in the phase call sequence
instead of in 570 lines of interleaved prose. Conditional-build branching
collapses toward the owners that already exist (`InteractionAutomationController`,
`ImGuiEditorOwner`) so the `#ifdef` lattice in `Execute` shrinks to phase-call
guards.

## Non-Goals

- No behavior, frame-order, or timing change of any kind. This is move-only
  sequencing extraction; validation and replay comparisons depend on the
  stable order.
- No new owner types, no context bag, no callback pack, no `*Internal`
  aggregation. The existing frame views are the calling convention
  (`frame-view-calling-convention` closure stands).
- No change to the message-pump drain policy (`win32-message-pump-drain`
  closure stands, including the 256-message cap).
- No `Run` state additions.

## Phases

- [ ] RX0 — Frame-order census. Document the current `Execute` body as an
  ordered phase list with exact line ranges, the state each span reads and
  writes, and every `#ifdef` region's true owner. This census is the
  extraction map and the review oracle for "nothing moved out of order".
- [ ] RX1 — Extract the frame turn. Pull the per-frame body into named
  private phase methods per the RX0 map (e.g. pump/drain, frame-begin +
  view construction, automation-before-input, input turn, simulation,
  capture-and-advance, render, present/frame-end), each taking existing
  frame views by reference. `Execute` retains the loop, exit latch
  resolution, and phase sequence only. Move-only: identical call order,
  identical strings, identical exit codes.
- [ ] RX2 — Conditional-build consolidation. Relocate automation and
  development-UI intent assembly currently inlined in `Execute` into the
  owners that already hold that authority, so conditional builds guard whole
  phase calls rather than interleaved statement islands. No behavior change;
  Automation and Release builds compile identically in effect.
- [ ] RX3 — Closure. Independent ownership review over the logical `Run`
  frame surface (`Run.h`, `RunFrame.cpp`, `RunInput.cpp`, `RunRender.cpp`,
  sibling Run TUs) per the god-object closure rule: phases are sequencing
  only, no phase method grew business authority, no reach-back appeared.
  Final gates below.

## Dependencies And Decisions

- Second in the round-2 campaign binding order (after
  `physics-standalone-world-unification`); runs before the renderer
  decomposition so that plan rebases on the final frame-phase shape.
- Owner decision ratified at registration: phase methods live on `Run`
  itself — this is sequencing, not new ownership; extracting a "FrameDriver"
  owner is explicitly rejected as a forwarding shape.
- `runtime-renderer-decomposition` RR-tasks touching `RunRender.cpp` rebase
  on RX1's extraction; do not run the two plans' overlapping tasks
  concurrently.

## Acceptance

- `Run::Execute` body is under ~80 lines: loop, exit resolution, phase calls.
- The RX0 census maps one-to-one onto the extracted phase methods with no
  reordered read/write.
- Zero new members on `Run`; zero new owner types; `#ifdef` regions inside
  phase bodies reduced to phase-call guards or moved behind existing owners.
- Independent review records no sequencing-order deviation and no authority
  accretion.

## Validation

- Per task: focused Profile/Automation builds and the targeted interaction
  or lifecycle doctest answering that task's question.
- RX1/RX2/RX3 pre-commit: `tools\validate_full.bat` (Run*/Runtime mapped
  gate). RX2 additionally builds the Automation configuration directly.
- Zero behavioral baseline, golden, screenshot, or replay refresh; byte-exact
  physics CSV and unchanged DX12 images prove the move-only claim.
