# Replay R4 Live-Owner And Identity Closure — 2026-07-11

## Outcome

Replay capture, workspace input, picking, velocity editing, prediction, render
overrides, cause overlays, and path overlays now borrow explicit physics,
body/collider, render-presentation, and scene-entity owners. Production replay
paths no longer repair or traverse `GameModelCollection` to reach those owners.
The debug automation `ReplayLiveWorld` remains the composition fixture for
probe scene construction; it is not retained by `ReplayRuntime`.

Replay-owned headers now store `ReplayBodyId` as identity and
`Physics::ModelRowHint` only as a repairable lookup cache. The v2 artifact keeps
its existing four-byte dictionary slot for wire compatibility, writes dictionary
order there, and treats it as ordering data rather than identity.

Restore resolves every sampled `ReplayBodyId` to a live handle before mutation,
then writes body and solver state through `PhysicsEngine`. `SceneController`
coordinates the cold topology shrink across physics, presentation, render, and
entity rows. Removed body handles are retired by `PhysicsBodyStore`.

## Deletion And Narrowing Proof

- `ReplayCaptureInput` has no model-collection pointer.
- Replay render-pose application accepts `RenderInstanceStore`,
  `PhysicsBodyStore`, and `ColliderStore` directly.
- Replay workspace input accepts `PhysicsEngine`, `SceneEntityStore`, and render
  presentation records directly.
- Replay overlay and prediction code receives `PhysicsEngine` plus
  `SceneEntityStore`; it no longer reads `GameModel` presentation timers or
  invokes model-topology repair.
- Velocity mutation and replay restore call `PhysicsEngine` directly.
- No replay header has a bare model-index data member. Frame-local API
  parameters may still use an `int` row cursor, while retained state uses typed
  hints paired with stable replay IDs.
- The old collection-wide `TrimModelsForReplayRestore` command is deleted;
  `SceneController::TrimForReplayRestore` owns cross-owner ordering and the
  collection exposes only its presentation-row operation.

## Comment Audit

All 29 touched source-bearing files were inspected against
`Agentic/Reference/comment-style-guide.md`. The 26 production files retain full
learning headers and the changed identity, wire-compatibility, topology,
allocation, and lifetime-sensitive paths have nearby invariant/why comments.
The three touched doctest files contain only focused field/API updates and do
not require learning headers.

## Validation

- `tools\validate_all_cpu_tests.bat` — passed in 12.5s: 124/124 doctest cases,
  2698 assertions, runtime interaction policy Debug/Release, scene parser, and
  DX12 architecture tests.
- `tools\validate_replay_scrub.bat` — final pass in 81.1s; Debug/Profile replay
  scrub, restore, branch, and prediction probes passed.
- `tools\validate_interaction_clicks.bat` — passed in 8.9s; inspect gizmo and
  replay prediction reports both wrote `ok=1`.
- `tools\validate_physics.bat` — passed in 13.3s; standalone smoke passed and
  `physics_regression_solver.csv` matched 20,001 lines byte-exactly.
- `tools\validate_dx12_renderer.bat` — final pass in 45.2s after formatting only
  stopped two earlier attempts; zero InfoQueue errors and all three committed
  screenshot baselines passed.
- `tools\validate_full.bat` — passed in 53.9s; format/metadata/build, CPU lanes,
  DX12, standalone physics, and byte-exact physics all passed with zero build
  warnings.

No baseline was changed.
