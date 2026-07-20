# ImGui + Tracy E17 Owner Playtest

Build: `Profile\SKULLBONEZ_CORE.exe` from the final E17 source on branch
`nightrunner-21st-july` at commit `d4c8088d`.

Time: about 10 minutes. Legacy and ImGui are alternatives; do not launch two
engine processes for comparison. `Ctrl+0` swaps the one live process atomically.

## 1. Confirm the unchanged default

```powershell
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --replay on --replay-seconds 4 --scene SkullbonezData/scenes/stacking.scene.json --interactive on
```

Expected: ImGui does not appear. Legacy is the selected implementation. The
scene's authored Legacy visibility is still respected; press plain `0` if the
floating Legacy window starts hidden. Plain `0` only minimizes/restores Legacy.

## 2. Compare explicit Legacy and ImGui in one process

Start with the full Legacy surface forced visible:

```powershell
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --dev-ui legacy --replay on --replay-seconds 4 --scene SkullbonezData/scenes/stacking.scene.json --interactive on
```

Check the existing tabs, footer, F5/F6 overlays, replay scrubber, and cause
window. Press `Ctrl+0`: Legacy must disappear before the ImGui dock appears.
Press `Ctrl+0` again: ImGui must disappear before Legacy returns. There should
be no doubled replay/cause-tree pixels and no focus tug-of-war in either state.

For a direct ImGui launch:

```powershell
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --dev-ui imgui --replay on --replay-seconds 4 --scene SkullbonezData/scenes/stacking.scene.json --interactive on
```

Expected default topology:

- scene, hierarchy, and assets/create down the left;
- game viewport dominant in the centre;
- Inspector, World/Simulation, Rendering, Diagnostics, and compact
  Causality on the right;
- replay transport across the bottom, with the status bar below it.

Try scene filter/load, selection, one Inspector edit followed by undo/redo,
World and Rendering controls, render-target diagnostics, replay pause/step/
scrub/return-live, and `View > Reset Editor Layout`. Nothing in ImGui should
mutate the scene until a typed action is committed.

## 3. Connect Tracy

Build or use the pinned viewer named in the editor, then launch:

```powershell
$env:SKORE_TRACY_MODE = 'standard'
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --dev-ui imgui --replay on --replay-seconds 4 --scene SkullbonezData/scenes/stacking.scene.json --interactive on
Remove-Item Env:SKORE_TRACY_MODE
```

Open/connect Tracy from the editor. Confirm owner lanes, submitted frames,
physics/render/replay/UI zones, and development-tool allocation plots appear.
The console should report Tracy backing below the 512 MiB cap. Standard mode is
not free: the E17 measurements record its actual overhead.

## Feedback record

Please record:

- blocking: crash, lost command, simultaneous Legacy/ImGui pixels, focus theft,
  unreadable/clipped default layout, or replay ownership error;
- non-blocking: preferred grouping, labels, default panel visibility, spacing,
  or shortcuts;
- verdict: ready / not ready for extended hands-on use.

The 2026-07-21 current-tip assisted playtest found no blocker and recommends
**ready**. Owner acceptance is still pending: reply `ready`, or reply
`not ready` and separate blocking findings from non-blocking preferences.

No Legacy retirement or default switch is part of this evaluation. Legacy
remains compiled, selectable, and the launch default until a later explicit
owner decision.
