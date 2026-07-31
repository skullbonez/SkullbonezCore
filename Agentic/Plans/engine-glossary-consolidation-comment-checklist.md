# Engine Glossary Consolidation Source Checklist

Date: 2026-07-31
Plan: `Agentic/Plans/TODO/engine-glossary-consolidation.md`
Evidence: `Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc0-inventory.md`

Scope authority: `git ls-files SkullbonezSource`, filtered to tracked
`.cpp`, `.h`, `.hpp`, `.inl`, and `.hlsl` files.

Progress: **458/575 checked; 117 deferred; 117 unchecked.**

A checked row means the file has been inspected against the updated comment
guide, shared definitions have moved to the authoritative glossary, genuinely
local vocabulary remains, the filler sentence is absent, nearby invariant/
hazard/lifetime comments are adequate, and every `Related:` path resolves.
Deferred files stay unchecked and require an inline reason.

## SkullbonezSource/Assets

- [x] `SkullbonezSource/Assets/AssetKeys.h`
- [ ] `SkullbonezSource/Assets/AssetSystem.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Assets/AssetSystem.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Assets/TextureCollection.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Assets/TextureCollection.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.

## SkullbonezSource/Core/Allocation

- [x] `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.cpp`
- [x] `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h`
- [x] `SkullbonezSource/Core/Allocation/DevelopmentToolsCapability.h`
- [x] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp`
- [x] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h`
- [x] `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp`
- [x] `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h`

## SkullbonezSource/Core

- [x] `SkullbonezSource/Core/AmortizedTask.cpp`
- [x] `SkullbonezSource/Core/AmortizedTask.h`
- [x] `SkullbonezSource/Core/ByteView.h`
- [ ] `SkullbonezSource/Core/Common.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Core/Config.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Core/Config.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Core/FatalError.cpp`
- [x] `SkullbonezSource/Core/FatalError.h`
- [x] `SkullbonezSource/Core/Fence.h`
- [x] `SkullbonezSource/Core/FloatingPointContract.h`
- [x] `SkullbonezSource/Core/LockOrderValidator.cpp`
- [x] `SkullbonezSource/Core/LockOrderValidator.h`
- [ ] `SkullbonezSource/Core/Log.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Core/Log.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Core/MainMemoryStats.h`
- [ ] `SkullbonezSource/Core/PlatformProfiler.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Core/PlatformProfiler.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Core/PlatformWin32.h`
- [x] `SkullbonezSource/Core/Profiler.cpp`
- [ ] `SkullbonezSource/Core/Profiler.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Core/SbDiagnosticStore.h`
- [x] `SkullbonezSource/Core/SbResult.cpp`
- [x] `SkullbonezSource/Core/SbResult.h`
- [x] `SkullbonezSource/Core/SceneCapacity.h`
- [x] `SkullbonezSource/Core/StringHash.h`
- [ ] `SkullbonezSource/Core/Timer.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Core/Timer.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Core/TracyClientOwner.cpp`
- [x] `SkullbonezSource/Core/TracyClientOwner.h`
- [x] `SkullbonezSource/Core/WindowConstants.h`
- [x] `SkullbonezSource/Core/WorkerPool.cpp`
- [x] `SkullbonezSource/Core/WorkerPool.h`

## SkullbonezSource/Gameplay

- [x] `SkullbonezSource/Gameplay/TornadoField.cpp`
- [x] `SkullbonezSource/Gameplay/TornadoField.h`
- [x] `SkullbonezSource/Gameplay/TornadoGameplay.cpp`
- [x] `SkullbonezSource/Gameplay/TornadoGameplay.h`
- [x] `SkullbonezSource/Gameplay/TornadoVisualPass.cpp`
- [x] `SkullbonezSource/Gameplay/TornadoVisualPass.h`

## SkullbonezSource/Maths

- [x] `SkullbonezSource/Maths/Frustum.cpp`
- [x] `SkullbonezSource/Maths/Frustum.h`
- [ ] `SkullbonezSource/Maths/GeometricMath.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Maths/GeometricMath.h`
- [ ] `SkullbonezSource/Maths/GeometricStructures.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Maths/MathsCommon.h`
- [ ] `SkullbonezSource/Maths/Matrix4.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Maths/Matrix4.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Maths/OrbitalMechanics.cpp`
- [x] `SkullbonezSource/Maths/OrbitalMechanics.h`
- [ ] `SkullbonezSource/Maths/Quaternion.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Maths/Quaternion.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Maths/RotationMatrix.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Maths/RotationMatrix.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Maths/Vector3.h`

## SkullbonezSource/Physics

- [ ] `SkullbonezSource/Physics/BoundingBox.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Physics/BoundingBox.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Physics/BoundingSphere.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Physics/BoundingSphere.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Physics/BuoyancySystem.cpp`
- [x] `SkullbonezSource/Physics/BuoyancySystem.h`
- [x] `SkullbonezSource/Physics/ColliderStore.cpp`
- [x] `SkullbonezSource/Physics/ColliderStore.h`
- [x] `SkullbonezSource/Physics/CollisionShape.h`
- [ ] `SkullbonezSource/Physics/ContactSolverCommon.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Physics/ConvexHullShape.cpp`
- [x] `SkullbonezSource/Physics/ConvexHullShape.h`

## SkullbonezSource/Physics/Diagnostics

- [x] `SkullbonezSource/Physics/Diagnostics/SkullScope.cpp`
- [x] `SkullbonezSource/Physics/Diagnostics/SkullScope.h`

## SkullbonezSource/Physics

- [x] `SkullbonezSource/Physics/DisjointSet.h`
- [ ] `SkullbonezSource/Physics/ObjectContactManifold.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Physics/ObjectContactManifold.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Physics/PersistentContactSolver.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Physics/PersistentContactSolver.h`
- [x] `SkullbonezSource/Physics/PhysicsApi.h`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [x] `SkullbonezSource/Physics/PhysicsBroadphaseDebugView.h`
- [x] `SkullbonezSource/Physics/PhysicsDebugData.h`
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsModel.h`
- [ ] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.h`
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsView.h`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h`
- [x] `SkullbonezSource/Physics/PhysicsEngine.ReplayPredictionCloneScope.h`
- [x] `SkullbonezSource/Physics/PhysicsFixedList.h`
- [x] `SkullbonezSource/Physics/PhysicsHandles.h`
- [x] `SkullbonezSource/Physics/PhysicsMass.h`
- [x] `SkullbonezSource/Physics/PhysicsObjectPolicy.cpp`
- [x] `SkullbonezSource/Physics/PhysicsObjectPolicy.h`
- [x] `SkullbonezSource/Physics/PhysicsRuntimeSettings.h`
- [x] `SkullbonezSource/Physics/PhysicsSceneVectorReserve.h`
- [x] `SkullbonezSource/Physics/PhysicsSolverSnapshot.h`
- [x] `SkullbonezSource/Physics/PhysicsStageCapacity.h`
- [x] `SkullbonezSource/Physics/PhysicsTerrainView.cpp`
- [x] `SkullbonezSource/Physics/PhysicsTerrainView.h`
- [x] `SkullbonezSource/Physics/PhysicsTimestep.h`
- [ ] `SkullbonezSource/Physics/PhysicsWorld.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Physics/PhysicsWorld.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Physics/PhysicsWorldForces.h`
- [x] `SkullbonezSource/Physics/Ragdoll.cpp`
- [x] `SkullbonezSource/Physics/Ragdoll.h`
- [ ] `SkullbonezSource/Physics/SleepIslandSystem.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Physics/SleepIslandSystem.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Physics/SolverBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [ ] `SkullbonezSource/Physics/SpatialGrid.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.

## SkullbonezSource/Physics/Stages

- [x] `SkullbonezSource/Physics/Stages/ExternalForceStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/ExternalForceStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h`

## SkullbonezSource/Physics

- [x] `SkullbonezSource/Physics/TerrainContactManifold.cpp`
- [x] `SkullbonezSource/Physics/TerrainContactManifold.h`
- [x] `SkullbonezSource/Physics/TerrainSupportClassifier.h`

## SkullbonezSource/Rendering

- [x] `SkullbonezSource/Rendering/DrawCallTrace.cpp`
- [x] `SkullbonezSource/Rendering/DrawCallTrace.h`

## SkullbonezSource/Rendering/DX12

- [ ] `SkullbonezSource/Rendering/DX12/BLASDX12.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Rendering/DX12/BLASDX12.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12ResourceBuilder.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12TextureRegistry.h`
- [ ] `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Rendering/DX12/FramebufferDX12.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.PipelineState.h`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/SBTDX12.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Rendering/DX12/SBTDX12.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.cpp`
- [x] `SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.h`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/TLASDX12.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Rendering/DX12/TLASDX12.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.

