/*
File: SkullbonezSource/Runtime/App/OperatorUiProjection.h
Purpose:
  Declares application-owned projection into the detached GameUI frame value.

Summary:
  App samples domain owners in a fixed order and fills one UI value before
  Render receives it for GPU submission. Render therefore cannot reopen Scene,
  Diagnostics, Input, Camera, or tool authority while drawing the frame.

Invariants:
  - Every source borrow ends before the completed UI frame is submitted.
  - Reserve-capacity rows remain live until the synchronous UI draw completes.
  - Projection performs no GPU work and emits no process command.

Related:
  - SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
  - SkullbonezSource/Runtime/UI/GameUI/UI.h
*/
#pragma once

#include "../../Core/MainMemoryStats.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
struct CinematicRenderConfig;
} // namespace Core
namespace Threading
{
class WorkerPool;
}
namespace Rendering
{
class Dx12Diagnostics;
}
namespace UI
{
class InGameUI;
struct InGameUIFrameData;
struct OperatorEditorFrameView;
struct RunSceneBrowserState;
struct UIRuntimeReserveCapacityRow;
} // namespace UI
namespace Runtime
{
class DiagnosticsRuntime;
class RuntimeInputContext;
class SceneWorld;
struct ContinuousOrbitalForecastView;
struct CameraControlState;
struct OverlayDebugState;
struct RenderPresentationSettings;
struct RunScreenshotState;
struct ReplayHudStatus;
struct RunEditorPlacementState;
struct RunRayCastTestState;
struct RuntimeFrameMetricsSnapshot;
struct RuntimeRenderModelFrameView;
struct RuntimeUiTextFrameFacts;
struct RuntimeViewModel;
struct SceneSessionState;

inline Core::MainMemoryStats ProjectMemoryTabAvailability( bool sourceValid, const Core::MainMemoryStats& sampledMemory )
{
    return sourceValid ? sampledMemory : Core::MainMemoryStats {};
}

RuntimeViewModel BuildOperatorRuntimeViewModel( const SceneSessionState& scene, const SceneWorld& world, int sceneCount,
                                                const RunScreenshotState& screenshot, bool presentationInterpolation,
                                                bool presentationPinned, float presentationAlpha );
void ProjectOperatorEditorScene( UI::OperatorEditorFrameView& view, const char* currentScenePath,
                                 const UI::RunSceneBrowserState& sceneBrowser, int currentSceneBrowserIndex,
                                 const SceneSessionState& scene, const SceneWorld& world );
void ProjectOperatorEditorRendering( UI::OperatorEditorFrameView& view, const RenderPresentationSettings& presentation,
                                     const Core::EngineConfig& config, const Core::CinematicRenderConfig& cinematic,
                                     const OverlayDebugState& debug, const RuntimeUiTextFrameFacts& uiTextFacts,
                                     bool cinematicRendering, bool shadowsEnabled );
void ProjectOperatorEditorForecast( UI::OperatorEditorFrameView& view, const ContinuousOrbitalForecastView& forecast );
int ProjectOperatorEditorHierarchy( UI::OperatorEditorFrameView& view, const RunEditorPlacementState& editor,
                                    const SceneWorld& world, bool crossScenePauseLocked, bool fixedStep,
                                    bool buildingAssetsAvailable );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
void ProjectOperatorEditorInspectorAndWorld( UI::OperatorEditorFrameView& view, const RunEditorPlacementState& editor,
                                             const SceneWorld& world, int selectedHierarchyRow,
                                             const SceneSessionState& scene, const Core::EngineConfig& config );
#endif

void ProjectOperatorUiDiagnostics( UI::InGameUIFrameData& uiData, const ReplayHudStatus& replayHud,
                                   const RuntimeFrameMetricsSnapshot& metrics, const RuntimeRenderModelFrameView& models,
                                   DiagnosticsRuntime& diagnosticsRuntime, UI::InGameUI& ui,
                                   Threading::WorkerPool* workerPool, Core::Profiler* profiler,
                                   UI::UIRuntimeReserveCapacityRow* reserveCapacityRows,
                                   Rendering::Dx12Diagnostics& renderDiagnostics );
void ProjectOperatorUiPresentation( UI::InGameUIFrameData& uiData, const SceneSessionState& scene,
                                    const RuntimeViewModel& runtimeViewModel, const UI::RunSceneBrowserState& sceneBrowser,
                                    const UI::OperatorEditorFrameView& operatorEditorView, bool sceneHasCurrentEntry,
                                    const char* currentScenePath, int currentSceneBrowserIndex,
                                    float sceneEnergyForDisplay );
void ProjectOperatorUiSettings( UI::InGameUIFrameData& uiData, const OverlayDebugState& debug,
                                const RenderPresentationSettings& renderPresentation, const SceneWorld& world,
                                const Core::EngineConfig& config, const Core::CinematicRenderConfig& cinematic,
                                bool cinematicRendering );
void ProjectOperatorUiInteraction( UI::InGameUIFrameData& uiData, const RunRayCastTestState& rayCastTest,
                                   const RunEditorPlacementState& editor, const RuntimeInputContext& runtimeInput,
                                   const CameraControlState& camera, const UI::InGameUI& ui, uint32_t cameraModeEnabledMask,
                                   const char* cameraModeLabel );
} // namespace Runtime
} // namespace SkullbonezCore
