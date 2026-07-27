# SbResult Frame Path Cost SR0 Census

Date: 2026-07-27
Plan phase: `sbresult-frame-path-cost` SR0
Result: COMPLETE — sentinel-only success construction selected

## Measured Surface

A reconciled multiline definition scan found **176 named**
`SbResult`-returning function definitions plus **one explicit trailing-return
lambda**, for **177 callable definitions**. The repository has **200** explicit `SbResult::Success()` sites,
**176** explicit `SbResult::Failure()` sites, and 78 direct owner/message reads
across 33 consumer files (excluding `SbResult.h` itself).

The original plan named only the two `Run` frame-phase returns. The current
call graph is wider: cached texture selection, DX12 frame ownership, optional
DXR TLAS rebuild, input, and development-UI paths also return `SbResult` from a
frame turn. The complete definition census classifies 57 as frame-reachable,
51 as scene-load/resource-build, and 69 as cold or explicitly on-demand.

### Frame-reachable — 57

| Source | Definitions |
|---|---|
| `Assets/TextureCollection.cpp` | `SelectTexture`, `EnsureTexture` |
| `Rendering/DX12/Dx12FrameOwner.cpp` | `FinishAndReopen`, `EnsureOpen`, `SubmitClosed`, `WaitForGpu`, `FlushUploadBuffer`, `CommitClose`, `CommitWait`, `RetainFailure`, `RetainDeviceLoss`, `SignalFrame`, `WaitForFrameFence`, `Dx12CaptureFrame::EnsureOpen`, `Dx12DiagnosticsFrame::EnsureOpen`, `Dx12DiagnosticsFrame::WaitForFenceValue` |
| `Rendering/DX12/Dx12ImGuiRendererOwner.cpp` | `EnsureGameViewportTexture`, `CaptureGameViewport`, `RenderDrawData` |
| `Rendering/DX12/RenderBackendDX12.CommandRecordingState.h` | `Dx12CommandRecordingState::CommitClose`, `CommitAllocatorReset`, `CommitListReset`, `CommitWait`, `RetainFailure`; `Dx12DeviceHealthState::RetainDeviceLoss`; `Dx12FaultInjectionState::BeforeSubmission` |
| `Rendering/DX12/RenderBackendDX12.cpp` | `Dx12BackendOperationResult`, `Dx12FrameOwner::Present` |
| `Rendering/DX12/RenderBackendDX12.DXR.cpp` | `Dx12RaytracingOwner::BuildScene` |
| `Rendering/DX12/RenderBackendDX12.h` | `Dx12TextureCommands::EnsureOpen` |
| `Rendering/DX12/RenderDeviceDX12.cpp` | `Dx12RuntimeResult`, `Dx12FenceTimeline::Signal`, `Dx12FenceTimeline::WaitForValue` |
| `Rendering/DX12/TLASDX12.cpp` | `TLAS::Build` |
| `Runtime/App/RunFrame.cpp` | `Run::PresentFramePhase` |
| `Runtime/Automation/InteractionAutomationController.cpp` | `InteractionAutomationController::SubmitOperatorEditorReplayCommand` |
| `Runtime/Capture/RuntimeStressController.cpp` | `Run::RunUIStressActions`; trailing-return lambda `executeSceneGeneratedControlAction` |
| `Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` | `ApplyAutomationCommand`, `CaptureGameViewport`, `RenderPreparedDrawData` |
| `Runtime/Input/Input.cpp` | `CaptureDeviceInputFrame`, `SetNativeMouseCapture` |
| `Runtime/Render/RuntimeRenderer.cpp` | `RenderDevelopmentUi` |
| `Runtime/Scene/SceneRequestQueue.cpp` | `Submit` |
| `Runtime/UI/OperatorEditorFrameComposer.cpp` | `Run::RenderOperatorUiPhase` |
| `UI/OperatorEditorExchange.cpp` | `SubmitBounded`, `MergeQueue`, all six `SubmitOperatorEditorCommand` overloads, `NormalizeLegacyOperatorEditorCommands`, `ProjectOperatorEditorCommands` |
| `World/SkyBox.cpp` | `Render` |

“Frame-reachable” includes conditional frame branches such as development UI,
pipeline synchronization, automation, stress, DXR, and scene-request input. It
does not claim that every row runs in every configuration.

### Scene-load or render-resource build — 51

