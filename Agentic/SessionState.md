# SkullbonezCore Session State

Date: 2026-07-21

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-20th-july` |
| Current baseline | Synced `origin/main` after PRs #127/#128; dependency direction, allocation namespace, physics facade/settings, Run de-accretion, render graph, Render HAL, and gameplay extraction are closed with exact proofs and independent review clear. |
| Current objective | Close replay boundary's filed allocation/identity defects without stopping at the findings. |
| Active/future progress | 18 / 22 live tasks; 82%. |
| UI ruling | Legacy remains the default. ImGui is explicit `--dev-ui imgui`; atomic hot swap is allowed, simultaneous Legacy/ImGui activation is forbidden. |
| Last broad local gate | Gameplay T3 final `validate_full` passes in 145.4 s: all CPU/coverage/runtime lanes, zero DX12 validation errors, accepted images, and byte-exact physics. |
| Validation for current edits | Replay RP0 is documentation-only. Its targeted Automation strict probes and symbolized source attribution are recorded in the plan; no repository validation script is required. |

## Live Queue

NOW. Two live plans, 22 tasks; 18 complete (82%). The remaining architecture-review campaign
(registered 2026-07-20 from
`Reports/2026-07-20/engine-architecture-review.md`) is the active queue in
binding order: replay-boundary-containment → replay-policy-debt-closure. Owner
decisions at registration: the render-graph and Render HAL migrations are
complete; PhysicsEngine absorbs PhysicsScene; gameplay
extracts to a new top-level `SkullbonezSource/Gameplay/` module.

`dependency-direction-restoration` is closed 6/6 and archived in
`Agentic/Reports/2026-07-20/dependency-direction-restoration-closure.md`.
`allocation-namespace-restoration` is closed 1/1 and archived in
`Agentic/Reports/2026-07-20/allocation-namespace-restoration-closure.md`.
`physics-facade-unification` is closed 3/3 and archived in
`Agentic/Reports/2026-07-20/physics-facade-unification-closure.md`.
`physics-settings-snapshot` is closed 4/4 and archived in
`Agentic/Reports/2026-07-20/physics-settings-snapshot-closure.md`.
`run-execute-deaccretion` is closed 3/3 and archived in
`Agentic/Reports/2026-07-20/run-execute-deaccretion-closure.md`.
`render-graph-completion` is closed 6/6 and archived in
`Agentic/Reports/2026-07-20/render-graph-completion-closure.md`.
`render-hal-modernization` is closed 6/6 and archived in
`Agentic/Reports/2026-07-21/render-hal-modernization-closure.md`.
`gameplay-module-extraction` is closed 4/4 and archived in
`Agentic/Reports/2026-07-21/gameplay-module-extraction-closure.md`.
`replay-boundary-containment` is closed 3/3 and archived in
`Agentic/Reports/2026-07-21/replay-boundary-containment-closure.md`.

`imgui-tracy-editor-campaign` is 17/18; E17 remains unchecked only for
extended hands-on owner acceptance, now parked and non-blocking. Do not
change the default during that evaluation: Legacy is default, ImGui is
explicit opt-in, and only one UI surface owns focus/input at a time.

## Audio Removal

The `codex/remove-audio-pr` branch removes runtime audio ownership and contact
processing, both UI surfaces, operator commands and diagnostics, startup flags
and probes, XAudio linkage, `stb_vorbis`, the contact-audio scene, and all shipped
audio data. Config format v5 deterministically strips legacy
`contact_audio_*` settings. The only remaining audio spellings in live code and
tools are that v4-to-v5 migration and its fixtures.

## Physics Closure

The body-count campaign closed 8/8 on 2026-07-20. Final Profile
`Frame/Physics` P50 changed from P0 as follows: scale-200 0.1106 -> 0.1075 ms,
scale-520 0.8486 -> 0.8021 ms, scale-1,000 1.0688 -> 1.0850 ms,
scale-2,000 1.7852 -> 1.8878 ms, and sleepy-5,000 2.1479 -> 1.3026 ms.
The sleeping-heavy witness is 39.36% faster, while the all-awake 1,000/2,000
witnesses are 1.52%/5.75% slower; the owner explicitly accepted that trade.

The new 520-body unit fixture and six-scene runtime matrix certify byte-exact
0/1/4-worker determinism (18/18 process matches). Algorithmic and tested
multithreaded determinism are present; cross-platform and rollback determinism
are not certified. Full evidence:
`Agentic/Reports/2026-07-20/physics-body-count-scale-closure.md`.

## Final Validation

| Command | Time | Result |
|---|---:|---|
| `python tools\check_allocation_policy.py --self-test` | 0.09 s | PASS |
| `python tools\check_allocation_policy.py --repo .` | 9.14 s | PASS; zero allowlist errors |
| `tools\validate_fast.bat` | 56.95 s | PASS |
| `tools\validate_physics_deep.bat` | 128.05 s | PASS |
| `tools\validate_build.bat Automation` | 14.40 s | PASS; zero warnings/errors |
| `tools\validate_full.bat` | 142.49 s | PASS |
| `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 2` | 1.205 s | PASS; marker emission enabled |
| `tools\validate_full.bat` (L5 closure tip) | 143.84 s | PASS |
| `tools\validate_physics.bat` (C1) | 79.83 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_perf.bat` (C1) | 109.20 s | PASS; zero allocation violations/regressions |
| replay visual fidelity (C1, one engine generation) | 391.01 s | PASS; all positive/negative controls |
| `tools\validate_full.bat` (C1) | 143.12 s | PASS |
| `tools\validate_tests.bat` (C2) | 12.84 s | PASS; 327 cases, 61,131 assertions |
| `tools\validate_tests.bat` (C3 review fixes) | 12.59 s | PASS; 328 cases, 61,341 assertions |
| `tools\validate_physics.bat` (C3) | 79.13 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_perf.bat` (C3) | 108.54 s | PASS; allocation and comparison gates report no regression |
| `tools\validate_full.bat` (C3) | 140.74 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X0) | 13.38 s | PASS; moved controller and Run call site compile |
| direct Release build (X0) | 38.67 s | PASS; production macro path compiles |
| `tools\validate_ui_stress.bat` (X0) | 72.77 s | PASS; moved command matrix, clean logs, zero DX12 errors |
| `tools\validate_full.bat` (X0) | 139.54 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X1 tip) | 10.46 s | PASS; zero warnings/errors |
| direct Release build (X1 tip) | 25.93 s | PASS; zero warnings/errors |
| focused runtime interaction policy test (X1) | 2.44 s | PASS; 1 case, 25 assertions |
| `tools\validate_ui_stress.bat` (X1) | 87.85 s | PASS; direct surface-command matrix and zero DX12 errors |
| `tools\validate_full.bat` (X1) | 148.65 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X2) | 19.72 s | PASS; zero warnings/errors |
| direct Release build (X2) | 49.56 s | PASS; production macro path, zero warnings/errors |
| focused scene proceed-policy test (X2) | 2.05 s | PASS; 1 case, 9 assertions |
| `tools\validate_automation.bat` (X2) | 40.46 s | PASS; combined replay/prediction/development-UI/Ctrl+0 lane |
| `tools\validate_full.bat` (X2) | 166.92 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (G1) | 19.20 s | PASS; zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (G1 run 1) | 78.30 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G1 run 2) | 55.00 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G1 run 3) | 55.10 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (G1) | 61.58 s | PASS; bounded PID-scoped run, crash-free |
| direct Automation build (G2) | 19.20 s | PASS; zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (G2 run 1) | 79.70 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G2 run 2) | 55.10 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G2 run 3) | 55.70 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (G2) | 61.96 s | PASS; bounded PID-scoped run, crash-free |
| direct Automation build (G3) | 21.87 s | PASS; zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (G3 run 1) | 66.98 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G3 run 2) | 55.18 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G3 run 3) | 55.24 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (G3) | 61.70 s | PASS; bounded PID-scoped run, crash-free |
| replay visual fidelity (G3, one engine generation) | 447.61 s | PASS; all positive/negative controls, zero refresh |
| direct Automation build (G4) | 23.95 s | PASS; zero warnings/errors |
| `tools\validate_dx12_renderer.bat` (G4 run 1) | 75.40 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G4 run 2) | 56.0 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (G4 run 3) | 55.57 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (G4) | 62.11 s | PASS; PID 39000, bounded PID-scoped run, crash-free |
| Debug DX12 architecture build/test (G5) | 26.50 s | PASS; single-path normal/capture contracts |
| direct Automation build (G5) | 18.31 s | PASS; zero warnings/errors |
| Legacy / ImGui / text-only / capture probes (G5) | 4.74 s | PASS; all exit 0, ImGui 5 frames / 77 draws |
| replay visual fidelity (G5, one engine generation) | 454.96 s | PASS; 2,401 ticks, all positive/negative controls, zero refresh |
| `tools\validate_format.bat` (G5 preflight correction) | 12.78 s | PASS; one touched header formatted |
| `tools\validate_full.bat` (G5 closure tip) | 177.73 s | PASS; CPU/coverage and five runtime lanes, 44,401-line physics CSV byte-exact |
| `tools\validate_perf.bat` (G5) | 104.76 s | PASS; zero gameplay/reserve violations and no regression |
| `tools\run_graphics_stress.bat 1` (G5) | 62.66 s | PASS; PID 27628, bounded PID-scoped run, crash-free |
| `tools\validate_dx12_renderer.bat` (M2 run 1) | 79.64 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (M2 run 2) | 56.00 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (M2 run 3) | 56.14 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (M2) | 62.62 s | PASS; 13,045 frames, 358 scene loads, graceful PID-scoped stop, empty stderr |
| `tools\validate_full.bat` (M2) | 157.12 s | PASS; all CPU/coverage and five runtime lanes, 44,401-line physics CSV byte-exact |
| `tools\validate_dx12_renderer.bat` (M3 run 1) | 79.46 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (M3 run 2) | 56.07 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\validate_dx12_renderer.bat` (M3 run 3) | 55.78 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (M3) | 62.28 s | PASS; 13,149 frames, 361 scene loads, graceful PID-scoped stop, empty stderr |
| `tools\validate_perf.bat` (M3) | 110.79 s | PASS; absolute budgets and DX12/physics comparisons, no regressions |
| `tools\validate_full.bat` (M3) | 144.36 s | PASS; all CPU/coverage and five runtime lanes, 44,401-line physics CSV byte-exact |
| `tools\validate_format.bat` (M3 final) | 13.41 s | PASS; 276 headers aligned and all source formatted |
| `tools\validate_dx12_renderer.bat` (M4) | 78.0 s | PASS; zero DX12 errors and unchanged baselines |
| `tools\run_graphics_stress.bat 1` (M4) | 61.0 s | PASS; 12,663 frames, 348 scene loads, graceful PID-scoped stop, empty stderr |
| Debug DXR-capability render-suite probe (M4) | 7.0 s | PASS; exit 0, empty stderr, `supported=1 tier=11` |
| `tools\validate_full.bat` (M4) | 156.0 s | PASS; 329 cases/61,354 assertions, all CPU/coverage and five runtime lanes, 44,401-line physics CSV byte-exact |
| `tools\validate_full.bat` (M5 final) | 149.05 s | PASS; 329 cases/61,354 assertions, all CPU/coverage and five runtime lanes, zero DX12 errors, 44,401-line physics CSV byte-exact |
| `tools\validate_perf.bat` (M5 final) | 106.07 s | PASS; DX12 0.7245 ms average / 1.2042 ms P99, zero gameplay/reserve policy violations |
| `tools\run_graphics_stress.bat 1` (M5 final) | 61.99 s | PASS; 12,239 frames, 336 scene loads, graceful PID-scoped stop, empty stderr, 19 PSO misses fixed |
| platform-profiler 10-frame probe (M5 final) | 1.28 s | PASS; exit 0, marker emission requested/enabled, empty stderr |
| focused tornado force witness (T1) | 4.00 s | PASS; 1 case, 7 assertions, exact preserved body-state bits |
| `tools\validate_full.bat` (T1 final) | 199.48 s | PASS; all CPU/coverage/runtime lanes, zero DX12 errors, unchanged images, 44,401-line physics CSV byte-exact |
| `tools\validate_perf.bat` (T1) | 106.09 s | PASS; zero steady-gameplay allocations and no DX12/physics regression |
| replay visual fidelity (T1 final, one engine generation) | 440.53 s | PASS; 2,401 ticks, causal/durable-artifact proof and all negative controls |
| `tools\validate_fast.bat` (T2 final) | 80.83 s | PASS; format/metadata/size gates and zero-warning Profile/Debug builds |
| `tools\validate_dx12_renderer.bat` (T2 final) | 57.63 s | PASS; zero InfoQueue errors and all three screenshots within committed thresholds |
| `tools\run_graphics_stress.bat 1` (T2 final) | 62.74 s | PASS; PID 17628, bounded PID-scoped stop, crash-free, empty stderr |
| allocation policy self-test + repository scan (T3) | 9.37 s | PASS; 412 files, zero allowlist errors |
| `tools\validate_perf.bat` (T3 unchanged-tip rerun) | 108.58 s | PASS; zero gameplay/reserve violations and no DX12/physics regression |
| replay visual fidelity (T3 reconciled, one engine generation) | 452.2 s | PASS; 2,401 ticks and all positive/negative controls; hash-only provenance reconciliation |
| `tools\validate_dx12_renderer.bat` (T3 final) | 57.14 s | PASS; 43 fresh shader stages, zero InfoQueue errors, accepted captures |
| `tools\run_graphics_stress.bat 1` (T3 final) | 61.71 s | PASS; PID 48420, bounded PID-scoped stop, crash-free, empty stderr |
| `tools\validate_full.bat` (T3 final) | 145.4 s | PASS; CPU umbrella and five runtime lanes, byte-exact physics |
| `tools\validate_physics.bat` (T3 final) | 55.62 s | PASS; 44,401-line CSV byte-exact |

The first full gate found one Automation-only orphaned `GameObjects`
using-directive after the SkullScope namespace move. It was removed before the
targeted Automation and final full passes.

## Next Handoff

Continue `replay-policy-debt-closure` RP1 on `nightrunner-20th-july`. RP0
reproduced the strict two-generation failure (40,353 gameplay / 40,350 policy
violations; valid `ok=true` report), symbolized every material top-24 site, and
source-proved that the process-global allocation phase races the thread-local
reserve owner. The apparent Replay JSON family is DX12 shader-manifest loading;
the Render/Replay stringstream family is RenderGraph diagnostics. RP1 must make
phase state thread-local with a cross-thread regression, rerun strict
attribution, and only then bind genuine live growth to the existing three
Replay owners or fixed capacity. E17 extended owner playtest remains parked;
keep Legacy default until explicit owner authorization.