## SkullbonezSource/Rendering

- [ ] `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/PrimitiveBatchRenderer.h`
- [ ] `SkullbonezSource/Rendering/PrimitiveMeshBuilder.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/RenderCommandTypes.h`
- [x] `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h`
- [x] `SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp`
- [x] `SkullbonezSource/Rendering/RenderGpuTimingOwner.h`
- [ ] `SkullbonezSource/Rendering/RenderGraph.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Rendering/RenderGraph.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`
- [ ] `SkullbonezSource/Rendering/RenderInstanceRenderer.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.h`
- [x] `SkullbonezSource/Rendering/RenderMaterial.h`
- [x] `SkullbonezSource/Rendering/RenderPipeline.cpp`
- [x] `SkullbonezSource/Rendering/RenderPipeline.h`
- [x] `SkullbonezSource/Rendering/RenderRasterBindingContract.h`
- [x] `SkullbonezSource/Rendering/RenderRaytracingTypes.h`
- [x] `SkullbonezSource/Rendering/RenderResourceTypes.h`
- [x] `SkullbonezSource/Rendering/RenderSceneSnapshot.h`
- [x] `SkullbonezSource/Rendering/ShaderContracts.h`
- [x] `SkullbonezSource/Rendering/ShaderReflectionContracts.h`
- [ ] `SkullbonezSource/Rendering/Shadow.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Rendering/Text.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Rendering/Text.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Rendering/WorldRenderExtension.h`

