# ImGui + Tracy E17 Separate-Mode Evaluation

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Campaign task: E17 — separate-mode owner evaluation, independent review, and
campaign closure

## Outcome

All automatable E17 implementation, evaluation, source-audit, independent-
review, and validation work is captured here. Legacy remains compiled,
selectable, and the launch default. ImGui remains an explicit alternative, and
`Ctrl+0` hot swaps by deactivating the source before activating the target.
There is no `both` state or path that intentionally draws the two development
surfaces at once.

Owner extended hands-on acceptance is not available inside this automated run,
so E17 remains unchecked as a retained checkpoint under the binding
2026-07-19 MASTER-PLAN directive. The owner playtest is
`imgui-tracy-e17-playtest.md`. This unavailable feedback step does not stop the
authorized Physics P1 loopback or later plan work.

## Final mode contract

- Omitted `--dev-ui` selects Legacy and preserves the scene-authored Legacy
  visible/minimized/hidden preference. It never selects ImGui.
- `--dev-ui legacy` explicitly selects and exposes the Legacy surface.
- `--dev-ui imgui` explicitly hides Legacy before exposing ImGui.
- `--dev-ui both` is rejected; `DevelopmentUiMode` has only Legacy and ImGui.
- Plain `0` retains Legacy minimize/restore semantics. `Ctrl+0` is the atomic
  surface swap in either direction.
- A scene or replay load reapplies the selected process surface after authored
  Legacy visibility is loaded, preventing a dormant implementation from
  reappearing.
- F5/F6 and legacy replay presentation remain Legacy tools. The final replay
  renderer fence emits neither scrubber nor cause-tree pixels while ImGui owns
  presentation, although ImGui reads the same immutable replay values.

This contract supersedes E0's historical `both`/coexistence ratification and
the same wording retained in some intermediate evidence reports. Those reports
remain useful implementation history; they are not the final mode authority.

## Complete Legacy control disposition

Disposition vocabulary:

- **Kept**: original control and pixels remain unchanged and reachable in
  Legacy mode.
- **Regrouped**: ImGui offers the same typed owner action or fact in the named
  dock while Legacy still keeps it.
- **Tracy in ImGui**: generic profiling moves to the external Tracy viewer only
  for ImGui evaluation; Legacy keeps the old profiler.
- **Deferred / Legacy reachable**: intentionally not cloned into the first
  ImGui direction; no control was deleted or made unreachable.

The exhaustive E0 inventory names every control, enum, frame value, keyboard
binding, and owner seam. The rows below dispose that complete inventory against
the final implementation.

