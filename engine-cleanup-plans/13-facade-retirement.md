# Facade Retirement Plan

Date: 2026-07-08
Status: To Eval (revised 2026-07-08 — structural reframing)
Owner: Runtime, rendering, physics, and architecture cleanup agents

## Revision Note

This plan was rewritten to fix a design flaw in its first draft. The first draft
targeted the **word** "facade": it made "`rg -i "facad"` returns 0" an acceptance
criterion and proposed a boundary-checker rule banning identifiers that end in
`Facade`. That optimizes for the absence of a word, not the absence of the
coupling. A pointer-bag renamed from `EngineContextFacade` to
`EngineContextOwner` passes every such gate while remaining exactly as coupled.
Worse, the first draft whitelisted "owner", "capability", "store", "system", and
"boundary" as positive fixtures — which is Goodhart's law pre-installed: it tells
the next facade what to call itself. And it added more rules to an already very
large boundary checker to police spelling.

The structural targets the first draft identified were correct. This revision
keeps those and discards the lexical enforcement. **A rename is never
completion.** Done means the coupling is gone: the aggregate type is deleted, the
pointer-bag is split, the reach-through accessor is removed. What the surface is
*called* afterward is irrelevant.

## Goal

Graduate or delete facade-shaped surfaces. Each surface listed below must reach
one of exactly two end states:

1. **Graduate** into a real domain owner: a narrowed contract that exposes only
   what its callers use, with no reach-through accessor that hands out the
   thing it was supposed to wrap.
2. **Delete**: the surface disappears and callers talk to the real owner.

Renaming the type, or scrubbing the word "facade" from a comment, satisfies
neither. This plan does not care about the word; it cares about the shape.

## Definition Of Done (applies to every surface)

A surface is done only when all of these hold:

- It exposes no accessor that returns its own wrapped implementation for callers
  to reach through (e.g. `SimulationController::System()`,
  `RenderBackendDX12::device()`, a global `Gfx()` returning the aggregate).
- No consumer receives more dependencies than it uses. Passing a whole
  runtime-owned graph to a subsystem that touches three fields is not done.
- The narrowed contract is enforced by construction (the type simply does not
  offer the wide surface), not by a comment asking callers not to use it.
- If the surface is kept, it owns concrete behavior/policy of its own. A type
  whose only job is to forward calls and name ownership is deleted, not kept.

## Non-Goals (explicitly removed from the first draft)

- **No word ban.** We do not target `rg -i "facad"` == 0. Comments may say
  "this replaced a facade" for audit history.
- **No `*Facade`-suffix lint rule.** A regex on an identifier suffix cannot
  distinguish a clean design from a rename-to-dodge-the-linter, and adds
  maintenance to `check_runtime_boundaries.py` for no structural guarantee.
- **No positive-vocabulary whitelist.** We do not bless "owner"/"capability"/
  "store"/"system" wording; naming is a downstream, cosmetic concern.

## Remaining Facade Surfaces

Each row's end state is now **structural and rename-proof**. The work itself is
owned by the plans in the last column; this document owns only the shared rule
and the cross-cutting acceptance invariants below.

| ID | Surface | Current evidence | Required structural end state | Owning plan(s) |
|----|---------|------------------|-------------------------------|----------------|
| FAC-001 | Renderer aggregate `IRenderBackend` | `Rendering/IRenderBackend.h` — a "Compatibility aggregate" that multiply-inherits 5 capability interfaces and declares no methods of its own; reached via a global accessor | The aggregate **type is deleted**. Every caller borrows one of the already-existing narrow interfaces (`IRenderDeviceLifecycle`, `IRenderResourceFactory`, `IRenderCommandContext`, `IRenderDiagnostics`, `IRenderCaptureBackend`, `IRenderRayTracing`). No global returns the aggregate. | `render-graph-irender-interface-plan.md`, `fable_plans/07-blocker-remediation-plan.md`, `overnight-blockers-2026-07-07.md` |
| FAC-002 | `EngineContext` pointer-bag | `Runtime/EngineContext.h` — `EngineContextBindings` holds 13 subsystem pointers + `EngineServices` ~7 more; bound as one graph in `Run::BindEngineContext()` | `EngineContextBindings` is **deleted or split** into owner-specific records. No extracted system receives the whole runtime graph. If `EngineServices Services()` has no real callers, delete it rather than keep it as decorative architecture. | `run-shell-extraction-plan.md`, `engine-architecture-next-steps-plan.md` |
| FAC-003 | `Run` composition shell | `Runtime/Run.h`, `Runtime/RunState.h` (self-described "staging boundary, not a destination") | `Run` retains only launcher + frame-coordination methods. Lifecycle, scene, input, capture, diagnostics, and render policy move to real owners. Measured by Run's method/member count dropping, not by comment edits. | `run-shell-extraction-plan.md`, `runtime-run-decomposition-plan.md` (Done — reopen if `Run` still owns these) |
| FAC-004 | `SimulationController` wrapper | `Runtime/SimulationController.h` — 97 lines total; exposes `System()` returning the underlying `SimulationSystem&` | **Delete** it and call `SimulationSystem` directly, **or** move real timestep policy into it *and* remove the `System()` reach-through. Keeping a forwarding wrapper with a passthrough accessor is not an allowed end state. | (needs an explicit owner — currently none) |
| FAC-005 | Public physics API boundary | `Physics/PhysicsEngine.h`, `Physics/PhysicsApi.h` | The public physics API exposes **no** `GameModel`, no raw dense `modelIndex`, and no solver container types in its signatures. This is a type-level boundary check, independent of any wording in the headers. | `physics-game-model-authority-plan.md`, `game-model-data-boundary-plan.md`, `fable_plans/06-stable-identity-plan.md` |
| FAC-006 | Camera collection wording | `Runtime/Camera.h`, `Runtime/CameraCollection.h` | Cosmetic only — **descoped**. `CameraCollection` is already the camera owner; there is no structural change to make. Do not spend a work slice renaming comments. | none (drop) |
| FAC-007 | `RenderBackendDX12` concrete owner | `Rendering/DX12/RenderBackendDX12.h` | Keep as the concrete DX12 backend owner. The only structural risk is its cached borrowed aliases (`m_device`/`m_swapChain`/`m_commandList`) duplicating pointers `Dx12RenderDevice` owns — fix dual ownership so device recreation cannot dangle them. Naming is not the issue here. | `dx12-final-architecture-next-steps.md` |

