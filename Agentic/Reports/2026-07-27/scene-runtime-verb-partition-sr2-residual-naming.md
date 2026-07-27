# Scene Runtime Verb Partition SR2 — Residual Naming

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Result: PASS

## Naming Resolution

| Residual name | Resolved name / shape |
|---|---|
| `SceneRuntime.{h,cpp}` | `SceneSessionState.{h,cpp}`; `SceneSession` owns the request queue, session state, and lifecycle ledger |
| `SceneRuntimeCoordinator.h` | `SceneLoadRequest.h`; the header contains the value used to request a scene load |
| `SceneRuntimeGeneratedControls.{h,cpp}` | `SceneGeneratedControlTransaction.{h,cpp}`; the existing GV3 transaction now carries its owner name |
| `SceneRuntimeStyle.h` | `SceneCinematicPolicy.h`; the surviving operation is a pure cinematic policy |
| `SceneRuntimeUICommandResult` | `SceneUICommandSubmissionResult` |
| `SceneRuntimeGeneratedControlAction` | `SceneGeneratedControlAction` |

`SceneController` publicly extends the lightweight `SceneSession` owner. This
folds the old `SceneRuntime` surface into the controller without a forwarding
facade or a `Runtime()` sub-owner escape hatch, while preserving a render-free
owner that unit tests can instantiate. `SceneSession` owns the queue, state, and
lifecycle ledger. `SceneController::RecordLifecycleEvent` adds the controller's
`SceneWorld` topology-publication precondition and then records through that
owner.

There is no `class SceneRuntime`, no `SceneController::Runtime()` accessor, and
no new context or service bag.

## Acceptance Evidence

Tracked-source search:

```text
git grep -n -E 'SceneRuntime(Create|Style|UiOptions|Defaults|GeneratedControls|Reset|Coordinator)|SceneRuntime\.(h|cpp)' -- SkullbonezSource SkullbonezTests ':!*.md'
```

returns zero rows. Project/filter validation reports 784/784 production items
and 113/113 test items with zero errors.

All moved includes, dependency fixtures, ownership rulings, allocation-policy
entries, and Visual Studio project/filter declarations use the resolved owner
or domain names. The Automation build additionally caught two tracked sources
hidden from the default ripgrep view; both now include
`SceneSessionState.h`.

## Validation

- `tools\validate_tests.bat`: PASS, 417/417 unit tests.
- `tools\validate_fast.bat`: PASS, including format, metadata, dependency,
  ownership/scar, size, Profile build, and tests.
- `tools\validate_physics.bat`: PASS, byte-exact 44,401-line oracle.
- `tools\validate_automation.bat`: PASS, including the Profile negative
  boundary and Automation replay, prediction, and development-UI smoke.
- `tools\validate_full.bat`: PASS in 344 seconds from the final source state.
  Mandatory CPU preflight and all runtime lanes passed; Debug built; DX12 run
  `20260727T031622Z` reported zero InfoQueue errors and matched
  `water_ball_test`, `solver_smoke`, and `space_three_body`; Physics remained
  byte-exact. No baseline was refreshed.
