/*
File: SkullbonezSource/Runtime/Editor/EditorTools.h
Purpose:
  Declares editor placement helpers shared by input routing and editor tools.

Mental model:
  Input owns gestures. Editor tools own how those gestures translate into
  editable object scale, clamp ranges, placement semantics, and editor command
  side effects that can be described with explicit borrowed context.

Glossary:
  Asset system: Runtime-owned registry that resolves editor asset-library names
    without querying process-global state.
  Placement gesture: Mouse drag and wheel input used to size an object before
    placement commits.
  Hull scale: Per-axis size multiplier for convex hull editor assets.
  Uniform scale: One shared size value applied to all axes.
  Scale lock: Rule that keeps authored multi-part tree/root proportions stable.

Invariants:
  - Scale helpers must be deterministic and side-effect free.
  - Command helpers must take every mutable service through an explicit context.
  - Object-type helpers must stay aligned with the editor tab object enum.
  - Preview, preflight, and commit contexts must borrow the same asset registry.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
*/
#pragma once

#include "EditorHullAssets.h"
#include "../InputController.h"
#include "../RuntimeCameraMode.h"
#include "../RuntimeInteractionController.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace GameObjects
{
class GameModelCollection;
}
namespace Geometry
{
class Terrain;
}
namespace Basics
{
class EngineConfig;
class RuntimeInteractionController;
class RuntimeCommandQueue;
class RuntimeInputContext;
struct RunEditorPlacementState;
struct RunSceneState;

namespace RunInternal
{
struct EditorSaveHotkeyContext
{
    RuntimeInputContext& input;
    GameObjects::GameModelCollection& models;
    const RunSceneState& scene;
    Environment::WorldEnvironment& world;
    Environment::CameraCollection& cameras;
    RuntimeCommandQueue& commands;
};

struct EditorTerrainPlacement
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
};

struct EditorPlacementPreviewContext
{
    RunEditorPlacementState& editor;
    Geometry::Terrain* terrain;
    const Assets::AssetSystem& assets;
};

struct EditorObjectPlacementContext
{
    RunEditorPlacementState& editor;
    GameObjects::GameModelCollection& models;
    RunSceneState& scene;
    const EngineConfig& config;
    Environment::WorldEnvironment& world;
    Geometry::Terrain* terrain;
    const Assets::AssetSystem& assets;
    int activeModelCapacity;
};

struct EditorObjectPlacementRequest
{
    int objectType;
    bool fixedObject;
    Math::Vector::Vector3 terrainPoint;
};

struct EditorObjectPlacementResult
{
    bool placed = false;
    int modelCountBefore = 0;
    int modelCountAfter = 0;
    int objectType = 0;
    bool fixedObject = false;
    bool autoTerrainAlign = false;
    Math::Vector::Vector3 terrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementScale = Math::Vector::ZERO_VECTOR;
    float placementYawRadians = 0.0f;
};

struct EditorGizmoContext
{
    RunEditorPlacementState& editor;
    GameObjects::GameModelCollection& models;
    RuntimeInteractionController& interaction;
};

struct EditorKeyboardShortcutContext
{
    RuntimeInputContext& input;
};

struct EditorKeyboardShortcutResult
{
    bool altDown = false;
    bool togglePlacementMode = false;
};

struct EditorPlacementModeChangeResult
{
    bool placementModeEnabled = false;
    WorldInteractionOwner worldOwner = WorldInteractionOwner::EditorGizmo;
};

struct EditorObjectTypeRequestResult
{
    bool objectTypeChanged = false;
    bool enterPlacementMode = false;
};

int EditorMouseWheelSteps( int wheelDelta );
Assets::EditorHullAsset EditorHullAssetForType( int objectType );
bool EditorPlacementUsesUniformScale( int objectType );
bool EditorPlacementUsesHullScaleFactors( int objectType );
bool EditorPlacementUsesTreeScaleLock( int objectType );
Math::Vector::Vector3 EditorDefaultPlacementScale( int objectType );
Math::Vector::Vector3 EditorClampPlacementScale( int objectType, const Math::Vector::Vector3& scale );
Math::Vector::Vector3 EditorPlacementScaleFromGesture( int objectType,
                                                       const Math::Vector::Vector3& startScale,
                                                       float dragPixelsX,
                                                       float dragPixelsY,
                                                       int wheelSteps );
bool TryGetEditorTerrainPlacement( Geometry::Terrain* terrain,
                                   const Math::Vector::Vector3& rayOrigin,
                                   const Math::Vector::Vector3& rayDirection,
                                   EditorTerrainPlacement& outPlacement );
bool TryComputeEditorObjectCenter( int objectType,
                                   const Math::Vector::Vector3& terrainPoint,
                                   const Math::Vector::Vector3& placementScale,
                                   const Math::Orientation::Quaternion& orientation,
                                   const Assets::AssetSystem& assets,
                                   Math::Vector::Vector3& outCenter );
bool TryUpdateEditorPlacementPreview( EditorPlacementPreviewContext context,
                                      int objectType,
                                      const EditorTerrainPlacement* mousePlacement );
bool CanPlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context, EditorObjectPlacementRequest request );
bool PlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context,
                                      EditorObjectPlacementRequest request,
                                      EditorObjectPlacementResult& outResult );