## SkullbonezSource/Runtime/App

- [x] `SkullbonezSource/Runtime/App/ApplicationExitState.cpp`
- [x] `SkullbonezSource/Runtime/App/ApplicationExitState.h`
- [ ] `SkullbonezSource/Runtime/App/Init.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/App/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/App/InputFrame.h`
- [x] `SkullbonezSource/Runtime/App/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp`
- [x] `SkullbonezSource/Runtime/App/ReplayReserveInventory.h`
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.h`
- [x] `SkullbonezSource/Runtime/App/ReplayRuntimePackets.h`
- [x] `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`
- [x] `SkullbonezSource/Runtime/App/ReplayValidation.cpp`
- [x] `SkullbonezSource/Runtime/App/ReplayValidation.Internal.h`
- [x] `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp`
- [ ] `SkullbonezSource/Runtime/App/Run.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Runtime/App/Run.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Runtime/App/RunFrame.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/App/RunLaunchOptions.h`
- [x] `SkullbonezSource/Runtime/App/RunLaunchOptions.Renderer.h`
- [x] `SkullbonezSource/Runtime/App/RunRender.cpp`
- [x] `SkullbonezSource/Runtime/App/RunStartupState.h`
- [x] `SkullbonezSource/Runtime/App/RunTimerState.h`
- [x] `SkullbonezSource/Runtime/App/Window.cpp`
- [ ] `SkullbonezSource/Runtime/App/Window.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.

## SkullbonezSource/Runtime/Automation

- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.h`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.cpp`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.cpp`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h`
- [x] `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp`
- [x] `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h`

## SkullbonezSource/Runtime/Camera

- [x] `SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp`
- [x] `SkullbonezSource/Runtime/Camera/AttachedCameraController.h`
- [ ] `SkullbonezSource/Runtime/Camera/Camera.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/Camera/Camera.h`
- [ ] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Runtime/Camera/CameraCollection.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/Camera/CameraControlState.cpp`
- [x] `SkullbonezSource/Runtime/Camera/CameraControlState.h`
- [x] `SkullbonezSource/Runtime/Camera/RuntimeCameraMode.h`

## SkullbonezSource/Runtime/Capture

- [x] `SkullbonezSource/Runtime/Capture/CaptureController.cpp`
- [x] `SkullbonezSource/Runtime/Capture/CaptureController.h`
- [x] `SkullbonezSource/Runtime/Capture/CaptureSystem.cpp`
- [x] `SkullbonezSource/Runtime/Capture/CaptureSystem.h`
- [x] `SkullbonezSource/Runtime/Capture/GraphicsStressController.h`
- [x] `SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/Capture/RuntimeStressController.h`

## SkullbonezSource/Runtime/Debug

- [ ] `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h`
- [ ] `SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Runtime/Debug/CollisionVisualizer.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp`
- [ ] `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.

