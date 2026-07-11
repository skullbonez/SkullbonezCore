# Stale Plan Reference Cleanup 15.6 Checklist

Date: 2026-07-10
Status: Complete — 86/86 scoped files inspected
Impact area: source/test/tool learning-header references
Owner: engine-cleanup review gap 15.6
Scope source: tracked source-bearing files with a reference to a missing plan
as measured on 2026-07-10

## Rules

A checkbox is marked only after every missing plan reference in that file is
retargeted to a live owner or deleted. Replacement text must describe a current
code/ownership contract; do not replace one dead historical breadcrumb with
another. Inspect nearby comments for meaning, but keep edits strictly
comment/documentation-only.

If a file needs behavioral code or include changes, leave it unchecked and move
that change to its owning source plan and validation gate.

## Inventory

- [x] `Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp`
- [x] `SkullbonezSource/Core/AmortizedTask.h`
- [x] `SkullbonezSource/Core/FatalError.cpp`
- [x] `SkullbonezSource/Core/FatalError.h`
- [x] `SkullbonezSource/Core/Fence.h`
- [x] `SkullbonezSource/Core/LockOrderValidator.h`
- [x] `SkullbonezSource/Core/MainMemoryStats.h`
- [x] `SkullbonezSource/Core/SbResult.h`
- [x] `SkullbonezSource/Core/WorkerPool.h`
- [x] `SkullbonezSource/Physics/BuoyancySystem.h`
- [x] `SkullbonezSource/Physics/DisjointSet.h`
- [x] `SkullbonezSource/Physics/PhysicsApi.cpp`
- [x] `SkullbonezSource/Physics/PhysicsApi.h`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [x] `SkullbonezSource/Physics/PhysicsEngineStoreQueries.h`
- [x] `SkullbonezSource/Physics/PhysicsFixedList.h`
- [x] `SkullbonezSource/Physics/PhysicsHandles.h`
- [x] `SkullbonezSource/Physics/PhysicsScene.h`
- [x] `SkullbonezSource/Physics/SolverBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/TornadoGameplay.cpp`
- [x] `SkullbonezSource/Physics/TornadoGameplay.h`
- [x] `SkullbonezSource/Rendering/DrawCallTrace.cpp`
- [x] `SkullbonezSource/Rendering/DrawCallTrace.h`
- [x] `SkullbonezSource/Rendering/RenderMaterial.h`
- [x] `SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.h`
- [x] `SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h`
- [x] `SkullbonezSource/Runtime/Audio/ContactAudioService.h`
- [x] `SkullbonezSource/Runtime/DemoDirector.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [x] `SkullbonezSource/Runtime/RunCameraState.h`
- [x] `SkullbonezSource/Runtime/RunDemoDirector.cpp`
- [x] `SkullbonezSource/Runtime/RunDemoDirector.h`
- [x] `SkullbonezSource/Runtime/RunSubsystemState.h`
- [x] `SkullbonezSource/Runtime/RuntimeCommandQueue.h`
- [x] `SkullbonezSource/Runtime/RuntimeInteractionController.h`
- [x] `SkullbonezSource/Runtime/RuntimePickGeometry.h`
- [x] `SkullbonezSource/Runtime/RuntimePickService.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneControllerState.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h`
- [x] `SkullbonezTests/TestAssetSystem.cpp`
- [x] `SkullbonezTests/TestBounds.cpp`
- [x] `SkullbonezTests/TestConvexHull.cpp`
- [x] `SkullbonezTests/TestDemoDirector.cpp`
- [x] `SkullbonezTests/TestDeterminism.cpp`
- [x] `SkullbonezTests/TestDx12OnlyRuntime.cpp`
- [x] `SkullbonezTests/TestGeometricMath.cpp`
- [x] `SkullbonezTests/TestMatrix4.cpp`
- [x] `SkullbonezTests/TestPersistentContactSolver.cpp`
- [x] `SkullbonezTests/TestPhysicsHandles.cpp`
- [x] `SkullbonezTests/TestQuaternion.cpp`
- [x] `SkullbonezTests/TestRenderResourceDoubles.h`
- [x] `SkullbonezTests/TestReplayRecorder.cpp`
- [x] `SkullbonezTests/TestReplayRecorderFullCaptureBoundary.cpp`
- [x] `SkullbonezTests/TestReserveAllocator.cpp`
- [x] `SkullbonezTests/TestRuntimeInputBindings.cpp`
- [x] `SkullbonezTests/TestSceneParserUnit.cpp`
- [x] `SkullbonezTests/TestSolverBroadphaseStage.cpp`
- [x] `SkullbonezTests/TestSpatialGrid.cpp`
- [x] `SkullbonezTests/TestTerrain.cpp`
- [x] `SkullbonezTests/TestVector3.cpp`
- [x] `tools/check_allocation_policy.py`
- [x] `tools/check_dx12_baselines.py`
- [x] `tools/validate_dx12_renderer.bat`

## Acceptance

- [x] Re-run the exact scoped `git ls-files`/reference inventory and reconcile
  it with this checklist: zero missing, extra, or duplicate files.
- [x] No source/test/tool comment references a plan path that does not exist.
- [x] Remaining plan references point to the current owning plan, not a deleted
  completion artifact.
- [x] Diff inspection proves comment/documentation-only changes.
- [x] `git diff --check` passes.

Evidence (2026-07-10): the tracked source-bearing inventory and checklist both
contain 86 unique files with zero missing, extra, or duplicate rows. The scoped
diff contains 86 learning-header/comment hunks and no behavior, include, or
literal changes; all referenced live plan paths resolve, and `git diff --check`
passes.

## Validation

Comment/documentation-only: no repository validation. If an include or behavior
change is required, it leaves this checklist and uses the file-to-validation
mapping.
