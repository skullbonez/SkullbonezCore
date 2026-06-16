# SkullbonezCore — Known Bugs

## Remove YAML from orchestrator

JSON only.  Single source of truth

## TECH DEBT

## TODO: Collapse skybox rendering to one draw call

The DX12 draw-call trace reports six skybox draws because `SkyBox::Render`
loops over six separate face meshes and binds one 2D face texture per draw.
Replace the six face draws with one skybox draw, preferably using a cube texture
or equivalent single-resource layout, so the main view and reflection pass each
submit one skybox draw instead of six.

## TODO: Extract Cine UI tab from InGameUI facade

The UI code cleanup plan was completed and validated, but the newer Cine UI
controls were added inline in `SkullbonezSource/UI/SkullbonezUI.cpp`. Move the
Cine slider specs, feature toggle specs, scene-mode combo handling, content
height, input handling, and drawing into a dedicated `UITabCinematic.h/.cpp`
module so `InGameUI` stays a coordinator instead of growing tab-specific logic
again.

## DONE: Rewrite CLI argument parsing in SkullbonezInit.cpp

`SkullbonezInit.cpp` now tokenizes the raw Windows command-line string into an argv-style token list, supports `--flag=value` and `--flag value`, and dispatches flag/value options through small directive tables. Remaining CLI work should be tracked as specific feature requests, such as friendly scene aliases or richer help output, rather than another wholesale parser rewrite.

## TODO: CPU/GPU spike histogram overlay

The existing profiler overlay shows rolling averages and bar graphs of frame time, but gives no visibility into outlier frames — a single 50ms spike is invisible when averaged into a 60fps rolling window. Add a spike histogram: a fixed-width ring buffer of the last N frame times (CPU and GPU separately), rendered as a scrolling bar graph in the HUD overlay. Each bar is one frame; bars exceeding the frame budget (e.g. 16.6ms at 60Hz) are drawn in a distinct colour. This would make hitching, GC-style stalls, and DX12 pipeline bubbles immediately visible without needing to export a perf CSV and analyse it offline.

## TODO Command line args

I never want to type this again: --scene SkullbonezData/scenes/stacking.scene
Should simply be --scene stacking or --suite myTests

## TODO: Clock speeds

Why is DX tests running slow and GL fast?

## TODO: Physics text file

20K lines for our physics regression test...  This is overkill.  We should just keep a line at a 60hz rate.

## TODO: Scene reset

Allow scene reset on press of R key

## RUNTIME BUGS

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

## TODO: Add profiler visual mode that excludes VSync wait

When `VsyncWait` is large, it consumes almost the entire profiler bar scale and
makes the real CPU work visually tiny even when those rows have useful timing
differences. Add an overlay option such as `Exclude VSync from visual scale` so
the numeric `VsyncWait` row can remain visible while the bar normalization uses
frame time minus wait time. The toggle should affect only the visual bar scale,
not the recorded timings, exported data, or inclusive frame totals.

## DONE: Body alpha slider does not reliably affect debug bodies

The Physics tab `Body alpha` control has been fixed and verified end-to-end from
mouse drag to `physicsDebugAlpha` to visible body transparency.

## DONE: Camera-fired bullets are inconsistent

Nudge/free-mode left-click bullets now clear stale persistent contact cache rows
when recycled, so previous warm-start impulses cannot follow a teleported silver
bullet into its next shot.

## DONE: Physics tab slider hitboxes are vertically offset

Physics tab slider hitboxes have been fixed and verified against rendered
track/thumb bounds.

## TODO: Fix ugly mouse cursor


## PHYSICS BUGS

## TODO: Catto debug visuals

UI next prev buttons busted, should be a combo box anyway, also I want to create a scene that shows a collision with a catto step by step

## TODO: DX12 physics difference

Check regression logs on DX12 - it looks different.

## TODO: Fix stacking

Boxes stacked on top of each other (see `stacking.scene`) tend to drift or
topple slowly over several hundred frames rather than reaching a rock-solid
rest. The solver converges for individual resting contacts but multi-body
stacks expose gaps in the constraint ordering and lack of warm-starting across
frames. Likely needs persistent contact caching (warm-start accumulated
impulses from the previous frame) and/or position-stabilisation correction
applied to the full stack chain, not just individual contact pairs.

## TODO: Fix balls contact resting state

Balls resting on terrain (see `at_rest.scene`) exhibit a visible micro-bounce
or jitter before the sleep threshold kicks in. The ImpulseSolver's restitution
path applies a small bounce impulse even at near-zero normal velocity, keeping
the ball alive longer than it should be. The fix is to gate restitution behind
a minimum separation velocity (typically 1–2 × the Baumgarte bias speed) so
that low-energy contacts go straight to the resting / positional-correction
path rather than bouncing. The same issue affects box-on-terrain resting.

## TODO: Box-ball interpenetration

Interpenetration can be seen during ball settle time between box and ball around frame 570 of at_rest.scene
