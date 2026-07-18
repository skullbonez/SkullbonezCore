# ImGui + Tracy E13 Rendering, Audio, and Diagnostics Evidence

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Campaign task: E13 — Consolidate Rendering, Audio, and Diagnostics on the right

## Outcome

E13 is complete. The development editor's utility rail now presents one
canonical Rendering surface, one Audio Authoring surface, and bounded domain
Diagnostics without creating a second authority for scene, render, audio, or
runtime state. The legacy UI remains compiled and reachable in Legacy / ImGui /
Both modes.

Rendering uses a shared catalog for 25 ordinary sky/render values, 64 cinematic
values, and 8 cinematic feature toggles. Both the legacy and ImGui presentations
consume the same ordinary catalog, while the ImGui surface groups both modes
under Lighting, Environment, Shadows, Post, Water, and Terrain/Materials.
Common Lighting and Environment authoring starts open; the advanced sections
and render-target diagnostics start closed.

Audio Authoring projects 13 global values, 16 contact recipes with 4 material
bands each, and a bounded 64-sample library. Diagnostics publishes bounded
Physics, Renderer, Render Targets, Engine Memory, Workers, Audio, and UI facts.
Generic timelines, histograms, and percentile views are deliberately absent:
Tracy owns those investigations.

## Authority and bounded-data contract

- `OperatorEditorExchange` carries fixed, trivially-copyable Rendering, Audio,
  and Diagnostics views and fixed command queues.
- ImGui keeps active slider previews locally and emits one typed commit on
  release. Preview commands do not mutate canonical owners.
- Legacy and ImGui commands normalize into the same reducer and deterministic
  duplicate/conflict rules. Legacy fields are drained once after normalization.
- Runtime render and audio owners remain authoritative. Diagnostics is a cached,
  bounded snapshot and never serializes overlay state into authored scenes.
- The audio catalog is borrowed only for the frame in which it is projected;
  ImGui does not retain runtime sample pointers.
- Renderer selection is intentionally absent because DX12 is the only runtime
  renderer.

## Legacy disposition reconciliation

| Legacy surface | E13 disposition |
|---|---|
| Prof timeline, timing tree, histograms, percentiles | Tracy supersedes these only in ImGui; legacy remains reachable. Worker and bounded owner counters live in Diagnostics. |
| Phys collision, broadphase, contacts, axes, sleep, probes, vectors, raycast, pipeline, alpha, linger, impulse/projectile debug | `Diagnostics > Physics`; E12 retains canonical World/Simulation authoring. |
| Sound enable/simple mode, global tuning, contact recipes, material bands, sample library | `Audio Authoring`. |
| Sound reducer activity, counters, and flash state | `Diagnostics > Audio`. |
| Opt terrain/water visibility and water/shadow controls | Canonical Rendering sections. Replay-specific options remain for E15. |
| Render visibility facts, shadow controls, save defaults, 25 ordinary sliders | Rendering authoring plus Renderer diagnostics. |
| Targets, 12 render-target rows | Normally closed `Diagnostics > Render Targets`. |
| Sky and Cine editors, 64 cinematic values and 8 features | Merged into the six canonical Rendering sections. The legacy cinematic preset selector remains reachable during coexistence. |
| Mem fixed capacities and replay reserve facts | Normally closed `Diagnostics > Engine Memory`; replay authoring remains for E15. |
| Footer VSync, hitboxes, renderer selector | VSync is in Rendering, hitbox/essential facts are Diagnostics/status, and the obsolete renderer selector is omitted from ImGui. |

No retained control is silently deleted. Causality remains a separate compact
right-side tool for E14 rather than absorbing authoring or generic profiling.

## Native evidence

The visible Debug application was launched as:

```text
Debug\SKULLBONEZ_CORE.exe --scene stacking --interactive on --dev-ui imgui --vsync off --replay off
```

PID 38256 produced the following final captures:

- `TestOutput/validation/imgui_e13_final_rendering.png`
- `TestOutput/validation/imgui_e13_final_audio.png`
- `TestOutput/validation/imgui_e13_final_diagnostics_toggle.png`
- `TestOutput/validation/imgui_e13_audio_toggle_off.png`

The Physics Collision diagnostic was enabled through the typed command queue,
captured, and restored. Contact audio was disabled through the same reducer,
captured, and restored. Labels fit the right rail at the default resolution.
The process closed through `WM_CLOSE` by PID with empty stderr; stdout records
clean ImGui and DX12 shutdown after 28,959 frames and 540,035 ImGui draws.

## Focused tests and comment audit

The focused operator exchange suite passed 7/7 cases and 205/205 assertions.
It covers preview/commit projection, legacy normalization, duplicate
arbitration, malformed multi-scalar rejection, and the expanded frame
fingerprint.

The comment-style audit reconciled the scoped `git ls-files` inventory and
checked all 10 touched source-bearing files. Checked: 10. Deferred: 0.

## Final-source gates

The Codex PTY was the available console for scripted gates, so output was
mirrored to the named logs. The native application probe itself was visible.

| Command | Result | Wall time | Evidence |
|---|---|---:|---|
| `tools\validate_ui.bat` | PASS; Profile/Debug zero-warning builds, clean formatting, zero DX12 InfoQueue errors, screenshot and blur checks | 61.35 s | `TestOutput/validation/imgui_e13_validate_ui.log` |
| `tools\validate_physics.bat` | PASS; standalone/handle smoke and byte-exact 44,401-line varied-scene oracle | 55.75 s | `TestOutput/validation/imgui_e13_validate_physics.log` |
| `tools\validate_full.bat` | PASS; 309 doctest cases, 21,812 assertions, coverage floors, Automation/replay, DX12 comparisons, physics oracle | 151.25 s | `TestOutput/validation/imgui_e13_validate_full.log` |
| `tools\validate_perf.bat` | PASS; allocation guard, reserve policy, absolute budgets, and regressions | 108.07 s | `TestOutput/validation/imgui_e13_validate_perf.log` |
| `python tools\check_allocation_policy.py --repo .` | PASS; 404 files, zero allowlist errors | 9.20 s | `TestOutput/validation/imgui_e13_allocation_policy.log` |
| `tools\validate_build.bat Release` | PASS; zero warnings/errors | 46.85 s | `TestOutput/validation/imgui_e13_release_build.log` |
| Release exact-token scan | PASS; ImGui/Tracy/E13 strings absent | 0.10 s | command output retained in the task transcript |

`validate_full` also produced the zero-error DX12 manifest at
`TestOutput/validation/dx12_renderer/20260718T214007Z/manifest.json`. E13 did not
change render-backend or shader source, so the campaign's conditional bounded
graphics-stress requirement did not apply. No SkullScope trace was used.

No authored scene, data migration, physics baseline, replay golden, or visual
golden changed. Physics P1 remains independently blocked only on exact owner
authority for replay topology `199 -> 200` and the mechanically derived
`physics_query_varied.json` transition.