bool BeginEditorGizmoDragGesture( EditorGizmoContext context, int modelIndex, int axis, bool angular );
void EndEditorGizmoDragGesture( EditorGizmoContext context );
void CancelEditorGizmoDragState( EditorGizmoContext context );
void ResetEditorUnfocusedInputState( EditorGizmoContext context );
void ClearEditorManipulationState( EditorGizmoContext context );
EditorKeyboardShortcutResult HandleEditorKeyboardShortcuts( EditorKeyboardShortcutContext context );
EditorPlacementModeChangeResult
SetEditorPlacementMode( EditorGizmoContext context, bool enabled, bool clearManipulation );
EditorPlacementModeChangeResult ToggleEditorPlacementMode( EditorGizmoContext context );
void EnterEditorModeState( EditorGizmoContext context, RunCameraMode restoreCameraMode );
void ExitEditorModeState( EditorGizmoContext context );
bool SetEditorPlaceStaticObject( RunEditorPlacementState& editor, bool placeStaticObject );
void ToggleEditorPlaceStaticObject( RunEditorPlacementState& editor );
void ToggleEditorTerrainAlign( RunEditorPlacementState& editor );
EditorObjectTypeRequestResult
SelectEditorObjectType( EditorGizmoContext context, int requestedObjectType, bool enterPlacementMode );
int HitEditorGizmoAxis( EditorGizmoContext context,
                        const Math::Vector::Vector3& rayOrigin,
                        const Math::Vector::Vector3& rayDirection );
int HitEditorRotationGizmoAxis( EditorGizmoContext context,
                                const Math::Vector::Vector3& rayOrigin,
                                const Math::Vector::Vector3& rayDirection );
bool TryEditorAxisRayParameter( EditorGizmoContext context,
                                int axis,
                                const Math::Vector::Vector3& rayOrigin,
                                const Math::Vector::Vector3& rayDirection,
                                float& outAxisT );
bool TryEditorRotationRayAngle( EditorGizmoContext context,
                                int axis,
                                const Math::Vector::Vector3& rayOrigin,
                                const Math::Vector::Vector3& rayDirection,
                                float& outAngle );
void MoveSelectedEditorObjectAlongAxis( EditorGizmoContext context,
                                        const Math::Vector::Vector3& rayOrigin,
                                        const Math::Vector::Vector3& rayDirection );
void RotateSelectedEditorObjectAroundAxis( EditorGizmoContext context,
                                           const Math::Vector::Vector3& rayOrigin,
                                           const Math::Vector::Vector3& rayDirection );
void ScaleSelectedEditorObjectAlongAxis( EditorGizmoContext context,
                                         const Math::Vector::Vector3& rayOrigin,
                                         const Math::Vector::Vector3& rayDirection );
void UpdateEditorGizmoHotAxes( EditorGizmoContext context,
                               const Math::Vector::Vector3& rayOrigin,
                               const Math::Vector::Vector3& rayDirection,
                               bool scaleMode );
void HandleEditorSaveHotkeys( EditorSaveHotkeyContext context );
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
