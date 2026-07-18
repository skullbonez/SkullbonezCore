/*
File: SkullbonezSource/Runtime/AttachedCameraController.cpp
Purpose:
  Implements attach-camera target recovery and pose solving.

Summary:
  The controller owns Attach target selection, durable follow/orbit state, and
  synchronous camera pose mutation. It borrows model stores and cameras for one
  command at a time; composition code publishes returned selection facts to UI
  and input owners without reaching back into controller state.

Glossary:
  Replay body id: Stable physics identity used to recover a followed body when
    dense model rows are rebuilt.
  Ragdoll eyes: Attach submode that places the camera near a resolved head body
    and looks along that body's forward axis.
  Orbit wheel: Mouse-wheel zoom applied to attach orbit distance.
  Presentation pose: Allocation-free interpolated body endpoint used by follow
    cameras while target identity and selection remain physics-authoritative.

Invariants:
  - Duplicate replay ids invalidate the target instead of selecting an
    arbitrary body.
  - Orbit pitch and distance are clamped before producing a camera pose.
  - Invalid or degenerate pose math fails closed without changing the camera.
  - Presentation sampling never changes the durable attached target identity.

Related:
  - SkullbonezSource/Runtime/AttachedCameraController.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "AttachedCameraController.h"
#include "CameraCollection.h"
#include "RuntimePickService.h"
#include "Scene/SceneWorld.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Physics/PhysicsEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

using namespace SkullbonezCore::GameObjects;
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
constexpr int ATTACHED_CAMERA_WHEEL_DELTA = 120;
constexpr float ATTACHED_CAMERA_ORBIT_DEFAULT_PITCH = 0.30f;
constexpr float ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MIN = -1.35f;
constexpr float ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MAX = 1.35f;
constexpr float ATTACHED_CAMERA_ORBIT_MIN_DISTANCE_RADIUS = 1.25f;
constexpr float ATTACHED_CAMERA_ORBIT_MAX_DISTANCE_RADIUS = 40.0f;
constexpr float ATTACHED_CAMERA_ORBIT_WHEEL_FACTOR = 0.88f;

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


Vector3 WorldToTargetVector( const RotationMatrix& rotation, const Vector3& worldVector )
{
    return rotation.TransposeMultiply( worldVector );
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


float WrapAttachedCameraOrbitYaw( float yaw )
{
    while ( yaw > _PI )
    {
        yaw -= _2PI;
    }
    while ( yaw < -_PI )
    {
        yaw += _2PI;
    }
    return yaw;
}


float AttachedCameraOrbitMinDistance( float targetRadius )
{
    return (std::max)( 1.0f, targetRadius * ATTACHED_CAMERA_ORBIT_MIN_DISTANCE_RADIUS );
}


float AttachedCameraOrbitMaxDistance( float targetRadius )
{
    const float minDistance = AttachedCameraOrbitMinDistance( targetRadius );
    return (std::max)( minDistance + 1.0f, targetRadius * ATTACHED_CAMERA_ORBIT_MAX_DISTANCE_RADIUS );
}


float ClampAttachedCameraOrbitDistance( float targetRadius, float distance )
{
    if ( !std::isfinite( distance ) )
    {
        distance = targetRadius * 8.0f;
    }
    return std::clamp( distance,
                       AttachedCameraOrbitMinDistance( targetRadius ),
                       AttachedCameraOrbitMaxDistance( targetRadius ) );
}


float ClampAttachedCameraOrbitPitch( float pitch )
{
    if ( !std::isfinite( pitch ) )
    {
        return ATTACHED_CAMERA_ORBIT_DEFAULT_PITCH;
    }
    return std::clamp( pitch, ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MIN, ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MAX );
}


Vector3 AttachedCameraOrbitOffset( float yaw, float pitch, float distance )
{
    const float cosPitch = cosf( pitch );
    return Vector3( sinf( yaw ) * cosPitch * distance, sinf( pitch ) * distance, cosf( yaw ) * cosPitch * distance );
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
        sprintf_s( label,
                   sizeof( label ),
                   "Attach: %s %s%s",
                   submode,
                   m_state.target.name[0] ? m_state.target.name : "target",
                   m_state.activeFollow ? "" : " Pinned" );
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


bool AttachedCameraController::TickFollow( const Runtime::SceneWorld& collection,
                                           Environment::CameraCollection& cameras,
                                           float orbitYawDelta,
                                           float orbitPitchDelta,
                                           float presentationAlpha )
{
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
    if ( !BuildFollowPose( collection,
                           m_state,
                           target,
                           modelIndex,
                           currentPose,
                           orbitYawDelta,
                           orbitPitchDelta,
                           presentationAlpha,
                           command ) )
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


bool AttachedCameraController::TryGetPresentationListenerPosition( const Runtime::SceneWorld& collection,
                                                                   const Environment::CameraCollection& cameras,
                                                                   float presentationAlpha,
                                                                   Vector3& outPosition ) const
{
    if ( !m_state.activeFollow )
    {
        return false;
    }
    // Lifetime: pose solving updates orbit/entry bookkeeping, so audio uses a
    // frame-local state copy and cannot consume camera controller transitions.
    AttachedCameraState state = m_state;
    int modelIndex = -1;
    AttachedCameraPhysicsTarget target;
    if ( !TryResolvePhysicsTarget( collection, state.target, target, &modelIndex ) )
    {
        return false;
    }
    Quaternion presentedOrientation;
    if ( collection.TryGetPresentationPose( modelIndex, presentationAlpha, target.position, presentedOrientation ) )
    {
        target.rotation = BodyRotation( presentedOrientation );
    }
    const AttachedCameraPose currentPose{ cameras.GetRenderCameraTranslation(),
                                          cameras.GetRenderCameraView(),
                                          cameras.GetRenderCameraUp() };
    AttachedCameraPoseCommand command;
    if ( !BuildFollowPose( collection,
                           state,
                           target,
                           modelIndex,
                           currentPose,
                           0.0f,
                           0.0f,
                           presentationAlpha,
                           command ) )
    {
        return false;
    }
    outPosition = command.pose.eye;
    return true;
}


bool AttachedCameraController::CycleMode( const Runtime::SceneWorld& collection,
                                          Environment::CameraCollection& cameras )
{
    AttachedCameraPhysicsTarget target;
    bool shouldCaptureFixedOffset = false;
    if ( !CycleSubmode( collection, m_state, target, shouldCaptureFixedOffset ) )
    {
        return false;
    }
    if ( shouldCaptureFixedOffset )
    {
        AttachedCameraPose pose{ cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };
        CaptureFixedOffset( m_state, pose, target );
    }
    return true;
}


bool AttachedCameraController::TogglePin( const Runtime::SceneWorld& collection,
                                          Environment::CameraCollection& cameras )
{
    m_state.activeFollow = !m_state.activeFollow;
    if ( m_state.activeFollow )
    {
        AttachedCameraPhysicsTarget target;
        if ( TryResolvePhysicsTarget( collection, m_state.target, target ) )
        {
            AttachedCameraPose pose{ cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };
            CaptureFixedOffset( m_state, pose, target );
        }
        m_state.needsEntryTween = true;
    }
    return m_state.activeFollow;
}


bool AttachedCameraController::ApplyOrbitInput( const Runtime::SceneWorld& collection,
                                                Environment::CameraCollection& cameras,
                                                bool attachModeActive,
                                                int unhandledWheelDelta,
                                                bool uiBlocksCameraMouse )
{
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
        AttachedCameraPose pose{ cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };
        CaptureOrbit( m_state, pose, target );
    }
    return ApplyOrbitWheel( m_state, target, unhandledWheelDelta );
}


bool AttachedCameraController::SetTarget( const Runtime::SceneWorld& collection,
                                          Environment::CameraCollection& cameras,
                                          int modelIndex,
                                          AttachedCameraTargetSelection& outSelection )
{
    if ( !SelectTarget( collection, m_state, modelIndex, outSelection ) )
    {
        return false;
    }
    const AttachedCameraPose pose{ cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };
    CaptureFixedOffset( m_state, pose, outSelection.physics );
    return true;
}


AttachedCameraSeedResult AttachedCameraController::SeedTarget( const Runtime::SceneWorld& collection,
                                                               Environment::CameraCollection& cameras,
                                                               int seedModelIndex,
                                                               AttachedCameraTargetSelection& outSelection )
{
    outSelection = AttachedCameraTargetSelection{};
    AttachedCameraPhysicsTarget currentTarget;
    if ( TryResolvePhysicsTarget( collection, m_state.target, currentTarget ) )
    {
        const AttachedCameraPose pose{ cameras.GetCameraTranslation(), cameras.GetCameraView(), cameras.GetCameraUp() };
        CaptureFixedOffset( m_state, pose, currentTarget );
        m_state.activeFollow = true;
        return AttachedCameraSeedResult::ReusedTarget;
    }
    if ( seedModelIndex >= 0 )
    {
        return SetTarget( collection, cameras, seedModelIndex, outSelection ) ? AttachedCameraSeedResult::SelectedSeed
                                                                              : AttachedCameraSeedResult::Failed;
    }
    m_state.activeFollow = true;
    return AttachedCameraSeedResult::NoSeed;
}


bool AttachedCameraController::PickTarget( const Runtime::SceneWorld& collection,
                                           Environment::CameraCollection& cameras,
                                           bool hasWorldRay,
                                           const Vector3& rayOrigin,
                                           const Vector3& rayDirection,
                                           AttachedCameraTargetSelection& outSelection )
{
    outSelection = AttachedCameraTargetSelection{};
    RuntimePickResult pick;
    if ( hasWorldRay )
    {
        RuntimePickRequest request;
        request.purpose = RuntimePickPurpose::AttachCameraTarget;
        request.bodyStore = &collection.BodyStore();
        request.colliderStore = &collection.Colliders();
        request.rayOrigin = rayOrigin;
        request.rayDirection = rayDirection;
        if ( RuntimePickService::TryPickModel( request, pick ) )
        {
            return SetTarget( collection, cameras, pick.modelRow.value, outSelection );
        }
    }
    ClearTarget( m_state );
    return false;
}


void AttachedCameraController::Reset( AttachedCameraState& state )
{
    state = AttachedCameraState{};
}


void AttachedCameraController::ClearTarget( AttachedCameraState& state )
{
    state.target = AttachedCameraTarget{};
    state.hasFixedOffset = false;
    state.hasOrbit = false;
    state.hasLastLookDirection = false;
}


bool AttachedCameraController::TryAttachTargetHandlesFromModelIndex( const SceneWorld& collection,
                                                                     int modelIndex,
                                                                     AttachedCameraTarget& target )
{
    const PhysicsBodyStore& bodyStore = collection.BodyStore();
    const ColliderStore& colliderStore = collection.Colliders();
    const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex );
    const ColliderRecord* collider =
        body ? colliderStore.RecordForHandle( colliderStore.HandleForBodyHandle( body->handle ) ) : nullptr;
    if ( !body || !collider || collider->body != body->handle )
    {
        return false;
    }

    target.body = body->handle;
    target.collider = collider->handle;
    target.modelRow.value = modelIndex;
    target.replayBodyId = body->replayBodyId;
    return true;
}


bool AttachedCameraController::TryResolveTargetIdentity( const SceneWorld& collection,
                                                         AttachedCameraTarget& target,
                                                         int& outModelIndex )
{
    outModelIndex = -1;
    const PhysicsBodyStore& bodyStore = collection.BodyStore();
    const ColliderStore& colliderStore = collection.Colliders();

    if ( target.body.IsValid() )
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( target.body );
        const int modelIndex = bodyStore.ModelIndexForHandle( target.body );
        const ColliderRecord* collider =
            target.collider.IsValid() ? colliderStore.RecordForHandle( target.collider ) : nullptr;
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
            target.replayBodyId = body->replayBodyId;
            outModelIndex = modelIndex;
            return true;
        }
    }

    const int modelCount = bodyStore.Count();
    const int cachedIndex = target.modelRow.value;
    if ( cachedIndex >= 0 && cachedIndex < modelCount )
    {
        const bool hasReplayId = target.replayBodyId != 0;
        bool cachedIndexMatches = true;
        if ( hasReplayId )
        {
            const PhysicsBodyRecord* cachedBody = bodyStore.RecordForModelIndex( cachedIndex );
            cachedIndexMatches = cachedBody && cachedBody->replayBodyId == target.replayBodyId;
        }
        if ( cachedIndexMatches && TryAttachTargetHandlesFromModelIndex( collection, cachedIndex, target ) )
        {
            outModelIndex = cachedIndex;
            return true;
        }
    }

    if ( target.replayBodyId != 0 )
    {
        int match = -1;
        const auto bodyRecords = bodyStore.Records();
        // Invariant: duplicate replay ids are corruption, not an arbitrary
        // first match. Scan the dense body rows so stale camera targets fail
        // closed without touching authoring/presentation data.
        for ( int i = 0; i < static_cast<int>( bodyRecords.size() ); ++i )
        {
            if ( bodyRecords[static_cast<std::size_t>( i )].replayBodyId == target.replayBodyId )
            {
                if ( match >= 0 )
                {
                    target = AttachedCameraTarget{};
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

    target = AttachedCameraTarget{};
    return false;
}


bool AttachedCameraController::TryResolvePhysicsTarget( const SceneWorld& collection,
                                                        AttachedCameraTarget& target,
                                                        AttachedCameraPhysicsTarget& outTarget,
                                                        int* outModelIndex )
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


bool AttachedCameraController::TryResolveRagdollHead( const SceneWorld& collection,
                                                      int selectedModelIndex,
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


bool AttachedCameraController::SelectTarget( const SceneWorld& collection,
                                             AttachedCameraState& state,
                                             int modelIndex,
                                             AttachedCameraTargetSelection& outSelection )
{
    outSelection = AttachedCameraTargetSelection{};
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

    strncpy_s( state.target.name,
               sizeof( state.target.name ),
               PresentationNameForModelIndex( collection, modelIndex ),
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


bool AttachedCameraController::CycleSubmode( const SceneWorld& collection,
                                             AttachedCameraState& state,
                                             AttachedCameraPhysicsTarget& outTarget,
                                             bool& outShouldCaptureFixedOffset )
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


void AttachedCameraController::CaptureFixedOffset( AttachedCameraState& state,
                                                   const AttachedCameraPose& currentPose,
                                                   const AttachedCameraPhysicsTarget& target )
{
    state.localEyeOffset = WorldToTargetVector( target.rotation, currentPose.eye - target.position );
    state.localViewOffset = WorldToTargetVector( target.rotation, currentPose.view - target.position );
    state.localUp = NormalizedOr( WorldToTargetVector( target.rotation, currentPose.up ), Vector3( 0.0f, 1.0f, 0.0f ) );
    Vector3 look = currentPose.view - currentPose.eye;
    if ( TryNormalizeVector( look ) )
    {
        state.lastLookDirection = look;
        state.hasLastLookDirection = true;
    }
    state.hasFixedOffset = true;
    CaptureOrbit( state, currentPose, target );
}


void AttachedCameraController::CaptureOrbit( AttachedCameraState& state,
                                             const AttachedCameraPose& currentPose,
                                             const AttachedCameraPhysicsTarget& target )
{
    Vector3 offset = currentPose.eye - target.position;
    float distance = sqrtf( VectorMagSquared( offset ) );
    if ( !std::isfinite( distance ) || distance < AttachedCameraOrbitMinDistance( target.radius ) )
    {
        Vector3 look = currentPose.view - currentPose.eye;
        if ( !TryNormalizeVector( look ) )
        {
            look = Vector3( 0.0f, 0.0f, 1.0f );
        }
        distance = target.radius * 8.0f;
        offset = -look * distance;
    }

    const float pitchDistance = (std::max)( distance, 0.001f );
    const float normalizedY = std::clamp( offset.y / pitchDistance, -1.0f, 1.0f );
    state.orbitDistance = ClampAttachedCameraOrbitDistance( target.radius, distance );
    state.orbitPitchRadians = ClampAttachedCameraOrbitPitch( asinf( normalizedY ) );
    state.orbitYawRadians = WrapAttachedCameraOrbitYaw( atan2f( offset.x, offset.z ) );
    state.hasOrbit = true;
}


bool AttachedCameraController::ApplyOrbitWheel( AttachedCameraState& state,
                                                const AttachedCameraPhysicsTarget& target,
                                                int unhandledWheelDelta )
{
    const int wheelSteps = unhandledWheelDelta / ATTACHED_CAMERA_WHEEL_DELTA;
    if ( wheelSteps == 0 )
    {
        return false;
    }

    const float nextDistance =
        state.orbitDistance * powf( ATTACHED_CAMERA_ORBIT_WHEEL_FACTOR, static_cast<float>( wheelSteps ) );
    state.orbitDistance = ClampAttachedCameraOrbitDistance( target.radius, nextDistance );
    state.hasOrbit = true;
    return true;
}


bool AttachedCameraController::BuildFollowPose( const SceneWorld& collection,
                                                AttachedCameraState& state,
                                                const AttachedCameraPhysicsTarget& target,
                                                int modelIndex,
                                                const AttachedCameraPose& currentPose,
                                                float orbitYawDelta,
                                                float orbitPitchDelta,
                                                float presentationAlpha,
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
            if ( collection.TryGetPresentationPose( headIndex,
                                                    presentationAlpha,
                                                    headState.position,
                                                    presentedHeadOrientation ) )
            {
                headState.rotation = BodyRotation( presentedHeadOrientation );
            }

            const float radius = (std::max)( 0.5f, headState.radius );
            const Vector3 eye =
                headState.position +
                TargetToWorldVector( headState.rotation, Vector3( 0.0f, 0.20f * radius, 0.85f * radius ) );
            const Vector3 forward =
                NormalizedOr( TargetToWorldVector( headState.rotation, Vector3( 0.0f, 0.0f, 1.0f ) ),
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

    if ( !state.hasOrbit )
    {
        CaptureOrbit( state, currentPose, target );
    }

    if ( orbitYawDelta != 0.0f || orbitPitchDelta != 0.0f )
    {
        state.orbitYawRadians = WrapAttachedCameraOrbitYaw( state.orbitYawRadians + orbitYawDelta );
        state.orbitPitchRadians = ClampAttachedCameraOrbitPitch( state.orbitPitchRadians + orbitPitchDelta );
    }

    state.orbitDistance = ClampAttachedCameraOrbitDistance( target.radius, state.orbitDistance );

    const Vector3 targetPosition = target.position;
    const Vector3 eye =
        targetPosition +
        AttachedCameraOrbitOffset( state.orbitYawRadians, state.orbitPitchRadians, state.orbitDistance );
    Vector3 view = targetPosition;
    Vector3 up = Vector3( 0.0f, 1.0f, 0.0f );
    if ( state.submode == AttachedCameraSubmode::VelocityForward )
    {
        Vector3 direction = target.linearVelocity;
        if ( !TryNormalizeVector( direction ) )
        {
            direction = state.hasLastLookDirection ? state.lastLookDirection : currentPose.view - currentPose.eye;
            if ( !TryNormalizeVector( direction ) )
            {
                direction = NormalizedOr( view - eye, Vector3( 0.0f, 0.0f, 1.0f ) );
            }
        }
        view = targetPosition + direction * (std::max)( target.radius, state.orbitDistance * 0.25f );
        state.lastLookDirection = direction;
        state.hasLastLookDirection = true;
    }

    if ( !IsFiniteVector( eye ) || !IsFiniteVector( view ) || !IsFiniteVector( up ) ||
         VectorMagSquared( view - eye ) <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    outCommand.pose.eye = eye;
    outCommand.pose.view = view;
    outCommand.pose.up = up;
    outCommand.startEntryTween = state.needsEntryTween;
    state.needsEntryTween = false;
    return true;
}
} // namespace Runtime
} // namespace SkullbonezCore
