/*
File: SkullbonezSource/Runtime/Editor/EditorTools.h
Purpose:
  Declares editor placement helpers shared by input routing and editor tools.

Summary:
  Input owns gestures. Editor tools own how those gestures translate into
  editable object scale, clamp ranges, placement semantics, and editor command
  side effects expressed through focused per-operation borrows.

Invariants:
  - Scale helpers must be deterministic and side-effect free.
  - Command helpers borrow only the concrete owners used by that operation.
  - Object-type helpers must stay aligned with the editor tab object enum.
  - Preview, preflight, and commit operations must borrow the same asset registry.

Related:
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../../Assets/EditorHullAssets.h"
#include "../Replay/ReplayAuthoringPackets.h"
#include "../Input/InputController.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsHandles.h"

namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore;
}
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
struct PresentationSaveState;
}
namespace Runtime
{
class SceneController;
}
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
namespace Runtime
{
class EditorTracer;
class RuntimeInteractionController;
class CaptureController;
class SceneEntityStore;
class SceneWorld;
struct RunEditorPlacementState;
struct SceneSessionState;

struct EditorTerrainPlacement
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
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
    // Invariant: flags report accepted UI commands for InputFrame transition
    // recording; mode toggles route camera/cursor work through InputRouter.
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
Math::Vector::Vector3 EditorPlacementScaleFromGesture( int objectType, const Math::Vector::Vector3& startScale,
                                                       float dragPixelsX, float dragPixelsY, int wheelSteps );
bool TryGetEditorTerrainPlacement( Geometry::Terrain* terrain, const Math::Vector::Vector3& rayOrigin,
                                   const Math::Vector::Vector3& rayDirection, EditorTerrainPlacement& outPlacement );
bool TryComputeEditorObjectCenter( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int objectType,
                                   const Math::Vector::Vector3& terrainPoint, const Math::Vector::Vector3& placementScale,
                                   const Math::Orientation::Quaternion& orientation, const Assets::AssetSystem& assets,
                                   Math::Vector::Vector3& outCenter );
bool TryUpdateEditorPlacementPreview( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, RunEditorPlacementState& editor,
                                      Geometry::Terrain* terrain, const Assets::AssetSystem& assets, bool scaleGestureActive,
                                      int objectType, const EditorTerrainPlacement* mousePlacement );
bool CanPlaceEditorObjectAtTerrainPoint( SceneWorld& world, const Assets::AssetSystem& assets, int activeModelCapacity,
                                         EditorObjectPlacementRequest request );
bool PlaceEditorObjectAtTerrainPoint( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, RunEditorPlacementState& editor,
                                      SceneWorld& world, SceneSessionState& scene, const Assets::AssetSystem& assets,
                                      int activeModelCapacity, EditorObjectPlacementRequest request,
                                      EditorObjectPlacementResult& outResult );
bool BeginEditorGizmoDragGesture( SceneWorld& world, RuntimeInteractionController& interaction, int modelIndex, int axis,
                                  RuntimeGizmoDragKind gizmoKind, int clientX, int clientY );
void EndEditorGizmoDragGesture( RuntimeInteractionController& interaction );
void EndEditorPlacementScaleGesture( RuntimeInteractionController& interaction );
void CancelEditorGizmoDragState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction );
void ResetEditorUnfocusedInputState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction );
void ClearEditorManipulationState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction );

// Concept: editor selection stores stable handles plus a row hint. Resolve at
// the tool boundary before UI-only code needs a temporary model row.
int ResolveSelectedEditorModelIndex( RunEditorPlacementState& editor, const Physics::PhysicsBodyStore& bodyStore );
int PeekSelectedEditorModelIndex( const RunEditorPlacementState& editor, const Physics::PhysicsBodyStore& bodyStore );

// Concept: split editor tool translation units share this store-backed
// transform vocabulary. Keep it narrow so gizmo math, overlay tracing, and
// placement commits do not rediscover pose or shape facts from legacy object record.
Math::Vector::Vector3 EditorAxisVector( int axis );
float EditorShapeAxisExtent( const Math::CollisionDetection::CollisionShapeReference& shape, int axis );
float EditorColliderRadius( const Physics::ColliderRecord& collider );
float EditorGizmoAxisLength( float modelRadius );
float EditorGizmoRotationRadius( float modelRadius );
const Physics::PhysicsBodyRecord* TryResolveEditorBodyRecord( const Physics::PhysicsBodyStore& bodyStore,
                                                              Physics::PhysicsBodyHandle bodyHandle, int modelIndex );
const Physics::PhysicsBodyRecord* TryResolveEditorBodyRecord( const Physics::PhysicsBodyStore& bodyStore, int modelIndex );
bool TryResolveEditorBodyCollider( const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                   Physics::PhysicsBodyHandle bodyHandle, Physics::PhysicsColliderHandle colliderHandle,
                                   int modelIndex, const Physics::PhysicsBodyRecord*& outBody,
                                   const Physics::ColliderRecord*& outCollider );
bool TryGetEditorSelectionFrame( const SceneWorld& world, Physics::PhysicsBodyHandle selectedBodyHandle,
                                 Physics::PhysicsColliderHandle selectedColliderHandle, int selectedIndex,
                                 Math::Vector::Vector3& outOrigin, float& outRadius );
bool TryTraceEditorSelectionOverlayFromStores( const SceneWorld& world, Physics::PhysicsBodyHandle selectedBodyHandle,
                                               Physics::PhysicsColliderHandle selectedColliderHandle, int selectedIndex,
                                               EditorTracer& tracer, Math::Vector::Vector3& outOrigin, float& outRadius );
void CaptureEditorGizmoDragGroupState( RunEditorPlacementState& editor, const SceneWorld& world, bool allowRagdollGroup );
int ValidCapturedEditorGizmoGroupCount( const RunEditorPlacementState& editor, int modelCount );
void WakeEditorPhysicsBody( SceneWorld& world, int modelIndex );
void SeedEditorPhysicsBodyAsleep( SceneWorld& world, int modelIndex );
bool ResetEditorModelMotionAndWake( SceneWorld& world, int index, Physics::PhysicsBodyUpdateDesc update );
bool ResetEditorModelMotionAndWake( SceneWorld& world, int index, Physics::PhysicsBodyUpdateDesc update,
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
float DistanceRayToSegmentSquared( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                                   const Math::Vector::Vector3& segmentA, const Math::Vector::Vector3& segmentB );
EditorKeyboardShortcutResult HandleEditorKeyboardShortcut( RuntimeInputAction action, bool isDown, bool wasPressed );
EditorPlacementModeChangeResult SetEditorPlacementMode( RunEditorPlacementState& editor,
                                                        RuntimeInteractionController& interaction, bool enabled,
                                                        bool clearManipulation );
EditorPlacementModeChangeResult ToggleEditorPlacementMode( RunEditorPlacementState& editor,
                                                           RuntimeInteractionController& interaction );
void EnterEditorModeState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction,
                           RunCameraMode restoreCameraMode );
void ExitEditorModeState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction );
bool SetEditorPlaceStaticObject( RunEditorPlacementState& editor, bool placeStaticObject );
void ToggleEditorPlaceStaticObject( RunEditorPlacementState& editor );
void ToggleEditorTerrainAlign( RunEditorPlacementState& editor, RuntimeInteractionController& interaction );
EditorObjectTypeRequestResult SelectEditorObjectType( RunEditorPlacementState& editor,
                                                      RuntimeInteractionController& interaction, int requestedObjectType,
                                                      bool enterPlacementMode );
EditorPlacementPreModeUICommandResult ApplyEditorPlacementPreModeUICommands( RunEditorPlacementState& editor,
                                                                             RuntimeInteractionController& interaction,
                                                                             const UI::UIEditorCommands& commands );
EditorPlacementPostModeUICommandResult ApplyEditorPlacementPostModeUICommands( RunEditorPlacementState& editor,
                                                                               RuntimeInteractionController& interaction,
                                                                               const UI::UIEditorCommands& commands );
int HitEditorGizmoAxis( RunEditorPlacementState& editor, SceneWorld& world, const Math::Vector::Vector3& rayOrigin,
                        const Math::Vector::Vector3& rayDirection );
int HitEditorRotationGizmoAxis( RunEditorPlacementState& editor, SceneWorld& world, const Math::Vector::Vector3& rayOrigin,
                                const Math::Vector::Vector3& rayDirection );
bool TryEditorAxisRayParameter( RunEditorPlacementState& editor, SceneWorld& world, int axis,
                                const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                                float& outAxisT );
Math::Vector::Vector3 EditorAxisDragPlaneNormal( int axis, const Math::Vector::Vector3& rayDirection );
bool TryEditorAxisPlaneRayParameter( int axis, const Math::Vector::Vector3& planeOrigin,
                                     const Math::Vector::Vector3& planeNormal, const Math::Vector::Vector3& rayOrigin,
                                     const Math::Vector::Vector3& rayDirection, float& outAxisT );
bool TryEditorRotationRayAngle( RunEditorPlacementState& editor, SceneWorld& world, int axis,
                                const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                                float& outAngle );
void MoveSelectedEditorObjectAlongAxis( RunEditorPlacementState& editor, SceneWorld& world,
                                        RuntimeInteractionController& interaction, const Math::Vector::Vector3& rayOrigin,
                                        const Math::Vector::Vector3& rayDirection );
void RotateSelectedEditorObjectAroundAxis( RunEditorPlacementState& editor, SceneWorld& world,
                                           RuntimeInteractionController& interaction, const Math::Vector::Vector3& rayOrigin,
                                           const Math::Vector::Vector3& rayDirection );
void ScaleSelectedEditorObjectAlongAxis( RunEditorPlacementState& editor, SceneWorld& world,
                                         RuntimeInteractionController& interaction, const Math::Vector::Vector3& rayOrigin,
                                         const Math::Vector::Vector3& rayDirection );
void UpdateEditorGizmoHotAxes( RunEditorPlacementState& editor, SceneWorld& world, const Math::Vector::Vector3& rayOrigin,
                               const Math::Vector::Vector3& rayDirection, bool scaleMode );

// InputController owns keybinding data. Editor tools keep the two unrelated
// cold side effects separate so scene-save authority never travels with capture.
void HandleEditorSceneSaveHotkey( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, SceneWorld& world,
                                  const SceneSessionState& scene, const GameObjects::PresentationSaveState& presentation,
                                  bool wasPressed );
void HandleEditorScreenshotHotkey( CaptureController& capture, bool wasPressed );
} // namespace Runtime
} // namespace SkullbonezCore
