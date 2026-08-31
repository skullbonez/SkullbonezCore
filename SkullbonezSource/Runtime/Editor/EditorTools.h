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

#include "../../Core/PlatformWin32.h"
#include "../../Assets/EditorHullAssets.h"
#include "EditorCommandHistory.h"
#include "../Replay/ReplayAuthoringPackets.h"
#include "../Replay/ReplayEventCommand.h"
#include "../Input/InputController.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Scene/SceneLifecycle.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsHandles.h"
#include "../Interaction/OperatorEditorObjectCatalog.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>

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
class EditorTracer;
class SceneController;
} // namespace Runtime
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
class RuntimeInteractionController;
class SceneEntityStore;
class SceneWorld;
struct SceneSessionState;

// Invariant: pointer-ray validity and arbitration flags are sampled from one
// frame so the preview never combines a ray with another turn's UI decision.
struct EditorPointerPreviewInput
{
    bool blocksCameraMouse = false;
    bool inspectGizmoActive = false;
    bool hasWorldRay = false;
    bool controlDown = false;
    Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
};

// Invariant: selection consumes ray validity, origin, direction, and gizmo
// ownership from the same routed pointer turn.
struct EditorPointerSelectionInput
{
    bool inspectGizmoActive = false;
    bool hasWorldRay = false;
    Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
};

struct EditorPlacementScalePointerResult
{
    ReplayEventCommand replayEvent;
    bool consumed = false;
    bool enteredInteractiveScene = false;
    bool endedGesture = false;
    bool recordReplayEvent = false;
};

// Invariant: drag buttons, selection, and world ray describe one routed pointer
// turn; mixing them can apply a gesture to the wrong body.
struct EditorGizmoDragPointerInput
{
    bool leftDown = false;
    bool leftReleased = false;
    bool suppressWorldAction = false;
    bool hasWorldRay = false;
    int selectedModelIndex = -1;
    Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
};

struct EditorGizmoDragPointerResult
{
    ReplayEventCommandBatch replayEvents;
    bool consumed = false;
    bool endedGesture = false;
};

enum class EditorPointerModeAction
{
    EndPlacementScale,
    EndGizmoDrag,
    BeginGizmoScale,
    BeginGizmoRotate,
    BeginGizmoTranslate,
    BeginPlacementScale
};

struct EditorPointerRouteResult
{
    static constexpr std::size_t MAX_MODE_ACTIONS = 2;
    ReplayEventCommandBatch replayEvents;
    RuntimeInteractionTransition interactionTransition;
    bool consumed = false;
    bool enteredInteractiveScene = false;
    bool hasInteractionTransition = false;
    std::array<EditorPointerModeAction, MAX_MODE_ACTIONS> modeActions = {};
    std::size_t modeActionCount = 0;
};

enum class EditorGizmoGestureKind
{
    None,
    Translate,
    Rotate,
    Scale
};

struct EditorGizmoGesturePlan
{
    EditorGizmoGestureKind kind = EditorGizmoGestureKind::None;
    WorldInteractionOwner owner;
    InteractionExitReason reason;
    Physics::PhysicsBodyHandle selectedBody;
    int axis = -1;
    int clientX = 0;
    int clientY = 0;
    float axisParameter = 0.0f;
    Math::Vector::Vector3 startPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion startOrientation;
    Math::Vector::Vector3 dragPlaneNormal = Math::Vector::ZERO_VECTOR;
    Math::CollisionDetection::CollisionShape startShape;
};

struct EditorGizmoGestureResult
{
    bool attempted = false;
    bool consumed = false;
    EditorGizmoGestureKind kind = EditorGizmoGestureKind::None;
};

struct EditorPlacementScaleStartResult
{
    bool consumed = false;
    bool beganGesture = false;
};

// Invariant: buttons, modifiers, client position, wheel delta, and capture mode
// are one sampled viewport event consumed atomically by placement routing.
struct EditorViewportPlacementInput
{
    int unhandledWheelDelta = 0;
    bool rightDown = false;
    bool leftDown = false;
    bool controlDown = false;
    bool blocksCameraMouse = false;
    bool hasClientPosition = false;
    bool inputModeIsViewportLook = false;
    RuntimeInteractionGestureKind gesture = RuntimeInteractionGestureKind::None;
    int clientX = 0;
    int clientY = 0;
};

