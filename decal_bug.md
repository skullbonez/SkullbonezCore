# Bug: Shadow Decals Cut Through Water Surface

## Symptom
Shadow decals (oval ground shadows projected beneath each ball) are visible bleeding
through the water surface in areas where the terrain is below the water plane.

## Repro
`decal_bug.bmp` already exists in the repo root — **look at it before doing anything else** to understand what the bug looks like.

To regenerate a fresh screenshot after making changes:
```bat
Debug\SKULLBONEZ_CORE.exe --scene decal_bug.scene
```
The game will render one frame, save `decal_bug.bmp` to the repo root, and exit.

**After running, check the file timestamp on `decal_bug.bmp` to confirm it was actually updated — do not inspect a stale image.**

## Key Code
- Shadow rendering: `SkullbonezSource/SkullbonezGameModelCollection.cpp` — `RenderShadows()`
- Shadow call site: `SkullbonezSource/SkullbonezRun.cpp` — `DrawPrimitives()`
- Water surface height: `m_cWorldEnvironment.GetFluidSurfaceHeight()` (returns `25.0` in this scene)
- Shadow offset config: `Cfg().shadowOffset`

## Attempted Fix (Reverted)
Added a `waterSurfaceY` parameter to `RenderShadows()` and skipped rendering any shadow where `groundY < waterSurfaceY`. Build passed but the fix did not work visually. All three files were reverted to original.
