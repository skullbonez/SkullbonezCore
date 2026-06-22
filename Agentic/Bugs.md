# SkullbonezCore — Known Bugs

## TECH DEBT



## TODO: Collapse skybox rendering to one draw call

The DX12 draw-call trace reports six skybox draws because `SkyBox::Render`
loops over six separate face meshes and binds one 2D face texture per draw.
Replace the six face draws with one skybox draw, preferably using a cube texture
or equivalent single-resource layout, so the main view and reflection pass each
submit one skybox draw instead of six.



## TODO Command line args

I never want to type this again: --scene SkullbonezData/scenes/stacking.scene.json
Should simply be --scene stacking or --suite myTests



## RUNTIME BUGS

## TODO: Cinematic render mode crashes when drawing physics debug lines

Trying to draw physics/debug overlay lines while cinematic render mode is active
can crash in the DX12 line drawing path. The observed failure happens around the
debug-line submit path, with stack context involving `DrawLinesColored(...)` and
the final `DrawInstanced(...)` call. Do not treat this as a physics solver bug;
the line data is only the trigger. The likely issue is that the physics debug
line pass is being submitted with render target, depth target, command-list, or
resource-state assumptions that are valid for the normal backbuffer path but not
for the cinematic HDR/post-process target path.

Fix instructions:

1. Reproduce with a cinematic scene and a physics debug overlay enabled, for
   example cinematic mode plus axes, contacts, terrain, or pipeline debug lines.
2. Break on the first DX12 validation message or crash and confirm which target
   is bound immediately before `DrawLinesColored(...)` submits its draw.
3. Compare the non-cinematic debug-line render path against the cinematic frame
   path. Check the active RTV/DSV, viewport/scissor, depth availability, root
   signature, PSO, descriptor heaps, upload lifetime, and resource transitions.
4. Decide whether physics debug lines should be drawn into the cinematic scene
   target before tonemapping, or drawn after the cinematic resolve as a final UI
   overlay. Route the pass through that contract explicitly instead of relying
   on whatever target happened to be active.
5. Add a narrow diagnostic or assertion near the debug-line submit path that
   names the active render target mode and rejects missing RTV/DSV or invalid
   resource state before reaching `DrawInstanced(...)`.
6. Validate the eventual fix with `tools\validate_dx12_renderer.bat`, and
   manually launch one cinematic scene with each relevant physics debug overlay
   enabled long enough to confirm there are zero DX12 validation errors.

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


## Bit of a bug in ALT key edit mode switching...
Mouse strangeness needs debugging
