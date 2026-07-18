/*
File: SkullbonezSource/UI/OperatorEditorExchange.h
Purpose:
  Defines the shared value boundary used by both operator editor surfaces.

Summary:
  Both operator front ends read the same domain-grouped frame snapshot and emit
  fixed-capacity typed command queues. A deterministic arbitration pass
  coalesces exact duplicate intent before projecting one canonical packet into
  the established runtime owner command paths.

Glossary:
  Surface: One operator presentation front end, currently Legacy or ImGui.
  Domain view: Read-only scene, property, rendering, replay, or tool values
    copied for one presentation frame.
  Tool command: One-frame edit-mode, history, pause, or step intent applied by
    the established editor and scene-flow owners after arbitration.
  Canonical packet: The existing InGameUICommands value consumed by runtime
    owner appliers after this exchange has resolved both front ends.
  Duplicate: The same typed action and payload emitted by both visible surfaces
    during one input turn.
  Conflict: Two commands with the same stable action identity but different
    payloads during one input turn.

Invariants:
  - Views and commands contain values or immediate-frame borrowed labels only;
    no scene, replay, renderer, input, or UI owner pointer crosses this seam.
  - Every queue is inline and bounded; submission cannot grow an STL container.
  - Legacy commands have deterministic first priority, exact secondary duplicates
    coalesce, and conflicting duplicate payloads fail through Lane R.
  - Projection produces at most one established owner request per action type.

Related:
  - SkullbonezSource/UI/UICommands.h
  - SkullbonezSource/Runtime/InputFrame.cpp
  - SkullbonezSource/Runtime/DevelopmentTools editor owner
  - Agentic/Plans/TODO/imgui-tracy-editor-campaign.md (E8-E10)
*/
#pragma once

#include "../Core/SbResult.h"

#include <cstdint>

