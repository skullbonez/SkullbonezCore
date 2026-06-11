# Low-Poly Blue-Sky Handoff - 2026-06-11

## Current Branch

- Branch: `codex/concept-cine-scenes`
- Visual rollback commit: `e312541 style: restore low-poly blue-sky baseline`
- Preferred visual anchor: `dc75bac style: strengthen low-poly sky clouds`
- Current screenshot: `Agentic/sceneshots/style_12_low_poly_art_style.png`

## User Direction

The user asked to go back to `dc75bac` because they liked the blue sky. Treat that commit as the low-poly art direction anchor for the next pass.

This was implemented as a forward commit, not a history rewrite. The rollback restored the low-poly visual assets from `dc75bac` while preserving later runtime work such as the `--hero` launch flag.

## Preserved Runtime Work

Keep the hero entry point:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --hero
```

This should boot into the low-poly hero scene and keep the simulation running. The user specifically wants the hero scene usable with balls and cubes bouncing.

## Visual Baseline

The restored visual state includes the blue/pink low-poly sky from `dc75bac`. Later experiments with warmer terrain, muted water, and the broad skybox mountain band were reverted because they drifted away from the preferred screenshot.

Do not reintroduce the flat horizontal mountain band that was present in `0caf32a`. If mountains come back, they must be part of the skybox, subtle, and integrated into the procedural sky rather than placed as scene props.

## Style Constraints

- Keep the low-poly style modular and data-driven through `.style` files.
- Do not move low-poly look settings back into hardcoded `ParseLook`-style logic.
- Do not change the heightmap multiplier by default. The low-poly scene should keep the current world scale settings, including the `1.0` height multiplier.
- Keep red/yellow texture treatment on game objects in the low-poly style:
  - balls
  - boxes
  - hero objects
- Preserve the fixed trees and fixed decorative objects. The user likes their shapes and colors.
- Fixed objects must remain physical collision participants so balls bounce off them.

## Files Restored From `dc75bac`

- `Agentic/sceneshots/style_12_low_poly_art_style.png`
- `SkullbonezData/shaders/sky_atmosphere.frag`
- `SkullbonezData/shaders/sky_atmosphere.hlsl`
- `SkullbonezData/shaders/water_calm.frag`
- `SkullbonezData/shaders/water_calm.hlsl`
- `SkullbonezData/styles/low_poly_art_style.style`

## Smoke Commands Used

The user requested no full validation until the end of the iteration cycle, so renderer validation was intentionally deferred.

Smoke capture used:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene TestOutput\style_12_low_poly_skybox_world_normal.scene
python tools\export_screenshot_png.py TestOutput\style_12_low_poly_skybox_world_normal.bmp Agentic\sceneshots\style_12_low_poly_art_style.png
```

Earlier, the hero boot path was smoke-tested with:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --hero
```

## Final Validation To Run Later

When the user calls for final validation, run renderer validation at minimum because style shaders and render output changed during the larger session:

```bat
tools\validate_renderers.bat
```

Because this branch also touched launch/runtime code for `--hero`, a final broad pass is likely appropriate before declaring the whole branch done:

```bat
tools\validate_full.bat
```

## Known Local State

At the time this handoff was written, `Agentic/Plans/pix-profiling-integration-plan.md` existed as an unrelated untracked local file. It was not included in the low-poly rollback or this handoff commit.

## Suggested Next Pass

Continue from the restored blue-sky screenshot. If adding mountains again, implement them as a skybox or sky shader layer only, and keep them gentle enough that the blue sky remains the first visual read. Use the screenshot harness to tune style parameters live, then update `Agentic/sceneshots/style_12_low_poly_art_style.png` when the look improves.
