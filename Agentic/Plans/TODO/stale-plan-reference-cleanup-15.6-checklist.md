# Stale Plan Reference Cleanup 15.6 Checklist

Date: 2026-07-10
Status: Planned — 0/86 scoped files inspected
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

- [ ] `Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp`
- [ ] `SkullbonezSource/Core/AmortizedTask.h`
- [ ] `SkullbonezSource/Core/FatalError.cpp`
- [ ] `SkullbonezSource/Core/FatalError.h`
- [ ] `SkullbonezSource/Core/Fence.h`
- [ ] `SkullbonezSource/Core/LockOrderValidator.h`
- [ ] `SkullbonezSource/Core/MainMemoryStats.h`
- [ ] `SkullbonezSource/Core/SbResult.h`
- [ ] `SkullbonezSource/Core/WorkerPool.h`
- [ ] `SkullbonezSource/Physics/BuoyancySystem.h`
- [ ] `SkullbonezSource/Physics/DisjointSet.h`
- [ ] `SkullbonezSource/Physics/PhysicsApi.cpp`
- [ ] `SkullbonezSource/Physics/PhysicsApi.h`
- [ ] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [ ] `SkullbonezSource/Physics/PhysicsEngineStoreQueries.h`
- [ ] `SkullbonezSource/Physics/PhysicsFixedList.h`
- [ ] `SkullbonezSource/Physics/PhysicsHandles.h`
- [ ] `SkullbonezSource/Physics/PhysicsScene.h`
- [ ] `SkullbonezSource/Physics/SolverBroadphaseStage.h`
- [ ] `SkullbonezSource/Physics/TornadoGameplay.cpp`
- [ ] `SkullbonezSource/Physics/TornadoGameplay.h`
- [ ] `SkullbonezSource/Rendering/DrawCallTrace.cpp`
- [ ] `SkullbonezSource/Rendering/DrawCallTrace.h`
- [ ] `SkullbonezSource/Rendering/RenderMaterial.h`
- [ ] `SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.h`
- [ ] `SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h`
- [ ] `SkullbonezSource/Runtime/Audio/ContactAudioService.h`
- [ ] `SkullbonezSource/Runtime/DemoDirector.h`
- [ ] `SkullbonezSource/Runtime/Editor/EditorTools.cpp`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [ ] `SkullbonezSource/Runtime/RunCameraState.h`
- [ ] `SkullbonezSource/Runtime/RunDemoDirector.cpp`
- [ ] `SkullbonezSource/Runtime/RunDemoDirector.h`
- [ ] `SkullbonezSource/Runtime/RunSubsystemState.h`
- [ ] `SkullbonezSource/Runtime/RuntimeCommandQueue.h`
- [ ] `SkullbonezSource/Runtime/RuntimeInteractionController.h`
- [ ] `SkullbonezSource/Runtime/RuntimePickGeometry.h`
- [ ] `SkullbonezSource/Runtime/RuntimePickService.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneControllerState.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h`
- [ ] `SkullbonezTests/TestAssetSystem.cpp`
- [ ] `SkullbonezTests/TestBounds.cpp`
- [ ] `SkullbonezTests/TestConvexHull.cpp`
- [ ] `SkullbonezTests/TestDemoDirector.cpp`
- [ ] `SkullbonezTests/TestDeterminism.cpp`
- [ ] `SkullbonezTests/TestDx12OnlyRuntime.cpp`
- [ ] `SkullbonezTests/TestGeometricMath.cpp`
- [ ] `SkullbonezTests/TestMatrix4.cpp`
- [ ] `SkullbonezTests/TestPersistentContactSolver.cpp`
- [ ] `SkullbonezTests/TestPhysicsHandles.cpp`
- [ ] `SkullbonezTests/TestQuaternion.cpp`
- [ ] `SkullbonezTests/TestRenderResourceDoubles.h`
- [ ] `SkullbonezTests/TestReplayRecorder.cpp`
- [ ] `SkullbonezTests/TestReplayRecorderFullCaptureBoundary.cpp`
- [ ] `SkullbonezTests/TestReserveAllocator.cpp`
- [ ] `SkullbonezTests/TestRuntimeInputBindings.cpp`
- [ ] `SkullbonezTests/TestSceneParserUnit.cpp`
- [ ] `SkullbonezTests/TestSolverBroadphaseStage.cpp`
- [ ] `SkullbonezTests/TestSpatialGrid.cpp`
- [ ] `SkullbonezTests/TestTerrain.cpp`
- [ ] `SkullbonezTests/TestVector3.cpp`
- [ ] `tools/check_allocation_policy.py`
- [ ] `tools/check_dx12_baselines.py`
- [ ] `tools/validate_dx12_renderer.bat`

## Acceptance

- [ ] Re-run the exact scoped `git ls-files`/reference inventory and reconcile
  it with this checklist: zero missing, extra, or duplicate files.
- [ ] No source/test/tool comment references a plan path that does not exist.
- [ ] Remaining plan references point to the current owning plan, not a deleted
  completion artifact.
- [ ] Diff inspection proves comment/documentation-only changes.
- [ ] `git diff --check` passes.

## Validation

Comment/documentation-only: no repository validation. If an include or behavior
change is required, it leaves this checklist and uses the file-to-validation
mapping.