| Legacy surface | Final disposition |
|---|---|
| Floating window chrome: drag, resize, close-to-minimize, maximize, restore, scroll, blur, cached drawing | **Deferred / Legacy reachable.** ImGui uses the engine-owned single-window dock host, deterministic reset, responsive rails, and DPI style; it does not clone floating Legacy chrome. |
| `Prof`: worker enable/count, marker tree, CPU/self/P50/P99, timeline spans, draw tree, worker graph | **Tracy in ImGui** for zones, timings, trees, timelines, distributions, and flame graphs. Worker selection and bounded renderer/engine counters are **regrouped** under Diagnostics. Entire old tab is **kept** in Legacy. |
| `Scene`: discovered-scene combo/filter, Demo, New/name, Reset, Reset Defaults, Save Defaults/current scene, time scale, scene/frame/status facts | **Regrouped** into Scene & Modes, File menu, World/Simulation, toolbar, and status. Legacy tab is **kept**. |
| `Edit`: edit/place/static/alignment modes, 37 recipes, selection facts, undo/redo | **Regrouped** into Scene & Modes, Hierarchy, Assets/Create, Inspector, viewport picking/gizmos, toolbar, and status. Legacy tab is **kept**. |
| Minimized editor palette: 37 recipes, tree variants, ragdoll/place/static/level chips, object count, camera mode, restore | Core authoring is **regrouped** into Assets/Create and toolbar. The compact Legacy palette presentation is **deferred / Legacy reachable**. |
| `Phys`: collision/transparency/broadphase; axes, contacts, sleep, pipeline, terrain probe; tornado shell/vectors; ray visualization; stage previous/next; alpha/linger; ray impulse/projectile speed; gravity/friction; tornado radius/height/inward/swirl/lift | World values are **regrouped** under World/Simulation. Every listed debug toggle/value and pipeline step is **regrouped** under Diagnostics/Physics. Legacy tab is **kept**. |
| `Sound`: enable/counters/flash/simple; 13 global values; sample Play/Use; material recipe selection and nine values; four bands and five values each; reducer facts | Authoring is **regrouped** into Audio Authoring; counters and flash mode into Diagnostics/Audio. Legacy tab is **kept**. |
| `Opt`: fixed step, terrain/water visibility, freeze/flat water, shadows, time scale, model count, presentation facts | **Regrouped** among World/Simulation, Rendering, Viewport/status, and replay diagnostics. Legacy tab is **kept**. |
| `Render`: shadows, Save CFG, visibility facts, all 25 ordinary lighting/sky/ground/shadow/water/material values | **Regrouped** into the six canonical Rendering sections and Diagnostics/Renderer. Legacy tab is **kept**. |
| `Targets`: selector, availability/type/size, live previews for up to 12 targets | **Regrouped** into normally closed Diagnostics/Render Targets. Legacy tab is **kept**. |
| `Ctrl`: seed, solver balls/boxes, fluid height/density | **Regrouped** into World/Simulation Population/seed and World forces/fluid. Legacy tab is **kept**. |
| `Sky`: Save Sky, Sky/Clouds/God rays/Volume, 26 direction/palette/cloud/ray/volume/grade values | **Regrouped** into Rendering/Environment and related canonical sections. Legacy tab is **kept**. |
| `Cine`: concept/cinematic preset selector, eight feature toggles, 64 cinematic values | Feature toggles and all values are **regrouped** into Lighting, Environment, Shadows, Post, Water, and Terrain/Materials. The legacy preset-selector presentation is **deferred / Legacy reachable**. |
| `Mem`: policy, retention/budget/clamp, process/replay/object totals, replay categories/trajectory, upload arena, reserve-growth table | Replay policy is **regrouped** into the bottom replay advanced section; fixed/replay/upload facts into Diagnostics/Engine Memory. Generic heap investigation is **Tracy in ImGui**. Legacy tab is **kept**. |
| F5 performance histogram | **Tracy in ImGui**; **kept** as a Legacy-only overlay. |
| F6 memory waterline and retained-event rail | Generic history is **Tracy in ImGui** and engine reserve facts are **regrouped** into Diagnostics; **kept** as a Legacy-only overlay. |
| Footer: renderer, water reflection, blur, VSync, hitboxes, perf/timeline, FPS/frame/draw/UI facts | VSync/reflection are **regrouped** into Rendering; hitbox/debug and essential facts into Diagnostics/status; perf/timeline are **Tracy in ImGui**. DX12 selector is intentionally omitted because DX12 is the only runtime renderer. Legacy blur/footer presentation is **deferred / Legacy reachable**. |
| Replay scrubber and cause window | Disposed control-by-control below. Legacy presentation is **kept** and now has an explicit inactive-surface render fence. |

## Replay and causality control disposition

| Legacy replay control | Final disposition |
|---|---|
| presentation/solver track and normalized scrub | **Regrouped** into the permanent bottom scrubber through typed `Scrub`. |
| recording start/stop | **Regrouped** into `REC`/`STOP`. |
| jump start/end, previous/next step, pause/play, return live | **Regrouped** into the bottom transport through typed replay commands. |
| Save, Load, Restore/Branch | **Regrouped** into the advanced bottom section through replay-owned commands and cold IO. |
| prediction toggle and 1–20 second horizon | **Regrouped** into bottom transport/advanced prediction. |
| replay memory policy, retention, and budget | **Regrouped** into the advanced replay section. |
| cause-tree selection and selected-row detail | **Regrouped** into compact Causality plus explicit Causality Detail. |
| `ALT VEL`, `RAGDOLL`, `PAST` | **Deferred / Legacy reachable.** These advanced Legacy presentation tools were not silently approximated by a new command owner. |
| world path/body selection and comma path-color cycle | Existing replay owner/input behavior is **kept**. Compact ImGui Causality consumes the selected typed view; the Legacy world-path gesture/presentation remains **Legacy reachable**. |
| bottom-edge reveal/fade gesture | **Deferred / Legacy reachable.** ImGui transport is permanently docked, so it needs no reveal gesture. |
| cause-window drag, resize, and independent scroll chrome | Selection/detail capability is **regrouped** into docks. The free-floating Legacy window mechanics are **deferred / Legacy reachable**. |

All original keyboard bindings remain owned by the engine input router. ImGui
adds no general binding cycle; text/keyboard capture is class-specific and
plain Legacy `0` is preserved. The only surface chord is `Ctrl+0`.

## Separate-mode visual evidence

The final E17 automation scripts are:

- `SkullbonezData/interaction/legacy_ui_e17_evaluation.json`
- `SkullbonezData/interaction/imgui_editor_e17_evaluation.json`

They passed with the following reports:

| State | Report result | Capture |
|---|---|---|
| omitted-selector Legacy | 1 action, 2 assertions, `ok=true`; ImGui recorded zero frames | historical capture, later overwritten by the explicit Legacy capture |
| explicit Legacy | 1 action, 2 assertions, `ok=true`; Legacy-only presentation visible | `TestOutput/interaction/imgui_e17_legacy_default.bmp` |
| explicit ImGui default | part of 11 actions, 8 assertions, `ok=true` | `TestOutput/interaction/imgui_e17_imgui_default.bmp` |
| 200-body ImGui replay workflow | same passing report; replay paused at tick 0 | `TestOutput/interaction/imgui_e17_imgui_workflow.bmp` |
| hot swap to Legacy | source ImGui hidden before full Legacy surface appears | `TestOutput/interaction/imgui_e17_hot_swap_legacy.bmp` |
| production `Ctrl+0` transition frame | same-frame assertions report `developmentUiSurface=imgui` and `legacyReplayPresentationActive=false` | `TestOutput/interaction/imgui_e17_hot_swap_transition.bmp` |
| hot swap back to ImGui | source Legacy hidden before dock appears | `TestOutput/interaction/imgui_e17_hot_swap_imgui.bmp` |

All six retained native captures were inspected at original resolution through
the screenshot-driven UI QA workflow. The dock topology matches the contract;
the central viewport remains dominant; text is readable; and there is no panel
overlap, clipping, bleed, modal trap, doubled replay presentation, or
simultaneous Legacy/ImGui surface.

## Measured overhead

Final-source measurements are recorded after the independent-review fixes and
before the formal final gates. Each lane uses the same Automation executable,
DX12, VSync off, fixed step, replay explicitly off,
`stacking.scene.json`, 3,600 frames, and three sequential visible-window runs.
No two engine processes run in parallel. Pinning replay off and the scene path
prevents persisted local runtime preferences from contaminating the comparison.

The Legacy lane is `--dev-ui legacy` with Tracy off. It represents ImGui
dormant/disabled at presentation: the development build still initializes the
context, but it records zero ImGui frames/draws. The ImGui lane is explicit
`--dev-ui imgui` with Tracy off. The Tracy lane adds
`SKORE_TRACY_MODE=standard` and a pinned `tracy-capture.exe` attachment to
`127.0.0.1:8086` for the full run. Raw logs and `.tracy` artifacts are under
`TestOutput/validation/imgui_e17_overhead_final/`.

| Lane | Wall ms/frame | CPU ms/frame | Peak private bytes | Peak working-set bytes |
|---|---:|---:|---:|---:|
| Legacy | 1.98848 | 1.76360 | 681,575,765 | 501,942,955 |
| ImGui | 2.11083 | 1.92274 | 689,668,096 | 503,792,981 |
| Tracy standard capture | 2.31414 | 2.14699 | 1,108,979,712 | 569,782,272 |

ImGui visible versus Legacy adds 0.12236 ms/frame wall time (6.15%),
0.15914 ms/frame CPU time (9.02%), 7.72 MiB peak private bytes, and
1.76 MiB peak working set. Tracy standard capture versus ImGui adds
0.20331 ms/frame wall time (9.63%), 0.22425 ms/frame CPU time (11.66%),
399.89 MiB peak private bytes, and 62.93 MiB peak working set. The three
captures are 1,673,316, 1,700,259, and 1,691,896 bytes. All nine engine
processes and all three capture processes exited 0 with empty stderr.

The first pre-fix harness trial that hid the native engine window produced a
zero-width DX12 fatal and was discarded. That was a measurement-harness error,
not an engine result; all accepted runs use a visible client window.

## Independent ownership review

The mandatory read-only review covered command authority, DX12 lifetime,
replay ownership, allocation-exception scope, Legacy default/exclusivity, test
coverage, and replacement-god-object risk.

The first pass found two credible closure defects:

1. Tracy's private rpmalloc used direct OS backing maps, bypassing the nominal
   256 MiB global-new owner row. E17 now installs engine callbacks before Tracy
   startup, atomically reserves every real backing range in the named owner,
   maps it with `VirtualAlloc`, releases it exactly once on full unmap, and
   fails Lane F rather than falling back untracked. A follow-up pass also found
   Tracy's embedded LZ4 stream using raw CRT allocation; the pinned vendor is
   now compiled with `LZ4_USER_MEMORY_FUNCTIONS`, whose linked hooks use the
   same named owner. Final standard captures justified a truthful 512 MiB cap.
2. Legacy cause-tree pixels could draw from the shared immutable replay view
   while ImGui drew its compact Causality panel. E17 now passes explicit
   surface authority into the legacy replay render context and returns before
   either scrubber or cause-tree draw when ImGui is selected. Legacy-hidden
   cause-tree behavior remains intact when Legacy is still the selected mode.