| Source | Definitions |
|---|---|
| `Assets/TextureCollection.cpp` | `LoadJpegTextureIntoSlot`, `CreateTextureFromSourceAsset`, `CreateJpegTexture`, `EnsureJpegTexture`, `RebuildTexturesFromSourceAssets` |
| `Physics/ConvexHullShape.cpp` | `HullLoadFailure`, `TryNormalized`, `ParseFiniteFloat`, `ParseUint16`, `RequireNoExtraTokens`, `ParseVec3`, `TryLoadFromFile` |
| `Rendering/DX12/BLASDX12.cpp` | `BLAS::Build` |
| `Rendering/DX12/RenderBackendDX12.DXR.cpp` | `CreateRootSignature`, `CreatePipeline`, `CreateReflectionTexture`, `CompleteSetup`, `InitDXR` |
| `Rendering/DX12/RenderDeviceDX12.cpp` | `CreateDepthStencilResource` |
| `Rendering/DX12/SBTDX12.cpp` | `SBT::Build` |
| `Rendering/DX12/TLASDX12.cpp` | `TLAS::Init` |
| `Runtime/Scene/SceneAuthoredSetup.cpp` | `AppendAuthoredSimpleRagdoll`, `ApplySceneBehaviorGroup`, `SceneAuthoredSetup::AppendSimpleRagdoll`, `SetUpSceneEntities` |
| `Runtime/Scene/SceneController.cpp` | `SubmitCreateScene` |
| `Runtime/Scene/SceneController.Load.cpp` | `UseDefaultTerrain`, `UseFlatSlopeTerrain`, `SceneLoadTransaction::Load`, `SceneController::Load` |
| `Runtime/Scene/SceneEntityStore.cpp` | `PreflightAppend` |
| `Runtime/Scene/SceneGeneratedControlTransaction.cpp` | `DrainAndReset` |
| `Runtime/Scene/SceneGeneratedSetup.cpp` | `SetUpSceneEntities`, `SetUpSolverObjects` |
| `Runtime/Scene/SceneWorld.cpp` | `CommitPhysicsSceneCapacity`, `ReserveAdditionalPhysicsSceneCapacity` |
| `Scene/AuthoredScene.cpp` | `TryLoadSceneFile` and all four `TryLoadFromFile` / `TryLoadStyleFromFile` overloads |
| `Scene/AuthoredSceneParser.cpp` | `TryLoadScene`, `TryLoadStyle`, `TryLoadDocument`, `TryLoadAuthoredSceneFromFileImpl`, `TryLoadStyleSceneFromFileImpl` |
| `Scene/AuthoredSceneParserSchema.h` | `ParserFailureResult` |
| `World/SkyBox.cpp` | `LoadTextures` |
| `World/Terrain.cpp` | `TryCreatePhysicsFromHeightMap`, `TryCreateFromHeightMap`, `LoadTerrainData` |

### Cold or on-demand — 69

