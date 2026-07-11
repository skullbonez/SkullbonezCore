# Simulation/Render Presentation Interpolation

Date: 2026-07-11
Status: Not started — 0%
Impact area: runtime frame loop, `RenderInstanceStore` presentation records,
attached camera, contact-audio listener, visual baselines (capture timing)
Origin: 2026-07-11 architecture gap review. Physics steps at a fixed 120 Hz
(`PHYSICS_FIXED_DT`); rendering presents the latest solved state with no
blending between steps. When the display rate does not divide 120, frames
alternate between showing one and two ticks of motion — motion strobes
instead of flowing. Note the precise defect: the accumulator keeps total
distance correct over time (objects do not drift "twice as far"); the
artifact is temporal aliasing between frames of unequal simulated advance.

## Goal

Rendering samples object transforms interpolated between the previous and
current physics states by the accumulator's leftover fraction
(`alpha = accumulator / PHYSICS_FIXED_DT`). Motion is smooth at any display
rate. Simulation, determinism, replay recording, and all physics baselines
are untouched — this is presentation-only.

## Scope decisions (binding)

- **Critical-path position.** Begin only after `entity-model-endgame.md` closes
  and render-backend ownership is stable. Presentation transforms, cameras,
  capture timing, and replay must not be migrated across obsolete identity or
  renderer-owner boundaries.

- **Interpolation, not extrapolation.** Render lags at most one physics tick
  (≤8.3 ms); never predict ahead of the solver.
- **Presentation-only.** Physics stores keep authoritative state; the
  interpolated transform exists only in render-facing presentation records.
  `validate_physics` must stay byte-exact through every phase.
- **Deterministic captures.** Validation screenshots must not become
  alpha-dependent: the capture path pins `alpha = 1.0` (exact solver state)
  so visual baselines stay byte-stable. This is the plan's highest
  regression risk to the validation system — it lands first.
- **Fixed storage.** Previous-state snapshots live in preallocated arrays
  sized to store capacity; zero steady-state allocation.
- Replay presentation already has its own sample interpolation; live-path
  work must not fork that logic — reuse or share the quaternion blend
  helpers where practical.

## Phases

### P1 — Capture-determinism guard first

Add the `alpha` plumbing end to end but hardwired to 1.0, and make the
screenshot/capture path pin it explicitly. Proves the pipeline touches
nothing when inert. Gate: `validate_dx12_renderer` (baselines unchanged),
`validate_physics` (byte-exact).

### P2 — Previous-state snapshot

After each fixed step, store position + orientation per body (double-buffer
in or beside `PhysicsBodyStore`/`RenderInstanceStore` presentation records —
flat arrays, copied in one pass). Handle discontinuities: teleport, scene
load/reset, body spawn/delete, and replay scrub set previous = current for
affected rows so nothing visibly lerps across a discontinuity.

Gate: `validate_physics` (byte-exact), `validate_perf` (per-tick copy cost;
measure and record).

### P3 — Interpolated presentation

Presentation records lerp position and nlerp orientation (shortest-arc,
normalized — slerp only if nlerp shows visible artifacts on fast spins) by
the live accumulator alpha. Live rendering uses interpolated records;
capture path keeps alpha = 1.0.

Gate: `validate_dx12_renderer` (baselines unchanged because captures pin
alpha), `validate_physics`, manual smoothness check on a high-refresh
display recorded in this plan.

### P4 — Camera and listener coherence

Attached/follow cameras and the contact-audio listener sample interpolated
presentation positions, not raw store rows, so the camera does not judder
against smoothed objects. Editor gizmos/pick rays keep authoritative
(non-interpolated) transforms — hit-testing against smoothed positions
would desync selection from physics.

Gate: `validate_full` (camera/frame-loop scope) + interaction proofs if the
pick path is touched.

### P5 — Toggle + closure

Launch/config toggle (`presentationInterpolation`, default on) for A/B
verification; diagnostics line showing live alpha. Touched-file comment
audit, rubber-duck review, `validate_full`, SessionState/MASTER-PLAN update,
delete plan on completion.

## Acceptance

- [ ] Motion is smooth at display rates that do not divide 120 Hz (manual
      high-refresh check recorded).
- [ ] `physics_regression_solver.csv` byte-exact in every phase.
- [ ] All visual baselines unchanged (captures pin alpha = 1.0).
- [ ] No visible lerp across teleports, scene resets, spawns, or scrubs.
- [ ] Zero steady-state allocation added; per-tick snapshot cost measured
      and recorded.

## Validation map

| Slice | Gate |
|-------|------|
| Any phase touching stores/frame loop | `validate_physics` (byte-exact CSV) |
| Presentation/record changes | `validate_dx12_renderer` (unchanged baselines) |
| Snapshot copy cost | `validate_perf` |
| Camera/listener changes | `validate_full` |
