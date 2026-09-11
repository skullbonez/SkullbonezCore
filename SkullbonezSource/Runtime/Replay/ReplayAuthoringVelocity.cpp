// Velocity input uses the same world-aligned vector-tip axes as drawing.
#include "ReplayAuthoring.h"
#include "ReplayCoordination.h"
#include "ReplayPresentation.h"
#include "ReplayScrubber.h"
#include "../../Assets/AssetKeys.h"
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


static bool TryResolveReplayVelocityBodyView( Physics::PhysicsSceneObjectId targetId,
                                              ModelRowHint targetModelRow,
                                              const PhysicsBodyStore& bodyStore,
                                              const ColliderStore& colliderStore,
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
    const ColliderRecord* collider = body ? colliderStore.RecordForHandle( colliderStore.HandleForBodyHandle( body->handle ) ) : nullptr;

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


bool ProjectVelocityPoint( const ReplayVelocityInputFrame& frame, const Vector3& point, Vector3& screen )
{
    const auto* m = frame.viewProjection.Data();
    const float w = m[3] * point.x + m[7] * point.y + m[11] * point.z + m[15];
    if ( w <= 0.001f )
    {
        return false;
    }
    // Rendering maps NDC into Window's presentation viewport, which shrinks
    // and moves as panels open. Picking must use that same client rectangle.
    screen = { frame.viewportX + ( ( m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12] ) / w + 1.0f ) * frame.screenWidth * 0.5f, frame.viewportY + ( 1.0f - ( m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13] ) / w ) * frame.screenHeight * 0.5f, 0.0f };
    return true;
}

int HitVelocityAxis( const ReplayVelocityBodyView& body, const ReplayVelocityInputFrame& frame, bool angular, ReplayVelocityScreenDrag& drag )
{
    if ( body.fixed )
    {
        return -1;
    }
    const float length = ReplayVelocityHandleLength( body.radius );
    const float scale = ReplayVelocityVectorScale( angular );
    const Vector3 tip = body.position + ( angular ? body.angularVelocity : body.linearVelocity ) * scale;
    Vector3 start;
    if ( !ProjectVelocityPoint( frame, tip, start ) )
    {
        return -1;
    }
    const Vector3 pointer( static_cast<float>( frame.mouseX ), static_cast<float>( frame.mouseY ), 0.0f );
    float best = 81.0f;
    int hit = -1;
    for ( int axis = 0; axis < 3; ++axis )
    {
        Vector3 end;
        if ( !ProjectVelocityPoint( frame, tip + EditorAxisVector( axis ) * length, end ) )
        {
            continue;
        }
        const Vector3 segment = end - start;
        const float squared = VectorMagSquared( segment );
        if ( squared < 16.0f )
        {
            continue;
        }
        const float t = std::clamp( Dot( pointer - start, segment ) / squared, 0.2f, 1.0f );
        const float distance = VectorMagSquared( pointer - ( start + segment * t ) );
        if ( distance < best && drag.Begin( start, end, pointer, length / scale ) )
        {
            hit = axis;
            best = distance;
        }
    }
    return hit;
}

} // namespace

ReplayKeyboardVelocityEditResult ReplayAuthoring::ApplyKeyboardVelocityEdit( const ReplayKeyboardVelocityEditInput& input, ReplayScrubber& scrubberOwner, const ReplayPresentation& presentationOwner )
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
                    const bool useInspectionCamera = ReplayScrubNeedsInspectionCamera( scrubber.liveAdvanceHeld, presentationOwner.CameraView().focusKind );

                    result.cameraAction = useInspectionCamera ? ReplayKeyboardVelocityEditCameraAction::EnterInspection : ReplayKeyboardVelocityEditCameraAction::ExitInspection;
                }

                result.setWorldOwner = true;
                result.worldOwner = ReplayWorldOwnerRequest::VelocityEdit;
            }
            else if ( input.velocityEditOwnsWorld )
            {
                result.setWorldOwner = true;
                result.worldOwner = ReplayWorldOwnerRequest::Scrub;
            }
        }

        scrubberOwner.KeepVisible( input.now, ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS );
    }

    ObserveVelocityEditAltKey( input.altDown );
    return result;
}


