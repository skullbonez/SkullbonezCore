/*
File: SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp
Purpose:
  Implements replay velocity-edit keyboard policy, picking, dragging, mutation, and overlay drawing.

Summary:
  Velocity edit is a replay-owned interaction mode. It turns mouse rays into linear or angular
  velocity edits. Held samples publish only a selected-path delta for cheap
  visual feedback. Release schedules one authoritative prediction while the
  provisional line remains visible.

Glossary:
  Velocity gizmo: Replay overlay that exposes linear axes and angular rings for one body.
  Live advance: Replay scrubber mode that lets edited live physics advance while tools stay active.
  Replay velocity body view: Local value view resolved from PhysicsBodyStore and
    ColliderStore for one replay target; model index is a UI/presentation hint
    after scene object identity resolves to a PhysicsBodyHandle.

Invariants:
  - Pointer capture must end whenever the drag exits or the edited target becomes invalid.
  - Edited velocities are clamped before waking or mutating the physics body.
  - A held drag mutates the live body but never schedules prediction work.
  - Release schedules at most one authoritative replacement generation.
  - Hit testing, drag-start values, and gizmo drawing must read store rows, not
    the post-step legacy object record body mirror.
  - Velocity-edit helper functions are file-local to this translation unit.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp
*/
#include "ReplayAuthoring.h"
#include "ReplayCoordination.h"
#include "ReplayPresentation.h"
#include "ReplayScrubber.h"
#include "../../Assets/AssetKeys.h"
#include "../Editor/EditorTools.h"
#include "../Tools/RuntimeTools.h"
#include "../Input/InputRouter.h"
#include "../Scene/SceneEntityStore.h"
#include "../../Core/Profiler.h"

#include "ReplayOverlayLayout.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Physics;
namespace Physics = SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
bool IsReplayToolOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
           owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
           owner == WorldInteractionOwner::ReplayCauseTree;
}


Vector3 EditorAxisVector( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 0.0f, 1.0f );
    default:
        return SkullbonezCore::Math::Vector::ZERO_VECTOR;
    }
}


float ReplayVelocityLinearBaseLength( float modelRadius )
{
    return (std::max)( 10.0f, modelRadius + 7.0f );
}


float ReplayVelocityLinearVisualAxisT( float modelRadius, float velocityComponent )
{
    const float sign = velocityComponent < 0.0f ? -1.0f : 1.0f;
    const float t = std::clamp( fabsf( velocityComponent ) / REPLAY_VELOCITY_EDIT_LINEAR_MAX, 0.0f, 1.0f );
    return sign * ( ReplayVelocityLinearBaseLength( modelRadius ) + t * REPLAY_VELOCITY_EDIT_LINEAR_EXTRA );
}


float ReplayVelocityLinearUnitsPerWorld()
{
    return REPLAY_VELOCITY_EDIT_LINEAR_MAX / REPLAY_VELOCITY_EDIT_LINEAR_EXTRA;
}


float ReplayVelocityAngularBaseRadius( float modelRadius )
{
    return (std::max)( 11.0f, modelRadius + 6.0f );
}


float ReplayVelocityAngularVisualRadius( float modelRadius, float angularComponent )
{
    const float t = std::clamp( fabsf( angularComponent ) / REPLAY_VELOCITY_EDIT_ANGULAR_MAX, 0.0f, 1.0f );
    return ReplayVelocityAngularBaseRadius( modelRadius ) + t * (std::max)( 5.0f, modelRadius * 0.85f );
}


float ReplayVelocityAxisComponent( const Vector3& value, int axis )
{
    if ( axis == 0 )
    {
        return value.x;
    }

    if ( axis == 1 )
    {
        return value.y;
    }

    return value.z;
}


void ReplayVelocitySetAxisComponent( Vector3& value, int axis, float component )
{
    if ( axis == 0 )
    {
        value.x = component;
    }
    else if ( axis == 1 )
    {
        value.y = component;
    }
    else
    {
        value.z = component;
    }
}


