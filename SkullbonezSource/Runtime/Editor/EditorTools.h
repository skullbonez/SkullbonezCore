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

#include "../Replay/ReplayInteractionController.h"

#include "EditorHullAssets.h"
#include "../InputController.h"
#include "../RuntimeCameraMode.h"
#include "../RuntimeInteractionController.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsHandles.h"

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
} // namespace GameObjects
namespace Geometry
{
class Terrain;
}
namespace Physics
{
class ColliderStore;
struct PhysicsBodyUpdateDesc;
class PhysicsEngine;
struct ColliderRecord;
struct PhysicsBodyRecord;
struct PhysicsColliderCreateDesc;
class PhysicsBodyStore;
} // namespace Physics
namespace UI
{
struct UIEditorCommands;
}
namespace Basics
{
class RunEditorTracer;
class RuntimeInteractionController;
class CaptureController;
class SceneEntityStore;
struct RunEditorPlacementState;
struct RunSceneState;

namespace RunInternal
{
struct EditorSaveHotkeyContext
{
    GameObjects::GameModelCollection& models;
    const SceneEntityStore& entities;
    const RunSceneState& scene;
    Environment::WorldEnvironment& world;
    Environment::CameraCollection& cameras;
    CaptureController& capture;
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
    Physics::PhysicsEngine& physics;
    RunSceneState& scene;
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
    // Construction identity for the selected model row after placement commits.
    Physics::PhysicsBodyHandle placedBody;
    Physics::PhysicsColliderHandle placedCollider;
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
    Physics::PhysicsEngine& physics;
    RuntimeInteractionController& interaction;
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

struct EditorPlacementPreModeUICommandResult
{
    // Invariant: flags report accepted UI commands for RunInput action logging;
    // mode toggles still require Run-owned camera/cursor transition work.
    bool setPlaceStatic = false;
    bool requestedObjectType = false;
    bool enterPlacementMode = false;
    bool toggleEditorMode = false;
    bool togglePlacementMode = false;
};

struct EditorPlacementPostModeUICommandResult
{
    bool toggledPlaceStatic = false;
    bool toggledTerrainAlign = false;
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
bool BeginEditorGizmoDragGesture( EditorGizmoContext context,
                                  int modelIndex,
                                  int axis,
                                  bool angular,
                                  int clientX,
                                  int clientY );
void EndEditorGizmoDragGesture( EditorGizmoContext context );
void CancelEditorGizmoDragState( EditorGizmoContext context );
void ResetEditorUnfocusedInputState( EditorGizmoContext context );
void ClearEditorManipulationState( EditorGizmoContext context );
// Concept: editor selection stores stable handles plus a row hint. Resolve at
// the tool boundary before UI-only code needs a temporary model row.
int ResolveSelectedEditorModelIndex( RunEditorPlacementState& editor, const Physics::PhysicsBodyStore& bodyStore );
int PeekSelectedEditorModelIndex( const RunEditorPlacementState& editor, const Physics::PhysicsBodyStore& bodyStore );
// Concept: split editor tool translation units share this store-backed
// transform vocabulary. Keep it narrow so gizmo math, overlay tracing, and
// placement commits do not rediscover pose or shape facts from GameModel.
Math::Vector::Vector3 EditorAxisVector( int axis );
float EditorShapeAxisExtent( const Math::CollisionDetection::CollisionShape& shape, int axis );
float EditorColliderRadius( const Physics::ColliderRecord& collider );
float EditorGizmoAxisLength( float modelRadius );
float EditorGizmoRotationRadius( float modelRadius );
const Physics::PhysicsBodyRecord* TryResolveEditorBodyRecord( const Physics::PhysicsBodyStore& bodyStore,
                                                              Physics::PhysicsBodyHandle bodyHandle,
                                                              int modelIndex );
const Physics::PhysicsBodyRecord* TryResolveEditorBodyRecord( const Physics::PhysicsBodyStore& bodyStore,
                                                              int modelIndex );
bool TryResolveEditorBodyCollider( const Physics::PhysicsBodyStore& bodyStore,
                                   const Physics::ColliderStore& colliderStore,
                                   Physics::PhysicsBodyHandle bodyHandle,
                                   Physics::PhysicsColliderHandle colliderHandle,
                                   int modelIndex,
                                   const Physics::PhysicsBodyRecord*& outBody,
                                   const Physics::ColliderRecord*& outCollider );
bool TryResolveEditorBodyCollider( const Physics::PhysicsBodyStore& bodyStore,
                                   const Physics::ColliderStore& colliderStore,
                                   int modelIndex,
                                   const Physics::PhysicsBodyRecord*& outBody,
                                   const Physics::ColliderRecord*& outCollider );
bool TryGetEditorSelectionFrame( const GameObjects::GameModelCollection& collection,
                                 const Physics::PhysicsBodyStore& bodyStore,
                                 const Physics::ColliderStore& colliderStore,
                                 Physics::PhysicsBodyHandle selectedBodyHandle,
                                 Physics::PhysicsColliderHandle selectedColliderHandle,
                                 int selectedIndex,
                                 Math::Vector::Vector3& outOrigin,
                                 float& outRadius );
bool TryTraceEditorSelectionOverlayFromStores( const GameObjects::GameModelCollection& collection,
                                               const Physics::PhysicsBodyStore& bodyStore,
                                               const Physics::ColliderStore& colliderStore,
                                               Physics::PhysicsBodyHandle selectedBodyHandle,
                                               Physics::PhysicsColliderHandle selectedColliderHandle,
                                               int selectedIndex,
                                               RunEditorTracer& tracer,
                                               Math::Vector::Vector3& outOrigin,
                                               float& outRadius );
void CaptureEditorGizmoDragGroupState( RunEditorPlacementState& editor,
                                       const GameObjects::GameModelCollection& collection,
                                       const Physics::PhysicsBodyStore& bodyStore,
                                       bool allowRagdollGroup );
int ValidCapturedEditorGizmoGroupCount( const RunEditorPlacementState& editor, int modelCount );
void WakeEditorPhysicsBody( GameObjects::GameModelCollection& collection,
                            Physics::PhysicsEngine& physics,
                            int modelIndex );
void SeedEditorPhysicsBodyAsleep( GameObjects::GameModelCollection& collection,
                                  Physics::PhysicsEngine& physics,
                                  int modelIndex );
void ResetEditorModelMotionAndWake( GameObjects::GameModelCollection& collection,
                                    Physics::PhysicsEngine& physics,
                                    int index,
                                    Physics::PhysicsBodyUpdateDesc update );
void ResetEditorModelMotionAndWake( GameObjects::GameModelCollection& collection,
                                    Physics::PhysicsEngine& physics,
                                    int index,
                                    Physics::PhysicsBodyUpdateDesc update,
                                    Physics::PhysicsColliderCreateDesc colliderDesc );
// Concept: replay velocity gizmos share the editor axis/ring vocabulary so
// scrub-time velocity edits and live editor gizmos draw comparable handles.
float ReplayVelocityLinearBaseLength( float modelRadius );
float ReplayVelocityLinearVisualAxisT( float modelRadius, float velocityComponent );
float ReplayVelocityLinearUnitsPerWorld();
float ReplayVelocityAngularBaseRadius( float modelRadius );
float ReplayVelocityAngularVisualRadius( float modelRadius, float angularComponent );
float ReplayVelocityAxisComponent( const Math::Vector::Vector3& value, int axis );
void ReplayVelocitySetAxisComponent( Math::Vector::Vector3& value, int axis, float component );
void ReplayVelocityAxisColor( int axis, float heat, bool hot, bool active, float& r, float& g, float& b );
Math::Vector::Vector3 EditorRotationRingBasisA( int axis );
Math::Vector::Vector3 EditorRotationRingBasisB( int axis );
float WrapEditorAngleDelta( float delta );
float DistanceRayToSegmentSquared( const Math::Vector::Vector3& rayOrigin,
                                   const Math::Vector::Vector3& rayDirection,
                                   const Math::Vector::Vector3& segmentA,
                                   const Math::Vector::Vector3& segmentB );
EditorKeyboardShortcutResult HandleEditorKeyboardShortcut( RuntimeInputAction action, bool isDown, bool wasPressed );
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
EditorPlacementPreModeUICommandResult ApplyEditorPlacementPreModeUICommands( EditorGizmoContext context,
                                                                             const UI::UIEditorCommands& commands );
EditorPlacementPostModeUICommandResult ApplyEditorPlacementPostModeUICommands( RunEditorPlacementState& editor,
                                                                               const UI::UIEditorCommands& commands );
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
Math::Vector::Vector3 EditorAxisDragPlaneNormal( int axis, const Math::Vector::Vector3& rayDirection );
bool TryEditorAxisPlaneRayParameter( int axis,
                                     const Math::Vector::Vector3& planeOrigin,
                                     const Math::Vector::Vector3& planeNormal,
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
// Concept: RunInput owns keybinding data, but editor tools still own the cold
// save and screenshot side effects behind this action boundary.
void HandleEditorSaveHotkey( EditorSaveHotkeyContext context, RuntimeInputAction action, bool wasPressed );
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