## SkullbonezSource/Runtime/DevelopmentTools

- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`

## SkullbonezSource/Runtime/Diagnostics

- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/OverlayDebugState.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h`

## SkullbonezSource/Runtime/Direction

- [x] `SkullbonezSource/Runtime/Direction/DemoDirector.cpp`
- [x] `SkullbonezSource/Runtime/Direction/DemoDirector.h`
- [x] `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp`
- [x] `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h`
- [x] `SkullbonezSource/Runtime/Direction/LiveStyleController.cpp`
- [x] `SkullbonezSource/Runtime/Direction/LiveStyleController.h`

## SkullbonezSource/Runtime/Editor

- [x] `SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorCommandHistory.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorGizmoTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorHistory.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorHullAssets.h`
- [ ] `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorOverlayTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorTracer.cpp`
- [x] `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`
- [x] `SkullbonezSource/Runtime/Editor/LauncherLaser.h`
- [ ] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/Editor/MousePickupTools.cpp`

## SkullbonezSource/Runtime/Input

- [x] `SkullbonezSource/Runtime/Input/Input.cpp`
- [ ] `SkullbonezSource/Runtime/Input/Input.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Runtime/Input/InputController.Bindings.cpp`
- [x] `SkullbonezSource/Runtime/Input/InputController.Bindings.h`
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp`
- [x] `SkullbonezSource/Runtime/Input/InputController.h`
- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp`
- [x] `SkullbonezSource/Runtime/Input/InputRouter.h`

## SkullbonezSource/Runtime/Interaction

- [x] `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.Commands.cpp`
- [x] `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.cpp`
- [x] `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h`
- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h`
- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp`
- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h`
- [x] `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.cpp`
- [x] `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.h`
- [x] `SkullbonezSource/Runtime/Interaction/RuntimePickService.cpp`
- [ ] `SkullbonezSource/Runtime/Interaction/RuntimePickService.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.

## SkullbonezSource/Runtime/Planning

- [x] `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.cpp`
- [x] `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.h`
- [x] `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.cpp`
- [x] `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.h`
- [x] `SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h`
- [x] `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`
- [x] `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h`
- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.cpp`
- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h`
- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h`
- [x] `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.cpp`
- [x] `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h`
- [x] `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.cpp`
- [x] `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.h`

## SkullbonezSource/Runtime/Prediction

- [x] `SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayCauseFocusSubmission.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPackets.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h`
- [x] `SkullbonezSource/Runtime/Prediction/TrajectoryStore.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/TrajectoryStore.h`

## SkullbonezSource/Runtime/Render

- [x] `SkullbonezSource/Runtime/Render/RenderDefaultsStore.cpp`
- [x] `SkullbonezSource/Runtime/Render/RenderDefaultsStore.h`
- [x] `SkullbonezSource/Runtime/Render/RenderDefaultsStore.Persistence.cpp`
- [x] `SkullbonezSource/Runtime/Render/RenderModelFramePublisher.cpp`
- [x] `SkullbonezSource/Runtime/Render/RenderModelFramePublisher.h`
- [x] `SkullbonezSource/Runtime/Render/RenderPresentationSettings.h`
- [x] `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp`
- [x] `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h`
- [x] `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp`
- [x] `SkullbonezSource/Runtime/Render/UiDrawSubmission.h`
- [x] `SkullbonezSource/Runtime/Render/UiTextPass.cpp`

## SkullbonezSource/Runtime/Replay

- [x] `SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayArtifactSource.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoring.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayCapturePackets.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayCoordination.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayEventCommand.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayIdentity.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlaySurface.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPathPackets.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentation.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentationPackets.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentationSubmission.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayProbeState.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRestoreService.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayScrubber.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayTimeline.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayTimelinePackets.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayToolPackets.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayTrajectoryPackets.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h`

## SkullbonezSource/Runtime

- [x] `SkullbonezSource/Runtime/RuntimeFrameViews.h`

## SkullbonezSource/Runtime/Scene

- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneAutomationGateConfiguration.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Creation.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Navigation.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Style.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneControllerState.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneLifecycle.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadRequest.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Preparation.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Presentation.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Reset.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneNavigationModel.Browser.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestQueue.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestQueue.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneResetPreservation.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneSaveOperations.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneSaveOperations.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneSessionState.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneSessionState.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneTerrain.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.h`

## SkullbonezSource/Runtime/Simulation

- [x] `SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp`
- [x] `SkullbonezSource/Runtime/Simulation/SimulationSystem.h`

## SkullbonezSource/Runtime/Startup

- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupCrashLogging.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupCrashLogging.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.h`