| Source | Definitions |
|---|---|
| `Core/Config.cpp` | `ReadConfigFormatVersion`, `EngineConfig::Load` |
| `Core/Timer.cpp` | `NoPerformanceCounterSupport`, `Timer::Initialise` |
| `Rendering/DX12/Dx12BackbufferCapture.cpp` | `CaptureBackbuffer` |
| `Rendering/DX12/Dx12DescriptorHeaps.cpp` | `DescriptorInitResult`, `Dx12DescriptorHeaps::Init` |
| `Rendering/DX12/Dx12Diagnostics.cpp` | `InitializeGpuTimers` |
| `Rendering/DX12/Dx12ImGuiRendererOwner.cpp` | `BindContext` |
| `Rendering/DX12/Dx12ShaderDevelopment.cpp` | `ReloadShadersFromSource`, `BakeSourceGeneration`, `ReloadBakedGeneration` |
| `Rendering/DX12/RenderBackendDX12.CommandRecordingState.h` | `Dx12RecreationTransaction::Fail` |
| `Rendering/DX12/RenderBackendDX12.cpp` | `Dx12BackendInitResult`, `RenderBackendDX12::Init`, `Dx12PipelineOwner::Initialize`, `Dx12FrameOwner::FlushGPU`, `DrainForResourceRelease`, `Resize` |
| `Rendering/DX12/RenderBackendDX12.Textures.cpp` | `Dx12TextureStartupResult`, `Dx12TextureOwner::Initialize`, `PrepareGenerateMipsShaderReload` |
| `Rendering/DX12/RenderDeviceDX12.cpp` | `Dx12StartupResult`, `Dx12RenderDevice::Init` |
| `Rendering/Text.cpp` | `Text2d::BuildFont` |
| `Runtime/App/ApplicationExitState.cpp` | `Resolve` |
| `Runtime/App/Init.cpp` | `InitRenderBackend` |
| `Runtime/App/RunTimerState.h` | `RunTimerState::Initialise` |
| `Runtime/App/ReplayValidation.Probes.cpp` | `ReplayProbeFailure`, `InjectReplaySaveProbePlacementCoverage`, `ValidateReplaySaveProbeArtifact`, `TickScrubProbe`, `CompleteRestoreProbe`, `CurrentFailure`, `VerifyLoadedPresentation`, `PrepareCheckpointFileProbe`, `CompleteCheckpointFileProbe`, `CompleteTargetFileProbe`, `PrepareBranchFileProbe`, `CompleteBranchFileProbe` |
| `Runtime/App/Run.cpp` | `BindRenderBackend`, `ApplyStartupOverrides`, `RunSceneLoadOnly` |
| `Runtime/App/RunFrame.cpp` | `Run::Execute` |
| `Runtime/App/Window.cpp` | `HandleScreenResize`, `CreateAppWindow` |
| `Runtime/Automation/InteractionAutomationController.cpp` | `ConfigureInteractionAutomation`, `InteractionAutomationResult` |
| `Runtime/Automation/InteractionAutomationReportWriter.cpp` | `InteractionAutomationRunStatus::Result`, `InteractionAutomationReportWriter::Write` |
| `Runtime/Capture/CaptureController.cpp` | `QueueScreenshot`, `SaveScreenshot`, `SaveBackbufferBmp` |
| `Runtime/Capture/CaptureSystem.cpp` | `WriteExact`, `SaveBackbufferBmp` |
| `Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` | `Start` |
| `Runtime/Render/RenderDefaultsStore.Persistence.cpp` | `PersistOrdinary`, `PersistCinematic` |
| `Runtime/Render/RenderResourceLifecycle.cpp` | `InitialiseProcessResources`, `EnsureUiTextResources`, `InitialiseSceneRayTracing` |
| `Runtime/Render/RuntimeRenderer.cpp` | `ReleaseBackendOwnedRuntimeResources` |
| `Runtime/Render/UiTextPass.cpp` | `EnsureGpuResources` |
| `Runtime/Scene/SceneController.Load.cpp` | `SaveCurrentDefaults` |
| `Runtime/Scene/SceneSaveOperations.cpp` | `SaveCompletePublication`, `SaveSceneLoadOnlySnapshot`, `SaveEditableSceneBeforeReplacement` |
| `Scene/SceneSnapshotWriter.cpp` | `Save` |
| `World/SkyBox.cpp` | `ResetRenderResources` |

The first scan recognized ordinary out-of-line definitions but missed
indented inline method bodies, `static inline` helpers, and the trailing-return
lambda. A second syntax pass explicitly covered those forms and reconciled all
16 omissions into the tables above.

## Capacity And Before Measurement

Win64 `sizeof(SbResult)` is **528 bytes**: one status byte, alignment, an
eight-byte owner pointer, and a 512-byte message.

The real maximum current message is **511 payload bytes plus null**.
`TestApplicationExitState` constructs a source longer than the carrier, requires
the returned message length to equal `FAILURE_MESSAGE_CAPACITY - 1`, and checks
the first and last retained payload bytes. Shrinking the buffer would therefore
truncate a currently protected failure message.

The Plan 5 final-source, pre-SR1 `validate_perf.bat` run passed:

| Scenario | Frame avg | Frame p99 | Input avg | Vsync avg |
|---|---:|---:|---:|---:|
| DX12, 1,940 frames | 0.8486 ms | 1.3123 ms | 0.0723 ms | 0.4150 ms |
| Physics bench, 2,340 frames | 0.4700 ms | 0.8471 ms | 0.0716 ms | 0.4286 ms |

No marker isolated `SbResult`; the total-frame gate observed no blocker.

## Representation Ruling

Select **sentinel-only success construction**:

- retain the 512-byte inline failure buffer and 528-byte Win64 carrier;
- on default/success construction initialize `owner` to `""` and only
  `message[0]` to `'\0'`;
- make copy/move operations branch on `ok`, copying only success sentinels or
  the completely initialized failure diagnostic;
- let `Failure` continue to fill the same inline buffer with `vsnprintf`;
- preserve direct `.error.owner` / `.error.message` access and `[[nodiscard]]`.

This changes success initialization from the complete 512-byte message array to
one observable message byte without changing the failure representation.

Rejected alternatives:

- **Shrink the buffer:** rejected because the protected maximum is 511 bytes.
- **External or owner-side failure storage:** rejected because 33 consumers
  directly read the result after return; moving diagnostics out of the value
  creates a lifetime contract and a broad API migration for no measured need.
- **Leave unchanged:** rejected because the wider census proves repeated
  frame-reachable successes, while the sentinel-only change removes their
  unnecessary clearing without changing size, ownership, or call sites.
