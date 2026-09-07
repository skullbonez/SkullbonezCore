/*
File: SkullbonezSource/Runtime/Scene/AttachedCameraController.cpp
Purpose:
  Implements attach-camera target recovery and pose solving.

Summary:
  The controller owns Attach target selection, durable follow/orbit state, and
  synchronous camera pose mutation. It borrows model stores and cameras for one
  command at a time; composition code publishes returned selection facts to UI
  and input owners without reaching back into controller state. Replay causal
  focus temporarily suspends ordinary Attach state inside this same owner and
  seeds fixed-relative orbit from the visible render pose.

Glossary:
  Ragdoll eyes: Attach submode that places the camera near a resolved head body
    and looks along that body's forward axis.
  Orbit wheel: Mouse-wheel zoom applied to attach orbit distance.
  Presentation pose: Allocation-free interpolated body endpoint used by follow
    cameras while target identity and selection remain physics-authoritative.

Invariants:
  - Duplicate scene object ids invalidate the target instead of selecting an
    arbitrary body.
  - Orbit pitch and distance are clamped before producing a camera pose.
  - Invalid or degenerate pose math fails closed without changing the camera.
  - Presentation sampling never changes the durable attached target identity.
  - Focused inspection follows the presented stable-id target without restarting its
    entry tween; exit restores the suspended ordinary Attach state exactly once.

Related:
  - SkullbonezSource/Runtime/Scene/AttachedCameraController.h
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "AttachedCameraController.h"
#include "../Camera/AttachedCameraController.InspectionPolicy.h"
#include "../Camera/CameraCollection.h"
#include "SceneWorld.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsApi.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
bool IsFiniteVector( const Vector3& v )
{
    return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z );
}


bool TryNormalizeVector( Vector3& v )
{
    // Hazard: attachment and replay camera targets can briefly collapse to a
    // zero-length vector after a reset or stale body recovery. Callers must use
    // a fallback instead of feeding NaNs into camera matrices.
    if ( !IsFiniteVector( v ) )
    {
        return false;
    }

    const float lengthSq = VectorMagSquared( v );

    if ( lengthSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    v *= 1.0f / sqrtf( lengthSq );
    return true;
}


Vector3 NormalizedOr( Vector3 v, const Vector3& fallback )
{
    if ( TryNormalizeVector( v ) )
    {
        return v;
    }

    Vector3 safeFallback = fallback;

    if ( TryNormalizeVector( safeFallback ) )
    {
        return safeFallback;
    }

    return Vector3( 0.0f, 1.0f, 0.0f );
}


RotationMatrix BodyRotation( Quaternion orientation )
{
    return orientation.GetOrientationMatrix();
}


Vector3 TargetToWorldVector( const RotationMatrix& rotation, const Vector3& localVector )
{
    return rotation * localVector;
}


float AttachedCameraTargetRadius( float bodyBoundingRadius, const ColliderRecord& collider )
{
    return (std::max)( (std::max)( collider.boundingRadius, bodyBoundingRadius ), 1.0f );
}


const char* PresentationNameForModelIndex( const SceneWorld& collection, int modelIndex )
{
    const auto presentationRecords = collection.RenderPresentationRecords();

    if ( modelIndex < 0 || modelIndex >= static_cast<int>( presentationRecords.size() ) )
    {
        return "";
    }

    return presentationRecords[static_cast<std::size_t>( modelIndex )].displayName;
}


bool EndsWith( const char* value, const char* suffix )
{
    if ( !value || !suffix )
    {
        return false;
    }

    const size_t valueLength = strlen( value );
    const size_t suffixLength = strlen( suffix );
    return valueLength >= suffixLength && strcmp( value + valueLength - suffixLength, suffix ) == 0;
}


} // namespace


AttachedCameraState& AttachedCameraController::State()
{
    return m_state;
}


const AttachedCameraState& AttachedCameraController::State() const
{
    return m_state;
}


const char* AttachedCameraController::ModeLabel() const
{
    static thread_local char label[96];
    const char* submode = "Fixed";

    if ( m_state.submode == AttachedCameraSubmode::VelocityForward )
    {
        submode = "Velocity";
    }
    else if ( m_state.submode == AttachedCameraSubmode::RagdollEyes )
    {
        submode = "Eyes";
    }

    if ( !m_state.target.modelRow.IsValid() )
    {
        sprintf_s( label, sizeof( label ), "Attach: pick target%s", m_state.activeFollow ? "" : " Pinned" );
    }
    else
    {
        sprintf_s( label, sizeof( label ), "Attach: %s %s%s", submode,
                   m_state.target.name[0] ? m_state.target.name : "target", m_state.activeFollow ? "" : " Pinned" );
    }

    return label;
}


void AttachedCameraController::CaptureReturnState( RunCameraMode previousMode, Environment::CameraCollection& cameras )
{
    if ( previousMode == RunCameraMode::Attach )
    {
        return;
    }

    m_state.returnMode = previousMode;
    m_state.hasReturnCameraPose = false;

    // Why: capture the render pose, not only the selected camera slot. Attach
    // may begin while another transition is still visible.
    m_state.returnCameraHash = cameras.GetSelectedCameraName();
    m_state.returnEye = cameras.GetRenderCameraTranslation();
    m_state.returnView = cameras.GetRenderCameraView();
    m_state.returnUp = cameras.GetRenderCameraUp();

    if ( VectorMagSquared( m_state.returnView - m_state.returnEye ) <= TOLERANCE * TOLERANCE )
    {
        m_state.returnEye = cameras.GetCameraTranslation();
        m_state.returnView = cameras.GetCameraView();
        m_state.returnUp = cameras.GetCameraUp();
    }

    m_state.hasReturnCameraPose = true;
}


void AttachedCameraController::RestoreReturnState( Environment::CameraCollection& cameras )
{
    if ( !m_state.hasReturnCameraPose )
    {
        return;
    }

    if ( cameras.HasCamera( m_state.returnCameraHash ) && !cameras.IsCameraSelected( m_state.returnCameraHash ) )
    {
        cameras.SelectCamera( m_state.returnCameraHash, false );
    }

    cameras.TweenPrimaryToPose( m_state.returnEye, m_state.returnView, m_state.returnUp );
    m_state.hasReturnCameraPose = false;
}


bool AttachedCameraController::ResolveTargetIdentity( const Runtime::SceneWorld& collection, int& outModelIndex )
{
    if ( TryResolveTargetIdentity( collection, m_state.target, outModelIndex ) )
    {
        return true;
    }

    ClearTarget( m_state );
    return false;
}


bool AttachedCameraController::TickFollow( Runtime::SceneWorld& collection, float orbitYawDelta, float orbitPitchDelta,
                                           float presentationAlpha )
{
    Environment::CameraCollection& cameras = collection.Cameras();

    if ( !m_state.activeFollow )
    {
        return false;
    }

    int modelIndex = -1;
    AttachedCameraPhysicsTarget target;

    if ( !TryResolvePhysicsTarget( collection, m_state.target, target, &modelIndex ) )
    {
        return false;
    }

    Quaternion presentedOrientation;

    if ( collection.TryGetPresentationPose( modelIndex, presentationAlpha, target.position, presentedOrientation ) )
    {
        target.rotation = BodyRotation( presentedOrientation );
    }

    AttachedCameraPose currentPose;
    currentPose.eye = cameras.GetCameraTranslation();
    currentPose.view = cameras.GetCameraView();
    currentPose.up = cameras.GetCameraUp();
    AttachedCameraPoseCommand command;

    if ( !BuildFollowPose( collection, m_state, target, modelIndex, currentPose, orbitYawDelta, orbitPitchDelta,
                           presentationAlpha, command ) )
    {
        return false;
    }

    // Why: only the first valid solve starts a tween. Later solves retarget the
    // live destination so a moving body never restarts the transition.
    if ( command.startEntryTween )
    {
        cameras.TweenPrimaryToPose( command.pose.eye, command.pose.view, command.pose.up );
    }
    else
    {
        cameras.SetPrimaryPose( command.pose.eye, command.pose.view, command.pose.up );
    }

    return true;
}


bool AttachedCameraController::CycleMode( Runtime::SceneWorld& collection )
{
    Environment::CameraCollection& cameras = collection.Cameras();
    AttachedCameraPhysicsTarget target;
    bool shouldCaptureFixedOffset = false;

    if ( !CycleSubmode( collection, m_state, target, shouldCaptureFixedOffset ) )
    {
        return false;
    }

    if ( shouldCaptureFixedOffset )
    {
        AttachedCameraPose pose { cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };

        CaptureFixedOffset( m_state, pose, target );
    }

    return true;
}


bool AttachedCameraController::TogglePin( Runtime::SceneWorld& collection )
{
    Environment::CameraCollection& cameras = collection.Cameras();
    m_state.activeFollow = !m_state.activeFollow;

    if ( m_state.activeFollow )
    {
        AttachedCameraPhysicsTarget target;

        if ( TryResolvePhysicsTarget( collection, m_state.target, target ) )
        {
            AttachedCameraPose pose { cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };

            CaptureFixedOffset( m_state, pose, target );
        }

        m_state.needsEntryTween = true;
    }

    return m_state.activeFollow;
}


bool AttachedCameraController::ApplyOrbitInput( Runtime::SceneWorld& collection, bool attachModeActive,
                                                int unhandledWheelDelta, bool uiBlocksCameraMouse )
{
    Environment::CameraCollection& cameras = collection.Cameras();

    if ( !attachModeActive || !m_state.activeFollow || m_state.submode == AttachedCameraSubmode::RagdollEyes ||
         uiBlocksCameraMouse )
    {
        return false;
    }

    AttachedCameraPhysicsTarget target;

    if ( !TryResolvePhysicsTarget( collection, m_state.target, target ) )
    {
        return false;
    }

    if ( !m_state.hasOrbit )
    {
        AttachedCameraPose pose { cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };

        CaptureOrbit( m_state, pose, target );
    }

    return ApplyOrbitWheel( m_state, target, unhandledWheelDelta );
}


bool AttachedCameraController::BeginFocusedInspection( Runtime::SceneWorld& collection,
                                                       const AttachedCameraFocusRequest& request,
                                                       AttachedCameraPose& outPreparedPose )
{
    if ( !m_hasSuspendedState )
    {
        // Lifetime: replay borrows the one camera-follow owner. Its ordinary
        // Attach target is suspended here and restored when inspection exits.
        m_suspendedState = m_state;
        m_hasSuspendedState = true;
    }

    AttachedCameraTarget target;
    target.sceneObjectId = request.sceneObjectId;
    target.modelRow = request.modelRow;
    int modelIndex = -1;

    if ( !TryResolveTargetIdentity( collection, target, modelIndex ) )
    {
        return false;
    }

    AttachedCameraPhysicsTarget physicsTarget;

    if ( !TryResolvePhysicsTarget( collection, target, physicsTarget, &modelIndex ) )
    {
        return false;
    }

    Environment::CameraCollection& cameras = collection.Cameras();
    const AttachedCameraPose visiblePose { cameras.GetRenderCameraTranslation(), cameras.GetRenderCameraView(),
                                           cameras.GetRenderCameraUp() };
    // Why: a causal row already resolved the position at the destination replay
    // frame. Preparing the dedicated camera against the live pre-transport body
    // made its endpoint drift while the main-to-causal blend was in progress.
    physicsTarget.position = request.point;
    physicsTarget.radius = (std::max)( request.radius, 1.0f );
    m_state.target = target;
    m_state.submode = AttachedCameraSubmode::FixedRelative;
    m_state.activeFollow = true;
    m_state.hasInspectionPivot = true;
    m_state.inspectionPivot = physicsTarget.position;
    m_state.inspectionRadius = physicsTarget.radius;
    SeedAttachedCameraFixedRelative( m_state, visiblePose, physicsTarget );
    m_state.needsEntryTween = false;

    AttachedCameraPoseCommand command;

    if ( !BuildAttachedCameraOrbitPose( m_state, physicsTarget, visiblePose, 0.0f, 0.0f, command ) )
    {
        return false;
    }

    outPreparedPose = command.pose;
    return true;
}


void AttachedCameraController::SetFocusedInspectionPosition( Physics::PhysicsSceneObjectId id,
                                                             const Math::Vector::Vector3& position )
{
    if ( m_hasSuspendedState && m_state.hasInspectionPivot && m_state.target.sceneObjectId == id )
    {
        m_state.inspectionPivot = position;
    }
}

bool AttachedCameraController::TickFocusedInspection( Runtime::SceneWorld& collection, float orbitYawDelta,
                                                      float orbitPitchDelta, int wheelDelta )
{
    if ( !m_hasSuspendedState )
    {
        return false;
    }

    AttachedCameraPhysicsTarget target;

    if ( !TryResolvePhysicsTarget( collection, m_state.target, target ) )
    {
        return false;
    }

    if ( !m_state.hasInspectionPivot )
    {
        return false;
    }

    // Why: App supplies the selected prediction pose while the live body stays
    // at the prediction source. Preserve the user's orbit as that pivot moves.
    target.position = m_state.inspectionPivot;
    target.radius = m_state.inspectionRadius;

    if ( wheelDelta != 0 )
    {
        (void)ApplyOrbitWheel( m_state, target, wheelDelta );
    }

    Environment::CameraCollection& cameras = collection.Cameras();
    const AttachedCameraPose currentPose { cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };
    AttachedCameraPoseCommand command;

    if ( !BuildAttachedCameraOrbitPose( m_state, target, currentPose, orbitYawDelta, orbitPitchDelta, command ) )
    {
        return false;
    }

    // Focused inspection publishes an already-selected causal slot directly.
    // Entry/return interpolation belongs to Replay's transition owner.
    cameras.SetPrimaryPose( command.pose.eye, command.pose.view, command.pose.up );
    return true;
}


void AttachedCameraController::EndFocusedInspection()
{
    if ( !m_hasSuspendedState )
    {
        return;
    }

    m_state = m_suspendedState;
    m_suspendedState = AttachedCameraState {};
    m_hasSuspendedState = false;
}


bool AttachedCameraController::SetTarget( Runtime::SceneWorld& collection, int modelIndex,
                                          AttachedCameraTargetSelection& outSelection )
{
    Environment::CameraCollection& cameras = collection.Cameras();

    if ( !SelectTarget( collection, m_state, modelIndex, outSelection ) )
    {
        return false;
    }

    const AttachedCameraPose pose { cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };

    CaptureFixedOffset( m_state, pose, outSelection.physics );
    return true;
}


AttachedCameraSeedResult AttachedCameraController::SeedTarget( Runtime::SceneWorld& collection, int seedModelIndex,
                                                               AttachedCameraTargetSelection& outSelection )
{
    Environment::CameraCollection& cameras = collection.Cameras();
    outSelection = AttachedCameraTargetSelection {};

    AttachedCameraPhysicsTarget currentTarget;

    if ( TryResolvePhysicsTarget( collection, m_state.target, currentTarget ) )
    {
        const AttachedCameraPose pose { cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };

        CaptureFixedOffset( m_state, pose, currentTarget );
        m_state.activeFollow = true;
        return AttachedCameraSeedResult::ReusedTarget;
    }

    if ( seedModelIndex >= 0 )
    {
        return SetTarget( collection, seedModelIndex, outSelection ) ? AttachedCameraSeedResult::SelectedSeed
                                                                     : AttachedCameraSeedResult::Failed;
    }

    m_state.activeFollow = true;
    return AttachedCameraSeedResult::NoSeed;
}


void AttachedCameraController::Reset( AttachedCameraState& state )
{
    state = AttachedCameraState {};
}


void AttachedCameraController::ClearTarget( AttachedCameraState& state )
{
    state.target = AttachedCameraTarget {};
    state.hasFixedOffset = false;
    state.hasOrbit = false;
    state.hasLastLookDirection = false;
    state.hasInspectionPivot = false;
}


bool AttachedCameraController::TryAttachTargetHandlesFromModelIndex( const SceneWorld& collection, int modelIndex,
                                                                     AttachedCameraTarget& target )
{
    const PhysicsBodyStore& bodyStore = collection.BodyStore();
    const ColliderStore& colliderStore = collection.Colliders();
    const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex );
    const ColliderRecord* collider = body
                                         ? colliderStore.RecordForHandle( colliderStore.HandleForBodyHandle( body->handle ) )
                                         : nullptr;

    if ( !body || !collider || collider->body != body->handle )
    {
        return false;
    }

    target.body = body->handle;
    target.collider = collider->handle;
    target.modelRow.value = modelIndex;
    target.sceneObjectId = body->sceneObjectId;
    return true;
}


bool AttachedCameraController::TryResolveTargetIdentity( const SceneWorld& collection, AttachedCameraTarget& target,
                                                         int& outModelIndex )
{
    outModelIndex = -1;
    const PhysicsBodyStore& bodyStore = collection.BodyStore();
    const ColliderStore& colliderStore = collection.Colliders();

    if ( target.body.IsValid() )
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( target.body );
        const int modelIndex = bodyStore.ModelIndexForHandle( target.body );
        const ColliderRecord* collider = target.collider.IsValid() ? colliderStore.RecordForHandle( target.collider )
                                                                   : nullptr;

        if ( body && ( !collider || collider->body != body->handle ) )
        {
            const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( body->handle );
            collider = colliderStore.RecordForHandle( colliderHandle );

            if ( collider )
            {
                target.collider = collider->handle;
            }
        }

        if ( body && collider && collider->body == body->handle )
        {
            // Concept: the attach camera follows physics identity, not vector
            // order. Model index is kept as a presentation hint after the live
            // handle proves which dense row currently owns the body.
            target.body = body->handle;
            target.modelRow.value = modelIndex;
            target.sceneObjectId = body->sceneObjectId;
            outModelIndex = modelIndex;
            return true;
        }
    }

    const int modelCount = bodyStore.Count();
    const int cachedIndex = target.modelRow.value;

    if ( cachedIndex >= 0 && cachedIndex < modelCount )
    {
        const bool hasSceneObjectId = target.sceneObjectId.IsValid();
        bool cachedIndexMatches = true;

        if ( hasSceneObjectId )
        {
            const PhysicsBodyRecord* cachedBody = bodyStore.RecordForModelIndex( cachedIndex );
            cachedIndexMatches = cachedBody && cachedBody->sceneObjectId == target.sceneObjectId;
        }

        if ( cachedIndexMatches && TryAttachTargetHandlesFromModelIndex( collection, cachedIndex, target ) )
        {
            outModelIndex = cachedIndex;
            return true;
        }
    }

    if ( target.sceneObjectId.IsValid() )
    {
        int match = -1;
        const auto bodyRecords = bodyStore.Records();

        // Invariant: duplicate scene object ids are corruption, not an arbitrary
        // first match. Scan the dense body rows so stale camera targets fail
        // closed without touching authoring/presentation data.
        for ( int i = 0; i < static_cast<int>( bodyRecords.size() ); ++i )
        {
            if ( bodyRecords[static_cast<std::size_t>( i )].sceneObjectId == target.sceneObjectId )
            {
                if ( match >= 0 )
                {
                    target = AttachedCameraTarget {};

                    return false;
                }

                match = i;
            }
        }

        if ( match >= 0 && TryAttachTargetHandlesFromModelIndex( collection, match, target ) )
        {
            outModelIndex = match;
            return true;
        }
    }

    target = AttachedCameraTarget {};
    return false;
}


bool AttachedCameraController::TryResolvePhysicsTarget( const SceneWorld& collection, AttachedCameraTarget& target,
                                                        AttachedCameraPhysicsTarget& outTarget, int* outModelIndex )
{
    int modelIndex = -1;

    if ( !TryResolveTargetIdentity( collection, target, modelIndex ) )
    {
        return false;
    }

    if ( outModelIndex )
    {
        *outModelIndex = modelIndex;
    }

    const PhysicsBodyStore& bodyStore = collection.BodyStore();
    const ColliderStore& colliderStore = collection.Colliders();
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( target.body );
    const ColliderRecord* collider = colliderStore.RecordForHandle( target.collider );

    if ( !body || !collider || collider->body != body->handle )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const auto hotFields = bodyStore.HotFields();
    outTarget.position = PhysicsBodyPosition( hotFields, bodyIndex );
    outTarget.linearVelocity = PhysicsBodyLinearVelocity( hotFields, bodyIndex );
    outTarget.rotation = BodyRotation( PhysicsBodyOrientation( hotFields, bodyIndex ) );
    outTarget.radius = AttachedCameraTargetRadius( hotFields.boundingRadius[bodyIndex], *collider );
    return true;
}


bool AttachedCameraController::TryResolveRagdollHead( const SceneWorld& collection, int selectedModelIndex,
                                                      int& outHeadModelIndex )
{
    outHeadModelIndex = -1;
    const int modelCount = collection.BodyStore().Count();

    if ( selectedModelIndex < 0 || selectedModelIndex >= modelCount )
    {
        return false;
    }

    const SceneEntityStore& entities = collection.Entities();

    if ( !entities.IsSimpleRagdollPart( selectedModelIndex ) )
    {
        return false;
    }

    if ( entities.TryFindSimpleRagdollPart( selectedModelIndex, 1, outHeadModelIndex ) )
    {
        return true;
    }

    // Why: the suffix fallback still serves old ragdoll display names, but its
    // membership boundary is the stable group root rather than a cached row.
    const PhysicsSceneObjectId rootObjectId = entities.GroupRootObjectIdAt( selectedModelIndex );

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( entities.IsSimpleRagdollPart( i ) && entities.GroupRootObjectIdAt( i ).value == rootObjectId.value &&
             EndsWith( PresentationNameForModelIndex( collection, i ), "_head" ) )
        {
            outHeadModelIndex = i;
            return true;
        }
    }

    return false;
}


bool AttachedCameraController::SelectTarget( const SceneWorld& collection, AttachedCameraState& state, int modelIndex,
                                             AttachedCameraTargetSelection& outSelection )
{
    outSelection = AttachedCameraTargetSelection {};
    const int modelCount = collection.BodyStore().Count();

    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        ClearTarget( state );
        return false;
    }

    AttachedCameraPhysicsTarget targetState;

    if ( !TryAttachTargetHandlesFromModelIndex( collection, modelIndex, state.target ) ||
         !TryResolvePhysicsTarget( collection, state.target, targetState ) )
    {
        ClearTarget( state );
        return false;
    }

    strncpy_s( state.target.name, sizeof( state.target.name ), PresentationNameForModelIndex( collection, modelIndex ),
               _TRUNCATE );

    state.activeFollow = true;
    state.needsEntryTween = true;

    if ( state.submode == AttachedCameraSubmode::RagdollEyes )
    {
        int headIndex = -1;

        if ( !TryResolveRagdollHead( collection, modelIndex, headIndex ) )
        {
            state.submode = AttachedCameraSubmode::FixedRelative;
        }
    }

    outSelection.physics = targetState;
    outSelection.body = state.target.body;
    outSelection.collider = state.target.collider;
    outSelection.modelRow.value = modelIndex;
    return true;
}


bool AttachedCameraController::CycleSubmode( const SceneWorld& collection, AttachedCameraState& state,
                                             AttachedCameraPhysicsTarget& outTarget, bool& outShouldCaptureFixedOffset )
{
    outShouldCaptureFixedOffset = false;
    int modelIndex = -1;

    if ( !TryResolvePhysicsTarget( collection, state.target, outTarget, &modelIndex ) )
    {
        return false;
    }

    int headIndex = -1;
    const bool hasEyes = TryResolveRagdollHead( collection, modelIndex, headIndex );
    AttachedCameraSubmode next = AttachedCameraSubmode::FixedRelative;

    if ( state.submode == AttachedCameraSubmode::FixedRelative )
    {
        next = AttachedCameraSubmode::VelocityForward;
    }
    else if ( state.submode == AttachedCameraSubmode::VelocityForward && hasEyes )
    {
        next = AttachedCameraSubmode::RagdollEyes;
    }

    state.submode = next;
    state.needsEntryTween = true;
    outShouldCaptureFixedOffset = next != AttachedCameraSubmode::RagdollEyes || !state.hasFixedOffset;
    return true;
}


void AttachedCameraController::CaptureFixedOffset( AttachedCameraState& state, const AttachedCameraPose& currentPose,
                                                   const AttachedCameraPhysicsTarget& target )
{
    SeedAttachedCameraFixedRelative( state, currentPose, target );
}


void AttachedCameraController::CaptureOrbit( AttachedCameraState& state, const AttachedCameraPose& currentPose,
                                             const AttachedCameraPhysicsTarget& target )
{
    CaptureAttachedCameraOrbit( state, currentPose, target );
}


bool AttachedCameraController::ApplyOrbitWheel( AttachedCameraState& state, const AttachedCameraPhysicsTarget& target,
                                                int unhandledWheelDelta )
{
    return ApplyAttachedCameraOrbitWheel( state, target, unhandledWheelDelta );
}


bool AttachedCameraController::BuildFollowPose( const SceneWorld& collection, AttachedCameraState& state,
                                                const AttachedCameraPhysicsTarget& target, int modelIndex,
                                                const AttachedCameraPose& currentPose, float orbitYawDelta,
                                                float orbitPitchDelta, float presentationAlpha,
                                                AttachedCameraPoseCommand& outCommand )
{
    if ( state.submode == AttachedCameraSubmode::RagdollEyes )
    {
        int headIndex = -1;

        if ( TryResolveRagdollHead( collection, modelIndex, headIndex ) )
        {
            AttachedCameraPhysicsTarget headState;
            AttachedCameraTarget headTarget;

            if ( !TryAttachTargetHandlesFromModelIndex( collection, headIndex, headTarget ) ||
                 !TryResolvePhysicsTarget( collection, headTarget, headState ) )
            {
                return false;
            }

            Quaternion presentedHeadOrientation;

            if ( collection.TryGetPresentationPose( headIndex, presentationAlpha, headState.position,
                                                    presentedHeadOrientation ) )
            {
                headState.rotation = BodyRotation( presentedHeadOrientation );
            }

            const float radius = (std::max)( 0.5f, headState.radius );
            const Vector3 eye = headState.position +
                                TargetToWorldVector( headState.rotation, Vector3( 0.0f, 0.20f * radius, 0.85f * radius ) );

            const Vector3 forward = NormalizedOr( TargetToWorldVector( headState.rotation, Vector3( 0.0f, 0.0f, 1.0f ) ),
                                                  Vector3( 0.0f, 0.0f, 1.0f ) );

            const Vector3 up = NormalizedOr( TargetToWorldVector( headState.rotation, Vector3( 0.0f, 1.0f, 0.0f ) ),
                                             Vector3( 0.0f, 1.0f, 0.0f ) );

            outCommand.pose.eye = eye;
            outCommand.pose.view = eye + forward;
            outCommand.pose.up = up;
            outCommand.startEntryTween = state.needsEntryTween;
            state.needsEntryTween = false;
            state.lastLookDirection = forward;
            state.hasLastLookDirection = true;
            return true;
        }

        state.submode = AttachedCameraSubmode::FixedRelative;
    }

    return BuildAttachedCameraOrbitPose( state, target, currentPose, orbitYawDelta, orbitPitchDelta, outCommand );
}
} // namespace Runtime
} // namespace SkullbonezCore
