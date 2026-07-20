# Physics Facade Unification

Status: Registered — 0/3 tasks (F0-F2)
Owner: repository owner; registered 2026-07-20 as campaign plan 2 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding C)
Ledger: F0-F2
Depends on: `dependency-direction-restoration` L0-L2 recommended first (clean
include tree makes the absorb mechanical); not a hard blocker.

## Objective

Delete the PhysicsEngine/PhysicsScene double facade. **Owner decision
2026-07-20: `PhysicsEngine` survives; `PhysicsScene` is absorbed and
deleted.** One public physics owner remains behind `PhysicsApi.h`
descriptors, with identical forwarding order and byte-exact behavior.

## Problem / Evidence

`PhysicsEngine.h` (204 lines) and `PhysicsScene.h` (235 lines) expose
near-identical method lists; `PhysicsEngine.cpp` is 469 lines of 1:1
forwarders. `PhysicsEngine.h` self-describes as migration scaffolding. This
is the nominal forwarding owner shape the Migration Cleanup Review Rule
forbids retaining, and it doubles the surface every physics API change must
touch.

## Non-Goals

- No behavior, ordering, or diagnostics change: PhysicsEngine's forwarder
  sequencing comments say forwarders must not reorder solver, store-refresh,
  replay, or diagnostics calls — the absorb preserves call order exactly.
- No PhysicsApi.h redesign; descriptor/command/view contracts are untouched.
- No PhysicsWorld or stage changes.
- No baseline, golden, or replay artifact refresh.

## Binding Decisions

1. `PhysicsScene.{h,cpp}` are deleted; their implementation moves into
   `PhysicsEngine.{cpp}` (plus a private header only if TU size demands it —
   any split must be a cohesive owner file, not a mechanical TU split).
2. No compatibility alias, `using PhysicsScene = ...`, or forwarding header
   may remain.
3. Method bodies move verbatim except for member-name mechanics; any
   non-mechanical edit is called out in the commit body.
4. Callers that name `PhysicsScene` directly (if any exist outside
   PhysicsEngine) are migrated to `PhysicsEngine` in the same commit.

## Tasks

- [ ] F0 — Inventory: enumerate both public surfaces, map every forwarder to
  its target, list all direct `PhysicsScene` users outside PhysicsEngine, and
  record any forwarder that is not a pure relay (extra logic must be
  preserved and named). Output: inventory table in this plan or the closure
  report. No validation (documentation).
- [ ] F1 — Absorb and delete: move PhysicsScene implementation into
  PhysicsEngine, delete `PhysicsScene.h/.cpp`, update includes and project
  files, migrate any direct callers, keep call order verbatim. Apply the
  comment standard to the touched files (facade vocabulary now lives on
  PhysicsEngine). Validation: `tools\validate_physics.bat` (byte-exact CSV)
  and `tools\validate_tests.bat` if any test names PhysicsScene.
- [ ] F2 — Closure: grep proof that `PhysicsScene` appears nowhere in source
  or project files; independent rubber-duck review confirming no authority
  moved anywhere except into PhysicsEngine and no compatibility spelling
  survives; final gates. Validation: `tools\validate_physics.bat` plus
  `tools\validate_full.bat` at closure tip.

## Acceptance

- Exactly one concrete public physics owner (`PhysicsEngine`) remains behind
  `PhysicsApi.h`; `PhysicsScene` is deleted with zero aliases.
- Physics regression CSV is byte-exact against committed baselines.
- Independent review is clear; a credible forwarding/authority finding
  reopens F1.

## Validation Summary

F1: `validate_physics` (+ `validate_tests` when tests are touched).
F2: `validate_physics` + `validate_full` at final source.
