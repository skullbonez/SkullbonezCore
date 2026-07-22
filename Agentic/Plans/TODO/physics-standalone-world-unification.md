# Physics Standalone World Unification

Date: 2026-07-22
Owner: skullbonez
State: Registered, not started
Ledger tasks: 5 (PU0-PU4)

## Problem And Evidence (2026-07-22, main tip 0c5263e1)

The engine carries two parallel physics simulations:

- `PhysicsEngine`/`PhysicsWorld` is the production path: staged pipeline
  (force, external-force, broadphase, narrowphase, terrain, contact solver,
  sleep), dense stores, byte-exact deterministic baselines.
- `PhysicsStandaloneWorld` in `SkullbonezSource/Physics/PhysicsApi.h:522` and
  `PhysicsApi.cpp` (2,226 lines) is a second simulation with its own
  semi-implicit Euler `Step()`, its own sphere-sphere/sphere-box contact
  generation, and islands stubbed to a permanently empty view ("until
  store-owned islands migrate behind the public API" — a migration that
  stalled).

The only production consumer of `PhysicsStandaloneWorld` outside
`PhysicsApi.*` is the standalone smoke probe at
`SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp:386`
(`RunPhysicsStandaloneSmoke()`), which backs the physics lane's
standalone-smoke engine process in `validate_physics` / `validate_full`.
`SkullbonezTests/` has zero direct references. A public API whose `Step()`
simulates differently from the engine that ships is worse than no API: any
consumer that trusts it learns wrong physics.

Prior rulings this plan supersedes or touches:

- `physics-facade-unification` (closed 2026-07-20) absorbed the *Scene*
  facade into `PhysicsEngine`; it did not address the standalone world.
- The round-6 monolith TU right-sizing cohesion ruling for `PhysicsApi.cpp`
  (2,226 lines) is superseded by this plan's owner-directed unification;
  re-litigation evidence is this plan's registration.

## Goal

One simulation. The public physics API surface (descriptors, handles, masked
updates, views, activation commands, ray cast, broadphase query) survives as
the contract, but every stepped behavior behind it is `PhysicsEngine`'s real
solver. The duplicate Euler integrator, duplicate contact generation, and the
stubbed island view are deleted with no compatibility spelling left behind.

## Non-Goals

- No change to `PhysicsEngine`/`PhysicsWorld` solver behavior, stage order,
  or determinism envelope. The 44,401-line physics CSV stays byte-exact.
- No new inheritance, callback packs, or forwarding facades.
- No change to `PhysicsSceneObjectId` identity policy (2026-07-11 ruling
  stands).
- No public-API expansion beyond what the smoke re-host strictly needs.

## Phases

- [ ] PU0 — Census. Inventory every symbol in `PhysicsApi.h`/`PhysicsApi.cpp`
  and classify: pure contract (descs/handles/masks/views), standalone-only
  simulation code, and shared helpers. Inventory every consumer of each
  class (production, tests, tools, validation scripts) and record what the
  standalone smoke actually asserts (hardcoded expected hashes vs internal
  invariants). Deliverable: classification table committed in this plan.
- [ ] PU1 — Decision record. From PU0, ratify the target: contract types are
  retained (relocated if needed), `PhysicsStandaloneWorld` is deleted, and
  the smoke probe is re-hosted as a `PhysicsEngine`-backed lifecycle smoke
  exercising the same public contract points (create/update/destroy bodies,
  colliders, point joints, activation commands, ray cast, broadphase query,
  deterministic hashes). Record how smoke hash expectations transition: they
  derive from the engine path after re-host, updated in the same commit, and
  are lifecycle evidence, not physics-behavior baselines.
- [ ] PU2 — Re-host the smoke. Implement the engine-backed smoke path per
  PU1, keep `--physics-standalone-smoke` (or its successor flag) exiting
  zero with meaningful lifecycle/determinism assertions, and keep the
  validation lane wiring (`validate_physics`, `validate_full`) intact in the
  same commit.
- [ ] PU3 — Delete the standalone simulation. Remove
  `PhysicsStandaloneWorld`, its scratch stores, its contact/island
  generation, and every dead helper; retain only contract types with live
  consumers. No `*Compatibility`/`*Bridge` respellings. Project files and
  filters updated in the same commit.
- [ ] PU4 — Closure. Independent rubber-duck review over the whole logical
  `PhysicsApi` surface (header + implementation + smoke harness) confirming
  one simulation, no authority escape, and no dead contract types. Final
  gates below from final source.

## Dependencies And Decisions

- Depends on no other plan; first in the round-2 campaign binding order.
- Owner decision ratified at registration: delete-and-re-host (not "finish
  the migration by teaching the standalone world the real solver"); the
  production solver is the only simulation.
- Any PU0 discovery of an external consumer beyond the smoke probe is
  recorded and ruled before PU2 proceeds.

## Acceptance

- Zero classes in the tree implement a second physics `Step()`.
- The smoke lane still runs one engine process and exits zero with
  lifecycle + determinism assertions backed by `PhysicsEngine`.
- Physics CSV baselines unchanged, byte-exact.
- Independent review records no compatibility spelling, forwarding shape, or
  dead contract type.

## Validation

- Per implementation task: focused build plus the targeted doctest/smoke run
  answering that task's question.
- PU2/PU3/PU4 pre-commit: `tools\validate_physics.bat` (byte-exact CSV plus
  standalone-smoke lane) and, at PU4 closure, `tools\validate_full.bat`.
- No behavioral baseline, golden, screenshot, or replay refresh. Divergence
  is reverted, never normalized.
