# Wide-Signature Parameter-Bag Remediation

Date: 2026-07-23
Owner: skullbonez
State: In progress
Ledger tasks: 6 (B0-B5)
Branch: `nightrunner-22nd-JUL-26`
PR: `#131`

## Goal

Correct every mechanical parameter object introduced or repurposed by the
three wide-signature campaigns. A type fails this audit when it exists mainly
to replace an argument list and its consumer immediately aliases or forwards
the fields. Removing mutable owners from such a packet is not sufficient.

The repair must decompose the underlying operation into cohesive phases or
restore explicit parameters where the accepted 12-parameter ceiling permits.
It must not replace one packet with several cosmetic packets, stored transient
host state, a callback, a service/context/bindings object, or an owner
reach-back.

## Census And Rulings

| Type or usage introduced by the campaigns | Ruling | Required treatment |
|---|---|---|
| `InGameUIInputFrame` | Mechanical bag | Split expanded and minimized input operations; remove the type |
| `SceneTabOpenComboFrame` | Mechanical bag | Split option/reset actions or use explicit values under the ceiling |
| `SceneTabDrawFrame` | Mechanical bag | Split Scene-tab draw phases by responsibility |
| `FxQuadCorners`, `FxQuadStyle`, `FxQuadTerrain` | Mechanical call packs | Remove `EmitFxQuad`; emit four vertices through the existing vertex primitive |
| `PhysicsNarrowphaseWakeAccess::SleepRows` | Mechanical constructor pack | Establish a real sleep-row owner/capability boundary |
| `Texture2DUploadDesc` | Mechanical creation descriptor introduced for arity | Use spans, derived counts, and explicit typed policy |
| `InstancedMeshCreateDesc` | Mechanical creation descriptor introduced for arity | Split static and instance layout creation phases |
| `InstancedMeshUploadTarget` | Mechanical private call pack | Read bound cold-upload owners in the owning operation |
| `RenderModelPassInput` | Mechanical bag | Decompose visibility selection and primitive submission |
| `ShadowMapRenderSelection` | Mechanical bag | Make invariant object rendering structural and terrain choice explicit |
| `ReplayCauseTreeInputSources` | Mechanical source pack | Split cause-tree actions around the data each action uses |
| `ReplayVelocityEditInputSources` | Mechanical source pack | Split target picking and drag mutation phases |
| `ReplayWorkspaceFrameInput` passed wholesale to extracted operations | Mechanical reuse | Keep it only at `TickWorkspace`; pass action facts onward |
| `ReplayLoadedPresentationActivationRequest` | Mechanical bag | Split activation, camera exit, scrubber arming, and camera entry |
| `ReplayPredictionFrameRequest` | Mechanical bag | Split source/rebuild decision, worker advancement, and publication |
| `ReplayRenderPreparationInput` | Mechanical bag | Split pose, overlay/ghost, packet publication, and focus masking |
| `ReplayScrubberGestureInput` | Mechanical bag | Separate scrub-drag and prediction-horizon operations |
| `ReplayV2TargetRestoreDiagnosticInput` | Mechanical bag | Build the real result diagnostic at each producer |
| `PhysicsBodyRestoreState` | Retain: real domain value | One complete body-state restore record |
| `ReplayScrubProbeDiagnostic`, `ReplayRestoreProbeDiagnostic`, `ReplayRestoreResultDiagnostic` | Retain: real emitted records | Stable NDJSON event payloads |
| `ReplayPredictionUpdateResult` and other result/output records | Retain: real effects | Producer-owned effects, not input indirection |

Any additional campaign-introduced input/context/request/descriptor found during
implementation is presumed mechanical until the final independent review
records a concrete domain identity and a non-adapter consumer.

## Ledger

- [x] B0 — Reopen the closure claim, inventory all campaign-introduced or
  repurposed parameter objects, and ratify the rulings above.
- [x] B1 — UI and Gameplay. Remove the broad UI/Scene input/draw packets and
  tornado quad call packs through real phase decomposition.
- [ ] B2 — Physics and Rendering. Remove the sleep-row constructor pack, DX12
  creation packs, render-model pass packet, and shadow selection packet.
- [ ] B3 — Replay interaction and authoring. Remove workspace/source/activation/
  gesture shortcuts and split the actual actions.
- [ ] B4 — Replay prediction, rendering, and restore diagnostics. Remove the
  frame/render/diagnostic input packets and expose cohesive owner operations.
- [ ] B5 — Closure. Reconcile this census against campaign history and current
  source, audit every touched source file, prove threshold-13 and dependency/
  allocation rules, run one independent hostile no-bag review, execute every
  mapped gate, supersede the three earlier closure claims, and update PR #131.

## Acceptance

- Every row ruled Mechanical above is absent from source.
- No replacement input/context/request/descriptor carries the same fields under
  a new spelling, and no consumer immediately aliases a replacement packet.
- Top-level transaction messages and real result/event/resource values retain
  a domain identity independent of function arity.
- `python tools\inventory_wide_signatures.py --threshold 13 --format json`
  returns `[]`; 12 and below remain accepted.
- Mutable owners stay explicit; hot paths add no allocation, callback,
  inheritance, lookup, or owner reach-back.
- No baseline, golden, screenshot, Replay artifact, physics CSV, schema,
  configuration, or allocation-policy inventory is refreshed.

## Validation

- Iteration: focused Profile builds and focused tests only.
- B1: `tools\validate_fast.bat`.
- B2: `tools\validate_physics.bat`, `tools\validate_dx12_renderer.bat`,
  `tools\validate_perf.bat`, and `tools\run_graphics_stress.bat 1`.
- B3-B4: focused Replay doctests while iterating.
- B5: inventory/static proofs, `tools\validate_replay_allocation_policy.bat`,
  `tools\validate_replay_v2_artifact.bat`, `tools\validate_replay_scrub.bat`,
  and `tools\validate_full.bat`; mapped render/physics gates remain cumulative.

## Comment-Audit Checklist

- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.h`
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.cpp`
- [x] `SkullbonezSource/UI/UI.h`
- [x] `SkullbonezSource/UI/UI.cpp`
- [x] `SkullbonezSource/UI/UIInput.h`
- [x] `SkullbonezSource/UI/UIInput.cpp`
- [x] `SkullbonezSource/UI/UITabScene.h`
- [x] `SkullbonezSource/UI/UITabScene.cpp`
- [x] `SkullbonezSource/Gameplay/TornadoVisualPass.cpp`
- [ ] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- [ ] `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
- [ ] `SkullbonezSource/Rendering/RenderResourceTypes.h`
- [ ] `SkullbonezSource/Rendering/RenderInstanceRenderer.h`
- [ ] `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayCoordination.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayAuthoring.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayPresentation.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayPrediction.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp`
- [ ] `SkullbonezSource/Runtime/RunRender.cpp`

The checklist is reconciled against `git diff --name-only b827f276..HEAD` plus
the uncommitted closure diff. Newly touched source files must be added and
audited before B5 closes.