bool ReplayAuthoring::PrepareVelocityEditInput( bool editorModeEnabled,
                                                bool scenePhysicsEnabled,
                                                int screenWidth,
                                                int screenHeight,
                                                const ReplayToolGestureView& gesture,
                                                ReplayInteractionRequest& outInteraction )
{
    outInteraction = {};

    if ( !VelocityEdit().enabled || editorModeEnabled || !scenePhysicsEnabled || screenWidth <= 0 || screenHeight <= 0 )
    {
        const bool endDragGesture = gesture.kind == ReplayToolGestureKind::VelocityDrag;
        ClearVelocityEditInputState();

        if ( endDragGesture )
        {
            outInteraction.EndGesture();
        }

        return false;
    }

    return true;
}


bool ReplayAuthoring::TickVelocityEditInput( ReplayPresentation& presentationOwner,
                                             ReplayScrubber& scrubberOwner,
                                             const ReplayPathPickInput& pointerRay,
                                             bool uiBlocksMouse,
                                             double now,
                                             const ReplayVelocityInputFrame& frame,
                                             PhysicsEngine& velocityPhysics,
                                             std::size_t entityCount,
                                             ReplayVelocityInputResult& outResult,
                                             ReplayInspectionCameraAction& outInspectionCameraAction )
{
    outResult = {};
    outInspectionCameraAction = ReplayInspectionCameraAction::None;
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Input" );
    const auto velocityDragActive = [&]() { return frame.gesture.kind == ReplayToolGestureKind::VelocityDrag; };

    const auto finishVelocityDrag = [&]()
    {
        (void)FinishVelocityEditDrag();

        outResult.interaction.EndGesture();
    };

    if ( !pointerRay.hasWorldRay )
    {
        if ( velocityDragActive() && ( frame.leftReleased || !frame.leftDown ) )
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
    const bool velocityStoresReady = velocityBodies.Count() == velocityColliders.Count() && velocityBodies.Count() == entityCount;

    const RunReplayPathVisualizerState& path = presentationOwner.PathVisualizer();
    const auto tryResolveVelocityBody = [&]( ReplayVelocityBodyView& outBody )
    { return velocityStoresReady && path.hasTarget && TryResolveReplayVelocityBodyView( path.targetId, path.targetModelRow, velocityBodies, velocityColliders, outBody ); };

    const auto applyReplayVelocityEditDrag = [&]()
    {
        // Hazard: a drag can outlive its target if the scene reloads or the
        // edited body is removed. All capture and active-axis state must unwind
        // before any velocity math touches the model collection.
        ReplayVelocityBodyView body;

        const ReplayToolGestureView& gesture = frame.gesture;

        if ( !tryResolveVelocityBody( body ) || gesture.kind != ReplayToolGestureKind::VelocityDrag || gesture.axis < 0 )
        {
            finishVelocityDrag();
            return;
        }

        Vector3 linearVelocity = VelocityEdit().dragStartLinearVelocity;
        Vector3 angularVelocity = VelocityEdit().dragStartAngularVelocity;

        auto& value = gesture.angular ? angularVelocity : linearVelocity;
        const float maximum = gesture.angular ? REPLAY_VELOCITY_EDIT_ANGULAR_MAX : REPLAY_VELOCITY_EDIT_LINEAR_MAX;
        const float component = ReplayVelocityAxisComponent( value, gesture.axis ) + VelocityEdit().screenDrag.Delta( static_cast<float>( frame.mouseX ), static_cast<float>( frame.mouseY ) );
        ReplayVelocitySetAxisComponent( value, gesture.axis, std::clamp( component, -maximum, maximum ) );

        constexpr float VELOCITY_CHANGE_EPSILON_SQUARED = 1.0e-10f;
        const bool velocityChanged = VectorMagSquared( linearVelocity - body.linearVelocity ) > VELOCITY_CHANGE_EPSILON_SQUARED ||
                                     VectorMagSquared( angularVelocity - body.angularVelocity ) > VELOCITY_CHANGE_EPSILON_SQUARED;

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
        if ( frame.leftDown )
        {
            applyReplayVelocityEditDrag();
        }

        if ( frame.leftReleased || !frame.leftDown )
        {
            finishVelocityDrag();
        }

        return true;
    }

    ReplayVelocityBodyView hotBody;
    const bool hasHotBody = !uiBlocksMouse && tryResolveVelocityBody( hotBody );
    ReplayVelocityScreenDrag screenDrag;
    const int hotAxis = hasHotBody ? HitVelocityAxis( hotBody, frame, VelocityEdit().angular, screenDrag ) : -1;
    SetVelocityEditHoverAxes( VelocityEdit().angular ? -1 : hotAxis, VelocityEdit().angular ? hotAxis : -1 );

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

    // App prepares the inspection camera before either handle captures the mouse.
    // Both modes retain the stock baseline before publishing the drag gesture.
    const auto beginVelocityDrag = [&]( const ReplayVelocityBodyView& body, int axis, bool angular )
    {
        outResult.enterInteractive = true;
        if ( scrubberOwner.SetLiveAdvanceHeld( true ) && !frame.replayToolOwnsWorld )
        {
            outResult.interaction.RequestWorldOwner( ReplayWorldOwnerRequest::Scrub );
        }

        outInspectionCameraAction = ReplayInspectionCameraAction::None;

        ReplayVelocityEditDragStart dragStart;
        dragStart.targetId = path.targetId;
        dragStart.linearVelocity = body.linearVelocity;
        dragStart.angularVelocity = body.angularVelocity;
        dragStart.screenDrag = screenDrag;

        armBaselineComparisonForDrag();
        BeginVelocityEditDrag( dragStart );
        outResult.interaction.BeginVelocityDrag( frame.mouseX, frame.mouseY, body.body, axis, angular );
        outResult.consumesMouse = true;
    };

    if ( !uiBlocksMouse && frame.leftPressed )
    {
        ReplayVelocityBodyView body;

        if ( frame.hasClientPosition && tryResolveVelocityBody( body ) && !body.fixed )
        {
            if ( hotAxis >= 0 )
            {
                beginVelocityDrag( body, hotAxis, VelocityEdit().angular );
                return true;
            }
        }

        // A scene press outside the handle abandons this complete experiment.
        // App restores the stock seed and returns the camera/workspace together.
        outResult.cancelExperiment = true;
        outResult.consumesMouse = true;
        return true;
    }

    outResult.consumesMouse = VelocityEdit().hotLinearAxis >= 0 || VelocityEdit().hotAngularAxis >= 0;
    return outResult.consumesMouse;
}


bool ReplayAuthoring::ApplyVelocityEditTargetPick( ReplayPresentation& presentationOwner,
                                                   ReplayScrubber& scrubberOwner,
                                                   const ReplayPathPickResult& pickResult,
                                                   double now,
                                                   ReplayVelocityInputResult& outResult,
                                                   ReplayInspectionCameraAction& outInspectionCameraAction )
{
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
        outResult.enterInteractive = true;
        const ReplayScrubberView scrubber = scrubberOwner.View();
        const bool shouldUseInspectionCamera = ReplayScrubNeedsInspectionCamera( scrubber.liveAdvanceHeld, presentationOwner.CameraView().focusKind );

        if ( scrubberOwner.SetLiveAdvanceHeld( true ) && shouldUseInspectionCamera )
        {
            outInspectionCameraAction = ReplayInspectionCameraAction::Enter;
        }

        outResult.interaction.RequestWorldOwner( ReplayWorldOwnerRequest::VelocityEdit );

        QueuePredictionRefresh( true );
        scrubberOwner.SetVisible( true, now, REPLAY_SCRUBBER_VISIBLE_SECONDS );
    }

    return true;
}