enum class EditorViewportModeAction
{
    None,
    Begin,
    End
};

struct EditorViewportPlacementResult
{
    bool resetMouseLook = false;
    bool enteredInteractiveScene = false;
    EditorViewportModeAction modeAction = EditorViewportModeAction::None;
};

struct RunEditorPlacementState
{
    static constexpr std::size_t GIZMO_DRAG_GROUP_CAPACITY = 16;

    EditorCommandHistory history;
    bool editorModeEnabled = false;
    bool placementModeEnabled = false;
    bool placeStaticObject = false;
    bool autoTerrainAlign = false;
    RunCameraMode restoreCameraModeAfterEditor = RunCameraMode::Demo;
    bool viewportLookActive = false;
    bool placementPreviewVisible = false;
    int objectType = UI::EditorTab::OBJECT_BOX;
    int placedObjectSerial = 0;
    Physics::ModelRowHint selectedModelRow;
    Physics::PhysicsBodyHandle selectedBody;
    Physics::PhysicsColliderHandle selectedCollider;
    int hotGizmoAxis = -1;
    int hotRotationAxis = -1;
    float gizmoDragStartAxisT = 0.0f;
    float gizmoDragStartRotationAngle = 0.0f;
    float placementYawRadians = 0.0f;
    int placementAltitudeSteps = 0;
    int placementScaleWheelSteps = 0;
    Math::Vector::Vector3 placementTerrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementCenter = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementRayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementRayHit = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementScale = Math::Vector::Vector3( 6.0f, 6.0f, 6.0f );
    Math::Vector::Vector3 placementScaleStart = Math::Vector::Vector3( 6.0f, 6.0f, 6.0f );
    Math::Vector::Vector3 placementScaleTerrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementScaleRayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion placementOrientation = Math::Orientation::IDENTITY_QUATERNION;
    POINT placementScaleStartClient = {};
    Math::Vector::Vector3 gizmoDragStartPosition = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 gizmoDragPlaneNormal = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion gizmoDragStartOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::CollisionDetection::CollisionShape gizmoDragStartShape;
    int gizmoDragGroupCount = 0;
    std::array<int, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupIndices = {};
    std::array<Math::Vector::Vector3, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupStartPositions = {};
    std::array<Math::Orientation::Quaternion, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupStartOrientations = {};
};

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

