/*
File: SkullbonezSource/Runtime/Scene/SceneController.Objects.cpp
Purpose:
  Coordinates transient presentation rows with physics/collider/render stores.

Summary:
  SceneController directly owns scene metadata, physics, and render presentation
  rows. Creation preflights every concrete store before its first mutation;
  cold repair and deletion preserve their shared dense order.

Glossary:
  Physics material: Per-object friction and drag coefficients owned by
    PhysicsScene and copied into authored descriptor rows at cold boundaries.
  Body simulation limit: Scalar cap owned by PhysicsScene before authored
    descriptors create PhysicsBodyStore rows.
  Contact policy: Terrain and contact thresholds owned by PhysicsScene so
    existing and newly added models receive the same physics policy.
  Body descriptor: PhysicsScene-owned authoring value that can rebuild a live
    PhysicsBodyStore row without reading legacy object record physics fields.
  Render instance store: Renderer-facing snapshot built from physics-owned pose
    and render-owned presentation rows before frame passes.
  Collider descriptor: Value packet containing shape/material facts that
    PhysicsScene turns into a live ColliderStore row.
  Topology drift: A body/collider/model count mismatch that means stores must
    import explicit construction descriptors before stepping.
  Scene-object group: Cold metadata that maps multi-part authored objects, such
    as ragdolls or releasable trees, to a stable root scene object id.
  Fixed-tree release: Authored scene rule that lets tree parts become dynamic
    when a related fixed part is hit strongly enough.
  Replay body id: PhysicsBodyStore-owned identity saved in replay samples so
    restore paths can reject stale model slots.
  Shadow caster stream: Opaque render bin resolved while scene material and
    collider facts are both available at the instance-build boundary.

Invariants:
  - SceneEntityStore order remains the scene alignment key for physics stores,
    render batches, and scene snapshots. Replay ids live in PhysicsBodyStore
    rows after append.
  - SceneEntityStore owns behavior groups with stable root ids. Dense root rows
    are derived only when physics or a model-index compatibility API requires one.
  - Render prep imports store-backed snapshots once before frame passes; render
    code must not rebuild model-derived pose streams.
  - Owner-side release paths repair topology once before resolving body handles
    from PhysicsBodyStore.
  - RenderInstanceStore owns transient contact feedback and imports durable
    material/name values from SceneEntityStore before each render snapshot.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneController.h"
#include "../../Core/Config.h"

#include "../../Core/FatalError.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Core/SkullScope.h"
#include "../../Runtime/Debug/CollisionVisualizer.h"
#include "../../Runtime/Debug/PhysicsDebugVisualizer.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <utility>
#include <variant>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::LoadPhysicsBodyHotState;
using SkullbonezCore::Physics::MakeModelRowHint;
using SkullbonezCore::Physics::MakePhysicsAuthoredBodyCountFromNonNegativeInt;
using SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt;
using SkullbonezCore::Physics::MakePhysicsColliderCountFromNonNegativeInt;
using SkullbonezCore::Physics::ModelRowHint;
using SkullbonezCore::Physics::PhysicsAuthoredBodyCount;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRefreshView;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration;
using SkullbonezCore::Physics::PhysicsBodyAngularVelocity;
using SkullbonezCore::Physics::PhysicsBodyCount;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyLinearVelocity;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderCount;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Rendering::RenderMaterial;
using SkullbonezCore::Rendering::RenderMaterialKind;
using SkullbonezCore::Rendering::ShadowCasterStream;

namespace
{
constexpr const char* SCENE_ENTITY_CREATION_OWNER = "Scene/EntityCreation";
constexpr int PINE_VISUAL_MATERIAL_MODE = 13;

ShadowCasterStream ResolveRegisteredShadowCasterStream( const ColliderRecord& collider, const RenderMaterial& material )
{
    switch ( collider.shapeKind )
    {
    case ColliderShapeKind::Sphere:
        return ShadowCasterStream::Sphere;
    case ColliderShapeKind::ConvexHull:
        return ShadowCasterStream::ConvexHull;
    case ColliderShapeKind::Box:
        // Why: the data-driven-shadow-caster-streams plan confines legacy pine
        // content knowledge to this scene-owner instance-build boundary. Frame
        // submission consumes only the resulting opaque stream id.
        if ( material.kind == RenderMaterialKind::Pine ||
             ( material.textureMode > 1.25f &&
               static_cast<int>( material.textureMode + 0.5f ) == PINE_VISUAL_MATERIAL_MODE ) )
        {
            return ShadowCasterStream::Pine;
        }
        return ShadowCasterStream::Box;
    }
    return ShadowCasterStream::None;
}

template <typename T> uint64_t VectorCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}


} // namespace


void SceneController::ReserveForActiveGameModelCapacity()
{
    // Invariant: model-order storage must be fully sized before steady frames.
    // Config can raise the active model capacity after construction, so each
    // setup/config boundary repeats the reserve instead of letting render-time
    // append paths discover the new capacity by reallocating.
    const std::size_t capacity = static_cast<std::size_t>( m_activeGameModelCapacity );
    m_physics.ReserveAuthoredBodyCapacity( capacity );
    m_renderInstanceStore.ReservePresentationCapacity( capacity );
}


const SceneBehaviorGroup& SceneController::BehaviorGroupAt( int modelIndex ) const
{
    return SceneEntities().At( modelIndex ).behaviorGroup;
}


int SceneController::ResolveBehaviorGroupRootModelIndex( const SceneBehaviorGroup& group ) const
{
    if ( group.kind == SceneBehaviorGroupKind::None )
    {
        return -1;
    }
    // Why: physics descriptors and compatibility APIs still consume dense rows,
    // but the scene owner stores only stable identity. Resolve at this cold
    // boundary instead of caching a movable root row in group metadata.
    const int rootIndex = SceneEntities().FindBySceneObjectId( group.rootObjectId );
    if ( rootIndex < 0 )
    {
        SB_FATAL( "GameObjects/SceneController",
                  "Behavior group root is missing. root_id=%u kind=%u",
                  group.rootObjectId.value,
                  static_cast<unsigned int>( group.kind ) );
    }
    return rootIndex;
}


std::vector<ModelRowHint> SceneController::BuildFixedTreeReleaseRootsForReload() const
{
    std::vector<ModelRowHint> fixedTreeReleaseRoots;
    const int sceneEntityCount = SceneEntityCount();
    fixedTreeReleaseRoots.reserve( static_cast<std::size_t>( sceneEntityCount ) );
    for ( int i = 0; i < sceneEntityCount; ++i )
    {
        fixedTreeReleaseRoots.push_back( MakeModelRowHint( FixedTreeReleaseRootForModelIndex( i ) ) );
    }
    return fixedTreeReleaseRoots;
}


std::vector<const char*> SceneController::BuildDiagnosticNamesForReload() const
{
    std::vector<const char*> diagnosticNames;
    diagnosticNames.reserve( static_cast<std::size_t>( SceneEntities().Count() ) );
    for ( int index = 0; index < SceneEntities().Count(); ++index )
    {
        diagnosticNames.push_back( SceneEntities().At( index ).displayName );
    }
    return diagnosticNames;
}


bool SceneController::RefreshPhysicsBodyStoreFromAuthoredDescriptors()
{
    const std::vector<uint32_t> replayBodyIds =
        Physics::PhysicsEngine::ReadBodies( m_physics ).BuildReplayBodyIdsForReload( SceneEntityCount() );
    const std::vector<ModelRowHint> fixedTreeReleaseRoots = BuildFixedTreeReleaseRootsForReload();
    const std::vector<const char*> diagnosticNames = BuildDiagnosticNamesForReload();
    PhysicsAuthoredBodyRefreshView refreshView;
    refreshView.replayBodyIds = replayBodyIds.empty() ? nullptr : replayBodyIds.data();
    refreshView.fixedTreeReleaseRoots = fixedTreeReleaseRoots.empty() ? nullptr : fixedTreeReleaseRoots.data();
    refreshView.diagnosticNames = diagnosticNames.empty() ? nullptr : diagnosticNames.data();
    refreshView.bodyCount = MakePhysicsAuthoredBodyCountFromNonNegativeInt( SceneEntityCount() );
    if ( !m_physics.RefreshBodyStoreFromAuthoredDescriptors( refreshView ) )
    {
        return false;
    }
    const PhysicsBodyStore& bodyStore = BodyStore();
    for ( int index = 0; index < bodyStore.Count(); ++index )
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( index );
        if ( !body )
        {
            SB_FATAL( "GameObjects/SceneController", "Refreshed body row is missing." );
        }
        SceneEntities().UpdateBodyHandleAt( index, body->handle, body->sceneObjectId );
    }
    return true;
}


int SceneController::FixedTreeReleaseRootForModelIndex( int modelIndex ) const
{
    const SceneBehaviorGroup& group = BehaviorGroupAt( modelIndex );
    return group.kind == SceneBehaviorGroupKind::ReleasableTree ? ResolveBehaviorGroupRootModelIndex( group ) : -1;
}


SceneEntityStore& SceneController::SceneEntities()
{
    return m_entities;
}


const SceneEntityStore& SceneController::SceneEntities() const
{
    return m_entities;
}


void SceneController::ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    m_activeGameModelCapacity = SkullbonezCore::Core::ActiveGameModelCapacity( config );
    SceneEntities().ConfigureCapacity( m_activeGameModelCapacity );
    ReserveForActiveGameModelCapacity();
    m_physics.ApplyRuntimeConfig( config );
}


void SceneController::AssertSceneCreationTopology( int expectedCount ) const
{
    const int descriptorCount = static_cast<int>( m_physics.AuthoredBodyDescriptorCount().value );
    const int bodyCount = BodyStore().Count();
    const int colliderCount = Colliders().Count();
    const int renderPresentationCount = m_renderInstanceStore.PresentationCount();
    const int renderCount = m_renderInstanceStore.Count();
    const bool reservationsReady =
        m_renderInstanceStore.PresentationCapacity() >= static_cast<std::size_t>( m_activeGameModelCapacity );
    if ( expectedCount < 0 || SceneEntities().Count() != expectedCount || descriptorCount != expectedCount ||
         bodyCount != expectedCount || colliderCount != expectedCount || renderPresentationCount != expectedCount ||
         renderCount != expectedCount || !reservationsReady )
    {
        SB_FATAL( "GameObjects/SceneController",
                  "Scene creation topology diverged. expected=%d entities=%d descriptors=%d "
                  "bodies=%d colliders=%d render_presentation=%d render=%d reservations_ready=%d",
                  expectedCount,
                  SceneEntities().Count(),
                  descriptorCount,
                  bodyCount,
                  colliderCount,
                  renderPresentationCount,
                  renderCount,
                  reservationsReady ? 1 : 0 );
    }
}


SceneEntityCreateResult SceneController::TryCreateSceneEntity( SceneEntityCreateDesc entity,
                                                               PhysicsBodyCreateDesc bodyDesc,
                                                               PhysicsColliderCreateDesc colliderDesc )
{
    const int activeCapacity = m_activeGameModelCapacity;
    const int modelIndex = SceneEntityCount();
    AssertSceneCreationTopology( modelIndex );
    if ( SceneEntityCount() >= activeCapacity )
    {
        return { SkullbonezCore::Core::SbResult::Failure(
                     SCENE_ENTITY_CREATION_OWNER,
                     "Exceeded active game model capacity; raise --model-capacity or game_model_capacity." ),
                 PhysicsBodyHandle{} };
    }
    const SkullbonezCore::Core::SbResult entityResult = SceneEntities().PreflightAppend( entity );
    if ( !entityResult.ok )
    {
        return { entityResult, PhysicsBodyHandle{} };
    }
    if ( bodyDesc.sceneObjectId.IsValid() && bodyDesc.sceneObjectId.value != entity.sceneObjectId.value )
    {
        return { SkullbonezCore::Core::SbResult::Failure( SCENE_ENTITY_CREATION_OWNER,
                                                          "Body scene object id %u does not match entity id %u.",
                                                          bodyDesc.sceneObjectId.value,
                                                          entity.sceneObjectId.value ),
                 PhysicsBodyHandle{} };
    }
    if ( !m_physics.CanRegisterAuthoredBody( MakePhysicsAuthoredBodyCountFromNonNegativeInt( modelIndex ) ) )
    {
        SB_FATAL( "GameObjects/SceneController",
                  "Physics creation storage is not preflight-ready. expected=%d descriptors=%u bodies=%d",
                  modelIndex,
                  m_physics.AuthoredBodyDescriptorCount().value,
                  BodyStore().Count() );
    }
    if ( !m_renderInstanceStore.CanAppendCreationRow( modelIndex ) )
    {
        SB_FATAL( "GameObjects/SceneController",
                  "Render creation storage is not preflight-ready. expected=%d presentation=%d render=%d",
                  modelIndex,
                  m_renderInstanceStore.PresentationCount(),
                  m_renderInstanceStore.Count() );
    }

    Rendering::RenderInstancePresentationRecord renderPresentation;
    renderPresentation.material = entity.renderMaterial;
    strncpy_s( renderPresentation.displayName,
               sizeof( renderPresentation.displayName ),
               entity.displayName,
               _TRUNCATE );
    renderPresentation.simpleRagdollPart = entity.behaviorGroup.kind == SceneBehaviorGroupKind::SimpleRagdoll;

    // Invariant: every recoverable check is complete before the first mutation.
    // The following owner appends use fixed or pre-reserved storage; any count or
    // identity mismatch is Lane F because partial topology cannot be recovered.
    bodyDesc.sceneObjectId = entity.sceneObjectId;
    bodyDesc.fixedTreeReleaseRootIndex = entity.behaviorGroup.kind != SceneBehaviorGroupKind::ReleasableTree
                                             ? -1
                                             : ( entity.behaviorGroup.rootObjectId.value == entity.sceneObjectId.value
                                                     ? modelIndex
                                                     : ResolveBehaviorGroupRootModelIndex( entity.behaviorGroup ) );
    // Lifetime: authored descriptors outlive this value-local entity packet.
    // Diagnostics receive stable SceneEntityStore names through the explicit
    // refresh/step view, so never retain the packet's displayName pointer.
    bodyDesc.diagnosticName = nullptr;
    const PhysicsAuthoredBodyRegistration registration =
        m_physics.RegisterAuthoredBody( bodyDesc, std::move( colliderDesc ) );
    const PhysicsBodyHandle bodyHandle = registration.body;
    const PhysicsBodyRecord* bodyRecord = BodyStore().RecordForHandle( bodyHandle );
    if ( !bodyRecord )
    {
        // Invariant: RegisterAuthoredBody returns the handle for the row it just
        // appended. A failed immediate lookup means collection/body-store
        // topology diverged after mutation.
        SB_FATAL( "GameObjects/SceneController", "Failed to resolve newly authored physics body record." );
    }
    const ColliderRecord* colliderRecord = Colliders().RecordForHandle( registration.collider );
    if ( !registration.IsValid() || !colliderRecord )
    {
        // Invariant: collider registration is the physics half of the creation
        // transaction. No collider handle means owner topology diverged after
        // preflight and cannot safely enter physics or render snapshots.
        SB_FATAL( "GameObjects/SceneController", "Failed to register newly authored physics collider record." );
    }
    renderPresentation.shadowCasterStream =
        ResolveRegisteredShadowCasterStream( *colliderRecord, renderPresentation.material );
    SceneEntities().CommitAppend( entity, bodyHandle );
    m_renderInstanceStore.CommitCreationRow(
        renderPresentation,
        *bodyRecord,
        LoadPhysicsBodyHotState( BodyStore().HotFields(), static_cast<std::size_t>( modelIndex ) ),
        *colliderRecord,
        modelIndex );
    AssertSceneCreationTopology( modelIndex + 1 );
    return { SkullbonezCore::Core::SbResult::Success(), bodyHandle };
}


bool SceneController::DestroySceneEntity( PhysicsBodyHandle body )
{
    const PhysicsBodyStore& bodyStore = BodyStore();
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    const int modelCount = SceneEntityCount();
    if ( modelIndex < 0 || modelIndex >= modelCount || bodyStore.Count() != modelCount ||
         Colliders().Count() != modelCount || m_renderInstanceStore.PresentationCount() != modelCount ||
         m_renderInstanceStore.Count() != modelCount )
    {
        return false;
    }
    const SceneEntityRecord& entity = SceneEntities().At( modelIndex );
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    if ( !bodyRecord || entity.body != body || entity.sceneObjectId.value != bodyRecord->sceneObjectId.value )
    {
        return false;
    }

    // Invariant: a group root cannot disappear while surviving metadata still
    // names it. Group deletion is an ordered caller operation that removes
    // dependants before the root.
    for ( int index = 0; index < modelCount; ++index )
    {
        if ( index == modelIndex )
        {
            continue;
        }
        const SceneEntityRecord& candidate = SceneEntities().At( index );
        if ( candidate.behaviorGroup.rootObjectId.value == entity.sceneObjectId.value ||
             candidate.asset.rootObjectId.value == entity.sceneObjectId.value )
        {
            return false;
        }
    }

    if ( !m_physics.DestroyAuthoredBody( body ) )
    {
        return false;
    }
    const bool entityRemoved = SceneEntities().DestroyAtSwapLast( modelIndex );
    const bool renderRemoved = m_renderInstanceStore.DestroyCreationRowAtSwapLast( modelIndex );
    if ( !entityRemoved || !renderRemoved )
    {
        SB_FATAL( "GameObjects/SceneController",
                  "Paired scene deletion diverged after physics commit. row=%d entity=%d render=%d",
                  modelIndex,
                  entityRemoved ? 1 : 0,
                  renderRemoved ? 1 : 0 );
    }
    // Invariant: PhysicsScene already compacted the live body/collider rows.
    // Reloading authored descriptors here would teleport every surviving body
    // whose solver/replay pose has diverged from its cold creation pose.
    RefreshRenderInstances();
    if ( m_renderInstanceStore.Count() != modelCount - 1 ||
         m_renderInstanceStore.PresentationCount() != modelCount - 1 )
    {
        SB_FATAL( "GameObjects/SceneController", "Paired scene deletion failed render refresh. row=%d", modelIndex );
    }
    AssertSceneCreationTopology( modelCount - 1 );
    return !BodyStore().Contains( body );
}


void SceneController::Clear()
{
    SceneEntities().Clear();
    m_physics.Clear();
    m_renderInstanceStore.Clear();
    AssertSceneCreationTopology( 0 );
}


void SceneController::BeginPhysicsStepPresentationCapture()
{
    // Invariant: this hook now precedes StepPhysics, whose first action used to
    // repair supported descriptor/body topology drift. Preserve that repair
    // boundary before RenderInstanceStore validates paired dense rows.
    if ( !RepairPhysicsBodyAndColliderTopology() )
    {
        SB_FATAL( "GameObjects/SceneController",
                  "Presentation capture could not repair scene/physics topology. entities=%d bodies=%d colliders=%d",
                  SceneEntityCount(),
                  BodyStore().Count(),
                  Colliders().Count() );
    }
    m_renderInstanceStore.BeginPhysicsStepPoseCapture( BodyStore() );
}


void SceneController::CompletePhysicsStepPresentationCapture()
{
    m_renderInstanceStore.CompletePhysicsStepPoseCapture( BodyStore() );
}


void SceneController::PrepareRenderInstances( float presentationAlpha )
{
    // Why: object rendering now reads the render instance store for transforms.
    // Preparing it once here prevents each render pass from re-importing the
    // same physics pose repeatedly.
    RefreshRenderInstances( presentationAlpha );
}


bool SceneController::TryGetModelPosition( int index, Vector3& outPosition ) const
{
    if ( index < 0 || index >= SceneEntityCount() )
    {
        return false;
    }

    // Why: object-follow cameras should read the same store-owned pose that
    // physics, diagnostics, replay capture, and render snapshots consume. A
    // missing model slot is a recoverable legacy-camera request, so callers
    // keep their previous target instead of aborting the frame.
    const PhysicsBodyStore& bodyStore = BodyStore();
    const PhysicsBodyRecord* record = bodyStore.RecordForModelIndex( index );
    if ( !record )
    {
        // Invariant: a live scene model must have a physics body row at the
        // same slot. Missing rows are topology drift, not recoverable camera
        // input, because rendering/replay snapshots would read divergent state.
        SB_FATAL( "GameObjects/SceneController",
                  "No physics body exists at the specified index.  (SceneController::TryGetModelPosition)" );
    }
    outPosition = PhysicsBodyPosition( bodyStore.HotFields(), static_cast<std::size_t>( index ) );
    return true;
}


bool SceneController::TryGetPresentationPose( int index,
                                              float presentationAlpha,
                                              Vector3& outPosition,
                                              Quaternion& outOrientation ) const
{
    return m_renderInstanceStore.TryGetPresentationPose( index, presentationAlpha, outPosition, outOrientation );
}


int SceneController::SceneEntityCount() const
{
    return m_renderInstanceStore.PresentationCount();
}


SceneBehaviorGroupKind SceneController::GroupKindAt( int modelIndex ) const
{
    return BehaviorGroupAt( modelIndex ).kind;
}


PhysicsSceneObjectId SceneController::GroupRootObjectIdAt( int modelIndex ) const
{
    return BehaviorGroupAt( modelIndex ).rootObjectId;
}


int SceneController::GroupPartIndexAt( int modelIndex ) const
{
    return BehaviorGroupAt( modelIndex ).partIndex;
}


bool SceneController::IsSimpleRagdollPart( int modelIndex ) const
{
    return GroupKindAt( modelIndex ) == SceneBehaviorGroupKind::SimpleRagdoll;
}


bool SceneController::IsSimpleRagdollTorso( int modelIndex ) const
{
    return IsSimpleRagdollPart( modelIndex ) && GroupPartIndexAt( modelIndex ) == 0;
}


int SceneController::RagdollRootModelIndexForPart( int modelIndex ) const
{
    const SceneBehaviorGroup& group = BehaviorGroupAt( modelIndex );
    if ( group.kind != SceneBehaviorGroupKind::SimpleRagdoll )
    {
        return modelIndex;
    }

    const int rootIndex = ResolveBehaviorGroupRootModelIndex( group );
    if ( IsSimpleRagdollPart( rootIndex ) )
    {
        return rootIndex;
    }
    return modelIndex;
}


bool SceneController::TryFindSimpleRagdollPart( int selectedModelIndex, int partIndex, int& outModelIndex ) const
{
    outModelIndex = -1;
    if ( !IsSimpleRagdollPart( selectedModelIndex ) )
    {
        return false;
    }

    const PhysicsSceneObjectId rootObjectId = GroupRootObjectIdAt( selectedModelIndex );
    for ( int i = 0; i < SceneEntityCount(); ++i )
    {
        const SceneBehaviorGroup& group = BehaviorGroupAt( i );
        if ( group.kind == SceneBehaviorGroupKind::SimpleRagdoll && group.rootObjectId.value == rootObjectId.value &&
             group.partIndex == partIndex )
        {
            outModelIndex = i;
            return true;
        }
    }
    return false;
}


int SceneController::GatherGroupMemberIndices( int selectedModelIndex, int* outIndices, int maxIndices ) const
{
    if ( outIndices && maxIndices > 0 )
    {
        for ( int i = 0; i < maxIndices; ++i )
        {
            outIndices[i] = -1;
        }
    }
    if ( !outIndices || maxIndices <= 0 || selectedModelIndex < 0 || selectedModelIndex >= SceneEntityCount() )
    {
        return 0;
    }

    const SceneBehaviorGroup& selectedGroup = BehaviorGroupAt( selectedModelIndex );
    if ( selectedGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll )
    {
        outIndices[0] = selectedModelIndex;
        return 1;
    }

    (void)ResolveBehaviorGroupRootModelIndex( selectedGroup );

    int count = 0;
    for ( int i = 0; i < SceneEntityCount() && count < maxIndices; ++i )
    {
        const SceneBehaviorGroup& group = BehaviorGroupAt( i );
        if ( group.kind == selectedGroup.kind && group.rootObjectId.value == selectedGroup.rootObjectId.value )
        {
            outIndices[count] = i;
            ++count;
        }
    }

    if ( count <= 0 )
    {
        outIndices[0] = selectedModelIndex;
        return 1;
    }
    return count;
}


#ifdef _DEBUG
bool SceneController::TryGetPhysicsDiagnosticsModelName( int index, const char*& outName ) const
{
    if ( index < 0 || index >= SceneEntityCount() )
    {
        return false;
    }

    outName = SceneEntities().At( index ).displayName;
    return true;
}


void SceneController::FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& outNames ) const
{
    const int clampedBodyCount = (std::max)( 0, bodyCount );
    outNames.assign( static_cast<std::size_t>( clampedBodyCount ), "" );
    const int copyCount = (std::min)( clampedBodyCount, SceneEntityCount() );
    for ( int i = 0; i < copyCount; ++i )
    {
        // Lifetime: these are borrowed display-name pointers for the current
        // Debug diagnostics write; the caller owns only the pointer table.
        outNames[static_cast<std::size_t>( i )] = SceneEntities().At( i ).displayName;
    }
}


#endif


SkullbonezCore::Core::MainMemoryGameObjectStats SceneController::CollectMemoryStats() const
{
    SkullbonezCore::Core::MainMemoryGameObjectStats stats;
    const Physics::PhysicsBodyStore& bodyStore = BodyStore();
    const Physics::ColliderStore& colliderStore = Colliders();
    const Rendering::RenderInstanceStore& renderStore = m_renderInstanceStore;

    stats.modelCount = static_cast<std::size_t>( m_renderInstanceStore.PresentationCount() );
    stats.modelCapacity = m_renderInstanceStore.PresentationCapacity();
    stats.bodyStoreCapacity = bodyStore.RecordCapacity();
    stats.colliderStoreCapacity = colliderStore.RecordCapacity();
    stats.renderStoreCapacity = renderStore.RecordCapacity();
    stats.modelVectorBytes = m_renderInstanceStore.PresentationCapacityBytes() + SceneEntities().CapacityBytes();
    stats.physicsStoreBytes = static_cast<uint64_t>( bodyStore.RecordCapacity() ) * sizeof( PhysicsBodyRecord );
    stats.colliderStoreBytes =
        static_cast<uint64_t>( colliderStore.RecordCapacity() ) * sizeof( Physics::ColliderRecord );
    stats.renderStoreBytes =
        static_cast<uint64_t>( renderStore.RecordCapacity() ) * sizeof( Rendering::RenderInstanceRecord );
    stats.physicsWorldBytes = m_physics.CollectPhysicsWorldMemoryBytes();
    stats.debugAndBroadphaseBytes = m_physics.CollectDebugAndBroadphaseMemoryBytes();
    stats.totalBytes = stats.modelVectorBytes + stats.physicsStoreBytes + stats.colliderStoreBytes +
                       stats.renderStoreBytes + stats.physicsWorldBytes;
    return stats;
}


bool SceneController::CanTrimPresentationRowsForSceneRestore( int modelCount ) const
{
    return modelCount >= 0 && modelCount <= SceneEntityCount() &&
           modelCount <= m_renderInstanceStore.PresentationCount();
}


bool SceneController::TrimPresentationRowsForSceneRestore( int modelCount )
{
    if ( !CanTrimPresentationRowsForSceneRestore( modelCount ) )
    {
        return false;
    }
    return m_renderInstanceStore.ResizePresentationRecords( modelCount );
}


void SceneController::CaptureReplaySolverWorldSnapshot( ReplaySolverWorldSnapshot& outSnapshot ) const
{
    m_physics.CaptureReplaySolverSnapshot( outSnapshot, MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


bool SceneController::RestoreReplaySolverWorldSnapshot( const ReplaySolverWorldSnapshot& snapshot )
{
    return m_physics.RestoreReplaySolverSnapshot( snapshot,
                                                  MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


bool SceneController::RepairPhysicsBodyTopology()
{
    if ( BodyStore().Count() != SceneEntityCount() )
    {
        // Invariant: topology repair imports construction rows only. Same-count
        // state edits are physics-store authority and must not be overwritten by
        // a convenience read that rebuilds descriptor rows.
        (void)RefreshPhysicsBodyStoreFromAuthoredDescriptors();
    }
    return BodyStore().Count() == SceneEntityCount();
}


bool SceneController::RepairPhysicsBodyAndColliderTopology()
{
    const int modelCount = SceneEntityCount();
    const bool bodyTopologyChanged = BodyStore().Count() != modelCount;
    const bool colliderTopologyChanged = Colliders().Count() != modelCount;
    if ( bodyTopologyChanged || colliderTopologyChanged )
    {
        // Why: body rows can be repaired from explicit body descriptors, but
        // collider shape/material rows are store-owned once created. Count drift
        // in ColliderStore is a construction bug, not a reason to rediscover
        // shape facts from legacy object record.
        if ( bodyTopologyChanged )
        {
            (void)RefreshPhysicsBodyStoreFromAuthoredDescriptors();
        }
        const bool colliderBindingsReady = m_physics.RefreshColliderSnapshot();
        return BodyStore().Count() == modelCount && Colliders().Count() == modelCount && colliderBindingsReady;
    }
    return BodyStore().Count() == modelCount && Colliders().Count() == modelCount;
}


const SkullbonezCore::Physics::PhysicsBodyStore& SceneController::BodyStore() const
{
    return Physics::PhysicsEngine::ReadBodies( m_physics );
}


const SkullbonezCore::Physics::ColliderStore& SceneController::Colliders() const
{
    return Physics::PhysicsEngine::ReadColliders( m_physics );
}


const SkullbonezCore::Rendering::RenderInstanceStore& SceneController::RenderInstances() const
{
    return m_renderInstanceStore;
}


SkullbonezCore::Rendering::RenderInstanceStore& SceneController::MutableRenderInstances()
{
    return m_renderInstanceStore;
}


bool SceneController::TryQueueReplayRenderPoseOverride( int modelIndex,
                                                        uint32_t replayBodyId,
                                                        const Math::Vector::Vector3& position,
                                                        const Math::Orientation::Quaternion& orientation )
{
    const Physics::PhysicsBodyStore& bodyStore = BodyStore();
    const Physics::PhysicsBodyHandle body = bodyStore.HandleForModelIndex( modelIndex );
    const Physics::PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    // Invariant: render-pose overrides are keyed by the physics-owned body id.
    // Model-index hints can lag during scrub/prediction presentation, so they
    // cannot approve which live render instance receives the pose.
    if ( !bodyRecord || bodyStore.ModelIndexForHandle( body ) != modelIndex ||
         bodyRecord->replayBodyId != replayBodyId )
    {
        return false;
    }

    return m_renderInstanceStore.OverridePose( modelIndex, replayBodyId, position, orientation, Colliders() );
}


const SkullbonezCore::Rendering::RenderInstanceStore& SceneController::GetRenderInstanceStore()
{
    RefreshRenderInstances();
    return m_renderInstanceStore;
}


double SceneController::GetSceneKineticEnergy()
{
    constexpr double REST_LINEAR_SPEED_SQ = 0.5 * 0.5;
    constexpr double REST_ANGULAR_SPEED_SQ = 0.3 * 0.3;
    double totalEnergy = 0.0;
    const PhysicsBodyStore& bodyStore = BodyStore();
    const auto bodies = bodyStore.Records();
    const auto hotFields = bodyStore.HotFields();
    for ( std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex )
    {
        const PhysicsBodyRecord& body = bodies[bodyIndex];
        if ( hotFields.fixed[bodyIndex] != 0u )
        {
            continue;
        }

        const Vector3 vel = PhysicsBodyLinearVelocity( hotFields, bodyIndex );
        const Vector3 omega = PhysicsBodyAngularVelocity( hotFields, bodyIndex );
        const double speedSq = static_cast<double>( vel.x ) * vel.x + static_cast<double>( vel.y ) * vel.y +
                               static_cast<double>( vel.z ) * vel.z;
        const double omegaSq = static_cast<double>( omega.x ) * omega.x + static_cast<double>( omega.y ) * omega.y +
                               static_cast<double>( omega.z ) * omega.z;
        if ( speedSq < REST_LINEAR_SPEED_SQ && omegaSq < REST_ANGULAR_SPEED_SQ )
        {
            continue;
        }

        const Vector3& inertia = body.rotationalInertia;
        const double angularEnergy = 0.5 * ( static_cast<double>( inertia.x ) * omega.x * omega.x +
                                             static_cast<double>( inertia.y ) * omega.y * omega.y +
                                             static_cast<double>( inertia.z ) * omega.z * omega.z );
        totalEnergy += 0.5 * static_cast<double>( body.mass ) * speedSq + angularEnergy;
    }
    return totalEnergy;
}


void SceneController::RefreshRenderInstances( float presentationAlpha )
{
    const int modelCount = SceneEntityCount();
    if ( !RepairPhysicsBodyTopology() )
    {
        m_renderInstanceStore.Clear();
        return;
    }
    if ( BodyStore().Count() != modelCount )
    {
        m_renderInstanceStore.Clear();
        return;
    }
    if ( !m_physics.RefreshColliderSnapshot() )
    {
        // Hazard: render rows consume collider shape/material data. If topology
        // drift has removed collider rows, do not manufacture a partial render
        // snapshot from stale model-owned shape fields.
        m_renderInstanceStore.Clear();
        return;
    }
    if ( BodyStore().Count() != modelCount || Colliders().Count() != modelCount )
    {
        m_renderInstanceStore.Clear();
        return;
    }
    if ( !m_renderInstanceStore.ResizePresentationRecords( modelCount ) )
    {
        m_renderInstanceStore.Clear();
        return;
    }
    // Owner boundary: model material and highlight values still live in
    // SceneController, but the render-facing presentation rows belong to
    // RenderInstanceStore before store projection creates draw records.
    const auto colliderRecords = Colliders().Records();
    for ( int i = 0; i < modelCount; ++i )
    {
        Rendering::RenderInstancePresentationRecord* presentation =
            m_renderInstanceStore.MutablePresentationRecordForModelIndex( i );
        if ( !presentation )
        {
            m_renderInstanceStore.Clear();
            return;
        }
        const SceneEntityRecord& entity = SceneEntities().At( i );
        presentation->material = entity.renderMaterial;
        presentation->shadowCasterStream =
            ResolveRegisteredShadowCasterStream( colliderRecords[static_cast<std::size_t>( i )],
                                                 presentation->material );
        strncpy_s( presentation->displayName, sizeof( presentation->displayName ), entity.displayName, _TRUNCATE );
        presentation->simpleRagdollPart = IsSimpleRagdollPart( i );
    }
    m_renderInstanceStore.Refresh( BodyStore(), Colliders(), presentationAlpha );
    if ( m_renderInstanceStore.Count() != modelCount )
    {
        m_renderInstanceStore.Clear();
        return;
    }
#ifdef _DEBUG
    const auto instances = m_renderInstanceStore.Records();
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const Rendering::RenderInstanceRecord& instance = instances[index];
        const Rendering::RenderInstanceHandle renderHandle = m_renderInstanceStore.HandleForModelIndex( i );

        assert( renderHandle.IsValid() );
        assert( instance.handle == renderHandle );
        assert( m_renderInstanceStore.ModelIndexForHandle( renderHandle ) == i );
    }
#endif
}


bool SceneController::ReleaseAttachedFixedTreeParts( int sourceIndex,
                                                     float releaseImpulseStrength,
                                                     const Vector3& seedLinearVelocity,
                                                     const Vector3& seedAngularVelocity )
{
    if ( sourceIndex < 0 || sourceIndex >= SceneEntityCount() )
    {
        return false;
    }

    // Owner: SceneController runtime-tool edge.
    // Reason: launcher hits still arrive as model indices, but fixed-state,
    // release policy, and same-tree propagation now belong to PhysicsBodyStore.
    // Deletion condition: runtime picking and scene identity use stable entity
    // ids or body handles directly. Checker budget: boundary grep blocks this
    // function from reading legacy object record fixed/position/tree body metadata or
    // reintroducing a per-release model writeback.
    if ( !RepairPhysicsBodyAndColliderTopology() )
    {
        return false;
    }

    const PhysicsBodyHandle sourceBody = BodyStore().HandleForModelIndex( sourceIndex );
    if ( !sourceBody.IsValid() )
    {
        return false;
    }

    if ( !m_physics.ReleaseFixedBodyAndAttachedTreeParts( sourceBody,
                                                          releaseImpulseStrength,
                                                          seedLinearVelocity,
                                                          seedAngularVelocity ) )
    {
        return false;
    }

    return true;
}


void SceneController::BeginCollisionVisualFrame()
{
    m_physics.BeginCollisionVisualFrame( MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


void SceneController::EndCollisionVisualFrame()
{
    m_physics.EndCollisionVisualFrame();
}
