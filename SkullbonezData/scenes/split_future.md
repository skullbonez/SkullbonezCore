# Split Future

Open `split_future` in the scene browser, or run from the repository root:

```bat
tools\launch_split_future.bat
```

This is an interactive physics scene inspired by cover concept 11.
The ball and cube use physical material shading, a shared warm/cool procedural
environment, coating detail, and a rounded cube mesh. A shallow wet surface
reflects the actual scene over textured terrain. The ball and cube start above
the ground, fall, bounce, collide and rotate.
The old fixed trace geometry has been removed so it cannot block interaction.

The launcher enables interactive mode and replay recording so the replay controls
and timeline are available. The scene starts with its own camera and unlimited
playback; Physics is on.
Red/yellow panel edges use pixel-footprint filtering. SMAA 1x High smooths the
world after tone mapping, before the UI. Only object style 14 enables SMAA.
For an identical-scene A/B capture, set `SKULLBONEZ_SMAA=off` before launching;
unset it to restore SMAA. This diagnostic override lasts only for that process.

Controls:

- Choose **Manipulator** from the camera/tool selector, then left-drag an object
  to pick it up and move it.
- Press **N** for Launcher; **M** switches between laser impulse and projectile.
  Aim with the camera and left-click to fire. Press N again to leave Launcher.
- Choose **Scene** for continuous playback. **Inspect** intentionally pauses
  physics; hold Space to advance there, or press F to return to Scene.
- Press **R** to restart the drop. Settled bodies wake up, and the ball
  regains its authored velocity and spin. Reset retains objects you launched; switch
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
8. `feature/split-future-08-startup-shader`
9. `feature/split-future-09-replay-reset`
10. `feature/split-future-10-smaa`
11. `feature/split-future-11-precision-lines`

Run the native scene-isolation check with:

```bat
python tools\validate_split_future.py
```

It checks both hero identities, falling motion, ground contact, selection,
a projectile fired through player input, pointer dragging, reset/reload, and a
ten-second run. Ordinary and cinematic control scenes are compared by settings,
camera and world pixels before and after visiting this scene. Captures and
results are written beneath `TestOutput/skarness/split-future-interactive`.

Focused replay/reset and SMAA checks:

```bat
python tools\validate_split_future_replay.py TestOutput/skarness/split-replay
python tools\validate_split_future_smaa.py --session TestOutput/skarness/split-smaa
```

The replay check settles both bodies to sleep, presses R, and verifies natural
playback and replay seeking. The SMAA check captures identical off/on states,
checks the pass graph and resized targets, and compares silhouette and coating
pixels. These tests use the Automation build.

Direction A (precision glow) styles this scene's replay paths with narrow
antialiased cores and restrained halos. The default root path is warm gold;
contact outlines remain cyan, while ending-pose and resting ghosts are dimmer.
Occluded geometry retains a faint hint. The presentation consumes the existing
prediction packet without changing its paths, contact poses, timing or caches.
Other object styles retain the original overlay presentation.

Press **F7** anywhere in the main scene views to toggle the Split Future look.
The shortcut is available in every camera mode, including with the UI hidden.
Holding the key toggles once. The session-wide override survives scene changes
and R resets, applies HDR/style modes 22/16/14/5 (including SMAA and precision
lines), and leaves authored lighting and saved scene/default files untouched.
Switching off restores an ordinary or other cinematic scene's own settings.
In an already-authored Split Future scene, switching off uses ordinary rendering
so the first press provides an actual before/after comparison. Restarting the
application returns to the scene's authored presentation.

Follow-up branch: `feature/split-future-12-f7-toggle`.