Focused regression evidence passed: the allocation-owner case ran 13/13
assertions and all four replay-overlay value cases ran 71/71. The final
same-frame automation assertion and transition capture exercise the production
`Ctrl+0` input path rather than a test-only surface setter. Three live
3,600-frame standard Tracy runs each produced a valid capture and clean engine/
capture exit. Startup reported 84,815,872 owner bytes, shutdown reported a
365,309,952-byte high water and 536,870,912-byte cap, and no allocation-policy
failure occurred. Retained rpmalloc backing after manual shutdown is still
truthfully counted until process exit. The final six renderer captures, not a
tautological boolean helper, prove that inactive Legacy presentation emits no
competing visible pixels while ImGui owns the UI.

The final independent follow-up review found no blocking issue after those
repairs. It confirmed the post-input surface re-sample closes same-frame
Legacy-to-ImGui overlap, the automation assertion observes the exact authority
consumed by late replay rendering, and both rpmalloc and LZ4 are accounted. Its
one non-blocking observation is that Tracy reports 285,605,888 active owner
bytes after manual shutdown. Those retained one-shot process-lifetime pages are
truthfully charged below the 512 MiB cap; this evidence does not claim
restart/reclamation behavior that the current process owner does not support.

No other authority defect was found: ImGui submits bounded typed values,
replay mutation terminates in `ReplayRuntime`, DX12 retains device/descriptor/
frame/resize/shutdown ownership, Legacy is the default, Release excludes both
tools, shared views are synchronous values/borrows, and the large
`BuildEditorShell` remains a presentation maintainability hotspot rather than a
replacement business-state god object.

## Comment audit

The campaign-wide checklist is
`Agentic/Plans/imgui-tracy-e17-comment-audit.md`. It reconciles the final
`git diff` scope against `git ls-files`: 95 tracked source-bearing files, 95
checked, 0 deferred, 0 unchecked. Missing learning-header fields: 0. The E17
fixes add nearby comments for allocator backing, cap reservation, map/unmap
lifetime, fatal failure, and exclusive replay presentation.

## Final-source gates

| Gate | Final-source result |
|---|---|
| `tools\validate_full.bat` | pass in 175.700 s; 312 doctest cases/21,876 assertions, coverage floors, Automation boundary, replay smoke, DX12 baselines with zero validation errors, and the 44,401-line byte-exact physics oracle all passed |
| `tools\validate_ui.bat` | pass in 55.046 s; screenshot, blur, clipping, and DX12 checks passed |
| `tools\validate_ui_stress.bat` | pass in 70.144 s; deterministic Legacy stress and zero DX12 errors |
| `tools\validate_dx12_renderer.bat` | pass in 55.189 s; zero DX12 validation errors and all committed comparisons passed |
| `tools\run_graphics_stress.bat 1` | pass in 61.642 s; exact PID stopped cleanly after the bounded minute |
| `tools\validate_perf.bat` | first run failed only `PHYSICS_BENCH Frame.avg` at +18.0% versus the 16% threshold while `Frame/VsyncWait` was +9,122%; unchanged retry passed in 107.103 s with physics-bench frame average 0.4066 ms. Both logs are retained; no performance baseline changed |
| allocation policy self-test / repository scan | pass in 0.141 s / 11.104 s; 405 files scanned, 0 allowlist errors |
| platform-profiler marker smoke | the literal command emitted the requested/enabled markers but is intentionally unbounded and was stopped by exact PID after 49.775 s; a bounded 180-frame Legacy/replay-off equivalent exited 0 in 1.639 s with marker emission enabled |
| `tools\validate_build.bat Release` and exclusion inspection | pass in 45.570 s with 0 warnings/errors; production compile/link inputs, root object inventory, and shipping executable contain no ImGui/Tracy development payload. The PDB retains one generic `tracy` source-symbol spelling from zero-cost profiler fields and is not claimed token-free |
| one `tools\validate_replay_visual_fidelity.bat` invocation | expected retained P1 checkpoint after 414.667 s: launcher shape and 16 control cases/72 assertions passed, then the sole failure was `causal.topologyCount: expected=199 actual=200` |

The interaction scripts also passed from final source: Legacy 1 action/2
assertions and ImGui 11 actions/8 assertions, including same-frame exclusive
surface authority. No authored behavior baseline, replay golden, physics CSV,
or coverage floor changed in E17. The replay mismatch is handed directly to the
binding post-E17 Physics P1 transition rather than refreshed here.

## Owner acceptance and handoff

The current Profile development build plus the concise playtest in
`imgui-tracy-e17-playtest.md` are ready for owner use. Blocking feedback would
reopen E17. Because that hands-on response is unavailable in this goal run,
the checkpoint remains retained and the plan checkbox remains open. This is the
only non-automatable E17 hold after the final gates.

There is no Legacy deletion, retirement, or default switch in this campaign.
