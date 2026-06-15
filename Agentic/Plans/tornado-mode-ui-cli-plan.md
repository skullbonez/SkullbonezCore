# Tornado Mode UI And CLI Plan

Status: planning draft  
Created: 2026-06-12  
Scope: physics force field, main demo runtime controls, command-line argument, scene/demo defaults, optional visual debug  
Implementation status: plan only, no code changes in this pass

## Goal

Add a tornado mode that can be turned on while running the main generated demo scene, and can also be enabled at startup with a command-line argument.

When enabled, nearby dynamic balls should be pulled into a vertical vortex, orbit around the funnel, climb or dip in the column, collide with each other naturally, and occasionally get flung out before possibly being captured again.

The first implementation should prioritize deterministic physics behavior and runtime control. Visual polish can follow once the motion feels good.

## User-Facing Contract

### Runtime UI

The main demo scene should expose a simple tornado control in the in-game UI.

Recommended first UI surface:

- Add a `Tornado` toggle to the Physics tab.
- Keep the existing Physics tab layout style: checkbox/toggle first, sliders only after the core behavior works.
- When toggled on, the main generated demo uses a default vortex centered in the terrain bowl.
- When toggled off, the force field stops immediately, but existing ball velocities remain physical.

Nice second-pass controls:

- `Strength` slider: scales inward pull, swirl steering, and lift together.
- `Radius` slider: changes capture radius around the tornado axis.
- `Ejection` slider or toggle: controls how often balls get flung outward.

### Command Line

Add startup arguments:

```bat
Profile\SKULLBONEZ_CORE.exe --tornado
Profile\SKULLBONEZ_CORE.exe --tornado on
Profile\SKULLBONEZ_CORE.exe --tornado off
```

Optional later tuning arguments:

```bat
Profile\SKULLBONEZ_CORE.exe --tornado --tornado-strength 1.35
Profile\SKULLBONEZ_CORE.exe --tornado --tornado-center 500 35 500 --tornado-radius 180
```

Recommended behavior:

- Bare `--tornado` means `on`.
- CLI state seeds the initial runtime state.
- UI can still toggle tornado on/off after launch unless a later explicit lockout mode is added.
- The option applies to the main generated demo and scene-loaded runs unless a scene directive explicitly overrides it.

## Target Architecture

### Tornado Field State

Add a small config/state object rather than scattering scalar fields:

```cpp
struct TornadoFieldConfig
{
    bool enabled = false;
    Math::Vector::Vector3 center = Math::Vector::Vector3( 500.0f, 35.0f, 500.0f );
    float radius = 180.0f;
    float height = 260.0f;
    float inwardAcceleration = 90.0f;
    float swirlSpeed = 130.0f;
    float liftAcceleration = 55.0f;
    float captureBlend = 0.25f;
    float ejectAcceleration = 260.0f;
    float ejectBand = 0.78f;
};
```

The exact numbers should be tuned in Profile builds, but the shape should stay clear:

- `inwardAcceleration`: pulls bodies toward the vortex axis.
- `swirlSpeed`: desired tangential orbit speed.
- `liftAcceleration`: upward flow inside the column.
- `captureBlend`: how strongly velocity is steered toward orbit each tick.
- `ejectAcceleration`: outward burst for fling-outs.
- `ejectBand`: upper-column height fraction where ejection becomes likely.

### Runtime Ownership

Recommended owner:

- Store active tornado runtime state in `RunRuntimeSettings` or a nearby runtime state struct in `SkullbonezRun.h`.
- Pass the active `TornadoFieldConfig` to `GameModelCollection`.
- Let `GameModelCollection` apply the field during physics ticks.

Reasoning:

- UI and CLI live in the run layer.
- Body iteration and sleep state live in `GameModelCollection`.
- The field must run inside fixed-step physics, not as render-frame logic.

### Physics Insertion Point

Apply tornado forces in `GameModelCollection::RunSolverPhysics(float dt)` after regular world forces and before broadphase.

Current nearby flow:

1. Apply world forces to awake models.
2. Build broadphase grid using updated velocities.
3. Run narrowphase and terrain detection.
4. Solve persistent contacts.
5. Integrate remaining time.

Target flow:

1. Apply world forces to awake models.
2. Apply tornado field to affected dynamic balls.
3. Build broadphase grid using tornado-adjusted velocities.
4. Continue existing collision, terrain, solver, and integration flow.

This keeps swept collision and broadphase honest: a ball accelerated by the tornado uses its new velocity for collision candidates in the same tick.

## Force Model

For each non-fixed dynamic ball inside the tornado cylinder:

1. Compute horizontal offset from the vortex axis.
2. Ignore bodies outside `radius` or outside `height`.
3. Wake sleeping bodies inside the active field.
4. Pull inward with falloff stronger near the center but clamped at the core.
5. Steer tangential velocity toward a desired orbit speed.
6. Add upward lift that fades near the top.
7. Occasionally add deterministic outward ejection near the upper band.

Pseudo-shape:

```cpp
Vector3 toAxis = Vector3( center.x - pos.x, 0.0f, center.z - pos.z );
float r = Length( toAxis );
float radial01 = Clamp01( r / config.radius );
float height01 = Clamp01( ( pos.y - config.center.y ) / config.height );

Vector3 inward = NormalizeSafe( toAxis );
Vector3 tangent = Vector3( -inward.z, 0.0f, inward.x );

float coreFalloff = SmoothStep( 1.0f, 0.0f, radial01 );
float columnMask = SmoothStep( 0.0f, 0.12f, height01 ) * SmoothStep( 1.0f, 0.78f, height01 );

Vector3 desiredTangential = tangent * config.swirlSpeed * columnMask;
Vector3 currentHorizontal = Vector3( vel.x, 0.0f, vel.z );
Vector3 steer = ( desiredTangential - currentHorizontal ) * config.captureBlend;

Vector3 acceleration =
    inward * config.inwardAcceleration * coreFalloff +
    steer +
    Vector3( 0.0f, config.liftAcceleration * columnMask, 0.0f );
```

