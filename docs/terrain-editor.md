# Terrain editor

Open **Tools > Editor**, enable **Editor**, then enable **Terrain brush**.
Move the brush over the viewport. The mouse wheel changes its radius;
hold the left mouse button to raise terrain or the right button to lower it.
The brush fades smoothly at its edge. Controls over Tools do not sculpt.
The visible mesh, collision surface and ray-traced geometry update together.

Use **Scene > Save Defaults** to save the level and any sculpted height map.
An untouched flat level needs no map file. A sculpted map is stored beside
its level as a signed, full-precision `.heightmap` file. Saving unchanged
heights reuses that file. Previous map versions remain available so a failed
level save cannot invalidate its previous terrain reference.

To create a level, type its name in the Scene dropdown. **Create new scene**
starts flat. **Create from height map...** lets you choose an existing
`.heightmap` (2 to 257 posts per side, spacing at least 0.001) or a legacy 256 x 256 unsigned-byte `.raw` map. Imported files
are referenced in place and remain unchanged. Sculpting an import writes
a separate map beside the new level when you save.

Terrain strokes reserve their storage during scene loading. The editor
reuses the collision grid, render mesh and DXR workspace while sculpting.
An unresolved Modify Velocity comparison must be accepted before sculpting;
its two futures share the terrain that existed when they were created.

Regression coverage: `python tools/validate_terrain_editor.py --session
TestOutput/skarness/terrain-editor-check` runs native brush controls, UI
exclusion, save/reload, unchanged-map reuse, flat creation, import and
cancellation. Session events and screenshots are preserved under that path.
