# Runtime Shell Decomposition

Date: 2026-07-09 (consolidated)
Status: In progress — ~25% complete
Impact area: runtime architecture, scene lifecycle, input routing, render host
Consolidates: `run-shell-extraction-plan.md`, the mega-file phase of
`fable_plans/04-build-layering-and-repo-hygiene` (M1–M3), the RUN-* rows from
the 2026-07-07 overnight blocker ledger, and item 15.2 of
`engine-cleanup-plans/15-review-gaps.md`. Full history in git history of those
files.

## Goal

`Run` is a thin application shell: process lifetime, startup/shutdown,
top-level frame order, final owner wiring — nothing else. A new scene, replay,
render, input, tool, or diagnostics feature lands inside its owning subsystem
without adding a `Run::*` method or threading a callback through `Run`.

Measured reality (2026-07-09): `Runtime/` is 68,491 lines — 47% of the engine.
The plan-01 split produced 16+ `Run*.cpp` TUs sharing private state through
`RunInternal.h` (partial-class emulation, not ownership transfer). Mega-TUs
persist: `Init.cpp` 3,203, `RunInput.cpp` 2,904, `RunRender.cpp` 2,089,
`RunPasses.cpp` 1,971, plus `UI.cpp` 3,117 and `TestSceneParser.cpp` 2,962.

## Design rules

1. Preserve behavior; extraction commits are boring and revertible.
2. Split by ownership of state and invariants, not by file length. A file
   split without an authority move does not count.
3. No stored `Run&`/`Run*` or broad host-bag dependencies in extracted systems.
4. Each phase leaves less reason for another file to call back into `Run`.

## Remaining work

### A. Narrow the render host (was run-shell Phase 1)

- [ ] A1. Split `RuntimeRenderHostBindings` into small view structs
  (`RenderWorldView`, `RenderSceneView`, `RenderReplayOverlayView`,
  `RenderToolOverlayView`, `RenderUiView`); move replay-overlay callback logic
  into `ReplayRuntime` and tool-overlay logic into `RuntimeTools`.
- [ ] A2. Texture lookup/select goes behind a renderer/asset view — no
  pass-level call back into `Run` for texture handles.
  Gate: `validate_dx12_renderer`, `validate_full` if behavior shifts.

### B. Move tool/replay decision flow out of `Run` (was Phase 2)

- [ ] B1. Replay scrub, focus-mask, prediction-ghost, launcher-visual
  decisions behind `ReplayRuntime` APIs; editor/manipulator/mouse-pickup/
  ray-test/launcher decisions behind `RuntimeTools` APIs.
- [ ] B2. Input/UI requests mutate via command structs; delete compatibility
  accessors per migrated route. Gate: `validate_full`.

### C. Scene lifecycle ownership (was Phase 3)

- [ ] C1. Scene load/reset snapshot+restore logic moves into scene runtime
  ownership; browser selection, adjacent-scene load, deck movement into a
  scene-facing service.
- [ ] C2. Replace `Run`-owned scene callbacks with explicit lifecycle events
  (`BeforeSceneUnload` … `AfterSceneActivated`), consumed via explicit update
  calls. Gate: `validate_full`.
- Coordinate with `TODO/physics-authority-and-identity.md` blocker rows
  PHYS-025/026/027 — the scene creation pipeline split serves both plans.

### D. Mega-TU decomposition (was fable-04 M1–M3)

- [ ] D1. `RunInput.cpp` split by the verified inventory (mechanical moves):
  attached-camera cluster, camera-modes cluster, interaction-transitions
  cluster, matching the existing `Run*Tools.cpp` naming. Sequence after/with
  `TODO/interaction-state-machine.md` phases so code moves once.
- [ ] D2. `TestSceneParser.cpp` (2,962 lines): split by schema domain
  (bodies/assets/groups/water/cameras) — or decide once to vendor a minimal
  JSON reader and delete the hand-rolled tokenizer. Gate: `validate_full`.
- [ ] D3. Convert remaining shared `.inl` composition to real TUs as areas are
  touched; pair hot-path conversions with `validate_perf`.

### E. Collapse the compatibility surface (was Phase 4 + 15.2)

- [ ] E1. Retire `RunInternal.h` as a shared-state channel: constants move to
  owning subsystem headers; inline helpers move next to their single caller or
  a named owner. Acceptance: `RunInternal.h` no longer defines cross-TU shared
  state used by more than one `Run*.cpp`, and no sibling "Internal" header
  replaces it.
- [ ] E2. Delete `Run::*` wrappers that only forward to a subsystem; keep only
  process-level methods on `Run`.
- [ ] E3. Slim `Core/Common.h`: delete the `Config.h` compatibility include
  and stale alias includes per its own in-file deletion schedule (the global
  accessor work it referenced is already done — `Cfg()`/`Gfx()` call sites are
  at zero).

## Known hard blockers (from the 2026-07-07 overnight ledger)

| Row | Knot |
|-----|------|
| RUN-010 | `Run::TakeInput` routes camera/editor/replay/scene/diagnostics commands through Run-private callbacks; needs decomposition into explicit command events with routing-matrix coverage (pairs with D1 and the interaction plan). |
| RUN-011 | `Run::DrainRuntimeCommands` drains scene load, cinematic, capture, quit policy, and replay logging in one ordered loop; needs per-subsystem handlers and an ordering decision. |
| RUN-009 | `Run::LoadScene` extraction crosses interaction/camera hooks, diagnostics reset, teardown ordering, and preserve-runtime semantics (pairs with C1/C2). |
| RUN-015 | Fixed-step loop ownership move needs a `SimulationController` ownership decision plus determinism proof. |

## Acceptance

- [ ] `Run.h` no longer exposes subsystem internals as the default
  integration path; new features do not add `Run::*` methods.
- [ ] `RuntimeRenderHost` removed or reduced to a small immutable context.
- [ ] `RunInternal.h` retired per E1.
- [ ] No source file over ~1,500 lines without a written justification.

## Validation map

| Slice | Gate |
|-------|------|
| Render host narrowing | `validate_dx12_renderer` (+ `validate_full` if scene/replay/tool render behavior changes) |
| Tool/replay/scene ownership moves | `validate_full` |
| Mechanical file splits | per file-to-validation map |
| Hot `.inl` → TU conversions | area gate + `validate_perf` |