bool ReplayAuthoring::BuildVelocityOverlayCommand( Physics::PhysicsSceneObjectId targetId,
                                                   ModelRowHint targetModelRow,
                                                   PhysicsEngine& velocityPhysics,
                                                   bool editorModeEnabled,
                                                   const ReplayToolGestureView& gesture,
                                                   ReplayVelocityOverlayCommand& outCommand ) const
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Overlay" );
    outCommand = ReplayVelocityOverlayCommand {};

    if ( !m_velocityEdit.enabled || editorModeEnabled )
    {
        return false;
    }

    ReplayVelocityBodyView body;

    if ( !TryResolveReplayVelocityBodyView( targetId,
                                            targetModelRow,
                                            SkullbonezCore::Physics::PhysicsEngine::ReadBodies( velocityPhysics ),
                                            SkullbonezCore::Physics::PhysicsEngine::ReadColliders( velocityPhysics ),
                                            body ) ||
         body.fixed || !body.shape )
    {
        return false;
    }

    outCommand.origin = body.position;
    outCommand.orientation = body.orientation;
    outCommand.shape = *body.shape;
    outCommand.radius = body.radius;
    outCommand.linearVelocity = body.linearVelocity;
    outCommand.angularVelocity = body.angularVelocity;
    outCommand.hotLinearAxis = m_velocityEdit.hotLinearAxis;
    outCommand.hotAngularAxis = m_velocityEdit.hotAngularAxis;
    outCommand.activeAxis = gesture.kind == ReplayToolGestureKind::VelocityDrag ? gesture.axis : -1;
    outCommand.activeAngular = m_velocityEdit.angular;
    return true;
}