Vector3 EditorRotationRingBasisA( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 2:
        return Vector3( 1.0f, 0.0f, 0.0f );
    default:
        return Vector3( 1.0f, 0.0f, 0.0f );
    }
}


Vector3 EditorRotationRingBasisB( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 1:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 1.0f, 0.0f );
    default:
        return Vector3( 0.0f, 1.0f, 0.0f );
    }
}


float WrapEditorAngleDelta( float delta )
{
    while ( delta > _PI )
    {
        delta -= 2.0f * _PI;
    }

    while ( delta < -_PI )
    {
        delta += 2.0f * _PI;
    }

    return delta;
}


float DistanceRayToSegmentSquared( const Vector3& rayOrigin, const Vector3& rayDirection, const Vector3& segmentA,
                                   const Vector3& segmentB )
{
    const Vector3 segment = segmentB - segmentA;
    const float segmentLenSq = Dot( segment, segment );

    if ( segmentLenSq <= TOLERANCE * TOLERANCE )
    {
        const Vector3 toPoint = segmentA - rayOrigin;
        const float rayT = (std::max)( 0.0f, Dot( toPoint, rayDirection ) );
        return VectorMagSquared( rayOrigin + rayDirection * rayT - segmentA );
    }

    const Vector3 w0 = rayOrigin - segmentA;
    const float a = Dot( rayDirection, rayDirection );
    const float b = Dot( rayDirection, segment );
    const float c = segmentLenSq;
    const float d = Dot( rayDirection, w0 );
    const float e = Dot( segment, w0 );
    const float denom = a * c - b * b;

    float rayT = 0.0f;
    float segmentT = 0.0f;

    if ( fabsf( denom ) > 1e-5f )
    {
        rayT = ( b * e - c * d ) / denom;
        segmentT = ( a * e - b * d ) / denom;
    }

    if ( rayT < 0.0f )
    {
        rayT = 0.0f;
        segmentT = std::clamp( e / c, 0.0f, 1.0f );
    }
    else if ( segmentT < 0.0f )
    {
        segmentT = 0.0f;
        rayT = (std::max)( 0.0f, -d / a );
    }
    else if ( segmentT > 1.0f )
    {
        segmentT = 1.0f;
        rayT = (std::max)( 0.0f, ( b - d ) / a );
    }

    const Vector3 rayPoint = rayOrigin + rayDirection * rayT;
    const Vector3 segmentPoint = segmentA + segment * segmentT;
    return VectorMagSquared( rayPoint - segmentPoint );
}

struct ReplayVelocityBodyView
{
    PhysicsBodyHandle body;
    ModelRowHint modelRow;
    Vector3 position = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion orientation = IDENTITY_QUATERNION;
    Vector3 linearVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 angularVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const CollisionShapeReference* shape = nullptr;
    float radius = 1.0f;
    bool fixed = false;
};


static bool TryResolveReplayVelocityBodyView( Physics::PhysicsSceneObjectId targetId, ModelRowHint targetModelRow,
                                              const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                              ReplayVelocityBodyView& outView )
{
    outView = ReplayVelocityBodyView {};

    if ( targetId.value == 0 )
    {
        return false;
    }

    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForSceneObjectId( targetId, targetModelRow.value );
    const int modelIndex = bodyStore.ModelIndexForHandle( bodyHandle );

    if ( !bodyHandle.IsValid() || modelIndex < 0 )
    {
        return false;
    }

    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    const ColliderRecord* collider = body
                                         ? colliderStore.RecordForHandle( colliderStore.HandleForBodyHandle( body->handle ) )
                                         : nullptr;

    if ( !body || !collider )
    {
        return false;
    }

    // Invariant: replay velocity edit resolves identity to a body handle before
    // it reads pose, velocity, or shape rows. modelIndex remains only for UI
    // gesture metadata and collider pairing while replay/editor identity moves
    // away from transient legacy object record order.
    outView.body = bodyHandle;
    outView.modelRow.value = modelIndex;
    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const auto hotFields = bodyStore.HotFields();
    outView.position = PhysicsBodyPosition( hotFields, bodyIndex );
    outView.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
    outView.linearVelocity = PhysicsBodyLinearVelocity( hotFields, bodyIndex );
    outView.angularVelocity = PhysicsBodyAngularVelocity( hotFields, bodyIndex );
    outView.shape = &collider->shape;
    outView.radius = (std::max)( 1.0f, (std::max)( hotFields.boundingRadius[bodyIndex], collider->boundingRadius ) );
    outView.fixed = hotFields.fixed[bodyIndex] != 0u;
    return outView.shape != nullptr;
}


