# SkullbonezCore — Known Bugs

## TECH DEBT



## TODO: Collapse skybox rendering to one draw call

The DX12 draw-call trace reports six skybox draws because `SkyBox::Render`
loops over six separate face meshes and binds one 2D face texture per draw.
Replace the six face draws with one skybox draw, preferably using a cube texture
or equivalent single-resource layout, so the main view and reflection pass each
submit one skybox draw instead of six.


## VISUALS

## Blue skybox is driving me nuts with its white clouds.


## TODO Command line args

I never want to type this again: --scene SkullbonezData/scenes/stacking.scene.json
Should simply be --scene stacking or --suite myTests



## RUNTIME BUGS

## TODO: Chase camera is 90 degrees out

The chase camera orientation is rotated 90 degrees from the expected follow
view. Investigate the camera basis/target transform used by chase mode and
restore the intended forward alignment.

## TODO: Launcher mode should not require right click

Launcher mode still needs right click before shooting. That input requirement
blocks firing and should be removed so launcher mode can shoot without the
extra right-click gate.


## TODO: Profiler tree accounting hides unbucketed physics time

The profiler overlay can make the `Frame/Physics` row look wrong: the parent
time is inclusive, but the visible child rows do not necessarily sum to the
same value because some work inside the physics scope is not represented as a
direct `Frame/Physics/...` child row. In practice this makes the missing time
appear to belong to whichever nearby row is expanded or visually adjacent, and
can make physics markers look like they live under `VsyncWait` instead of under
`Physics`. Rework the profiler tree/accounting so scoped hierarchy and
slash-delimited marker paths agree, then add explicit direct buckets for physics
setup, solver, sleep-support propagation, terrain, and integration subwork so
the parent total can be explained from the visible rows.


## PHYSICS BUGS

## TODO: Catto debug visuals

UI next prev buttons busted, should be a combo box anyway, also I want to create a scene that shows a collision with a catto step by step

## BUG - Trees - if you shoot any part of them they need to wake, we need a concept of an entire tree to fix this sort of thing.  A tree class.
