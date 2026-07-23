# Wide-Signature Parameter-Bag Remediation Closure

Date: 2026-07-23
Owner: skullbonez
Plan: `wide-signature-parameter-bag-remediation` B0-B5
Branch: `nightrunner-22nd-JUL-26`
PR: `#131`

## Result

The reopened campaign is complete. Every input, request, context, descriptor,
or call pack introduced or explicitly blessed by the three wide-signature
campaigns was re-audited for independent domain identity. Mechanical parameter
objects were removed through operation decomposition, direct construction of
real output values, or explicit parameters within the accepted 12-parameter
ceiling.

The final tracked-source threshold-13 inventory is empty. No replacement
packet, callback pack, service/context/bindings bag, stored transient owner,
forwarding facade, allocation, inheritance seam, or hidden owner reach-back was
introduced. No baseline, golden, screenshot, Replay artifact, physics CSV,
schema, configuration, or allocation-policy inventory was refreshed.

This report supersedes the no-bag and architectural-closure claims in:

- `wide-signature-reduction-closure.md`
- `wide-signature-decomposition-round-2-closure.md`
- `wide-signature-decomposition-round-3-closure.md`

Their historical implementation and validation evidence remains valid; this
report is authoritative for the final parameter-object census and rulings.

## History Reconciliation

The final audit reconstructed campaign-introduced types from the commit ranges
rooted at `09572af8`, `29b94ab7`, `c92cb781`, `1b1eee9b`, `8521f116`,
`d8c6f969`, `1111316a`, `c356a375`, `b7b16a48`, `86ac6df9`, `aabbf5af`, and
`c27aa400`, then reconciled that history against current source.

The following mechanical shapes are absent:

| Area | Removed mechanical shapes |
|---|---|
| UI / Gameplay | `InGameUIInputFrame`, `SceneTabOpenComboFrame`, `SceneTabDrawFrame`, `FxQuadCorners`, `FxQuadStyle`, `FxQuadTerrain` |
| Editor | `EditorTreePartDesc` |
| Physics | `PhysicsNarrowphaseWakeAccess::SleepRows` constructor pack |
| Rendering | `Texture2DUploadDesc`, `InstancedMeshCreateDesc`, `InstancedMeshUploadTarget`, `RenderModelPassInput`, `ShadowMapRenderSelection`, `RuntimeRenderInputs`, `RuntimeRenderServices` |
| Replay interaction | `ReplayCauseTreeInputSources`, `ReplayVelocityEditInputSources`, extracted-operation reuse of `ReplayWorkspaceFrameInput`, `ReplayLoadedPresentationActivationRequest`, `ReplayScrubberGestureInput`, `ReplayScrubberSurfaceDesc` |
| Replay prediction / presentation | `ReplayPredictionFrameRequest`, `ReplayPredictionJobDesc`, `ReplayFutureNodeDesc`, `ReplayRibbonVertexDesc`, `ReplayProbeVisualProjectionDesc`, `ReplayRenderPreparationInput` |
| Replay restore | `ReplayRestoreEditorPlaceEventDesc`, `ReplayV2TargetRestoreDiagnosticInput` |

`RuntimeRenderInputs` was a one-field wrapper and `RuntimeRenderServices` was a
multi-owner service bag. Both are gone. `RuntimeRenderer` now takes the actual
top-level frame transaction, derives frame values, and uses its concrete
owners where resource or debug-graph work is performed.

`ReplayWorkspaceFrameInput` remains only as the top-level input-turn message at
`TickWorkspace`; no extracted operation receives or copies it.

## Retained Domain Values

The audit retained values whose identity does not depend on function arity:

- `PhysicsBodyRestoreState` is a complete body-state restore record.
- `PhysicsBodyCreateDesc` is the stored and mutated authored-body recipe.
- `RuntimeRenderModelFrameView` is the scene-to-render frame publication.
- `ReplayScrubberSurfaceInput` is normalized control availability plus frame
  layout consumed by surface construction.
- `ReplayPathPickInput` is the pointer-ray/camera interaction value.
- `ReplayOverlayBuildInput` is a cohesive three-field overlay operation value.
- Replay probe/result diagnostics are stable emitted records.
- Prediction results and other output/effect records are producer-owned
  results, not input indirection.

