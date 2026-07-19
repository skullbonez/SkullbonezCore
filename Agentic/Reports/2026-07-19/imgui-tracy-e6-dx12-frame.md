# ImGui + Tracy E6 DX12 Frame Evidence

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Plan task: E6 — integrate ImGui with the engine-owned DX12 frame

## Outcome

E6 is complete. Development builds now submit Dear ImGui draw data after the
world and legacy UI and before Present through a concrete
`Dx12ImGuiRendererOwner`. Release still compiles no ImGui source or symbol.
The legacy UI remains unchanged and selectable beside ImGui.

## Ownership And Lifetime

- `RenderBackendDX12` directly owns `Dx12ImGuiRendererOwner`; it lends that
  narrow owner to `ImGuiEditorOwner` through the composition root. Neither
  owner reaches through `Run`, a callback pack, or a multi-domain services bag.
- `Dx12DescriptorHeaps` owns a separate 16-row shader-visible development UI
  heap. The vendor backend never receives the engine bindless heap or swap-chain
  authority. Allocation, free, active, and high-water accounting remain typed
  descriptor-owner operations.
- `static_assert(Dx12FrameOwner::FRAME_COUNT == 2)` binds ImGui's per-frame
  resource count to the engine's two-frame contract.
- The first visible ImGui frame creates the font texture through the pinned
  1.92 backend protocol. Its upload list, queue, and fence wait synchronously,
  so upload memory is retired before reuse. Shutdown occurs only after the
  runtime renderer drains GPU work and before descriptor/device teardown.
- Resize retains the dedicated descriptor/font resource because it borrows no
  backbuffer. Device-startup failure and normal teardown unwind the editor
  binding before its concrete render authority disappears.

## Frame Placement And State

`ImGuiEditorOwner::BeginFrame` begins the DX12 and ImGui frames. Its typed end
result is consumed after world/legacy UI recording and before the existing
post-draw and Present sequence. The renderer:

1. ignores zero-area/minimized display data safely;
2. ensures the engine command list is open;
3. transitions the current backbuffer only when it is not already in
   `RenderTarget` state, then explicitly binds that RTV;
4. binds only the development UI heap and calls the pinned backend's
   `RenderDrawData`;
5. restores the engine descriptor heap and invalidates cached pipeline/texture
   bindings before later engine work.

ImGui frames, command lists, indexed draws, vertices, and indices are recorded
separately from world-render statistics. E9 owns the first dock shell and
panels, so the E6 empty dockspace correctly produces zero indexed draws while
still exercising 324 renderer frames and its font/resource lifetime.

## Focused Evidence

- Profile targeted build: 14.575s, zero errors.
- Debug targeted build: 15.004s, zero errors.
- Four-frame visible ImGui smoke: exit 0 in 6.407s; four renderer/context
  frames; zero DX12 validation messages; descriptor shutdown
  `active=0`, `high_water=1/16`.
- Three fresh four-frame Profile processes: all exit 0 in 3.603s total; each
  ends `frames=4`, `descriptors=0`, `high_water=1/16`.
- Graceful ImGui-visible resize stress: 131/131 resizes acknowledged; engine
  static SRVs remained 20 -> 20; ImGui ended `frames=324`,
  `descriptors=0`, `high_water=1/16`; WM_QUIT and normal `Execute` return in
  3.351s. Evidence:
  `TestOutput/validation/e6_imgui_resize_graceful_stdout.txt`.
- First-submit fault injection (`SKULLBONEZ_DX12_FAULT=before-first-submit`):
  expected Lane R exit 1 in 1.863s, zero submissions, and clean renderer/context
  shutdown.
- The first custom infinite-stress probes were stopped by exact PID after the
  requested proof because this harness intentionally ignores `--frames`.
  A final PASS-triggered close supplied the clean-exit evidence above.
- A focused filter preflight initially found only that the new files used the
  broad `Render Backend` filter. Moving them to `Rendering\DX12` resolved it;
  the final inventory is 756/756.

## Required Gates

| Gate | Result |
|---|---|
| `tools\validate_dx12_renderer.bat` | PASS in 65.068s; zero validation messages and all DX12 screenshots matched. Manifest: `TestOutput/validation/dx12_renderer/20260718T163202Z/manifest.json`. |
| `tools\run_graphics_stress.bat 1` | PASS in 62.001s; exact PID 10556 closed by the harness after one minute, stderr empty, normal WM_QUIT/return, no crash or descriptor growth. |
| `tools\validate_fast.bat` | PASS in about 56s; 297 tests / 21,497 assertions, formatting clean, 756/756 filters, Profile and Debug ready. |
| `python tools\check_allocation_policy.py --repo .` | PASS in 9.132s; 401 files scanned, 43 allowlisted direct-heap findings, 146 dynamic-member findings, 669 growth findings, zero allowlist errors. |
| `tools\validate_full.bat` | PASS in 144.011s; 297 tests / 21,497 assertions, coverage floors, Automation/replay, DX12 screenshots with zero validation errors, and byte-exact physics all passed. Manifest: `TestOutput/validation/dx12_renderer/20260718T163749Z/manifest.json`. |
| `tools\validate_build.bat Release` | PASS in 50.641s; zero warnings and zero errors. |
| Release EXE/PDB scan | PASS; no `imgui`, `SKULLBONEZ_DEVELOPMENT_TOOLS`, or `Dx12ImGuiRendererOwner` strings. |

No baseline, screenshot golden, replay golden, physics CSV, or query golden was
changed.

## Comment Audit

The 12 touched C++ source-bearing files were inspected against
`Agentic/Reference/comment-style-guide.md` and the comment-style audit skill.
All 12 have the required learning structure and local ownership, lifetime,
invariant, hazard, or error-lane comments where the implementation is dense.
Checked: 12. Deferred: 0. Unchecked: 0. The small project-filter mapping is a
trivial tool edit and was inspected directly.

## Unrelated Live Blocker

Physics body-count P1 remains blocked only on exact owner approval for replay
`causal.topologyCount: 199 -> 200` and the mechanically derived
`physics_query_varied.json`. E6 did not touch either artifact.
