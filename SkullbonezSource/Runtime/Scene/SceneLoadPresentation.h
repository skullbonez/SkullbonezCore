/*
File: SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h
Purpose:
  Declares authored scene UI-option application outside Run.

Summary:
  Scene JSON publishes detached presentation and diagnostics reactions. App
  applies those values to the Diagnostics and UI owners after Scene mutation;
  Scene retains no diagnostics type or mutable presentation owner.

Invariants:
  - `preserveUIState` prevents authored scene UI from overriding live operator
    window/tab state during resets.
  - Stress options remain ordered even when visible UI state is preserved.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Scene/AuthoredScene.h
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Scene/AuthoredScene.h"
#include "../../Physics/PhysicsDebugData.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{
class InGameUI;
}
namespace Runtime
{
// Scene-authored and reset-preserved presentation values. Operator-only HUD,
// recording, and playback fields remain entirely Diagnostics-owned.
struct ScenePresentationValues
{
    bool waterFreeze = false;
    bool waterNoReflect = false;
    bool waterRtReflect = false;
    bool waterFlat = false;
    bool terrainHidden = false;
    bool waterHidden = false;
    uint32_t physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE;
    bool physicsDebugTransparent = false;
    float physicsDebugAlpha = 0.28f;
    float physicsDebugContactLinger = 0.45f;
    int physicsDebugPipelineStageCursor = 0;
    bool collisionVisualizer = false;
    bool textOnly = false;
    bool uiTestPattern = false;
    bool broadphaseOverlay = false;
    float frozenWaterTime = 0.0f;

    void ResetForSceneLoad()
    {
        *this = {};
    }
};

enum class SceneDiagnosticsReactionKind : uint8_t
{
    ResetForSceneLoad,
    ConfigurePerfLogFlush,
    ApplyScenePerfLog,
    SetUiStressEnabled,
    SetUiStressSeed,
    SetUiStressActions,
    ConfigureUiStress,
    ClosePerfLog,
    ResetPerfLogForSceneLoad,
    BeginPhysicsDiagnostics
};

struct SceneDiagnosticsReaction
{
    SceneDiagnosticsReactionKind kind = SceneDiagnosticsReactionKind::ResetForSceneLoad;
    bool enabled = false;
    int value = 0;
    int secondaryValue = 0;
    char path[256] = {};
    char rendererName[64] = {};
    bool explicitRenderFrameLockstep = false;
    bool effectiveRenderFrameLockstep = false;
};

inline constexpr std::size_t SCENE_DIAGNOSTICS_REACTION_CAPACITY = 12;

struct SceneDiagnosticsReactionBatch
{
    std::array<SceneDiagnosticsReaction, SCENE_DIAGNOSTICS_REACTION_CAPACITY> reactions = {};
    std::size_t count = 0;
};

struct SceneUiOptionDiagnosticsProjection
{
    SceneDiagnosticsReactionBatch reactions;
    bool applyTestPattern = false;
    bool testPatternEnabled = false;
};

inline SceneUiOptionDiagnosticsProjection ProjectSceneUiOptionDiagnostics( const SceneUIOptions& options,
                                                                            bool preserveUiState )
{
    SceneUiOptionDiagnosticsProjection projection;
    projection.applyTestPattern = !preserveUiState && options.hasTestPattern;
    projection.testPatternEnabled = options.testPatternEnabled;

    if ( options.hasStress )
    {
        SceneDiagnosticsReaction& reaction = projection.reactions.reactions[projection.reactions.count++];
        reaction.kind = SceneDiagnosticsReactionKind::SetUiStressEnabled;
        reaction.enabled = options.stressEnabled;
    }
    if ( options.hasStressSeed )
    {
        SceneDiagnosticsReaction& reaction = projection.reactions.reactions[projection.reactions.count++];
        reaction.kind = SceneDiagnosticsReactionKind::SetUiStressSeed;
        reaction.value = static_cast<int>( options.stressSeed );
    }
    if ( options.hasStressActions )
    {
        SceneDiagnosticsReaction& reaction = projection.reactions.reactions[projection.reactions.count++];
        reaction.kind = SceneDiagnosticsReactionKind::SetUiStressActions;
        reaction.value = options.stressActionsPerFrame;
    }

    return projection;
}

struct SceneUiActivation
{
    // Value-only copy of authored UI intent. The scene owner retains neither
    // the parsed AuthoredScene nor the complete UI owner across the load boundary.
    SceneUIOptions authoredOptions;
    double nowSeconds = 0.0;
    bool hasAuthoredOptions = false;
    bool preserveUIState = false;
    bool automationScene = false;
    bool forceVisible = false;
    bool forceUnminimized = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
