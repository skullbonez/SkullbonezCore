# Real-Time Physics Pacing

Date: 2026-08-23
Status: Active by owner direction; 0/3 phases complete.
Owner: Runtime simulation scheduler
Commit name: `SIM_PACING`
Priority: First active master-plan item; complete before `RAGDOLL_PHYSICS` FP2.

## Problem

`playback.fixedStep` currently selects one `PHYSICS_FIXED_DT` physics tick per
rendered frame. That makes live simulation speed depend on render throughput:
high unlocked FPS accelerates the world, while renderer stalls pause it. The
ordinary wall-clock accumulator already schedules fixed 1/120-second physics
ticks correctly, but authored fixed-step scenes bypass it.

## Intended Fix

- Keep Physics internally fixed at `PHYSICS_FIXED_DT`.
- Run live, unlimited, or operator-controlled scenes from elapsed wall time.
- Reserve one-tick-per-render lockstep for explicit deterministic automation,
  diagnostics, replay validation, and finite non-interactive captures.
- Give the lockstep policy an unambiguous name so it cannot be confused with
  fixed-frequency physics.
- Preserve fractional accumulator time when catch-up is capped and report any
  deliberately dropped whole ticks.

No solver, collision, scene coefficient, or SkullScope energy change belongs
in this repair.

## SP0 - Pin The Scheduling Contract

- [ ] Add focused scheduler tests proving equivalent simulated time at 60, 300,
      and 500 render FPS, plus slow-frame catch-up and explicit lockstep.
- [ ] Pin the effective-policy rule for live versus deterministic automation.

**Acceptance:** Current render-coupled live stepping fails the new tests for the
identified reason; explicit deterministic stepping remains specified.

## SP1 - Decouple Simulation From Rendering

- [ ] Route live scenes through the wall-clock accumulator regardless of their
      authored deterministic-playback setting.
- [ ] Keep explicit automation lockstep available and preserve accumulator
      remainder across capped catch-up.
- [ ] Update affected names and comments to distinguish fixed-frequency physics
      from render-frame lockstep.

**Acceptance:** Render FPS no longer changes elapsed simulation time, while
deterministic automation still advances exact fixed ticks.

## SP2 - Validate And Close

- [ ] Run the focused scheduler/runtime tests and mapped replay/physics checks.
- [ ] Verify VSync state is presentation policy only and never selects physics
      tick count.
- [ ] Audit touched source comments, record any baseline disposition, remove
      this completed plan, and release `RAGDOLL_PHYSICS` FP2.

**Acceptance:** High-FPS, low-FPS, hitch, pause/step, and deterministic modes
all satisfy the scheduling contract without changing Physics behavior.
