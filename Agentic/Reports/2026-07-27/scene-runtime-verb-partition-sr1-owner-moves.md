# Scene Runtime Verb Partition Consolidation — SR1 Owner Moves

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: SR1

## Result

The SR0 state-owning destinations are implemented without changing scene load,
reset, style, UI, defaults, queue, or filesystem behavior.

Seven verb-named implementation units are gone:

- `SceneRuntimeCreate.{h,cpp}`;
- `SceneRuntimeDefaults.{h,cpp}`;
- `SceneRuntimeLoad.{h,cpp}`;
- `SceneRuntimeReset.{h,cpp}`;
- `SceneRuntimeUiOptions.{h,cpp}`;
- `SceneRuntimeCoordinator.cpp`;
- `SceneRuntimeStyle.cpp`.

Their operations now sit behind the owner that mutates the state:

| Responsibility | Owner implementation |
|---|---|
| Scene creation | `SceneController::CreateScene` in `SceneController.Creation.cpp` |
| Reset/advance/UI request submission | `SceneController` in `SceneController.Navigation.cpp` |
| Live style, cinematic browser style, demo-hero style | `SceneController` in `SceneController.Style.cpp` |
| Browser discovery/current-path lookup | `UI::SceneNavigationModel` / `RunSceneBrowserState` in `SceneNavigationModel.Browser.cpp` |
| Ordinary/cinematic config persistence | private `RenderDefaultsStore` operations in `RenderDefaultsStore.Persistence.cpp` |
| Load preparation/commit | private `SceneLoadTransaction` operations in `SceneLoadTransaction.Preparation.cpp` |
| Reset capture/restore/override clearing | private `SceneLoadTransaction` operations in `SceneLoadTransaction.Reset.cpp` |
| Authored UI preparation/presentation | private `SceneLoadTransaction` operations in `SceneLoadTransaction.Presentation.cpp` |

The GV2 outer phase walk remains exactly
`Idle → Load → RuntimeReactions → Presentation → Complete`. Reset capture still
precedes load mutation, restoration still follows population, and authored UI
activation still occurs only in Presentation. GV3 was not changed.

## Topology And Cleanup

- Production and test project/filter rows were updated with each move.
- The JSON cold-boundary rule follows `SceneController.Creation.cpp`.
- Allocation-policy allowlist rows follow the creation, defaults-persistence,
  and load-preparation implementations; no authority or capacity changed.
- All removed public helpers have zero source/test call sites.
- Remaining `SceneRuntime*` units are the cohesive queue/session owner,
  value-only coordinator header, pure cinematic policy header, and the
  GV3-ratified generated-control transaction. SR2 owns their residual names.

## Validation

- `Profile\SKULLBONEZ_TESTS.exe`: pass, complete unit suite.
- `tools\validate_fast.bat`: pass; format, 784/784 production project/filter
  rows, dependency graph, aggregate/scar governance, Profile/Debug builds, and
  complete unit suite.
- `tools\validate_physics.bat`: pass; Debug build and byte-exact Physics
  determinism gate.
- `tools\validate_full.bat`: pass in 244.1 seconds; mandatory CPU preflight and
  all runtime lanes passed. DX12 run `20260727T024014Z` reported zero validation
  errors and accepted all three committed baselines without refresh.

The first full invocation exceeded the caller's five-minute capture window
after producing a passing DX12 summary. A fresh bounded invocation completed
normally; no baseline, schema, config, replay, Physics, or visual artifact was
updated.