int HitReplayVelocityLinearAxis( const ReplayVelocityBodyView& body, const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( body.fixed )
    {
        return -1;
    }

    const Vector3 origin = body.position;
    const float radius = body.radius;
    const float threshold = (std::max)( 1.15f, radius * 0.12f );
    const float thresholdSq = threshold * threshold;
    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;

    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( body.linearVelocity, axis );
        const Vector3 endpoint = origin + axisVector * ReplayVelocityLinearVisualAxisT( radius, component );
        const float distanceSq = DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, endpoint );

        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }

    return bestAxis;
}


int HitReplayVelocityAngularAxis( const ReplayVelocityBodyView& body, const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( body.fixed )
    {
        return -1;
    }

    const Vector3 origin = body.position;
    const float modelRadius = body.radius;
    int bestAxis = -1;
    float bestDiff = FLT_MAX;

    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 normal = EditorAxisVector( axis );
        const float denom = Dot( normal, rayDirection );

        if ( fabsf( denom ) <= 1e-4f )
        {
            continue;
        }

        const float rayT = ( Dot( normal, ( origin - rayOrigin ) ) ) / denom;

        if ( rayT < 0.0f )
        {
            continue;
        }

        const float ringRadius = ReplayVelocityAngularVisualRadius( modelRadius,
                                                                    ReplayVelocityAxisComponent( body.angularVelocity,
                                                                                                 axis ) );

        const float threshold = (std::max)( 1.10f, ringRadius * 0.08f );
        const Vector3 hitPoint = rayOrigin + rayDirection * rayT;
        const Vector3 radial = hitPoint - origin;
        const float radialDistance = VectorMag( radial - normal * ( Dot( radial, normal ) ) );
        const float diff = fabsf( radialDistance - ringRadius );

        if ( diff <= threshold && diff < bestDiff )
        {
            bestDiff = diff;
            bestAxis = axis;
        }
    }

    return bestAxis;
}


bool TryReplayVelocityAxisRayParameter( const ReplayVelocityBodyView& body, int axis, const Vector3& rayOrigin,
                                        const Vector3& rayDirection, float& outAxisT )
{
    if ( axis < 0 || axis > 2 || body.modelRow.value < 0 )
    {
        return false;
    }

    const Vector3 axisOrigin = body.position;
    const Vector3 axisVector = EditorAxisVector( axis );
    const Vector3 w = axisOrigin - rayOrigin;
    const float b = Dot( axisVector, rayDirection );
    const float d = Dot( axisVector, w );
    const float e = Dot( rayDirection, w );
    const float denom = 1.0f - b * b;

    if ( fabsf( denom ) <= 1e-5f )
    {
        return false;
    }

    outAxisT = ( b * e - d ) / denom;
    return true;
}


