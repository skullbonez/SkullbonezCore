# Split Future

Open `split_future` in the scene browser, or run from the repository root:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData/scenes/split_future.scene.json
```

This is a static lighting and material showcase inspired by cover concept 11.
The ball and cube use physical material shading, a shared warm/cool procedural
environment, coating detail, and a rounded cube mesh. A shallow wet surface
reflects the actual scene over textured terrain. Thin emissive traces and echo
rings are authored scene decoration, not recorded or predicted physics paths.

The scene starts with its own camera and unlimited playback; Physics is off.
No existing scene or global engine setting was changed. Styles 22 (sky), 16
(terrain), 14 (objects), and 5 (water) opt into this look. Ordinary rendering and
previous cinematic styles retain their existing paths. The rounded cube's
collision shape remains a box; the bevel is a visual treatment.

The shared sky is procedural. Diffuse illumination uses a low-frequency
approximation of that environment; glossy reflections use a roughness filter.
Ground reflections use the existing planar reflection pass.

The combined review branch is `feature/split-future`.
The five stacked branches preserve each implementation stage:

1. `feature/split-future-01-pbr`
2. `feature/split-future-02-environment`
3. `feature/split-future-03-surfaces`
4. `feature/split-future-04-wet-ground`
5. `feature/split-future-05-finish`

Run the native scene-isolation check with:

```bat
python tools\validate_split_future.py
```

It checks both hero identities, resets, a ten-second run, and round trips
through ordinary and cinematic control scenes. It compares their settings,
cameras and world pixels before and after visiting this scene. Captures and
results are written beneath `TestOutput/skarness/split-future-final`.
