# Split Future

Open `split_future` in the scene browser, or run from the repository root:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData/scenes/split_future.scene.json
```

This is an interactive physics scene inspired by cover concept 11.
The ball and cube use physical material shading, a shared warm/cool procedural
environment, coating detail, and a rounded cube mesh. A shallow wet surface
reflects the actual scene over textured terrain. The ball and cube start above
the ground, fall, bounce, collide and rotate.
The old fixed trace geometry has been removed so it cannot block interaction.

The scene starts with its own camera and unlimited playback; Physics is on.
Red/yellow panel edges use pixel-footprint filtering to suppress staircase edges.

Controls:

- Choose **Manipulator** from the camera/tool selector, then left-drag an object
  to pick it up and move it.
- Press **N** for Launcher; **M** switches between laser impulse and projectile.
  Aim with the camera and left-click to fire. Press N again to leave Launcher.
- Press **R** to restart the drop. Reset retains objects you launched; switch
  to another scene and back to Split Future to return to the original two objects.

No existing scene or global engine setting was changed. Styles 22 (sky), 16
(terrain), 14 (objects), and 5 (water) opt into this look. Ordinary rendering and
previous cinematic styles retain their existing paths. The rounded cube's
collision shape remains a box; the bevel is a visual treatment.

The shared sky is procedural. Diffuse illumination uses a low-frequency
approximation of that environment; glossy reflections use a roughness filter.
Ground reflections use the existing planar reflection pass.

The combined review branch is `feature/split-future`.
The stacked branches preserve each implementation stage and follow-up:

1. `feature/split-future-01-pbr`
2. `feature/split-future-02-environment`
3. `feature/split-future-03-surfaces`
4. `feature/split-future-04-wet-ground`
5. `feature/split-future-05-finish`
6. `feature/split-future-06-smooth-edges`
7. `feature/split-future-07-interactive`

Run the native scene-isolation check with:

```bat
python tools\validate_split_future.py
```

It checks both hero identities, falling motion, ground contact, selection,
a projectile fired through player input, pointer dragging, reset/reload, and a
ten-second run. Ordinary and cinematic control scenes are compared by settings,
camera and world pixels before and after visiting this scene. Captures and
results are written beneath `TestOutput/skarness/split-future-interactive`.
