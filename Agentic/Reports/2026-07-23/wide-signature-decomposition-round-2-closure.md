# Wide Signature Decomposition Round 2 Closure

Date: 2026-07-23
Owner: skullbonez
Plan: `wide-signature-decomposition-round-2` D0-D4
Branch: `nightrunner-22nd-JUL-26`

## Result

The campaign is complete. All eight tracked functions with at least 16
parameters were removed, and the final tracked-source threshold-16 scan returns
zero rows. The replacements expose real action, lifecycle, and value-lifetime
boundaries rather than hiding the same authority behind a context or service
bag.

Mutable physics, gameplay, scene, Replay, camera, interaction, input, config,
force, and worker owners remain explicit. New aggregates contain scalar values,
read-only spans/references, or one synchronous solver-sample borrow; none retain
mutable subsystem authority. No behavioral baseline, golden, screenshot,
Replay artifact, physics CSV, schema, configuration, or allocation-policy
inventory was refreshed.

## Decomposition

| Original row | Replacement |
|---|---|
| `SceneTab::HandleContentClick` (16) | Ordered header, closed-combo, and time-scale actions |
| `ApplySceneLoadConsumerOutputs` (16) | Runtime reactions followed by external presentation outputs |
| `ReplayPresentationOperations::ActivateLoadedPresentation` (17) | Scalar activation request; mutable owners remain explicit |
| `ReplayRuntime::TickScrubberInput` (21) | Existing frame-local `ReplayWorkspaceFrameInput` |
| `ReplayAuthoring::TickCauseTreeInput` (24) | Read-only cause-tree sources plus workspace input |
| `ReplayAuthoring::TickVelocityEditInput` (24) | Read-only velocity-edit sources plus explicit mutable physics |
| `UpdateReplayPrediction` (22) | Deleted; implementation moved into the prediction owner |
| `ReplayPrediction::UpdateFrame` (19) | Six explicit owners, value-only frame request, and result |

The prediction change is not a forwarding facade: the free helper no longer
exists, and `ReplayPrediction::UpdateFrame` directly owns the scheduling,
worker, cache, and publication transition.

## Inventory And Static Proofs

- `python tools\inventory_wide_signatures.py --self-test`: PASS.
- `python tools\inventory_wide_signatures.py --threshold 16 --format json`:
  `[]`.
- CodeGraph was synchronized against the final source: five changed files,
  224 nodes.
- Core dependency-direction proof: zero rows.
- Physics/Rendering upward-dependency proof: zero rows.
- Gameplay upward-dependency proof: zero rows.
- Downward `Runtime/Replay` include proof: zero rows.
- Campaign-wide comment audit: 20/20 touched source-bearing files checked,
  zero deferred.

## Commit Ledger

- `f84f2899` — ratify owner-safe decomposition.
- `cd974858` — split UI and scene composition.
- `1111316a` — narrow Replay input and activation.
- `c356a375` — collapse Replay prediction forwarding.

## Validation

| Command | Time | Result |
|---|---:|---|
| D3 focused Profile build | 11.3 s | PASS; 0 warnings, 0 errors |
| D3 `tools\validate_full.bat` | 135.0 s | PASS |
| D3 direct `tools\validate_replay_visual_fidelity.bat` | 425.5 s | PASS; one process/generation/presentation |
| D4 inventory/static proofs | 28.0 s | PASS; zero threshold-16/dependency rows |
| D4 `tools\validate_replay_allocation_policy.bat` | 4.3 s | PASS; strict two-generation probe |
| D4 `tools\validate_replay_v2_artifact.bat` | 33.6 s | PASS; save/restore and hashes |
| D4 `tools\validate_replay_scrub.bat` | 434.9 s | PASS; authoritative visual-fidelity alias |
| D4 `tools\validate_full.bat` | 101.7 s | PASS |

The final broad gate passed its CPU/coverage umbrella and five engine
processes, reported zero build warnings/errors and zero DX12 validation errors,
accepted all committed screenshot comparisons, and reproduced the 44,401-line
physics CSV byte-exactly. The scrub command is the repository's authoritative
alias for visual fidelity; its single engine process produced 2,401 ticks,
17/17 cases, 75/75 assertions, durable and causal proof, and all false-pass
controls without an artifact refresh.

## Independent Review

Run `wide-signature-decomposition-round-2-duck-01` used the `rubber-duck`
skill in strict read-only mode after implementation. Reviewer task:
`/root/wide_signature_round2_duck_01`; prompt 1,483 characters, response 3,059
characters, token count unavailable, elapsed approximately two minutes.

Verdict: PASS with no blocking or non-blocking findings. The reviewer confirmed
that action ordering is preserved, scene-load reactions precede presentation at
all seven callers, the Replay aggregates have bounded synchronous lifetimes,
the prediction helper is genuinely deleted, mutable owners remain explicit,
and no allocation, dependency, or Replay-boundary privilege was introduced.
Residual risk is limited to the lack of focused unit tests for legacy scene-tab
clicks and scene-load ordering; the order-preserving diffs and broad runtime
validation provide the closure evidence.
