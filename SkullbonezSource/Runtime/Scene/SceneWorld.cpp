/*
File: SkullbonezSource/Runtime/Scene/SceneWorld.cpp
Purpose:
  Implements the concrete scene-lifetime world owner and its cross-store
  topology transactions.

Summary:
  SceneWorld directly owns entity metadata, physics, cameras, terrain,
  environment settings, and render presentation. Creation preflights every
  concrete store before mutation; step, repair, replay trim, and deletion
  preserve shared dense order without reaching back into SceneController.

Glossary:
  Physics material: Per-object friction and drag coefficients owned by
    PhysicsEngine and copied into authored descriptor rows at cold boundaries.
  Body simulation limit: Scalar cap owned by PhysicsEngine before authored
    descriptors create PhysicsBodyStore rows.
  Contact policy: Terrain and contact thresholds owned by PhysicsEngine so
    existing and newly added models receive the same physics policy.
  Body descriptor: PhysicsEngine-owned authoring value that can rebuild a live
    PhysicsBodyStore row without reading legacy object record physics fields.
  Render instance store: Renderer-facing snapshot built from physics-owned pose
    and render-owned presentation rows before frame passes.
  Collider descriptor: Value packet containing shape/material facts that
    PhysicsEngine turns into a live ColliderStore row.
  Topology drift: A body/collider/model count mismatch that means stores must
    import explicit construction descriptors before stepping.
  Scene-object group: Cold metadata that maps multi-part authored objects, such
    as ragdolls or releasable trees, to a stable root scene object id.
  Fixed-tree release: Authored scene rule that lets tree parts become dynamic
    when a related fixed part is hit strongly enough.
  Scene object id: PhysicsBodyStore-owned identity saved in replay samples so
    restore paths can reject stale model slots.
  Shadow caster stream: Opaque render bin resolved while scene material and
    collider facts are both available at the instance-build boundary.

Invariants:
  - SceneEntityStore order remains the scene alignment key for physics stores,
    render batches, and scene snapshots. Scene object ids live in PhysicsBodyStore
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
  - SkullbonezSource/Runtime/Scene/SceneWorld.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneWorld.h"
#include "../../Core/Config.h"

#include "../../Core/FatalError.h"
#include "../Debug/CollisionVisualizer.h"
#include "../Debug/PhysicsDebugVisualizer.h"
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

} // namespace


SceneWorld::SceneWorld()
{
    ReserveForActiveSceneObjectCapacity();
}


void SceneWorld::ReserveForActiveSceneObjectCapacity()
{
    // Invariant: model-order storage must be fully sized before steady frames.
    // Config can raise the active model capacity after construction, so each
    // setup/config boundary repeats the reserve instead of letting render-time
    // append paths discover the new capacity by reallocating.
    const std::size_t capacity = static_cast<std::size_t>( m_activeSceneObjectCapacity );
    m_physics.ReserveAuthoredBodyCapacity( capacity );
    m_tornadoGameplay.ReserveBodyCapacity( m_activeSceneObjectCapacity );
    m_tornadoGameplay.ReserveVisualCapacity();
    m_renderInstanceStore.ReservePresentationCapacity( capacity );
}


std::vector<ModelRowHint> SceneWorld::BuildFixedTreeReleaseRootsForReload() const
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


std::vector<const char*> SceneWorld::BuildDiagnosticNamesForReload() const
{
    std::vector<const char*> diagnosticNames;
    diagnosticNames.reserve( static_cast<std::size_t>( Entities().Count() ) );
    for ( int index = 0; index < Entities().Count(); ++index )
    {
        diagnosticNames.push_back( Entities().At( index ).displayName );
    }
    return diagnosticNames;
}


void SceneWorld::RegisterPhysicsDiagnosticNames()
{
    const std::vector<const char*> diagnosticNames = BuildDiagnosticNamesForReload();
    m_physics.SetDiagnosticNames( diagnosticNames );
}


bool SceneWorld::RefreshPhysicsBodyStoreFromAuthoredDescriptors()
{
    const std::vector<PhysicsSceneObjectId> sceneObjectIds =
        Physics::PhysicsEngine::ReadBodies( m_physics ).BuildSceneObjectIdsForReload( SceneEntityCount() );
    const std::vector<ModelRowHint> fixedTreeReleaseRoots = BuildFixedTreeReleaseRootsForReload();
    const std::vector<const char*> diagnosticNames = BuildDiagnosticNamesForReload();
    PhysicsAuthoredBodyRefreshView refreshView;
    refreshView.sceneObjectIds = sceneObjectIds.empty() ? nullptr : sceneObjectIds.data();
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
            SB_FATAL( "Scene/SceneWorld", "Refreshed body row is missing." );
        }
        Entities().UpdateBodyHandleAt( index, body->handle, body->sceneObjectId );
    }
    return true;
}


int SceneWorld::FixedTreeReleaseRootForModelIndex( int modelIndex ) const
{
    const SceneBehaviorGroup& group = Entities().BehaviorGroupAt( modelIndex );
    return group.kind == SceneBehaviorGroupKind::ReleasableTree ? Entities().ResolveBehaviorGroupRootModelIndex( group )
                                                                : -1;
}


SceneEntityStore& SceneWorld::Entities()
{
    return m_entities;
}


const SceneEntityStore& SceneWorld::Entities() const
{
    return m_entities;
}


SkullbonezCore::Environment::CameraCollection& SceneWorld::Cameras()
{
    return m_cameras;
}


const SkullbonezCore::Environment::CameraCollection& SceneWorld::Cameras() const
{
    return m_cameras;
}


SkullbonezCore::Environment::WorldEnvironment& SceneWorld::Environment()
{
    return m_world;
}


const SkullbonezCore::Environment::WorldEnvironment& SceneWorld::Environment() const
{
    return m_world;
}


SceneTerrain& SceneWorld::Terrain()
{
    return m_terrain;
}


const SceneTerrain& SceneWorld::Terrain() const
{
    return m_terrain;
}


SkullbonezCore::Physics::PhysicsEngine& SceneWorld::Physics()
{
    return m_physics;
}


const SkullbonezCore::Physics::PhysicsEngine& SceneWorld::Physics() const
{
    return m_physics;
}


SkullbonezCore::Gameplay::TornadoGameplay& SceneWorld::Tornado()
{
    return m_tornadoGameplay;
}


const SkullbonezCore::Gameplay::TornadoGameplay& SceneWorld::Tornado() const
{
    return m_tornadoGameplay;
}

uint64_t SceneWorld::CollectGameplayMemoryBytes() const
{
    return m_tornadoGameplay.CollectMemoryBytes();
}

uint64_t SceneWorld::CollectGameplayDebugMemoryBytes() const
{
    return m_tornadoGameplay.CollectDebugMemoryBytes();
}


std::span<const float> SceneWorld::BuildWorldExtensionDebugLines()
{
    return m_tornadoGameplay.BuildDebugLineVertices();
}


void SceneWorld::ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    m_activeSceneObjectCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( config );
    Entities().ConfigureCapacity( m_activeSceneObjectCapacity );
    ReserveForActiveSceneObjectCapacity();
    m_tornadoGameplay.SetParallelForceEvaluation( config.physicsExecution.parallelExternalForceFields );
    m_physics.ApplyRuntimeConfig( config );
}


int SceneWorld::ActiveSceneObjectCapacity() const
{
    return m_activeSceneObjectCapacity;
}


ScenePhysicsPostStepOutput SceneWorld::StepPhysics( float fixedDt,
                                                    const Physics::PhysicsWorldForces& worldForces,
                                                    Threading::WorkerPool& workerPool )
{
    // Invariant: PhysicsBodyStore is the per-tick body authority. Descriptor
    // sidecars are imported only when topology changes; same-count editor or
    // replay mutations must commit explicitly before this step reads rows.
    RepairPhysicsBodyAndColliderTopology();

    Physics::PhysicsDiagnosticsCsvWriter diagnosticsCsvWriter;
    const Physics::ExternalForceFrameInput externalForces =
        m_tornadoGameplay.BuildForceFrame( fixedDt, Physics::PhysicsEngine::ReadBodies( m_physics ).Count() );
    m_physics.Step( fixedDt, worldForces, externalForces, workerPool, diagnosticsCsvWriter );

    return ScenePhysicsPostStepOutput{ Physics::PhysicsEngine::ReadFixedContactHighlightBodies( m_physics ) };
}


void SceneWorld::AssertSceneCreationTopology( int expectedCount ) const
{
    const int descriptorCount = static_cast<int>( m_physics.AuthoredBodyDescriptorCount().value );
    const int bodyCount = BodyStore().Count();
    const int colliderCount = Colliders().Count();
    const int renderPresentationCount = m_renderInstanceStore.PresentationCount();
    const int renderCount = m_renderInstanceStore.Count();
    const bool reservationsReady =
        m_renderInstanceStore.PresentationCapacity() >= static_cast<std::size_t>( m_activeSceneObjectCapacity );
    if ( expectedCount < 0 || Entities().Count() != expectedCount || descriptorCount != expectedCount ||
         bodyCount != expectedCount || colliderCount != expectedCount || renderPresentationCount != expectedCount ||
         renderCount != expectedCount || !reservationsReady )
    {
        SB_FATAL( "Scene/SceneWorld",
                  "Scene creation topology diverged. expected=%d entities=%d descriptors=%d "
                  "bodies=%d colliders=%d render_presentation=%d render=%d reservations_ready=%d",
                  expectedCount,
                  Entities().Count(),
                  descriptorCount,
                  bodyCount,
                  colliderCount,
                  renderPresentationCount,
                  renderCount,
                  reservationsReady ? 1 : 0 );
    }
}


SceneEntityCreateResult SceneWorld::TryCreateSceneEntity( SceneEntityCreateDesc entity,
                                                          PhysicsBodyCreateDesc bodyDesc,
                                                          PhysicsColliderCreateDesc colliderDesc )
{
    const int activeCapacity = m_activeSceneObjectCapacity;
    const int modelIndex = SceneEntityCount();
    AssertSceneCreationTopology( modelIndex );
    if ( SceneEntityCount() >= activeCapacity )
    {
        return { SkullbonezCore::Core::SbResult::Failure(
                     SCENE_ENTITY_CREATION_OWNER,
                     "Exceeded active scene object capacity; raise --model-capacity or game_model_capacity." ),
                 PhysicsBodyHandle{} };
    }
    const SkullbonezCore::Core::SbResult entityResult = Entities().PreflightAppend( entity );
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
        SB_FATAL( "Scene/SceneWorld",
                  "Physics creation storage is not preflight-ready. expected=%d descriptors=%u bodies=%d",
                  modelIndex,
                  m_physics.AuthoredBodyDescriptorCount().value,
                  BodyStore().Count() );
    }
    if ( !m_renderInstanceStore.CanAppendCreationRow( modelIndex ) )
    {
        SB_FATAL( "Scene/SceneWorld",
                  "Render creation storage is not preflight-ready. expected=%d presentation=%d render=%d",
                  modelIndex,
                  m_renderInstanceStore.PresentationCount(),
                  m_renderInstanceStore.Count() );
    }

    Rendering::RenderInstancePresentationRecord renderPresentation;
    renderPresentation.material = entity.renderMaterial;
    renderPresentation.editorVisible = entity.editorVisible;
    strncpy_s( renderPresentation.displayName,
               sizeof( renderPresentation.displayName ),
               entity.displayName,
               _TRUNCATE );
    renderPresentation.simpleRagdollPart = entity.behaviorGroup.kind == SceneBehaviorGroupKind::SimpleRagdoll;

    // Invariant: every recoverable check is complete before the first mutation.
    // The following owner appends use fixed or pre-reserved storage; any count or
    // identity mismatch is Lane F because partial topology cannot be recovered.
    bodyDesc.sceneObjectId = entity.sceneObjectId;
    bodyDesc.fixedTreeReleaseRootIndex =
        entity.behaviorGroup.kind != SceneBehaviorGroupKind::ReleasableTree
            ? -1
            : ( entity.behaviorGroup.rootObjectId.value == entity.sceneObjectId.value
                    ? modelIndex
                    : Entities().ResolveBehaviorGroupRootModelIndex( entity.behaviorGroup ) );
    // Lifetime: authored descriptors outlive this value-local entity packet.
    // Diagnostics receive stable SceneEntityStore names after the entity row
    // commits, so never retain this packet's displayName pointer.
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
        SB_FATAL( "Scene/SceneWorld", "Failed to resolve newly authored physics body record." );
    }
    const ColliderRecord* colliderRecord = Colliders().RecordForHandle( registration.collider );
    if ( !registration.IsValid() || !colliderRecord )
    {
        // Invariant: collider registration is the physics half of the creation
        // transaction. No collider handle means owner topology diverged after
        // preflight and cannot safely enter physics or render snapshots.
        SB_FATAL( "Scene/SceneWorld", "Failed to register newly authored physics collider record." );
    }
    renderPresentation.shadowCasterStream =
        ResolveRegisteredShadowCasterStream( *colliderRecord, renderPresentation.material );
    Entities().CommitAppend( entity, bodyHandle );
    m_renderInstanceStore.CommitCreationRow(
        renderPresentation,
        *bodyRecord,
        LoadPhysicsBodyHotState( BodyStore().HotFields(), static_cast<std::size_t>( modelIndex ) ),
        *colliderRecord,
        modelIndex );
    RegisterPhysicsDiagnosticNames();
    AssertSceneCreationTopology( modelIndex + 1 );
    return { SkullbonezCore::Core::SbResult::Success(), bodyHandle };
}


bool SceneWorld::DestroySceneEntity( PhysicsBodyHandle body )
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
    const SceneEntityRecord& entity = Entities().At( modelIndex );
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
        const SceneEntityRecord& candidate = Entities().At( index );
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
    const bool entityRemoved = Entities().DestroyAtSwapLast( modelIndex );
    const bool renderRemoved = m_renderInstanceStore.DestroyCreationRowAtSwapLast( modelIndex );
    if ( !entityRemoved || !renderRemoved )
    {
        SB_FATAL( "Scene/SceneWorld",
                  "Paired scene deletion diverged after physics commit. row=%d entity=%d render=%d",
                  modelIndex,
                  entityRemoved ? 1 : 0,
                  renderRemoved ? 1 : 0 );
    }
    // Invariant: PhysicsEngine already compacted the live body/collider rows.
    // Reloading authored descriptors here would teleport every surviving body
    // whose solver/replay pose has diverged from its cold creation pose.
    RefreshRenderInstances();
    if ( m_renderInstanceStore.Count() != modelCount - 1 ||
         m_renderInstanceStore.PresentationCount() != modelCount - 1 )
    {
        SB_FATAL( "Scene/SceneWorld", "Paired scene deletion failed render refresh. row=%d", modelIndex );
    }
    RegisterPhysicsDiagnosticNames();
    AssertSceneCreationTopology( modelCount - 1 );
    return !BodyStore().Contains( body );
}


bool SceneWorld::SetEditorEntityVisible( PhysicsSceneObjectId sceneObjectId, bool visible )
{
    const int modelIndex = Entities().FindBySceneObjectId( sceneObjectId );
    SceneEntityRecord* entity = Entities().TryGetMutable( modelIndex );
    if ( !entity || !m_renderInstanceStore.SetEditorVisible( modelIndex, visible ) )
    {
        return false;
    }
    entity->editorVisible = visible;
    return true;
}


bool SceneWorld::SetEditorEntityLocked( PhysicsSceneObjectId sceneObjectId, bool locked )
{
    SceneEntityRecord* entity = Entities().TryGetMutable( Entities().FindBySceneObjectId( sceneObjectId ) );
    if ( !entity )
    {
        return false;
    }
    entity->editorLocked = locked;
    return true;
}


void SceneWorld::Clear()
{
    Entities().Clear();
    m_physics.Clear();
    m_tornadoGameplay.Clear();
    m_renderInstanceStore.Clear();
    RegisterPhysicsDiagnosticNames();
    AssertSceneCreationTopology( 0 );
}


bool SceneWorld::TrimForReplayRestore( int bodyCount )
{
    const int liveBodyCount = Physics::PhysicsEngine::ReadBodies( m_physics ).Count();
    const int liveColliderCount = Physics::PhysicsEngine::ReadColliders( m_physics ).Count();
    const uint32_t authoredBodyCount = m_physics.AuthoredBodyDescriptorCount().value;
    if ( bodyCount < 0 || bodyCount > liveBodyCount || static_cast<uint32_t>( bodyCount ) > authoredBodyCount ||
         !CanTrimPresentationRowsForSceneRestore( bodyCount ) || bodyCount > m_entities.Count() )
    {
        return false;
    }

    const Physics::PhysicsBodyCount bodies = Physics::MakePhysicsBodyCountFromNonNegativeInt( bodyCount );
    const Physics::PhysicsColliderCount colliders = Physics::MakePhysicsColliderCountFromNonNegativeInt( bodyCount );
    const Physics::PhysicsAuthoredBodyCount authored =
        Physics::MakePhysicsAuthoredBodyCountFromNonNegativeInt( bodyCount );
    // Concept: replay topology restore is a two-phase transaction. Every owner
    // rejects an impossible target above before the first write. Once commit
    // starts, a failed shrink means an internal topology invariant broke; it is
    // not a recoverable replay-file error because earlier owners may already
    // have retired handles.
    // Invariant: physics rows shrink before presentation and metadata rows.
    // Every surviving handle was validated by scene object id before this command,
    // and PhysicsBodyStore retires removed handles.
    if ( !m_physics.TrimBodiesToCount( bodies ) ||
         ( liveColliderCount > bodyCount && !m_physics.TrimCollidersToCount( colliders ) ) ||
         !m_physics.TrimAuthoredBodyDescriptorsToCount( authored ) ||
         !TrimPresentationRowsForSceneRestore( bodyCount ) || !m_entities.TrimToCount( bodyCount ) )
    {
        SB_FATAL( "Runtime/SceneWorld",
                  "Replay topology commit failed after a successful preflight; live owners may be partially trimmed" );
    }
    RegisterPhysicsDiagnosticNames();
    return true;
}


void SceneWorld::BeginPhysicsStepPresentationCapture()
{
    // Invariant: this hook now precedes StepPhysics, whose first action used to
    // repair supported descriptor/body topology drift. Preserve that repair
    // boundary before RenderInstanceStore validates paired dense rows.
    if ( !RepairPhysicsBodyAndColliderTopology() )
    {
        SB_FATAL( "Scene/SceneWorld",
                  "Presentation capture could not repair scene/physics topology. entities=%d bodies=%d colliders=%d",
                  SceneEntityCount(),
                  BodyStore().Count(),
                  Colliders().Count() );
    }
    m_renderInstanceStore.BeginPhysicsStepPoseCapture( BodyStore() );
}


void SceneWorld::CompletePhysicsStepPresentationCapture()
{
    m_renderInstanceStore.CompletePhysicsStepPoseCapture( BodyStore() );
}


void SceneWorld::PrepareRenderInstances( float presentationAlpha )
{
    // Why: object rendering now reads the render instance store for transforms.
    // Preparing it once here prevents each render pass from re-importing the
    // same physics pose repeatedly.
    RefreshRenderInstances( presentationAlpha );
}


bool SceneWorld::TryGetModelPosition( int index, Vector3& outPosition ) const
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
        SB_FATAL( "Scene/SceneWorld",
                  "No physics body exists at the specified index.  (SceneWorld::TryGetModelPosition)" );
    }
    outPosition = PhysicsBodyPosition( bodyStore.HotFields(), static_cast<std::size_t>( index ) );
    return true;
}


bool SceneWorld::TryGetPresentationPose( int index,
                                         float presentationAlpha,
                                         Vector3& outPosition,
                                         Quaternion& outOrientation ) const
{
    return m_renderInstanceStore.TryGetPresentationPose( index, presentationAlpha, outPosition, outOrientation );
}


int SceneWorld::SceneEntityCount() const
{
    return m_renderInstanceStore.PresentationCount();
}


bool SceneWorld::CanTrimPresentationRowsForSceneRestore( int modelCount ) const
{
    return modelCount >= 0 && modelCount <= SceneEntityCount() &&
           modelCount <= m_renderInstanceStore.PresentationCount();
}


bool SceneWorld::TrimPresentationRowsForSceneRestore( int modelCount )
{
    if ( !CanTrimPresentationRowsForSceneRestore( modelCount ) )
    {
        return false;
    }
    return m_renderInstanceStore.ResizePresentationRecords( modelCount );
}


void SceneWorld::CaptureReplaySolverWorldSnapshot( Physics::PhysicsSolverSnapshot& outSnapshot ) const
{
    m_physics.CaptureReplaySolverSnapshot( outSnapshot, MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


bool SceneWorld::RestoreReplaySolverWorldSnapshot( const Physics::PhysicsSolverSnapshot& snapshot )
{
    return m_physics.RestoreReplaySolverSnapshot( snapshot,
                                                  MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


bool SceneWorld::RepairPhysicsBodyTopology()
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


bool SceneWorld::RepairPhysicsBodyAndColliderTopology()
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


const SkullbonezCore::Physics::PhysicsBodyStore& SceneWorld::BodyStore() const
{
    return Physics::PhysicsEngine::ReadBodies( m_physics );
}


const SkullbonezCore::Physics::ColliderStore& SceneWorld::Colliders() const
{
    return Physics::PhysicsEngine::ReadColliders( m_physics );
}


const SkullbonezCore::Rendering::RenderInstanceStore& SceneWorld::RenderInstances() const
{
    return m_renderInstanceStore;
}


SkullbonezCore::Rendering::RenderInstanceStore& SceneWorld::MutableRenderInstances()
{
    return m_renderInstanceStore;
}


const SkullbonezCore::Rendering::RenderInstanceStore& SceneWorld::GetRenderInstanceStore()
{
    RefreshRenderInstances();
    return m_renderInstanceStore;
}


double SceneWorld::GetSceneKineticEnergy()
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


void SceneWorld::RefreshRenderInstances( float presentationAlpha )
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
    // Owner boundary: authored material and display facts live in
    // SceneEntityStore, while render-facing presentation rows belong to
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
        const SceneEntityRecord& entity = Entities().At( i );
        presentation->material = entity.renderMaterial;
        presentation->editorVisible = entity.editorVisible;
        presentation->shadowCasterStream =
            ResolveRegisteredShadowCasterStream( colliderRecords[static_cast<std::size_t>( i )],
                                                 presentation->material );
        strncpy_s( presentation->displayName, sizeof( presentation->displayName ), entity.displayName, _TRUNCATE );
        presentation->simpleRagdollPart = Entities().IsSimpleRagdollPart( i );
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


bool SceneWorld::ReleaseAttachedFixedTreeParts( int sourceIndex,
                                                float releaseImpulseStrength,
                                                const Vector3& seedLinearVelocity,
                                                const Vector3& seedAngularVelocity )
{
    if ( sourceIndex < 0 || sourceIndex >= SceneEntityCount() )
    {
        return false;
    }

    // Owner: SceneWorld runtime-tool edge.
    // Reason: launcher hits still arrive as model indices, but fixed-state,
    // release policy, and same-tree propagation now belong to PhysicsBodyStore.
    // Deletion condition: runtime picking and scene identity use stable entity
    // ids or body handles directly.
    // Review evidence: this command resolves the row once, then mutates only
    // PhysicsEngine-owned body handles; it never writes entity metadata back.
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


void SceneWorld::BeginCollisionVisualFrame()
{
    m_physics.BeginCollisionVisualFrame( MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


void SceneWorld::EndCollisionVisualFrame()
{
    m_physics.EndCollisionVisualFrame();
}
