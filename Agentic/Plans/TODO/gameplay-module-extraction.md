# Gameplay Module Extraction

Status: Registered — 0/4 tasks (T0-T3)
Owner: repository owner; registered 2026-07-20 as campaign plan 7 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding G)
Ledger: T0-T3
Depends on: `physics-settings-snapshot` C1 (force-stage inputs are typed
values, giving the external-force seam its shape) and
`render-graph-completion` (pass registration seam for T2). T0-T1 can start
after plan 3; T2 requires plan 5.

## Objective

**Owner decision 2026-07-20: extracted gameplay content lives in a new
top-level `SkullbonezSource/Gameplay/` module** that sits above Physics and
Rendering. Move `TornadoGameplay`/`TornadoField` out of `Physics/` and
`TornadoVisualPass` ownership out of the fixed renderer pass list, plugging
in through two engine seams: a data-driven external-force lane on the force
stage and the plan-5 pass-registration path. The engine/content boundary
becomes a directory boundary provable by grep.

## Problem / Evidence

`PhysicsWorld` owns and sequences `TornadoGameplay` as a "stay-behind"
member (`PhysicsWorld.h:147`, `ApplyTornadoGameplay` on the facade), with
tornado config surfaces (`SetTornadoFieldConfig` etc.) on the physics
boundary; `RuntimeRenderer.h:319` bakes `TornadoVisualPass` into the fixed
pass list. Content is fused into the two hottest core modules; every future
game feature copying this pattern makes the engine less an engine.

## Non-Goals

- No behavior change: tornado force application order, magnitudes, worker
  scheduling, and determinism are preserved exactly. Byte-exact physics CSV
  (tornado scenes are in the varied baseline set) is the oracle; zero
  refresh authorized.
- Buoyancy, water, terrain, and world environment stay in the engine — they
  are environment, not content.
- No general plugin/scripting system: the external-force lane is a typed,
  fixed-capacity engine seam with `Gameplay/` as its first (compile-time)
  consumer.
- No visual change to the tornado; `TornadoVisualPass` moves ownership, not
  implementation.
- No new inheritance anywhere on the hot path (Hot-Path Data and
  Inheritance Review Rule): the force lane is value records in bounded
  arrays consumed by `PhysicsForceStage`, not a `*Sink`/callback interface.

## Binding Decisions

1. New module `SkullbonezSource/Gameplay/` (own filter folder in the
   vcxproj; direction rule from plan 1 extends: `Gameplay/` may include
   Core/Maths/Physics/Rendering/Scene contracts; nothing below it may
   include `Gameplay/`).
2. The force seam is a bounded per-tick value input to the force stage
   (e.g. `ExternalForceField` records: center/axis/falloff/strength rows in
   a fixed-capacity span) supplied by the owner that steps physics.
   Deterministic iteration order is part of the contract; the tornado's
   existing pair-scratch/worker behavior must map onto it without
   reordering floating-point accumulation (knife-edge risk recorded in the
   Danger Zones physics-determinism row).
3. If a lossless mapping to generic force records is not provable
   byte-exact, the fallback is an owner-sequenced typed hook at the exact
   current call position (`ApplyTornadoGameplay` timing), still owned by
   `Gameplay/` — recorded explicitly, with the generic lane as its deletion
   condition. Divergence is never normalized by baseline refresh.
4. Tornado config/query surfaces (`SetTornadoFieldConfig`,
   `GetTornadoSystemElapsedSeconds`, UI tab plumbing, replay/diagnostic
   references) move to the `Gameplay/` owner; physics keeps zero
   tornado-named API.
5. `TornadoVisualPass` registers through the plan-5 pass path from
   `Gameplay/`; `RuntimeRenderer` keeps zero tornado-named members.

## Tasks

- [ ] T0 — Seam design and census: enumerate every tornado reference in
  `Physics/`, `Runtime/`, `Rendering/`, `UI/`, replay, config, scenes, and
  baselines; specify the external-force lane contract (capacity, ordering,
  units, worker interaction) and the pass-registration usage; name which
  binding-decision path (2 or 3) the evidence supports. Output: census +
  contract committed into this plan. No validation (documentation).
- [ ] T1 — Physics extraction: create `Gameplay/`, move
  `TornadoGameplay`/`TornadoField` there, feed forces through the seam,
  delete tornado members/APIs from `PhysicsWorld`/`PhysicsEngine`.
  Proof: `grep -irn "tornado" SkullbonezSource/Physics` returns zero rows.
  Validation: `tools\validate_physics.bat` (byte-exact, tornado scenes
  included) + `tools\validate_perf.bat` (force-stage hot path touched);
  `tools\validate_physics_deep.bat` if any SkullScope tornado diagnostics
  move.
- [ ] T2 — Render extraction: `TornadoVisualPass` ownership and its
  settings snapshot move to `Gameplay/`, registering through the plan-5
  pass path; `RuntimeRenderer` loses tornado members. Proof:
  `grep -irn "tornado" SkullbonezSource/Runtime/Render
  SkullbonezSource/Rendering` returns zero rows. Validation:
  `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`.
- [ ] T3 — Closure: UI/replay/config reference reconciliation lands on the
  `Gameplay/` owner; direction-rule grep extended in `AGENTS.md`
  (`Gameplay/` inclusion rule); independent rubber-duck review (boundary
  check: physics/render kept zero content vocabulary; the seam did not
  become a callback pack); final gates. Validation:
  `tools\validate_full.bat` + `tools\validate_physics.bat` at closure tip.

## Acceptance

- `Physics/`, `Rendering/`, and `Runtime/Render/` contain zero
  tornado-named source; `Gameplay/` owns the content end to end.
- Physics CSV byte-exact and DX12 baselines identical with zero refresh; if
  binding-decision path 3 was used, the recorded hook names its deletion
  condition.
- Perf gate shows no force-stage regression outside noise (record numbers).
- Independent review clear on seam shape (bounded value lane, no hot-path
  inheritance/callbacks).

## Validation Summary

T1: `validate_physics` + `validate_perf` (+ `validate_physics_deep` when
SkullScope surfaces move). T2: `validate_dx12_renderer` + bounded stress.
T3: `validate_full` + `validate_physics` at final source.
