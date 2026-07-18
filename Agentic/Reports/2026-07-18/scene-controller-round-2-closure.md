# SceneController Decomposition Round 2 Closure

Date: 2026-07-18
Branch: `nightrunner-17th-july`
Plan: `scene-controller-decomposition-round-2` (S0-S6, 7/7)

## Outcome

The round-2 decomposition is complete. `SceneController` is now the scene
lifecycle/request coordinator and composes one concrete `SceneWorld` owner.
`SceneWorld` owns the scene-lifetime entity store, physics engine, cameras,
terrain, world environment, and render-instance store. Callers borrow that
cohesive owner or typed values; the controller does not retain UI navigation,
consumer owners, callback packs, host pointers, or compatibility facades.

Final measured surfaces:

- `SceneController`: 43 public declarations.
- Load participants: 18 concrete owners: 6 policy, 3 host, 5 interaction, and
  4 presentation, down from 22 before S5.
- Navigation crosses the load boundary as a detached value snapshot.
- Window, UI, contact-audio, automation, and navigation effects return as
  typed `SceneLoadConsumerOutputs` values.
- `SceneWorld` remains one cohesive scene-lifetime owner rather than a mutable
  multi-domain process bag: its six domains share reload/reset lifetime and
  typed subowner boundaries.

## S6 Ownership Remediation

The independent review was deliberately repeated until it produced zero
material findings. Credible findings reopened S6 and removed these remaining
authority tunnels:

- Same-batch successful load followed by defaults persistence now observes
  the committed navigation output rather than a stale submitted snapshot.
- Replay restore, validation probes, UI text, runtime tuning, contact audio,
  automation reports, editor tools, stress style, and renderer construction
  borrow one `SceneWorld` or consume typed scalar/value inputs.
- Physics diagnostics/startup helpers borrow `PhysicsEngine` directly; input
  diagnostics receive scalar authored-scene/entity facts.
- The renderer-side `RenderSceneView` duplicate view was deleted.
- Attached-camera and `RunCameraState` APIs derive Cameras and Terrain from one
  `SceneWorld` borrow.
- `DemoDirectorPlayback::Tick` derives its camera collection from the same
  `SceneRuntimeStyleContext::world` used for phase-style mutation.

The sixth read-only independent pass confirmed zero material ownership
findings across the logical module: no duplicated world/subowner signature,
retained controller/world pointer, callback/back-pointer/friend/`void*` reach
back, forwarding facade, broad replacement context, or successor god object.

## Comment Quality Gate

The checklist at
`Agentic/Reports/2026-07-18/scene-controller-round2-s6-comment-audit.md`
reconciles the final `git diff --name-only` inventory:

- 62 checked source-bearing files.
- 0 deferred.
- 0 unchecked.
- 62/62 contain the required learning-header sections plus local ownership,
  lifetime, invariant, or hazard comments where the changed seam needs them.

## Validation

The desktop tool session cannot open a separate visible console, so commands
ran through the available terminal and mirrored output to the named logs.

- Profile, Automation, and Debug `SKULLBONEZ_CORE` builds: all passed with
  0 warnings and 0 errors. Final Director repair MSBuild times were 9.18s,
  9.37s, and 8.38s respectively.
- Profile `SKULLBONEZ_TESTS` build: 0 warnings/errors; focused
  `Profile\SKULLBONEZ_TESTS.exe --test-case=*Scene*` passed 28/28 cases and
  726/726 assertions in 2.004s.
- `tools\validate_project_filters.bat`: passed 728/728 production project and
  filter items in 1.737s.
- `python tools\check_allocation_policy.py --self-test`: passed in 0.135s.
- `python tools\check_allocation_policy.py --repo .`: final rerun passed in
  8.925s with `scanned=381` and `allowlist_errors=0`. The first run correctly
  rejected an unnecessary new stale allowlist row; the row was removed rather
  than weakening the checker.
- `tools\validate_fast.bat`: final run passed after scoped clang-format repair;
  formatting, project filters, Profile/Debug zero-warning builds, and 291/291
  doctest cases with 21,455/21,455 assertions passed. Log:
  `TestOutput/agent_logs/scene_s6_validate_fast.log`.
- `tools\validate_full.bat`: passed in 143.261s. The mandatory CPU umbrella
  and all coverage floors passed; Automation replay/prediction smoke passed;
  DX12 reported zero validation errors and all committed captures passed;
  physics standalone/handle smoke passed and the 44,401-line regression CSV
  matched byte-for-byte. Log:
  `TestOutput/agent_logs/scene_s6_validate_full.log`.
- The sole S6 invocation of
  `tools\validate_replay_visual_fidelity.bat` passed in 435.698s: 2,401 ticks,
  200 moved wall bricks, 187 toppled wall bricks, 199 causal nodes, one
  presented cascade, 62 durable saved/loaded ticks, and every false-pass
  control detected its injected divergence. Log:
  `TestOutput/agent_logs/scene_s6_replay_visual_fidelity.log`.

No behavioral baseline, physics baseline, replay golden, screenshot baseline,
coverage floor, scene, or authored-data file was refreshed.

## Handoff

The plan leaves the active ledger under inventory rule 4. Round-7 execution
continues with `dx12-backend-ownership-decomposition` D0; the scene dependency
for `naming-and-identity-debt` is now satisfied.