bool TryReplayVelocityAngularRayAngle( const ReplayVelocityBodyView& body, int axis, const Vector3& rayOrigin,
                                       const Vector3& rayDirection, float& outAngle )
{
    if ( axis < 0 || axis > 2 || body.modelRow.value < 0 )
    {
        return false;
    }

    const Vector3 origin = body.position;
    const Vector3 normal = EditorAxisVector( axis );
    const float denom = Dot( normal, rayDirection );

    if ( fabsf( denom ) <= 1e-4f )
    {
        return false;
    }

    const float rayT = ( Dot( normal, ( origin - rayOrigin ) ) ) / denom;

    if ( rayT < 0.0f )
    {
        return false;
    }

    Vector3 radial = rayOrigin + rayDirection * rayT - origin;
    radial -= normal * ( Dot( radial, normal ) );
    const float radialLenSq = Dot( radial, radial );

    if ( radialLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    radial = radial * ( 1.0f / sqrtf( radialLenSq ) );

    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    outAngle = atan2f( Dot( radial, basisB ), Dot( radial, basisA ) );
    return true;
}
} // namespace

ReplayKeyboardVelocityEditResult ReplayAuthoring::ApplyKeyboardVelocityEdit( const ReplayKeyboardVelocityEditInput& input,
                                                                             ReplayScrubber& scrubberOwner,
                                                                             const ReplayPresentation& presentationOwner )
{
    ReplayKeyboardVelocityEditResult result;

    if ( input.toggleAllowed && input.altDown && !VelocityEdit().keyboardAltWasDown )
    {
        const bool enableVelocityEdit = !VelocityEdit().enabled;
        PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Toggle" );

        if ( SetVelocityEditEnabled( enableVelocityEdit ) )
        {
            result.cancelToolDrag = true;

            if ( enableVelocityEdit )
            {
                result.enterInteractive = true;

                if ( scrubberOwner.SetLiveAdvanceHeld( true ) )
                {
                    const ReplayScrubberView scrubber = scrubberOwner.View();
                    const bool useInspectionCamera = scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld ||
                                                     presentationOwner.CameraView().focusKind !=
                                                         RunReplayCameraFocusKind::None;

                    result.cameraAction = useInspectionCamera ? ReplayKeyboardVelocityEditCameraAction::EnterInspection
                                                              : ReplayKeyboardVelocityEditCameraAction::ExitInspection;
                }

                result.setWorldOwner = true;
                result.worldOwner = WorldInteractionOwner::ReplayVelocityEdit;
            }
            else if ( input.currentWorldOwner == WorldInteractionOwner::ReplayVelocityEdit )
            {
                result.setWorldOwner = true;
                result.worldOwner = WorldInteractionOwner::ReplayScrub;
            }
        }

        scrubberOwner.KeepVisible( input.now, ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS );
    }

    ObserveVelocityEditAltKey( input.altDown );
    return result;
}


bool ReplayAuthoring::PrepareVelocityEditInput( bool editorModeEnabled, bool scenePhysicsEnabled, int screenWidth,
                                                int screenHeight, InputRouter& inputRouter,
                                                RuntimeInteractionController& interaction )
{
    if ( !VelocityEdit().enabled || editorModeEnabled || !scenePhysicsEnabled || screenWidth <= 0 || screenHeight <= 0 )
    {
        const bool endDragGesture = interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayVelocityDrag;
        ClearVelocityEditInputState();

        if ( endDragGesture )
        {
            interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            inputRouter.ReleaseNativeCapture();
        }

        return false;
    }

    return true;
}


