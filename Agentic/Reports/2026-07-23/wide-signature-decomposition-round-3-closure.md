# Wide Signature Decomposition Round 3 Closure

Date: 2026-07-23
Owner: skullbonez
Plan: `wide-signature-decomposition-round-3` T0-T5
Branch: `nightrunner-22nd-JUL-26`

## Result

The second pass is complete. All nine tracked functions with 13, 14, or 15
parameters were removed, and the final tracked-source threshold-13 scan returns
zero rows. Per owner direction, functions with 12 or fewer parameters are
accepted.

No row was deferred or judged too difficult. The replacements are bounded
domain values with one synchronous producer/consumer lifetime. Mutable UI,
physics, rendering, Replay, scene, tool, tracer, input, interaction, frame,
texture, store, collider, and worker owners remain explicit. No generic
context/service/bindings/callback bag, forwarding facade, allocation, callback,
inheritance seam, owner reach-back, growth privilege, or dependency inversion
was introduced.

No baseline, golden, screenshot, Replay artifact, physics CSV, schema,
configuration, or allocation-policy inventory was refreshed.

## Decomposition

| Original row | Replacement |
|---|---|
| `LogReplayV2TargetRestoreDiagnostic` (15) | Bounded restore-diagnostic evidence value |
| `ReplayRuntime::PrepareRenderFrame` (15) | Five explicit mutable/domain owners plus a frame-local value snapshot |
| `SceneTab::Draw` (14) | Explicit widgets/commands plus bounded draw-frame values |
| `EmitFxQuad` (14) | Corner, style, and terrain values |
| `PhysicsNarrowphaseWakeAccess` constructor (14) | Private sleep-controller row capability; external physics owners explicit |
| `SceneTab::HandleOpenComboClick` (13) | Explicit widgets/commands plus bounded combo-frame values |
| `RenderInstanceRenderer::RenderModels` (13) | Explicit stores plus read-only model-pass input |
| `ShadowPass::RenderShadowMap` (13) | Explicit backend/store/worker owners plus private selection policy |
| `TickReplayScrubberGesture` (13) | Explicit mutable Replay/input owners plus pointer-frame values |

UI hit-test and drawing branches, tornado corner/color/terrain mapping, physics
wake ordering, render and shadow submission order, and Replay selection,
mutation, publication, and restore-diagnostic order are unchanged.

## Inventory And Static Proofs

- `codegraph sync .`: PASS against the final source.
- `python tools\inventory_wide_signatures.py --self-test`: PASS.
- `python tools\inventory_wide_signatures.py --threshold 13 --format json`:
  `[]`.
- Core dependency-direction proof: zero rows.
- Physics/Rendering upward-dependency proof: zero rows.
- Gameplay upward-dependency proof: zero rows.
- Downward `Runtime/Replay` include proof: zero rows.
- Allocation-policy self-test and repository scan: PASS; 429 files scanned,
  30 direct-heap findings, 129 dynamic-STL-member findings, 645 STL-growth
  findings, and zero allowlist errors.
- Campaign-wide comment audit: 16/16 touched source-bearing files checked,
  zero missing, zero extra, zero deferred.

## Commit Ledger

- `4fe78e43` — ratify the threshold-13 decomposition.
- `b7b16a48` — narrow UI and tornado visual inputs.
- `86ac6df9` — narrow the physics wake capability.
- `aabbf5af` — narrow render submission inputs.
- `c27aa400` — narrow Replay frame, scrubber, and diagnostic inputs.

Those five already-pushed implementation commits contain the required progress
subjects but no explanatory bodies. History was not rewritten. This report
records their owner-boundary decisions and validation evidence, and the closure
commit carries a full explanatory body.

## Validation

| Command | Time | Result |
|---|---:|---|
| T1 focused Profile build | 11.6 s | PASS; 0 warnings, 0 errors |
| T1 `tools\validate_fast.bat` | 32.3 s | PASS |
| T2 focused Profile build | 18.3 s | PASS; 0 warnings, 0 errors |
| T2 `tools\validate_physics.bat` | 49.3 s | PASS; deterministic byte-exact regression |
| T3 focused Profile build | 10.6 s | PASS; 0 warnings, 0 errors |
| T3 `tools\validate_dx12_renderer.bat` | 38.6 s | PASS; zero DX12 errors, three captures accepted |
| T3 `tools\validate_perf.bat` | 76.8 s | PASS |
| T3 `tools\run_graphics_stress.bat 1` | 60.9 s | PASS; PID 53472, crash-free |
| T4 focused Profile build | 10.4 s | PASS; 0 warnings, 0 errors |
| T4 `tools\validate_replay_visual_fidelity.bat` | 431.4 s | PASS; one process/generation, 2,401 ticks, all controls |
| T4 `tools\validate_full.bat` | 114.0 s | PASS |
| T5 inventory/static proofs | 27.0 s | PASS; zero threshold-13/dependency rows |
| T5 `tools\validate_replay_allocation_policy.bat` | 4.2 s | PASS; strict two-generation probe |
| T5 `tools\validate_replay_v2_artifact.bat` | 33.3 s | PASS |
| T5 `tools\validate_replay_scrub.bat` | 432.2 s | PASS; authoritative visual-fidelity alias |
| T5 `tools\validate_full.bat` | 99.5 s | PASS |

The first attempted T4 visual-fidelity command was stopped by the shell
wrapper's 10-second timeout during the Automation build. Its incomplete
`t4_validate_replay_visual_fidelity.log` contains no process ID, engine launch,
result, or prediction-generation evidence and is not validation evidence. The
PID-tracked retry is the sole completed T4 engine run; it passed in 431.4
seconds with one process/generation and all positive and negative controls.

The final broad gate passed its CPU/coverage umbrella and five engine
processes, reported zero build warnings/errors and zero DX12 validation errors,
accepted committed screenshot comparisons, and reproduced the 44,401-line
physics CSV byte-exactly. The authoritative scrub run produced 2,401 ticks and
passed durable-artifact, causal-reveal, determinism, and false-pass controls
without refreshing an artifact.

## Independent Review

Run `wide-signature-decomposition-round-3-duck-01` used the `rubber-duck`
skill in strict read-only mode after implementation. Reviewer task:
`/root/wide_signature_round3_duck_01`; prompt 1,391 characters, response 3,999
characters, token count unavailable, elapsed approximately seven minutes.

Verdict: approved with no blocking or material code findings. The reviewer
confirmed the zero-row inventory, exact 16/16 checklist reconciliation,
one-for-one producer mappings, synchronous lifetime safety, explicit mutable
owners, preserved UI/physics/render/Replay order, and absence of new allocation,
callback, inheritance, lookup, dependency, or growth privileges.

The only non-blocking findings were the explanatory-body omission recorded
above and the need to distinguish the abandoned build-only fidelity attempt
from the completed retry. Residual risk is low: Scene-tab hit-testing and
`PrepareRenderFrame` lack focused unit coverage, so confidence rests on the
mechanical one-for-one mappings, independent inspection, and broad integration
gates.
