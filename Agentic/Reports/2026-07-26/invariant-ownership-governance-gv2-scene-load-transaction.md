# Invariant Ownership Governance GV2 Scene-Load Transaction

Date: 2026-07-26

Plan phase: GV2 — Repair the scene-load transaction

Impact: Runtime/Scene ownership, Runtime/App and Runtime/Capture call sites,
focused owner-request tests, and Visual Studio project-filter metadata

## Result

GV2 is complete. `SceneLoadTransaction` is now the stack-scoped invariant
owner for a scene-load request batch. It owns only the detached request,
private output values, and `SceneLoadPhaseCursor`; every concrete runtime
owner is borrowed synchronously by the phase method that needs it.

The legal phase walk is:

`Idle -> Load -> RuntimeReactions -> Presentation -> Complete`

`SceneLoadTransaction::AdvanceOrFatal` rejects any other ordering through
lane F before the rejected phase can mutate an owner. A no-transition request
batch still enters `Load` through `FinishLoadPhase`, so all callers follow the
same reaction/presentation schedule.

## Ownership and arbitration

- `SceneController` retains cold scene mutation, queue/index state, lifecycle
  publication, and the fixed pending request batch.
- `SceneLoadTransaction` owns phase order, the load request value, private
  detached outputs, and mid-batch navigation/presentation arbitration.
- `SceneLoadConsumerOutputs` is no longer caller-visible.
- The transaction is non-copyable and stores no owner pointer or reference.
- Presentation arbitration uses loaded values only after this transaction has
  a real load request and the lifecycle reaches `AfterSceneCleared`.
- Navigation arbitration uses the committed loaded value only when the load
  set the transaction's apply flag.

The focused cursor test enumerates all 36 transitions among `Idle`, `Load`,
`RuntimeReactions`, `Presentation`, `Complete`, and the invalid `Count`
sentinel. It also proves the legal walk and that an illegal attempt leaves the
cursor usable for the next legal transition.

## Call-site migration

All previous scene-load consumers now construct one transaction and invoke
its phase methods:

- startup and scene-load-only automation in `Runtime/App/Run.cpp`;
- screenshot advancement and ordinary scene advancement in
  `Runtime/App/RunFrame.cpp`;
- direct input loads and the fixed pending request checkpoint in
  `Runtime/App/InputFrameExecution.cpp`;
- pending request execution in `Runtime/Scene/SceneRequestExecution.cpp`; and
- deterministic graphics-stress scene churn in
  `Runtime/Capture/RuntimeStressController.cpp`.

The stress-only Legacy UI suppression is now a bounded transaction operation;
it no longer reaches into the complete output record.

## Deletion proof

This command returns zero rows across source and tests:

```powershell
rg -n "SceneNavigationForFollowingRequest|ScenePresentationForFollowingRequest|SceneLoadConsumerOutputs|ApplySceneLoadRuntimeReactions|ApplySceneLoadPresentationOutputs" SkullbonezSource SkullbonezTests
```

The deleted free symbols were not replaced by aliases, forwarding wrappers,
callback packs, or a second retained state owner.

## Comment audit

Touched-file audit: 11 checked, 0 deferred, 0 unchecked. A subsystem checklist
was not required because this was a touched-file pass.

Audited files:

- `SkullbonezSource/Runtime/App/InputFrameExecution.cpp`
- `SkullbonezSource/Runtime/App/Run.cpp`
- `SkullbonezSource/Runtime/App/RunFrame.cpp`
- `SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp`
- `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`
- `SkullbonezSource/Runtime/Scene/SceneController.h`
- `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h`
- `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`
- `SkullbonezSource/Runtime/Scene/SceneWorld.h`
- `SkullbonezTests/TestOwnerRequestQueues.cpp`
- `tools/validate_project_filters.py`

The audit corrected the stale claim that `SceneController` owned load
transactions. The post-change headers distinguish cold scene mutation
(`SceneController`) from consumer phase order and arbitration
(`SceneLoadTransaction`), name the no-owner-reference invariant, and link the
focused test. The project-filter tool already documents its semantic-filter
invariant; the new Runtime/Scene prefix is declarative.

## Validation

- `tools\validate_tests.bat`: PASS; 394/394 cases and
  2,403,361/2,403,361 assertions.
- `tools\validate_project_filters.bat`: PASS; 783 production project items and
  783 filter items, zero errors.
- `tools\validate_full.bat`: PASS; mandatory CPU/coverage umbrella,
  Automation smoke, DX12 renderer validation with zero InfoQueue errors,
  committed screenshot comparisons, and byte-exact 44,401-line physics
  regression.
- Tracked baseline/config/scene/golden diff: empty.

One earlier full-gate attempt encountered the pre-existing exact-boundary
solver broadphase test intermittently. Its isolated rerun passed immediately;
the authoritative full-gate retry then passed the complete suite without any
source or baseline adjustment.

The plan-level independent ownership review remains GV4's required whole-plan
closure review. GV2's structural evidence is ready for that review: the
transaction's only retained fields are the request value, private outputs, and
phase cursor.
