# Floating Box Sleep Handoff

## Status

Box stacking is solved enough to commit first. The three-box `stacking.scene`
forms a stable stack, reaches sleep, and stays still through the scene end.

Do not fold the remaining `at_rest.scene` issue into the stacking change. Treat
it as the next physics bug.

## Next Bug

`SkullbonezData/scenes/at_rest.scene` can leave a box asleep while it is visibly
in the air after object-object collisions. A later ball impact can move it, but
the box can still remain effectively frozen instead of returning to normal
falling/resting behavior.

The bug is not that the scene needs more damping. The box is sleeping when it
should not sleep.

## Hard Constraint

NO DAMPING ALLOWED.

Do not fix this by reducing restitution, increasing drag, changing gravity,
changing scene timings, changing masses, or otherwise hiding the bad sleep state
with energy loss. Those are hacks for this bug. The fix belongs in sleep/contact
eligibility.

## Likely Cause

`m_groundedThisFrame` is currently used as a broad sleep eligibility proxy in
`SkullbonezGameModelCollection::RunSolverPhysics`.

Terrain contact sets it, but persistent object contacts also set it for both
bodies when the contact normal is mostly vertical:

```cpp
if ( fabsf( manifold.normal.y ) > 0.25f )
{
    m_groundedThisFrame[aIndex] = 1;
    m_groundedThisFrame[bIndex] = 1;
}
```

That is too blunt. A vertical-ish mid-air collision between two dynamic objects
can make a body look "grounded enough" for the sleep counter even though no
terrain or stable stack support exists.

## Reproduction

Use Debug so the physics CSV is emitted:

```bat
tools\validate_build.bat Debug
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --scene SkullbonezData/scenes/at_rest.scene --physics-log Debug/at_rest_float_repro.csv
```

The scene has `exit_on_complete`, so it should terminate automatically.

Watch the visual scene and inspect the CSV for any box with:

- `sleeping=1`
- no credible terrain/object support
- no falling response after being moved by another object

The most useful fields are `frame,name,posX,posY,posZ,speed,omegaMag,grounded,sleeping,sleepInhibited`.

## Failed Experiment To Avoid

A previous attempt added a separate `m_sleepSupportedState` propagation path and
changed box-vs-terrain detection to probe all OBB vertices directly against
terrain. That caused bad regressions: slippage returned, jitter increased, and
objects developed odd late motion. Do not resurrect that approach wholesale.

If terrain support needs better proof, keep it small and verify against both:

- `SkullbonezData/scenes/stacking.scene`
- `SkullbonezData/scenes/at_rest.scene`

## Suggested Direction

Keep `m_groundedThisFrame` only if it is reinterpreted as "sleep support this
frame", not merely "had a contact".

A plausible fix:

1. Terrain contact can make a body sleep-eligible when the terrain response says
   the contact supports sleep.
2. Object contact should only make a body sleep-eligible if the contact is a true
   support contact against gravity and the supporting body is itself already
   terrain-supported or part of a proven supported stack.
3. Side contacts, mid-air impacts, and contacts between two unsupported dynamic
   bodies must not increment the sleep counter.
4. A sleeping body that receives a real moving collision must wake and resume
   gravity/integration.

Do not compromise the stable stacking result while fixing this.

## Current Acceptance Split

Commit the stable stacking work first. Then fix the floating box in a follow-up
change with `at_rest.scene` as the primary regression scene.
