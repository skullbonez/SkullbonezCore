---
name: skore-physics-low-repro
description: Turn rare physics failures into deterministic unattended repro captures with seeded runs, targeted predicates, and snapshot logs.
---

# skore-physics-low-repro

## Purpose

Use this skill whenever a physics issue is rare, timing-sensitive, seed-sensitive, or difficult to reproduce by hand.

The goal is to convert "I saw an impossible physics state once" into:

1. a deterministic launch command,
2. an unattended detector,
3. a single hit log with enough state to replay and debug,
4. minimal manual input while the loop runs.

The current edge-rest detector is one example of this pattern, not the pattern itself.

## Generic Capture Pattern

For any low-repro physics bug:

1. Define the impossible state as a precise predicate.
2. Add a command-line capture mode, usually `--<problem>-test [log_path]`.
3. Write the log header before the simulation loop starts.
4. Disable normal interactive input during the unattended run unless the repro requires input.
5. Scan every frame after physics has advanced.
6. On first hit, write a full snapshot and stop the app.
7. Include a replay command hint with seed, renderer, scene, object mode, time scale, fixed-step state, and important feature toggles.

Keep the detector narrow enough to avoid false positives, but log enough context to refine it later.

## Required Log Contract

Every low-repro capture log should include:

- log path and start timestamp,
- renderer and physics mode,
- scene path or legacy/random marker,
- RNG seed and command-line seed override,
- fixed-step state and time scale,
- relevant runtime toggles (`--no-water`, object-generation overrides, solver/legacy mode),
- model count and selected model index,
- position, velocity, angular velocity, orientation, mass, restitution, inertia,
- collision shape details,
- sleep state and sleep-support state when relevant,
- terrain/sample data when terrain contact is relevant,
- contact/manifold data when object contact is relevant,
- detector-specific fields,
- a replay command hint.

Prefer reusing the same snapshot payload used by nudge-mode repro logging so manual and unattended captures produce comparable artifacts.

## Detector Design

A good detector usually answers four questions:

1. What object type or body set can fail?
2. What physical invariant is violated?
3. What nearby supports or contacts explain the state?
4. What conditions should exclude normal, physically valid cases?

Examples:

- Floating body: not sleeping, below speed threshold, no terrain/object contact, positive terrain gap.
- Impossible rest state: awake body, near-zero velocity, no support contacts.
- Penetration: contact manifold reports penetration above tolerance after solver step.
- Energy spike: total kinetic/potential energy increases above tolerance in a no-energy-source scene.
- Edge-rest box: awake box, low motion, only one or two terrain-supported vertices, no box-box contacts.

## Runbook

1. Build the target configuration:

```bat
tools\validate_build.bat Profile
```

2. Start the capture command from the repo root:

```powershell
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData/scenes/standing_box_repro.scene --seed 4096348761 --no-water
```

3. Monitor only the log while the loop runs:

```powershell
Get-Content Debug\standing_box_repro.log -Wait
```

4. Stop the test when the app exits on first hit.
5. Use the logged replay command and snapshot fields to debug in `Debug` or under CDB.

The log may be recreated at each run start depending on the detector. Preserve hit artifacts before rerunning the same path.

## Current Example: Edge-Rest Box

Example command:

```powershell
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData/scenes/standing_box_repro.scene --no-water --seed 4096348761
```

Edge-rest predicate:

- target is a box,
- target is not sleeping,
- speed squared is below `0.25`,
- angular speed squared is below `0.09`,
- terrain support count is `1..2` vertices,
- no contact manifold against any other box.

Known hit from the first capture:

- seed: `4096348761`
- model index: `38`
- supported vertices: `2`
- min terrain gap: `0.042717`
- max terrain gap: `3.013268`
- sleeping: `0`

The important lesson from this example is not the box-specific predicate. It is the reusable workflow: predicate, seed, unattended loop, snapshot, stop-on-hit, replay.

## Validation

For docs or detector documentation changes:

```bat
tools\validate_fast.bat
```

For new or changed detector code that touches `SkullbonezRun*` or physics behavior, use the stricter validation required by `AGENTS.md`; when unsure, run:

```bat
tools\validate_full.bat
```