## Work Sequencing

Do the surfaces in benefit order, each in its owning plan:

1. **FAC-001 first.** The narrow capability interfaces already exist, so this is
   near-pure Interface Segregation with the seams already cut — highest value,
   lowest risk. Route callers to the narrow interfaces, delete the aggregate
   type and its global accessor.
2. **FAC-002** — inventory `EngineContext` consumers, replace with narrow
   records, delete the bag (and `Services()` if unused).
3. **FAC-004** — collapse or graduate `SimulationController`; kill the `System()`
   passthrough either way.
4. **FAC-007** — resolve the DX12 borrowed-alias dual ownership.
5. **FAC-003 / FAC-005** — continue in their existing decomposition/authority
   plans; this doc only contributes the "graduate-or-delete, rename is not done"
   rule as their definition of done.

FAC-006 is dropped.

## Acceptance (structural invariants, rename-proof)

These greps target **types and accessors**, not the word "facade". They pass
only when the structure is actually gone:

- [ ] `rg -n "class IRenderBackend|IRenderBackend&\s+Gfx" SkullbonezSource` finds
  nothing — the aggregate type and its global accessor are deleted (not
  renamed).
- [ ] `rg -n "\bEngineContextBindings\b" SkullbonezSource` finds nothing, or every
  remaining consumer takes an owner-specific record; no subsystem is handed the
  whole graph.
- [ ] `EngineServices Services()` is either removed or has real, non-assertion
  callers.
- [ ] `rg -n "SimulationController::System\(|\.System\(\)" ` no longer exposes the
  underlying `SimulationSystem` for reach-through; `SimulationController` is
  deleted or owns concrete timing policy.
- [ ] `RenderBackendDX12` holds no borrowed device/swapchain/commandlist aliases
  that duplicate `Dx12RenderDevice`-owned pointers (or they are provably
  refreshed on device recreation).
- [ ] Public physics headers (`PhysicsApi.h`, `PhysicsEngine.h`) contain no
  `GameModel`, dense `modelIndex`, or solver-container types in public
  signatures.
- [ ] `Run`'s owned-member and public-method counts have measurably dropped and
  it no longer implements lifecycle/scene/input/capture/diagnostics/render
  policy.

Naming of any surviving surface is out of scope for acceptance.

## How to apply (this is a rule, not a standalone work item)

This doc has no steps of its own — its work is executed inside other plans. When
you finish any facade surface there, apply this checklist:

- [ ] The surface reached **graduate or delete** (see Definition of Done above),
  not a rename. Verify with the FAC row's **structural** acceptance grep (for a
  type/accessor), never `rg "facad"`.
- [ ] FAC-001 (aggregate), FAC-002 (context bag), FAC-004
  (`SimulationController`), FAC-007 (DX12 aliases) are executed by
  [plan 10](10-enginecontext-irenderbackend-boundary.md).
- [ ] FAC-003 (`Run` shell) is executed by
  [plan 01](01-run-god-object-decomposition.md).
- [ ] FAC-005 (public physics API exposes no `GameModel`/dense
  `modelIndex`/solver containers) has **no numbered owner plan yet** — flag to a
  human before acting.
- [ ] FAC-006 is dropped (cosmetic).

## Validation

- FAC-001 / FAC-007: `tools\validate_dx12_renderer.bat` (verify
  `dx12_validation.txt` == 0); `tools\validate_full.bat` if `Run` lifecycle
  changes broadly.
- FAC-002 / FAC-003: `tools\validate_full.bat`.
- FAC-004 / FAC-005: `tools\validate_physics.bat` (fixed-step determinism must
  not change).
- Comment-only tidy-ups: no repository validation required.
