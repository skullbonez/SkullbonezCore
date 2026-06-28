# Carmack Plan Rubber-Duck Review (Historical)

Date: 2026-06-28
Branch: Night-Runner-27th-June
Status: Superseded by later 2026-06-28 commits and the current post-plan review.
Scope: Cross-check active Carmack plans against their problem statements before
continuing one-slice-at-a-time commits.

## Current Status Note

This report is historical. It was accurate when written, but later commits on
2026-06-28 resolved or narrowed several findings below, including the
render-resource factory/DXR capability notes, standalone physics smoke evidence,
global-service classification, and allocation baseline evidence. Use
`Agentic/Reports/2026-06-28/carmack-test-post-plan-review.md` plus the current
active plans as the live status snapshot.

## Expected Outcome

The active Carmack work should only be marked complete when the source, plans,
and evidence answer the original failures: wide renderer capability access,
normal-path global services, graph/resource ownership gaps, standalone physics
embedding gaps, and allocation-policy evidence. The static allocation policy is
reviewed here because it is one of the Carmack findings, but implementation is
explicitly out of the current scope unless the user resumes it.

## Render Backend Capability Plan

Findings:

- [Non-blocking] The latest committed slice correctly removed direct `Gfx()`
  use from `RunPasses.cpp`, but the problem statement is not fully satisfied
  while `RenderFrameContext` still carries `IRenderResourceFactory*` for the
  whole frame. That keeps resource creation available outside create/rebuild
  phases even though the plan asks for factory access only during those phases.
- [Non-blocking] `RunPasses.cpp` still calls `GfxRayTracing()` for planar
  reflection. That is narrower than `Gfx()`, but it is still a global accessor
  in ordinary render-pass code and should move behind an explicit DXR capability
  input before claiming the render-pass boundary is clean.
- [Non-blocking] The plan's inventory checklist remains open: `IRenderBackend`
  method listing, capability mapping, and call-site mapping are not yet recorded
  in the plan. The code has made real progress, but the acceptance evidence is
  still slice-local rather than complete.

Missing evidence:

- Current full `IRenderBackend` method inventory and call-site capability map.
- Perf evidence if any future adapter/dynamic-geometry slice touches hot
  per-frame paths.

Next step:

Move resource-factory access out of the per-frame context or move the remaining
DXR reflection accessor into an explicit borrowed capability, then lower the
matching guardrail count after validation.

## Global Service Lifetime Plan

Findings:

- [Non-blocking] The counted global-service ratchet is useful, but it is not a
  semantic classification. The plan still needs a recorded split of each
  remaining accessor into bootstrap, shutdown, OS callback bridge, compatibility
  adapter, or normal-path debt.
- [Non-blocking] Current progress is concentrated in runtime render passes.
  Other normal-path callers remain in assets, render helpers, text/UI, window,
  scene/runtime, physics debug, world, and profiling code. That means the
  problem statement is not resolved even though the latest renderer slice lowered
  the count.
- [Non-blocking] `GfxRayTracing()` remains in runtime pass and UI/scene paths.
  It is a narrow capability accessor, but still a process-global normal-path
  service until it is borrowed from the composition root.

Missing evidence:

- Updated classification table for all remaining global-service allowlist rows.
- Startup/shutdown bind-order notes for the newer render service borrowers.

Next step:

Use the latest count as a ratchet, then choose one global family with a clear
owner, preferably runtime DXR capability borrowing or resource-factory phase
narrowing, and record its classification before editing source.

## Render Graph Resource Ownership Plan

Findings:

- [Blocking for plan completion] The current graph callbacks prove callback
  execution for several pass families, but the problem statement asks for
  production ownership of frame pass execution, resource transitions, transient
  render-target lifetime, descriptor lifetime, and diagnostics. The plan cannot
  be considered close to done while the inventory section is still unchecked.
- [Blocking for plan completion] Render graph resources are still mostly
  external/imported frame objects with `HandoffValidated` barrier policy. That
  is useful migration scaffolding, not graph-owned allocation or descriptor
  lifetime.
- [Non-blocking] Existing `Unknown` access is guarded, which is good, but the
  success bar needs positive concrete access ownership for each migrated
  resource, not just a ratchet against new unknown access.

Missing evidence:

- Pass/resource inventory, manual barrier inventory, and descriptor ownership
  inventory.
- Fresh DX12 screenshot/validation baseline before the next pass/resource
  migration.
- Perf evidence before any transient allocator or descriptor-storage change is
  claimed allocation-safe.

Next step:

Commit an inventory slice first, then migrate the next low-risk pass/resource
family with explicit graph resource names and access states.

## Physics Standalone Boundary Plan

Findings:

- [Blocking for plan completion] `SimulationTickInput` no longer accepts
  `GameModelCollection*`, which is real progress, but the standalone problem
  statement is not satisfied until a small harness can create and step physics
  without `Run`, renderer, scene parsing, or `GameModelCollection`.
- [Blocking for plan completion] Public physics API work is still mostly a
  guardrail. The plan still needs create/update/query/delete descriptors,
  immutable views, deterministic ordering rules, and a world or owner token if
  multiple isolated worlds are required.
- [Non-blocking] The compatibility borrowers are not yet inventoried in the
  plan by group. Without that table, future slices can accidentally mix
  step-critical, diagnostics, replay, scene setup, editor/tool, and render mirror
  changes.

Missing evidence:

- Current `GameModelCollection` / `PhysicsModelAccess` borrower inventory
  reconciled against `tools\check_runtime_boundaries.py`.
- Standalone physics smoke command or tool with deterministic final state/hash.
- Byte-exact `tools\validate_physics.bat` evidence after any physics-visible
  implementation slice.

Next step:

Complete and commit the compatibility-borrower inventory, choose the first
borrower group, then add the smallest standalone physics smoke path.

## Runtime Static Allocation Policy Plan

Findings:

- [Blocking if this scope resumes] The plan is still an allocation-policy draft.
  No baseline `validate_perf` output, runtime phase tracker, reserve allocator,
  static checker, or gameplay/replay allocation guard is in place yet.
- [Non-blocking for current scope] This policy remains excluded from current
  implementation. Do not claim the Carmack performance issue is resolved in the
  final Carmack test for this run.

Missing evidence:

- `tools\validate_perf.bat` baseline and allocation guard output.
- Runtime owner-to-phase inventory.
- Synthetic static-checker tests and perf-script integration.

Next step:

Leave this unchecked for the current branch goal unless the user explicitly
resumes option 3 implementation.
