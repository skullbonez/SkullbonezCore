# Unified Error Handling Policy Plan

Date: 2026-07-06
Status: Phase 1 policy and ratchet complete on 2026-07-07; Phase 2 replay probe conversion/evidence complete on 2026-07-08; SpatialGrid, PhysicsWorld, GameModelCollection pure topology, legacy camera pose-read, append Lane R conversions, scene/style TryLoad entry-boundary conversion, and one direct missing-camera parser throw removal complete; remaining deeper loader/asset and DX12 conversions pending
Impact area: all subsystems, incrementally; policy + mechanical conversion
Validation for this document: none (documentation-only)

## Problem

The codebase has **three coexisting error philosophies and no rule for which
applies where**:

- **355 `throw` sites across 47 files**, against only **28 `catch` sites in
  11 files** — most throws are uncaught-by-design, i.e. exceptions used as a
  crash mechanism with extra steps.
- Fatal failure elsewhere is handled by asserts/fail-fast (the allocation gate
  mandates "assert in Profile/Debug or fail fatally in Release with
  diagnostics") — a *different* crash mechanism.
- Recoverable paths use ad hoc bool returns with logging.

The throws themselves serve at least four distinct intents, all spelled the
same way (`throw std::runtime_error`):

1. **Programmer invariants** at runtime: `GameModelCollection.cpp` ("Failed to
   resolve newly authored physics body record"), `SpatialGrid.cpp` (13 sites),
   `PhysicsWorld.cpp` (6) — should-never-happen conditions inside gameplay
   systems.
2. **Self-test probes**: `RunFrame.cpp` has ~60 throws that are assertions
   inside replay/scrub stress probes ("replay scrub probe mutated the live
   body...").
3. **Data validation**: `ConvexHullShape.cpp` (41), `TestSceneParser.cpp`,
   asset/texture loaders — bad input files, genuinely recoverable.
4. **Environment/API failure**: `RenderDeviceDX12.cpp` (38), DX12 wrappers —
   device/HRESULT failures during init or resource creation.

Consequences: editor actions can crash the app through an uncaught loader
throw; probe failures are indistinguishable from engine bugs in crash triage;
exception paths through hot physics code (`SpatialGrid`) undermine the
hot-path discipline; and reviewers have no standard to review against.

## The policy (definition of done, part 1)

Adopt and document in `AGENTS.md` a three-lane rule:

- **Lane F — Fatal invariant.** Should-never-happen state. Mechanism: a single
  `SB_FATAL( owner, "msg", diagnostics... )` macro — asserts in Debug/Profile,
  fail-fast with logged owner/diagnostics in Release. Matches the wording the
  allocation gate already uses. **Never `throw`.** Used in: physics, stores,
  solver, frame loop, replay internals.
- **Lane R — Recoverable result.** Anything triggered by external input:
  file/scene/asset loading, editor commands, automation scripts, device-lost.
  Mechanism: `SbResult`/`SbExpected<T>`-style return (value or error struct
  with owner + message), propagated to the nearest UI/log boundary. The
  operation fails; the app does not.
- **Lane P — Probe/stress assertion.** Self-test probes report through a
  dedicated `ProbeFailure` channel (log + machine-readable automation failure
  or nonzero CLI exit, depending on the probe owner), not through the exception
  machinery. Probe failures must be machine-readable instead of becoming
  `fatal_exception` crashes.

**Exceptions are banned for new code in all three lanes.** Existing throws are
converted lane by lane; `/EHsc` stays on (MSVC stdlib needs it) but the
codebase stops authoring throws.

## Phased slices

### Phase 1 — policy + ratchet (one sitting, before any conversion)

- Write the lane rule into `AGENTS.md` (review section + a short lane table).
- Add `SB_FATAL` and the result type to Core (tiny, header-only; unit-tested
  under plan 01).
- Extend `tools/check_runtime_boundaries.py`: census of `throw` sites with a
  stored budget (355). Any increase fails validation. Self-test included.

### Phase 2 — probes first (biggest single cluster, lowest risk)

- Convert `RunFrame.cpp`'s ~60 probe throws to the `ProbeFailure` channel.
  P2.1 discovery found these replay probes are Debug-only CLI diagnostics, not
  direct `--interaction-script` actions, so the conversion surfaces failure via
  `replay_probe_failed` logging and a nonzero process exit while preserving the
  old assertion messages.

### Phase 3 — hot-path invariants

- `SpatialGrid`, `PhysicsWorld`, `PersistentContactSolver`,
  `GameModelCollection` body/collider paths → `SB_FATAL`. This also removes
  exception paths from code the hot-path gate governs.
- Gate: `validate_physics` byte-exact (mechanism swap must not reorder math).

SpatialGrid and PhysicsWorld are complete and validated as one-file commits in
this phase. GameModelCollection classification is complete: pure post-append
topology failures are Lane F, legacy object-follow camera reads are a no-op/fatal
split, and append capacity/missing-identity/group-descriptor failures are Lane R
results. Authored scene group metadata still belongs to the loader/editor Lane R
work below.

### Phase 4 — loaders and editor surface

- `TestSceneParser`, `ConvexHullShape` bake/load validation, texture/asset
  loaders → Lane R results. Scene-load failure surfaces as a logged, user
  visible "scene failed to load: <reason>" instead of a crash; editor
  placement failure becomes a UI-visible no-op. The 28 existing `catch` sites
  shrink as their matching throws disappear; each removed catch is reviewed
  for what it was actually swallowing.

2026-07-08 entry-boundary note: scene/style loading now has non-throwing
`TestScene::TryLoadFromFile` and `TryLoadStyleFromFile` entry points, and
runtime startup/load-only paths return nonzero on failed scene loads. The
deeper parser throw tokens and Terrain, TextureCollection, AssetSystem, and
ConvexHullShape loader clusters remain in this phase.

### Phase 5 — DX12 layer

- Device/resource creation failures: init-time → Lane R up to the boot
  boundary (clean "device unsupported" exit); steady-state → the existing
  DX12 validation/fatal path. This is the most delicate cluster; do it last,
  gated by `validate_dx12_renderer` plus the 3-consecutive-run rule.

## Guardrails

- `throw` budget ratchet (phase 1) — the count only goes down: 355 → 0.
- Review rule in `AGENTS.md`: any new `throw` is a review failure; any new
  fatal path must name its lane.

## Risks

- Behavior changes hide in conversion: a throw that *was* being caught
  somewhere (28 catch sites) and silently recovered must keep recovering —
  audit each catch before deleting its throw, not after.
- `SB_FATAL` in Release must emit diagnostics before dying (owner, message,
  key values) or crash triage gets worse, not better; reuse the allocation
  guard's existing owner/capacity/high-water diagnostic formatting.

## Validation map

| Slice | Validation |
|-------|-----------|
| Policy + ratchet | `validate_fast`, then run the changed checker |
| Probe conversion | Run the affected interaction proofs + forced CLI probe failure + `validate_fast` (`validate_full` too when Run/Runtime/Init files are touched) |
| Physics invariants | `validate_physics` (+ `validate_perf` if signatures change) |
| Loader lane | `validate_full` (scene/asset load is broad scope) |
| DX12 lane | `validate_dx12_renderer` × 3 consecutive |