class EditorToolsOwner
{
  public:
    explicit EditorToolsOwner( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
        : m_resultDiagnostics( resultDiagnostics )
    {
    }

    RunEditorPlacementState& Editor();
    const RunEditorPlacementState& Editor() const;
    void AppendPlacementGhost( EditorTracer& tracer, const Assets::AssetSystem& assets ) const;
    bool PrepareSelectionCommand( const RuntimeInteractionCommand& command, const SceneWorld& world,
                                  RuntimeInteractionSelectionPlan& outPlan );
    bool CommitSelectionCommand( const RuntimeInteractionSelectionPlan& plan, RuntimeInteractionEvent& outEvent );
    bool ApplySelectionCommand( const RuntimeInteractionCommand& command, const SceneWorld& world );
    bool PrepareEditorPointerSelection( const EditorPointerSelectionInput& input, const SceneWorld& world,
                                        RuntimeInteractionSelectionPlan& outPlan, WorldInteractionOwner& outOwner,
                                        InteractionExitReason& outReason );
    EditorPlacementScalePointerResult RouteEditorPlacementScalePointer( bool leftReleased, bool suppressWorldAction,
                                                                        SceneWorld& world, SceneSessionState& scene,
                                                                        Assets::AssetSystem& assets, int activeModelCapacity,
                                                                        RuntimeInteractionController& interaction );
    EditorGizmoDragPointerResult RouteEditorGizmoDragPointer( const EditorGizmoDragPointerInput& input, SceneWorld& world,
                                                              RuntimeInteractionController& interaction );
    void RecordEditorTransformHistory( SceneWorld& world, RuntimeGizmoDragKind gizmoKind, int selectedModelIndex );
    void RecordEditorPlacementHistory( SceneWorld& world, int modelCountBefore, int modelCountAfter );
    bool UndoEditorCommand( SceneWorld& world, SceneSessionState& scene );
    bool RedoEditorCommand( SceneWorld& world, SceneSessionState& scene );
    bool DuplicateEditorSelection( SceneWorld& world, SceneSessionState& scene );
    bool DeleteEditorSelection( SceneWorld& world, SceneSessionState& scene );
    void ClearEditorHistory();
    bool PrepareEditorGizmoGesture( bool inspectGizmoActive, bool scaleMode, int selectedModelIndex, bool hasWorldRay,
                                    const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                                    int clientX, int clientY, SceneWorld& world, RuntimeInteractionController& interaction,
                                    EditorGizmoGesturePlan& outPlan );
    EditorGizmoGestureResult CommitEditorGizmoGesture( const EditorGizmoGesturePlan& plan, SceneWorld& world,
                                                       RuntimeInteractionController& interaction );
    EditorPlacementScaleStartResult BeginEditorPlacementScalePointer( bool inspectGizmoActive, bool hasClientPosition,
                                                                      int clientX, int clientY,
                                                                      RuntimeInteractionController& interaction );
    EditorViewportPlacementResult RouteEditorViewportPlacement( const EditorViewportPlacementInput& input );
    bool HasActiveEditorInteractionState( const RuntimeInteractionController& interaction ) const;
    bool InspectGizmoInteractionActive( RunCameraMode cameraMode, bool replayInspectionActive ) const;
    int RefreshEditorPointerPreview( const EditorPointerPreviewInput& input, SceneWorld& world,
                                     RuntimeInteractionController& interaction, const Assets::AssetSystem& assets );
    void ClearEditorInteractionForTransition( bool clearSelection, SceneWorld& world,
                                              RuntimeInteractionController& interaction );
    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet, SceneWorld& world,
                                RuntimeInteractionController& interaction );

  private:
    void ApplyEditorGizmoDrag( const EditorGizmoDragPointerInput& input, const RuntimeInteractionGesture& gesture,
                               SceneWorld& world, RuntimeInteractionController& interaction );
    void RecordEditorGizmoScaleRelease( const EditorGizmoDragPointerInput& input, SceneWorld& world,
                                        const RuntimeInteractionGesture& gesture, ReplayEventCommandBatch& events );
    void RecordEditorGizmoPoseRelease( const EditorGizmoDragPointerInput& input, SceneWorld& world,
                                       ReplayEventCommandBatch& events );
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    RunEditorPlacementState m_editor;
    SceneLifecycleGenerationObserver m_sceneLifecycleObserver;
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
std::size_t ProjectEditorOverlaySelection( RunEditorPlacementState& editor, const SceneWorld& world,
                                           std::span<Physics::PhysicsBodyHandle> bodies,
                                           std::span<Physics::PhysicsColliderHandle> colliders );

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
void CaptureEditorGizmoDragGroupState( RunEditorPlacementState& editor, const SceneWorld& world, bool allowRagdollGroup );
int ValidCapturedEditorGizmoGroupCount( const RunEditorPlacementState& editor, int modelCount );
void WakeEditorPhysicsBody( SceneWorld& world, int modelIndex );
void SeedEditorPhysicsBodyAsleep( SceneWorld& world, int modelIndex );
bool ResetEditorModelMotionAndWake( SceneWorld& world, int index, Physics::PhysicsBodyUpdateDesc update );
bool ResetEditorModelMotionAndWake( SceneWorld& world, int index, Physics::PhysicsBodyUpdateDesc update,
                                    Physics::PhysicsColliderCreateDesc colliderDesc );

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
std::string BuildEditorScreenshotPath();
} // namespace Runtime
} // namespace SkullbonezCore