bool ReplayAuthoring::TickVelocityEditInput( ReplayPresentation& presentationOwner, ReplayScrubber& scrubberOwner,
                                             const ReplayPathPickInput& pointerRay, bool uiBlocksMouse, double now,
                                             InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                             PhysicsEngine& velocityPhysics, std::size_t entityCount,
                                             bool& outEnterInteractive, bool& outPathPickRequested,
                                             ReplayInspectionCameraAction& outInspectionCameraAction )
{
    outPathPickRequested = false;
    outInspectionCameraAction = ReplayInspectionCameraAction::None;
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Input" );
    const RuntimeMouseEdges& pointer = inputRouter.UiSnapshot().mouse;
    const bool leftDown = pointer.leftDown;
    const bool leftPressed = pointer.leftPressed;
    const bool leftReleased = pointer.leftReleased;
    const auto velocityDragActive = [&]()
    { return interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayVelocityDrag; };

    const auto finishVelocityDrag = [&]()
    {
        (void)FinishVelocityEditDrag();

        interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayVelocityDrag );
        inputRouter.ReleaseNativeCapture();
    };

    const auto shouldUseInspectionCamera = [&]()
    {
        const ReplayScrubberView scrubber = scrubberOwner.View();

        return scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld ||
               presentationOwner.CameraView().focusKind != RunReplayCameraFocusKind::None;
    };

    const Vector3& rayOrigin = pointerRay.rayOrigin;
    const Vector3& rayDirection = pointerRay.rayDirection;

    if ( !pointerRay.hasWorldRay )
    {
        if ( velocityDragActive() && ( leftReleased || !leftDown ) )
        {
            finishVelocityDrag();
        }

        return velocityDragActive();
    }

    // Invariant: the frame boundary prepares paired physics rows before replay
    // input. This handler reads those explicit owners and never repairs legacy
    // model topology from inside an interaction hot path.
    const PhysicsBodyStore& velocityBodies = PhysicsEngine::ReadBodies( velocityPhysics );
    const ColliderStore& velocityColliders = PhysicsEngine::ReadColliders( velocityPhysics );
    const bool velocityStoresReady = velocityBodies.Count() == velocityColliders.Count() &&
                                     velocityBodies.Count() == entityCount;

    const RunReplayPathVisualizerState& path = presentationOwner.PathVisualizer();
    const auto tryResolveVelocityBody = [&]( ReplayVelocityBodyView& outBody )
    {
        return velocityStoresReady && path.hasTarget &&
               TryResolveReplayVelocityBodyView( path.targetId, path.targetModelRow, velocityBodies, velocityColliders,
                                                 outBody );
    };

    const auto applyReplayVelocityEditDrag = [&]( const Vector3& dragRayOrigin, const Vector3& dragRayDirection )
    {

        // Hazard: a drag can outlive its target if the scene reloads or the
        // edited body is removed. All capture and active-axis state must unwind
        // before any velocity math touches the model collection.
        ReplayVelocityBodyView body;

        const RuntimeInteractionGesture& gesture = interaction.Gesture();

        if ( !tryResolveVelocityBody( body ) || gesture.kind != RuntimeInteractionGestureKind::ReplayVelocityDrag ||
             gesture.axis < 0 )
        {
            finishVelocityDrag();
            return;
        }

        Vector3 linearVelocity = VelocityEdit().dragStartLinearVelocity;
        Vector3 angularVelocity = VelocityEdit().dragStartAngularVelocity;

        if ( gesture.angular )
        {
            float currentAngle = 0.0f;

            if ( !TryReplayVelocityAngularRayAngle( body, gesture.axis, dragRayOrigin, dragRayDirection, currentAngle ) )
            {
                return;
            }

            const float angleDelta = WrapEditorAngleDelta( currentAngle - VelocityEdit().dragStartAngle );
            const float component = ReplayVelocityAxisComponent( VelocityEdit().dragStartAngularVelocity, gesture.axis ) +
                                    angleDelta * ( REPLAY_VELOCITY_EDIT_ANGULAR_MAX / _PI );

            ReplayVelocitySetAxisComponent( angularVelocity, gesture.axis,
                                            std::clamp( component, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX,
                                                        REPLAY_VELOCITY_EDIT_ANGULAR_MAX ) );
        }
        else
        {
            float axisT = 0.0f;

            if ( !TryReplayVelocityAxisRayParameter( body, gesture.axis, dragRayOrigin, dragRayDirection, axisT ) )
            {
                return;
            }

            const float component = ReplayVelocityAxisComponent( VelocityEdit().dragStartLinearVelocity, gesture.axis ) +
                                    ( axisT - VelocityEdit().dragStartAxisT ) * ReplayVelocityLinearUnitsPerWorld();

            ReplayVelocitySetAxisComponent( linearVelocity, gesture.axis,
                                            std::clamp( component, -REPLAY_VELOCITY_EDIT_LINEAR_MAX,
                                                        REPLAY_VELOCITY_EDIT_LINEAR_MAX ) );
        }

        linearVelocity.x = std::clamp( linearVelocity.x, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );

        linearVelocity.y = std::clamp( linearVelocity.y, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );

        linearVelocity.z = std::clamp( linearVelocity.z, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );

        angularVelocity.x = std::clamp( angularVelocity.x, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX,
                                        REPLAY_VELOCITY_EDIT_ANGULAR_MAX );

        angularVelocity.y = std::clamp( angularVelocity.y, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX,
                                        REPLAY_VELOCITY_EDIT_ANGULAR_MAX );

        angularVelocity.z = std::clamp( angularVelocity.z, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX,
                                        REPLAY_VELOCITY_EDIT_ANGULAR_MAX );

        constexpr float VELOCITY_CHANGE_EPSILON_SQUARED = 1.0e-10f;
        const bool velocityChanged = VectorMagSquared( linearVelocity - body.linearVelocity ) >
                                         VELOCITY_CHANGE_EPSILON_SQUARED ||
                                     VectorMagSquared( angularVelocity - body.angularVelocity ) >
                                         VELOCITY_CHANGE_EPSILON_SQUARED;

        // A stationary held pointer resolves to the velocity already stored.
        // Skipping that no-op prevents needless replacement generations while
        // still publishing every materially changed pointer sample.
        if ( velocityChanged && velocityPhysics.SetBodyVelocity( body.body, linearVelocity, angularVelocity, true ) )
        {

            // Why: held samples bend only the selected published path. This
            // fixed-size command replaces its predecessor without scheduling a
            // private-world build or disturbing the other retained paths.
            QueueVelocityEditPreview( VelocityEdit().dragTargetId, linearVelocity - VelocityEdit().dragStartLinearVelocity );
            scrubberOwner.SetVisible( true, now, REPLAY_SCRUBBER_VISIBLE_SECONDS );
        }
    };

    if ( velocityDragActive() )
    {
        if ( leftDown && !uiBlocksMouse )
        {
            applyReplayVelocityEditDrag( rayOrigin, rayDirection );
        }

        if ( leftReleased || !leftDown )
        {
            finishVelocityDrag();
        }

        return true;
    }

    ReplayVelocityBodyView hotBody;
    const bool hasHotBody = !uiBlocksMouse && tryResolveVelocityBody( hotBody );
    const int hotAngularAxis = hasHotBody ? HitReplayVelocityAngularAxis( hotBody, rayOrigin, rayDirection ) : -1;
    const int hotLinearAxis = ( !hasHotBody || hotAngularAxis >= 0 )
                                  ? -1
                                  : HitReplayVelocityLinearAxis( hotBody, rayOrigin, rayDirection );

    SetVelocityEditHoverAxes( hotLinearAxis, hotAngularAxis );

    const auto armBaselineComparisonForDrag = [&]()
    {
        if ( presentationOwner.PathVisualizer().hasTarget )
        {

            // Why: the old future must be retained before the first drag tick
            // dirties prediction. The visualizer owns the actual capture so it
            // can reuse the same rest-pose and replay-reserve rules as drawing.
            QueueVelocityMutationBaselinePreparation();
        }
    };

    if ( !uiBlocksMouse && leftPressed )
    {
        const RuntimePointerEvent& runtimePointer = inputRouter.RuntimeSnapshot().pointer;
        ReplayVelocityBodyView body;

        if ( runtimePointer.hasClientPosition && tryResolveVelocityBody( body ) && !body.fixed )
        {
            const POINT mouse { runtimePointer.clientX, runtimePointer.clientY };

            if ( VelocityEdit().hotAngularAxis >= 0 )
            {
                float startAngle = 0.0f;

                if ( TryReplayVelocityAngularRayAngle( body, VelocityEdit().hotAngularAxis, rayOrigin, rayDirection,
                                                       startAngle ) )
                {
                    outEnterInteractive = true;

                    if ( scrubberOwner.SetLiveAdvanceHeld( true ) && !IsReplayToolOwner( interaction.Owner() ) )
                    {
                        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                         WorldInteractionOwner::ReplayScrub,
                                                                         InteractionExitReason::EnterReplay );
                    }

                    if ( shouldUseInspectionCamera() )
                    {
                        outInspectionCameraAction = ReplayInspectionCameraAction::Enter;
                    }
                    else
                    {
                        outInspectionCameraAction = ReplayInspectionCameraAction::Exit;
                    }

                    RuntimeInteractionGesture gesture;
                    gesture.kind = RuntimeInteractionGestureKind::ReplayVelocityDrag;
                    gesture.button = RuntimePointerButton::Left;
                    gesture.startX = mouse.x;
                    gesture.startY = mouse.y;
                    gesture.body = body.body;
                    gesture.axis = VelocityEdit().hotAngularAxis;
                    gesture.angular = true;

                    if ( !interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayVelocityEdit, gesture ) )
                    {
                        return false;
                    }

                    ReplayVelocityEditDragStart dragStart;
                    dragStart.targetId = path.targetId;
                    dragStart.angle = startAngle;
                    dragStart.linearVelocity = body.linearVelocity;
                    dragStart.angularVelocity = body.angularVelocity;
                    armBaselineComparisonForDrag();
                    BeginVelocityEditDrag( dragStart );
                    inputRouter.RequestNativeCapture();
                    return true;
                }
            }
            else if ( VelocityEdit().hotLinearAxis >= 0 )
            {
                float axisT = 0.0f;

                if ( TryReplayVelocityAxisRayParameter( body, VelocityEdit().hotLinearAxis, rayOrigin, rayDirection,
                                                        axisT ) )
                {
                    outEnterInteractive = true;

                    if ( scrubberOwner.SetLiveAdvanceHeld( true ) && !IsReplayToolOwner( interaction.Owner() ) )
                    {
                        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                         WorldInteractionOwner::ReplayScrub,
                                                                         InteractionExitReason::EnterReplay );
                    }

                    if ( shouldUseInspectionCamera() )
                    {
                        outInspectionCameraAction = ReplayInspectionCameraAction::Enter;
                    }
                    else
                    {
                        outInspectionCameraAction = ReplayInspectionCameraAction::Exit;
                    }

                    RuntimeInteractionGesture gesture;
                    gesture.kind = RuntimeInteractionGestureKind::ReplayVelocityDrag;
                    gesture.button = RuntimePointerButton::Left;
                    gesture.startX = mouse.x;
                    gesture.startY = mouse.y;
                    gesture.body = body.body;
                    gesture.axis = VelocityEdit().hotLinearAxis;
                    gesture.angular = false;

                    if ( !interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayVelocityEdit, gesture ) )
                    {
                        return false;
                    }

                    ReplayVelocityEditDragStart dragStart;
                    dragStart.targetId = path.targetId;
                    dragStart.axisT = axisT;
                    dragStart.linearVelocity = body.linearVelocity;
                    dragStart.angularVelocity = body.angularVelocity;
                    armBaselineComparisonForDrag();
                    BeginVelocityEditDrag( dragStart );
                    inputRouter.RequestNativeCapture();
                    return true;
                }
            }
        }

        // Target picking needs scene publications that gizmo mutation does not.
        // Defer that phase to ReplayRuntime instead of carrying another source pack.
        outPathPickRequested = true;
        return true;
    }

    return VelocityEdit().hotLinearAxis >= 0 || VelocityEdit().hotAngularAxis >= 0;
}