Implementation detail to decide during coding:

- Either add this as an explicit velocity delta, `vel += acceleration * dt`, then clamp.
- Or convert it into a force-like value consistent with `WorldEnvironment::AddWorldForces`.

The explicit `deltaV` path is easier to tune and avoids overloading the existing gravity/fluid world force code. If chosen, keep clamps tight and document that tornado mode is a gameplay force field, not a collision impulse.

## Ejection Behavior

The fling-out should be deterministic and stateful, not random per frame.

Recommended approach:

- Add retained per-body tornado state arrays in `GameModelCollection`.
- Track time spent captured and a cooldown.
- When a body reaches the upper `ejectBand`, has enough tangential speed, and is off cooldown, add one outward/upward velocity burst.
- Derive variation from stable body index and capture time bucket, not wall-clock randomness.

Example policy:

- Captured for at least `0.65s`.
- Height fraction above `0.78`.
- Cooldown finished.
- Eject every Nth qualifying body based on `bodyIndex % 3`, so not all balls leave together.

This gives the requested “some getting flung out” behavior without making validation flaky.

## Sleep Rules

Existing physics skips sleeping bodies during force application. Tornado mode must wake affected bodies explicitly.

Rules:

- If tornado is enabled and a dynamic body is inside the capture cylinder, wake it.
- Reset its sleep counter.
- Keep fixed/floating objects immune.
- Consider applying to spheres first; boxes can be a later option if the demo wants debris.

This is necessary for the main demo: otherwise settled balls near the terrain can remain asleep while the tornado is visibly active.

## UI Implementation Steps

1. Add a `toggleTornado` field to `UIPhysicsCommands`.
2. Add one more checkbox in `UIPhysicsTabState`.
3. Draw the `Tornado` toggle in `UITabPhysics.cpp`.
4. Consume `uiCommands.physics.toggleTornado` in `SkullbonezRunInput.cpp`.
5. Store active state in the run layer.
6. Include the active tornado state in `InGameUIFrameData` so the checkbox reflects reality.

Keep the first UI pass small. Sliders can come after the default tornado feels good.

## CLI Implementation Steps

1. Add command-line parsing for `--tornado`, accepting bare, `on`, and `off`.
2. Add a runtime field such as `m_cmdHasTornadoOverride` and `m_cmdTornadoEnabled`.
3. During generated demo setup and scene load, initialize active tornado state from the CLI override.
4. If no CLI override exists, default tornado off.
5. Document the argument in `Agentic/Reference/runtime-reference.md` after implementation.

Optional later:

- `--tornado-strength <float>`
- `--tornado-radius <float>`
- `--tornado-center <x> <y> <z>`
- `--tornado-no-eject`

## Main Demo Defaults

Default tornado values should be tuned for the existing generated demo object range:

- Center around terrain/demo center: approximately `(500, 35, 500)`.
- Radius large enough to catch settled demo balls, around `160-220`.
- Height high enough for visible vertical orbiting, around `220-300`.
- Swirl strong enough to create broad circular motion without exceeding CCD expectations.
- Lift strong enough to visibly suspend some balls, not all balls permanently.

If the main demo random spawn creates too many out-of-range objects, add one of these:

- A wider default capture radius for generated demo only.
- A gentle long-range inward “storm wind” outside the core radius.
- A UI/demo preset that spawns all balls nearer the bowl when tornado is on.

## Optional Scene Directive

Even though this request is UI and CLI focused, a scene directive would make test scenes easy:

```text
tornado on 500 35 500 180 260 90 130 55 0.25 260
```

Recommended interpretation:

`tornado <on|off> <centerX> <baseY> <centerZ> <radius> <height> <inward> <swirl> <lift> <capture> <eject>`

Scene directive precedence should be:

1. CLI override, if explicitly provided.
2. Scene directive.
3. Default off.

## Visual Debug Follow-Up

After physics works, add a small visual indicator:

- A translucent vertical cylinder or spiral line overlay.
- Debug-only first, tied to physics debug or tornado enabled state.
- No shader-heavy vortex until behavior is tuned.

If a production visual effect is added, renderer validation becomes required because DX12 screenshot baselines and validation logs must stay clean.

## Validation Plan

Documentation-only plan changes require no validation.

For implementation, PR-bound validation should be:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Add this if visual tornado rendering or shader work is included:

```bat
tools\validate_dx12_renderer.bat
```

Manual smoke launches during development:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --tornado --interactive
```

Use targeted launches only while iterating. The formal validation scripts remain PR/commit gates.

## Risks And Guardrails

- High tornado acceleration can cause CCD misses or broadphase overload. Clamp per-tick velocity deltas.
- Waking too many bodies every tick can erase sleep-policy performance wins. Keep the active field radius bounded.
- Ejection must be deterministic. Avoid wall-clock random, frame-rate random, or unordered container iteration.
- UI toggling should not reset the scene. It should mutate live runtime state.
- Command-line `--tornado` should not force cinematic rendering, renderer choice, sleep policy, or object counts.

## Suggested First Slice

1. Add CLI `--tornado`.
2. Add run-layer tornado state.
3. Add `GameModelCollection::SetTornadoField`.
4. Apply deterministic velocity-field behavior to balls.
5. Add the Physics-tab `Tornado` checkbox.
6. Tune default values in the main generated demo.
7. Add docs for the command-line argument.
8. Run PR-gate physics and perf validation before committing implementation.
