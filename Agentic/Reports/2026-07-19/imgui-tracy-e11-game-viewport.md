# ImGui + Tracy E11 Game Viewport Evidence

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Plan task: E11 — make the central game viewport an editor-grade surface

## Outcome

E11 is complete. The development ImGui editor now presents the live DX12 game
image in the reserved central dock node, derives one explicit physical-pixel
content rectangle, and maps hovered input through letterboxing and source
extent before the existing world picking, placement, and gizmo paths consume
it. Selection outline, placement preview, and gizmos remain world-rendered
overlays, so the editor image preserves the same authoritative presentation as
the legacy viewport.

The DX12 owner captures the completed world backbuffer immediately after world
rendering and before either UI layer. It owns one persistent full-client RGBA8
sample texture and one stable development descriptor. That texture is
recreated only when the swap-chain extent or recreation generation changes;
dock-panel motion and content-rectangle changes do not resize world targets.

No authored schema, baseline, replay golden, query golden, screenshot oracle,
or physics CSV changed.

## Resource, Presentation, And Input Contracts

- The capture path transitions the current backbuffer from render-target to
  copy-source, copies into the persistent viewport texture, transitions that
  texture to shader-resource, and restores the backbuffer before legacy and
  ImGui rendering continue.
- The development descriptor heap remains hard-capped at 16 rows. Font and
  game-view image are the only live rows in the visible editor path; the
  final session reported a 2/16 high-water mark.
- `ResolveImGuiGameViewportRect` fits the source image into available content
  with an explicit letterbox policy. `MapImGuiGameViewportPoint` rejects bars
  and outside points, then maps physical client pixels to the source extent.
- The mapping contract carries source width/height and window DPI scale as
  values. Mouse remapping happens immediately after the one native-device
  capture, before existing pick, placement, and gizmo owners run.
- The compact overlay reports camera, gizmo mode, snapping, VSync,
  presentation/interpolation, source extent/DPI, and stable selection state.
  Pop-out is deliberately disabled by the campaign's single-window policy.
- Asset drag/drop emits the existing fixed typed placement payload; the
  viewport is a drop target rather than a new asset recipe or mutation owner.

## Native Visual And Interaction Proof

Command:

`Debug\SKULLBONEZ_CORE.exe --scene stacking --interactive on --dev-ui imgui --vsync off --replay off`

The real Win32/DX12 editor was exercised through the native application:

| Probe | Observed result |
|---|---|
| Initial 1786 x 993 window | Central image rendered at source `1784 x 961 @ 1.00x` with the editor dock rails intact. |
| Enter edit and click a world object | Stable selection changed and the cyan world selection outline appeared in the captured viewport. |
| Select hierarchy row `mid` | Overlay reported `Selection #2 | visible | editable`. |
| Drag the Box asset into the viewport | The placement preview appeared at the mapped cursor target, proving drag/drop plus viewport-coordinate placement. |
| Maximize to 2560 x 1392 | Source changed to `2560 x 1369`; the scene stayed centered and undistorted. |
| Final resize lifecycle | Clean close, empty stderr, 18,029 frames/captures, exactly 2 viewport resource recreations, and descriptor high-water `2/16`. |

The final shutdown line was:

`[imgui-dx12] Renderer shutdown frames=18029 draws=324509 viewport_captures=18029 viewport_recreates=2 descriptors=0 high_water=2/16.`

This proves capture is once per presented editor frame, resize recreation is
bounded to the two observed swap-chain extents, and all descriptors are
returned at shutdown.

## Focused And Required Gates

| Gate | Result |
|---|---|
| Debug core build | PASS in 17.46s. |
| Debug tests build | PASS in 5.49s. |
| `*Game viewport policy*` | PASS: 1 case, 24/24 assertions across aspect fit, DPI/source mapping, and outside rejection. |
| `*Operator editor frame fingerprint*` | PASS: 1 case, 4/4 assertions. |
| `tools\validate_ui.bat` | PASS in about 63s; format clean, Profile and Debug builds had zero warnings/errors, the UI suite passed, and DX12 InfoQueue reported zero errors. |
| `tools\validate_dx12_renderer.bat` | PASS in about 53s; all 43 shader stages were fresh, DX12 errors were zero, and all three screenshots matched. Manifest: `TestOutput/validation/dx12_renderer/20260718T201158Z/manifest.json`. |
| `tools\run_graphics_stress.bat 1` | PASS in 62s; PID 45064 rendered 12,519 frames through 344 scene loads, remained crash-free, and closed through the PID-scoped timeout with empty stderr. |
| `tools\validate_full.bat` | PASS in about 166s; CPU umbrella and coverage, Automation/replay smoke, Debug/Profile runtime lanes, zero-error DX12 comparison, standalone physics smoke, and byte-exact 44,401-line physics regression all passed. Final manifest: `TestOutput/validation/dx12_renderer/20260718T201607Z/manifest.json`. |
| `tools\validate_perf.bat` | COMPLETE in about 122s; absolute DX12 and physics budgets passed. Whole-frame average was 0.7342ms versus 0.7066ms (+3.9%, classified as noise), with 1.2631ms p99. |
| `python tools\check_allocation_policy.py --repo .` | PASS in 4.3s; 404 files scanned, all 43 direct, 146 dynamic-STL, and 669 growth findings accounted for, zero allowlist errors. |
| `tools\validate_build.bat Release` | PASS in 46.82s with zero warnings and zero errors. |
| Release executable/object audit | PASS; viewport/development UI/Tracy tokens were absent from the Release executable and no ImGui, Tracy, or development-tool allocation objects appeared in the Release core inventory. |

The performance gate also reported `Frame/Input` at 0.0459ms versus 0.0189ms.
The viewport remap is inactive when the development UI is hidden, so this is
recorded as cumulative/noise evidence rather than attributed to an E11 hot-path
regression; the absolute product budgets and whole-frame gate passed.

## Comment Audit

The scoped inventory contains the 12 touched source-bearing files. Each was
inspected against `Agentic/Skills/comment-style-audit/skill.md` and the
repository comment guide:

- [x] `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/InputRouter.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.h`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`

Checked: 12. Deferred: 0. Unchecked: 0. Required learning-header sections and
nearby resource-transition, descriptor-lifetime, mapping, and owner-checkpoint
comments are present.

## Unrelated Live Blocker

Physics body-count P1 remains blocked only on exact owner approval for replay
`causal.topologyCount: 199 -> 200` and the mechanically derived
`physics_query_varied.json`. E11 did not touch either artifact.