bool ReplayAuthoring::TryPickVelocityEditTarget( ReplayPresentation& presentationOwner, ReplayScrubber& scrubberOwner, const ReplaySolverFrameSample* currentSolverSample,
                                                 const SceneEntityStore& entities, std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                                 PhysicsEngine& velocityPhysics, const ReplayPathPickInput& pointerRay, RuntimeInteractionController& interaction,
                                                 double now, bool& outEnterInteractive, ReplayInspectionCameraAction& outInspectionCameraAction )
{
    const PhysicsBodyStore& velocityBodies = PhysicsEngine::ReadBodies( velocityPhysics );
    const ColliderStore& velocityColliders = PhysicsEngine::ReadColliders( velocityPhysics );

    if ( velocityBodies.Count() != velocityColliders.Count() || velocityBodies.Count() != entities.Count() )
    {
        return false;
    }

    // Concept: velocity edit owns replay body targeting. A click on the body
    // selects the gizmo target instead of falling through to world selection.
    ReplayPathPickInput pickInput;
    pickInput.hasWorldRay = pointerRay.hasWorldRay;
    pickInput.rayOrigin = pointerRay.rayOrigin;
    pickInput.rayDirection = pointerRay.rayDirection;
    const ReplayPathPickResult pickResult = presentationOwner.TryPickPathTarget( pickInput, entities, velocityBodies,
                                                                                 velocityColliders, presentation,
                                                                                 currentSolverSample );

    if ( pickResult.picked )
    {
        QueuePredictionCacheReset();
    }
    else if ( pickResult.exitInspectionCamera )
    {
        const bool ownedSimulationPause = presentationOwner.ClearCameraFocus();
        ClearCauseTreeFocus();
        const ReplayScrubberView scrubber = scrubberOwner.View();

        if ( ownedSimulationPause && scrubber.liveAdvanceHeld && !scrubber.historicalSamplePaused )
        {
            scrubberOwner.SetLiveAdvanceHeld( false );
        }

        presentationOwner.ClearPathState();
        ResetCauseTreeRows();
        QueuePredictionCacheReset();
    }

    if ( presentationOwner.PathVisualizer().hasTarget )
    {
        outEnterInteractive = true;
        const ReplayScrubberView scrubber = scrubberOwner.View();
        const bool shouldUseInspectionCamera = scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld ||
                                               presentationOwner.CameraView().focusKind != RunReplayCameraFocusKind::None;

        if ( scrubberOwner.SetLiveAdvanceHeld( true ) && shouldUseInspectionCamera )
        {
            outInspectionCameraAction = ReplayInspectionCameraAction::Enter;
        }

        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayVelocityEdit,
                                                         InteractionExitReason::EnterReplay );

        QueuePredictionRefresh( true );
        scrubberOwner.SetVisible( true, now, REPLAY_SCRUBBER_VISIBLE_SECONDS );
    }

    return true;
}