namespace SkullbonezCore::UI
{
struct InGameUICommands;

struct OperatorEditorSceneView
{
    // Lifetime: sceneName is borrowed only for the current presentation frame.
    const char* sceneName = "";
    // Lifetime: sceneOptions and every pointed-to label are borrowed from the
    // scene-navigation owner for this one synchronous presentation frame.
    const char* const* sceneOptions = nullptr;
    int currentSceneIndex = -1;
    int sceneCount = 0;
    int currentFrame = 0;
    int modelCount = 0;
    float timeScale = 1.0f;
    bool canSaveCurrentScene = false;
    bool dirty = false;
};

inline constexpr uint32_t OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY = 512u;

struct OperatorEditorHierarchyRow
{
    // Lifetime: displayName is borrowed from SceneEntityStore for this frame.
    const char* displayName = "";
    uint32_t sceneObjectId = 0u;
    uint32_t groupRootObjectId = 0u;
    int groupPartIndex = -1;
    bool assetBacked = false;
    bool visible = true;
    bool locked = false;
    bool selected = false;
};

struct OperatorEditorHierarchyView
{
    OperatorEditorHierarchyRow rows[OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY] = {};
    uint32_t rowCount = 0u;
    uint32_t totalRowCount = 0u;
    uint32_t selectedSceneObjectId = 0u;
    bool truncated = false;
};

struct OperatorEditorAssetView
{
    int selectedObjectType = 0;
    int objectTypeCount = 0;
    bool registeredLibraryAvailable = false;
};

struct OperatorEditorPropertyView
{
    float worldGravity = 0.0f;
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
};

struct OperatorEditorRenderingView
{
    bool vsyncEnabled = false;
    bool shadowsEnabled = false;
    bool cinematicRendering = false;
    bool presentationInterpolation = true;
    float presentationAlpha = 1.0f;
};

struct OperatorEditorReplayView
{
    int memoryPreset = 0;
    int requestedRetentionSeconds = 0;
    int requestedBudgetMiB = 0;
    int presentationRetentionSeconds = 0;
    int solverRetentionSeconds = 0;
    bool memoryBudgetClamped = false;
    bool solverWindowReduced = false;
};

struct OperatorEditorSurfaceView
{
    bool legacyVisible = true;
    bool secondaryVisible = false;
};

struct OperatorEditorToolView
{
    bool editorModeEnabled = false;
    bool placementModeEnabled = false;
    bool placeStaticObject = false;
    bool crossScenePauseLocked = false;
    bool fixedStep = false;
    bool autoTerrainAlign = false;
    int undoDepth = 0;
    int redoDepth = 0;
};

struct OperatorEditorFrameView
{
    OperatorEditorSceneView scene;
    OperatorEditorPropertyView property;
    OperatorEditorRenderingView rendering;
    OperatorEditorReplayView replay;
    OperatorEditorSurfaceView surfaces;
    OperatorEditorToolView tools;
    OperatorEditorHierarchyView hierarchy;
    OperatorEditorAssetView assets;
};

enum class OperatorEditorSceneCommandType : uint8_t
{
    ResetCurrentScene,
    ResetSceneDefaults,
    SetCurrentSceneIndex,
    SaveCurrentScene,
    CreateScene
};

struct OperatorEditorSceneCommand
{
    OperatorEditorSceneCommandType type = OperatorEditorSceneCommandType::ResetCurrentScene;
    int sceneIndex = -1;
    char sceneName[64] = {};
};

enum class OperatorEditorPropertyCommandType : uint8_t
{
    SetTimeScale,
    SetWorldGravity
};

struct OperatorEditorPropertyCommand
{
    OperatorEditorPropertyCommandType type = OperatorEditorPropertyCommandType::SetTimeScale;
    float value = 0.0f;
};

enum class OperatorEditorRenderingCommandType : uint8_t
{
    ToggleVsync
};

struct OperatorEditorRenderingCommand
{
    OperatorEditorRenderingCommandType type = OperatorEditorRenderingCommandType::ToggleVsync;
};

enum class OperatorEditorReplayCommandType : uint8_t
{
    SetMemoryPolicy
};

struct OperatorEditorReplayCommand
{
    OperatorEditorReplayCommandType type = OperatorEditorReplayCommandType::SetMemoryPolicy;
    int presetIndex = -1;
    int retentionSeconds = -1;
    int budgetMiB = -1;
};

enum class OperatorEditorToolCommandType : uint8_t
{
    ToggleEditorMode,
    TogglePlacementMode,
    Undo,
    Redo,
    ToggleCrossScenePause,
    StepPausedScene,
    SelectSceneObject,
    DeleteSelection,
    DuplicateSelection,
    SetPlacementObjectType,
    SetPlaceStatic,
    ToggleTerrainAlign,
    SetEntityVisible,
    SetEntityLocked
};

struct OperatorEditorToolCommand
{
    OperatorEditorToolCommandType type = OperatorEditorToolCommandType::ToggleEditorMode;
    uint32_t sceneObjectId = 0u;
    int value = 0;
    bool enabled = false;
};

template <typename Command, uint32_t Capacity> struct OperatorEditorCommandQueue
{
    static constexpr uint32_t capacity = Capacity;
    Command commands[Capacity] = {};
    uint32_t count = 0u;

    [[nodiscard]] bool Empty() const noexcept
    {
        return count == 0u;
    }
};

using OperatorEditorSceneCommandQueue = OperatorEditorCommandQueue<OperatorEditorSceneCommand, 8u>;
using OperatorEditorPropertyCommandQueue = OperatorEditorCommandQueue<OperatorEditorPropertyCommand, 4u>;
using OperatorEditorRenderingCommandQueue = OperatorEditorCommandQueue<OperatorEditorRenderingCommand, 2u>;
using OperatorEditorReplayCommandQueue = OperatorEditorCommandQueue<OperatorEditorReplayCommand, 2u>;
using OperatorEditorToolCommandQueue = OperatorEditorCommandQueue<OperatorEditorToolCommand, 16u>;

struct OperatorEditorCommandQueues
{
    OperatorEditorSceneCommandQueue scene;
    OperatorEditorPropertyCommandQueue property;
    OperatorEditorRenderingCommandQueue rendering;
    OperatorEditorReplayCommandQueue replay;
    OperatorEditorToolCommandQueue tools;

    [[nodiscard]] bool Empty() const noexcept
    {
        return scene.Empty() && property.Empty() && rendering.Empty() && replay.Empty() && tools.Empty();
    }
};

struct OperatorEditorArbitrationResult
{
    OperatorEditorCommandQueues commands;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    uint32_t acceptedLegacyCommands = 0u;
    uint32_t acceptedSecondaryCommands = 0u;
    uint32_t coalescedDuplicateCommands = 0u;
};

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorSceneCommandQueue& queue,
                                                            const OperatorEditorSceneCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorPropertyCommandQueue& queue,
                                                            const OperatorEditorPropertyCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorRenderingCommandQueue& queue,
                                                            const OperatorEditorRenderingCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorReplayCommandQueue& queue,
                                                            const OperatorEditorReplayCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorToolCommandQueue& queue,
                                                            const OperatorEditorToolCommand& command,
                                                            bool* duplicate = nullptr );

// Converts the representative legacy packet fields into the shared queues and
// clears those fields so only the post-arbitration projection reaches owners.
SkullbonezCore::Core::SbResult NormalizeLegacyOperatorEditorCommands( InGameUICommands& commands );
OperatorEditorArbitrationResult ArbitrateOperatorEditorCommands( const OperatorEditorCommandQueues& legacy,
                                                                 const OperatorEditorCommandQueues& secondary );
SkullbonezCore::Core::SbResult ProjectOperatorEditorCommands( const OperatorEditorCommandQueues& exchange,
                                                              InGameUICommands& commands );
uint64_t FingerprintOperatorEditorFrameView( const OperatorEditorFrameView& view ) noexcept;
} // namespace SkullbonezCore::UI