## SkullbonezSource/Runtime/Tools

- [x] `SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`

## SkullbonezSource/Runtime/UI

- [x] `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp`
- [x] `SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.cpp`
- [x] `SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.h`
- [x] `SkullbonezSource/Runtime/UI/RuntimeUiSurface.h`
- [x] `SkullbonezSource/Runtime/UI/RuntimeViewModel.cpp`
- [x] `SkullbonezSource/Runtime/UI/RuntimeViewModel.h`

## SkullbonezSource/Scene

- [ ] `SkullbonezSource/Scene/AuthoredScene.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/Scene/AuthoredScene.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/Scene/AuthoredSceneParser.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserSchema.h`
- [x] `SkullbonezSource/Scene/AuthoredTornadoConfig.h`
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.h`

## SkullbonezSource/UI

- [x] `SkullbonezSource/UI/OperatorEditorExchange.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.h`
- [ ] `SkullbonezSource/UI/UI.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UI.h`
- [x] `SkullbonezSource/UI/UIBackdropBlur.cpp`
- [x] `SkullbonezSource/UI/UIBackdropBlur.h`
- [ ] `SkullbonezSource/UI/UIButton.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIButton.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UICache.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UICache.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UICheckBox.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UICheckBox.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIComboBox.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIComboBox.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UICommands.h`
- [x] `SkullbonezSource/UI/UIDraw.cpp`
- [x] `SkullbonezSource/UI/UIDraw.h`
- [x] `SkullbonezSource/UI/UIDrawList.cpp`
- [x] `SkullbonezSource/UI/UIDrawList.h`
- [ ] `SkullbonezSource/UI/UIDrawWidgets.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIDrawWidgets.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UIEditorMiniPalette.cpp`
- [x] `SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp`
- [x] `SkullbonezSource/UI/UIFontMetrics.cpp`
- [x] `SkullbonezSource/UI/UIFontMetrics.h`
- [x] `SkullbonezSource/UI/UIFrameComposition.cpp`
- [x] `SkullbonezSource/UI/UIFrameComposition.h`
- [ ] `SkullbonezSource/UI/UIIconButton.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIIconButton.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UIInput.cpp`
- [x] `SkullbonezSource/UI/UIInput.h`
- [ ] `SkullbonezSource/UI/UILayout.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UILayout.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp`
- [x] `SkullbonezSource/UI/UIProfilerOverlayPresenter.h`
- [x] `SkullbonezSource/UI/UIRenderAuthoringCatalog.h`
- [x] `SkullbonezSource/UI/UIRenderDiagnostics.h`
- [x] `SkullbonezSource/UI/UISceneNavigationModel.h`
- [ ] `SkullbonezSource/UI/UIScrollBar.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIScrollBar.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UISlider.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UISlider.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIState.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIStyle.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIStyle.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabBar.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabBar.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabCinematic.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabCinematic.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabControls.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabControls.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UITabEditor.cpp`
- [x] `SkullbonezSource/UI/UITabEditor.h`
- [x] `SkullbonezSource/UI/UITabMemory.cpp`
- [x] `SkullbonezSource/UI/UITabMemory.h`
- [ ] `SkullbonezSource/UI/UITabOptions.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabOptions.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabPhysics.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabPhysics.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UITabProfiler.cpp`
- [ ] `SkullbonezSource/UI/UITabProfiler.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UITabProfilerHistogram.cpp`
- [ ] `SkullbonezSource/UI/UITabScene.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabScene.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UITabSky.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UITabSky.h`
- [ ] `SkullbonezSource/UI/UIWindowChrome.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/UI/UIWindowChrome.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.cpp`
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.h`

## SkullbonezSource/World

- [x] `SkullbonezSource/World/FluidSurfaceAdjustment.h`
- [ ] `SkullbonezSource/World/SkyBox.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/World/SkyBox.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [x] `SkullbonezSource/World/Terrain.cpp`
- [ ] `SkullbonezSource/World/Terrain.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/World/WorldEnvironment.cpp` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
- [ ] `SkullbonezSource/World/WorldEnvironment.h` - Deferred to GC3: basename-led summary requires non-tautology adjudication.