void ReplayAuthoring::AppendVelocityEditOverlay( Physics::PhysicsSceneObjectId targetId, ModelRowHint targetModelRow,
                                                 PhysicsEngine& velocityPhysics, bool editorModeEnabled,
                                                 const RuntimeInteractionGesture& gesture, EditorTracer& tracer ) const
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Overlay" );

    if ( !m_velocityEdit.enabled || editorModeEnabled )
    {
        return;
    }

    ReplayVelocityBodyView body;

    if ( !TryResolveReplayVelocityBodyView( targetId, targetModelRow,
                                            SkullbonezCore::Physics::PhysicsEngine::ReadBodies( velocityPhysics ),
                                            SkullbonezCore::Physics::PhysicsEngine::ReadColliders( velocityPhysics ),
                                            body ) ||
         body.fixed || !body.shape )
    {
        return;
    }

    tracer.AddReplayVelocityGizmo( body.position, body.orientation, *body.shape, body.radius, body.linearVelocity,
                                   body.angularVelocity, m_velocityEdit.hotLinearAxis, m_velocityEdit.hotAngularAxis,
                                   gesture.kind == RuntimeInteractionGestureKind::ReplayVelocityDrag ? gesture.axis : -1,
                                   gesture.kind == RuntimeInteractionGestureKind::ReplayVelocityDrag && gesture.angular );
}