## Commit Ledger

- `1da77442` — reopen mechanical parameter objects
- `89641d6f` — remove UI and Gameplay call packs
- `bcc7f474` — remove Physics and Rendering packs
- `fbcb1ccd` — split Replay interaction operations
- `bbb2929f` — split Replay prediction and render phases
- closure commit — reconcile missed history, remove the remaining descriptors
  and Runtime render bags, and record final evidence

## Static Proofs

- `python tools\inventory_wide_signatures.py --threshold 13 --format json`:
  `[]`.
- Mechanical removed-type scan: zero rows.
- `ReplayWorkspaceFrameInput` occurs only at construction, declaration, and
  definition of `TickWorkspace`.
- Core dependency-direction proof: zero rows.
- Physics/Rendering upward-dependency proof: zero rows.
- Gameplay upward-dependency proof: zero rows.
- Downward `Runtime/Replay` include proof: zero rows.
- Allocation-policy repository scan: 429 files, 30 direct-heap findings, 129
  dynamic-STL-member findings, 645 STL-growth findings, zero allowlist errors.
- Project/filter reconciliation: 745 items, zero errors.

## Validation

| Command | Time | Result |
|---|---:|---|
| B1 `tools\validate_fast.bat` | recorded at B1 | PASS |
| B2 `tools\validate_physics.bat` | 76.2 s | PASS; deterministic byte-exact regression |
| B2 `tools\validate_dx12_renderer.bat` | 54.4 s | PASS; zero DX12 errors |
| B2 `tools\run_graphics_stress.bat 1` | 61.2 s | PASS; crash-free bounded run |
| B2 `tools\validate_perf.bat` | 82.3 s | PASS; no allocation or performance regression |
| B4 focused Replay doctests | 2.3 s | PASS; 53 cases / 799 assertions |
| B5 `tools\validate_fast.bat` | 55.4 s | PASS; zero warnings/errors |
| B5 `tools\validate_replay_allocation_policy.bat` | 4.1 s | PASS; strict two-generation policy clean |
| B5 `tools\validate_replay_v2_artifact.bat` | 63.4 s | PASS |
| B5 `tools\validate_replay_scrub.bat` | 432.5 s | PASS; authoritative visual-fidelity gate, 2,401 ticks, all controls |
| B5 `tools\validate_full.bat` | 115.7 s | PASS; CPU/coverage umbrella and five runtime lanes |

`validate_replay_scrub.bat` is the historical wrapper for
`validate_replay_visual_fidelity.bat`; its successful invocation executes the
authoritative visual-fidelity gate and therefore proves both required commands
without launching a duplicate engine generation.

The first B5 scrub invocation exceeded the calling shell's five-minute timeout
during the active 6,800-frame engine run. Its wrapper was terminated before
the post-run oracle and it is not validation evidence. The PID-tracked rerun
and its captured exit status are the sole final scrub/fidelity evidence.

The final broad gate passed all 346 doctest cases / 68,715 assertions, the
coverage floors, and all five runtime processes. It reported zero build
warnings/errors, zero DX12 validation errors, three committed screenshot
comparisons accepted, and a byte-exact 44,401-line physics CSV.

## Comment Audit

The source-of-truth checklist is
`Agentic/Plans/TODO/wide-signature-parameter-bag-remediation.md`. It reconciles
exactly to the campaign diff from `b827f276`: 55/55 changed source-bearing
files checked, zero missing, zero extra, and zero deferred.

## Independent Review

One final hostile, read-only no-bag review reconstructed the campaign history,
inspected the current diff and retained records, and returned no blocking or
non-blocking findings. It confirmed:

- all targeted mechanical types are absent;
- the threshold-13 inventory is empty;
- no replacement struct or class was introduced;
- `RuntimeRenderInputs` and `RuntimeRenderServices` are gone;
- retained values have independent domain, event, publication, or input-turn
  identities; and
- Physics wake access exposes behavior rather than raw sleep rows.

The review's sole residual risk was behavioral equivalence of the Replay
prediction/render phase splits, explicitly assigned to the final Replay and
broad runtime gates above.
