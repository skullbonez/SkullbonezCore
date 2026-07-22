/*
File: SkullbonezSource/Physics/PhysicsEngine.cpp
Purpose:
  Coordinates PhysicsWorld with deterministic body and collider stores.

Summary:
  PhysicsWorld still owns the solver. PhysicsEngine is the coordination boundary
  that refreshes body and collider state around that solver while preserving
  the live model order and the caller-owned presentation boundary.

Glossary:
  Solver: Physics step that integrates motion and applies collision/contact
    impulses.
  Store: Ordered snapshot of one ownership concern such as bodies, colliders,
    or render instances.
  Pending impulse: One-shot velocity edit queued on a body record and consumed
    by the next solver step.
  Physics material: Runtime policy for collider friction and sphere drag.
  Velocity edit: Replay-authored command that changes live body velocity before
    prediction or the next step samples the body store.
  Fixed-tree release: Store-owned command that turns authored fixed props into
    dynamic bodies and wakes same-tree parts after an accepted impulse.
  Sleep: Solver optimization that stops integrating stable bodies until an
    explicit wake or contact event reactivates them.
  Determinism: Same inputs produce byte-exact validation artifacts.

Invariants:
  - Store refresh order must preserve deterministic model-view order.
  - Step delegates to PhysicsWorld without changing floating-point order.
  - Pending impulses stay store-owned until consumed; render projection refresh
    is separate from solver sleep/island mutation.
  - Velocity edits stay store-owned until the normal step boundary projects
    body state for presentation.
  - Wake commands update solver sleep/island state without rebuilding render
    projection records.
  - Authored create, destroy, update, trim, and replay-restore commands
    invalidate derived awake/grid state, including same-count row replacement.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.h
*/
#include "PhysicsEngine.h"
#include "../Core/Config.h"
#include "../Core/SceneCapacity.h"
#include "PhysicsApi.h"

#include "../Core/Common.h"
#include "../Core/FatalError.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>
#include <variant>

using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius;
using SkullbonezCore::Math::CollisionDetection::GetShapePosition;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMag;
using SkullbonezCore::Physics::BodySimulationLimits;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::ContactPolicy;
using SkullbonezCore::Physics::PhysicsAuthoredBodyCount;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRefreshView;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration;
using SkullbonezCore::Physics::PhysicsBodyCount;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyHotFieldsConstView;
using SkullbonezCore::Physics::PhysicsBodyOrientation;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsBodyUpdateDesc;
using SkullbonezCore::Physics::PhysicsBroadphaseQueryResultView;
using SkullbonezCore::Physics::PhysicsColliderCount;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsConstraintHandle;
using SkullbonezCore::Physics::PhysicsDebugContact;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsMaterial;
using SkullbonezCore::Physics::PhysicsPipelineRecord;
using SkullbonezCore::Physics::PhysicsRayCastHit;
using SkullbonezCore::Physics::PhysicsRuntimeSettings;
using SkullbonezCore::Physics::PointJointConstraint;


namespace
{
int CountAsInt( PhysicsBodyCount count )
{
    return static_cast<int>( count.value );
}


int CountAsInt( PhysicsColliderCount count )
{
    return static_cast<int>( count.value );
}


int CountAsInt( PhysicsAuthoredBodyCount count )
{
    return static_cast<int>( count.value );
}


ColliderShapeKind ShapeKindForColliderDesc( const SkullbonezCore::Math::CollisionDetection::CollisionShape& shape )
{
    if ( std::holds_alternative<BoundingBox>( shape ) )
    {
        return ColliderShapeKind::Box;
    }
    if ( std::holds_alternative<ConvexHullShape>( shape ) )
    {
        return ColliderShapeKind::ConvexHull;
    }
    return ColliderShapeKind::Sphere;
}


// Why: descriptor import is a cold authoring boundary. The dense row shape and
// cheap collider discriminator are derived inside PhysicsEngine so collection
// code cannot become a second ColliderStore layout owner.
ColliderRecord MakeColliderRecordFromDesc( const PhysicsColliderCreateDesc& desc, const PhysicsBodyRecord& body )
{
    ColliderRecord record;
    record.body = body.handle;
    record.sceneObjectId = desc.sceneObjectId.IsValid() ? desc.sceneObjectId : body.sceneObjectId;
    record.shape = desc.shape;
    record.shapeKind = ShapeKindForColliderDesc( desc.shape );
    record.boundingRadius = desc.boundingRadius;
    record.restitution = desc.restitution;
    record.friction = desc.friction;
    record.contactMaterialId = desc.contactMaterialId;
    strncpy_s( record.contactMaterialName, sizeof( record.contactMaterialName ), desc.contactMaterialName, _TRUNCATE );
    record.projectedSurfaceArea = desc.projectedSurfaceArea;
    record.dragCoefficient = desc.dragCoefficient;
    return record;
}


bool BodyPassesQueryFilters( const PhysicsBodyHotFieldsConstView& hotFields,
                             std::size_t bodyIndex,
                             bool includeFixedBodies,
                             bool includeSleepingBodies,
                             bool sleepEnabled )
{
    if ( !includeFixedBodies && hotFields.fixed[bodyIndex] != 0u )
    {
        return false;
    }
    return includeSleepingBodies || !sleepEnabled || hotFields.awake[bodyIndex] != 0u;
}


float EffectiveColliderRadius( const ColliderRecord& collider )
{
    // Invariant: a conservative query sphere must include a collider's local
    // offset or a broadphase candidate can disappear before exact testing.
    const float shapeRadius =
        GetShapeBoundingRadius( collider.shape ) + VectorMag( GetShapePosition( collider.shape ) );
    return collider.boundingRadius > shapeRadius ? collider.boundingRadius : shapeRadius;
}


Vector3 ColliderWorldCenter( const PhysicsBodyHotFieldsConstView& hotFields,
                             std::size_t bodyIndex,
                             const ColliderRecord& collider )
{
    const RotationMatrix rotation = PhysicsBodyOrientation( hotFields, bodyIndex ).GetOrientationMatrix();
    return PhysicsBodyPosition( hotFields, bodyIndex ) + rotation * GetShapePosition( collider.shape );
}


bool IntersectRaySphere( const Vector3& rayOrigin,
                         const Vector3& rayDirection,
                         const Vector3& center,
                         float radius,
                         float& outDistance )
{
    const Vector3 originToCenter = rayOrigin - center;
    const float directionProjection = originToCenter * rayDirection;
    const float distanceTerm = ( originToCenter * originToCenter ) - radius * radius;
    if ( distanceTerm > 0.0f && directionProjection > 0.0f )
    {
        return false;
    }
    const float discriminant = directionProjection * directionProjection - distanceTerm;
    if ( discriminant < 0.0f )
    {
        return false;
    }
    outDistance = -directionProjection - sqrtf( discriminant );
    if ( outDistance < 0.0f )
    {
        outDistance = 0.0f;
    }
    return true;
}


bool SphereOverlapsAabb( const Vector3& center, float radius, const Vector3& min, const Vector3& max )
{
    const float closestX = center.x < min.x ? min.x : ( center.x > max.x ? max.x : center.x );
    const float closestY = center.y < min.y ? min.y : ( center.y > max.y ? max.y : center.y );
    const float closestZ = center.z < min.z ? min.z : ( center.z > max.z ? max.z : center.z );
    return SkullbonezCore::Math::Vector::DistanceSquared( center, Vector3( closestX, closestY, closestZ ) ) <=
           radius * radius;
}
} // namespace


PhysicsEngine::PhysicsEngine()
{
}

void PhysicsEngine::BindProfiler( SkullbonezCore::Core::Profiler* profiler ) noexcept
{
    m_world.BindProfiler( profiler );
}


void PhysicsEngine::ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    // Concept: this is the one process-config-to-Physics stamp boundary. Every
    // fixed-step consumer below receives values owned by PhysicsEngine.
    m_runtimeSettings = RuntimeSettingsFromConfig( config );
    m_physicsMaterial = PhysicsMaterial::FromSettings( m_runtimeSettings.material );
    m_bodySimulationLimits = BodySimulationLimits::FromSettings( m_runtimeSettings.body );
    m_contactPolicy = ContactPolicy::FromSettings( m_runtimeSettings.body, m_runtimeSettings.terrain );
    m_world.ApplyRuntimeSettings( m_runtimeSettings );
    m_colliderStore.ApplyPhysicsMaterial( m_physicsMaterial );
    for ( PhysicsBodyCreateDesc& desc : m_authoredBodyDescs )
    {
        ApplyAuthoredBodyPolicy( desc );
    }
}


PhysicsRuntimeSettings PhysicsEngine::RuntimeSettingsFromConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    PhysicsRuntimeSettings settings;
    settings.material.sphereDragCoefficient = config.physicsMaterial.sphereDragCoeff;
    settings.material.terrainFrictionCoefficient = config.physicsMaterial.frictionCoeff;
    settings.material.objectFrictionCoefficient = config.physicsMaterial.objectFrictionCoeff;
    settings.material.rollingFrictionCoefficient = config.physicsMaterial.rollingFrictionCoeff;
    settings.body.angularVelocityLimit = config.bodySimulation.velocityLimit;
    settings.body.contactRestitutionThreshold = config.bodySimulation.contactRestitutionThreshold;
    settings.body.contactEpsilon = config.bodySimulation.contactEpsilon;
    settings.solver.slop = config.persistentContactSolver.slop;
    settings.solver.baumgarteBeta = config.persistentContactSolver.baumgarteBeta;
    settings.solver.positionCorrectionPercent = config.persistentContactSolver.positionCorrectionPercent;
    settings.solver.iterations = config.persistentContactSolver.iterations;
    settings.terrain.threshold = config.terrainContact.threshold;
    settings.terrain.slop = config.terrainContact.slop;
    settings.terrain.baumgarteBeta = config.terrainContact.baumgarteBeta;
    settings.terrain.maxBaumgarteBias = config.terrainContact.maxBaumgarteBias;
    settings.sleep.linearSpeed = config.physicsSleep.linearSpeed;
    settings.sleep.angularSpeed = config.physicsSleep.angularSpeed;
    settings.sleep.frames = config.physicsSleep.frames;
    settings.broadphase.cellSize = config.broadphase.cellSize;
    settings.execution.parallel = config.physicsExecution.parallel;
    settings.execution.parallelApplyForces = config.physicsExecution.parallelApplyForces;
    settings.execution.parallelMutualGravity = config.physicsExecution.parallelMutualGravity;
    settings.execution.parallelNarrowphase = config.physicsExecution.parallelNarrowphase;
    settings.execution.parallelTerrainDetect = config.physicsExecution.parallelTerrainDetect;
    settings.execution.parallelIntegrate = config.physicsExecution.parallelIntegrate;
    settings.worldForces.gravity = config.worldForces.gravity;
    return settings;
}


void PhysicsEngine::ApplyAuthoredBodyPolicy( PhysicsBodyCreateDesc& desc ) const
{
    desc.friction = m_physicsMaterial.frictionCoefficient;
    desc.angularVelocityLimit = m_bodySimulationLimits.angularVelocityLimit;
    desc.contactEpsilon = m_contactPolicy.contactEpsilon;
    if ( BoundingSphere* sphere = std::get_if<BoundingSphere>( &desc.shape ) )
    {
        sphere->SetDragCoefficient( m_physicsMaterial.sphereDragCoefficient );
        desc.dragCoefficient = m_physicsMaterial.sphereDragCoefficient;
    }
}


void PhysicsEngine::ApplyAuthoredColliderPolicy( PhysicsColliderCreateDesc& desc ) const
{
    desc.friction = m_physicsMaterial.frictionCoefficient;
    if ( BoundingSphere* sphere = std::get_if<BoundingSphere>( &desc.shape ) )
    {
        sphere->SetDragCoefficient( m_physicsMaterial.sphereDragCoefficient );
        desc.dragCoefficient = m_physicsMaterial.sphereDragCoefficient;
    }
}


void PhysicsEngine::ReserveAuthoredBodyCapacity( std::size_t capacity )
{
    m_authoredBodyDescs.reserve( capacity );
    m_world.ReserveBodyScratchCapacity( capacity );
}


PhysicsAuthoredBodyCount PhysicsEngine::AuthoredBodyDescriptorCount() const
{
    PhysicsAuthoredBodyCount count;
    count.value = static_cast<uint32_t>( m_authoredBodyDescs.size() );
    return count;
}

bool PhysicsEngine::CanRegisterAuthoredBody( PhysicsAuthoredBodyCount expectedBodyCount ) const
{
    const std::size_t expected = static_cast<std::size_t>( expectedBodyCount.value );
    return m_authoredBodyDescs.size() == expected &&
           m_bodyStore.Count() == static_cast<int>( expectedBodyCount.value ) &&
           m_authoredBodyDescs.size() < m_authoredBodyDescs.capacity() &&
           expected < SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS;
}


bool PhysicsEngine::TrimAuthoredBodyDescriptorsToCount( PhysicsAuthoredBodyCount bodyCount )
{
    const std::size_t targetCount = static_cast<std::size_t>( bodyCount.value );
    if ( targetCount > m_authoredBodyDescs.size() )
    {
        return false;
    }
    m_authoredBodyDescs.erase( m_authoredBodyDescs.begin() + static_cast<std::ptrdiff_t>( targetCount ),
                               m_authoredBodyDescs.end() );
    return AuthoredBodyDescriptorCount().value == bodyCount.value;
}


void PhysicsEngine::Clear()
{
    m_world.Clear();
    m_authoredBodyDescs.clear();
    m_bodyStore.Clear();
    m_colliderStore.Clear();
}


bool PhysicsEngine::RefreshBodyStoreFromAuthoredDescriptors( const PhysicsAuthoredBodyRefreshView& refreshView )
{
    const std::size_t descriptorCount = m_authoredBodyDescs.size();
    if ( static_cast<std::size_t>( refreshView.bodyCount.value ) != descriptorCount ||
         ( descriptorCount > 0u &&
           ( !refreshView.sceneObjectIds || !refreshView.fixedTreeReleaseRoots || !refreshView.diagnosticNames ) ) )
    {
        return false;
    }

    std::vector<PhysicsBodyCreateDesc> bodyDescs;
    bodyDescs.reserve( descriptorCount );

    for ( int i = 0; i < static_cast<int>( descriptorCount ); ++i )
    {
        PhysicsBodyCreateDesc desc = m_authoredBodyDescs[static_cast<std::size_t>( i )];
        desc.sceneObjectId = refreshView.sceneObjectIds[static_cast<std::size_t>( i )];
        desc.fixedTreeReleaseRootIndex = refreshView.fixedTreeReleaseRoots[static_cast<std::size_t>( i )].value;
        desc.diagnosticName = refreshView.diagnosticNames[static_cast<std::size_t>( i )];
        ApplyAuthoredBodyPolicy( desc );
        bodyDescs.push_back( desc );
    }

    LoadBodyDescriptors( bodyDescs );
    return m_bodyStore.Count() == CountAsInt( AuthoredBodyDescriptorCount() );
}


void PhysicsEngine::LoadBodyDescriptors( const std::vector<PhysicsBodyCreateDesc>& bodyDescs )
{
    m_bodyStore.LoadFromDescriptors( bodyDescs, m_world.GetSleepStates() );
    m_world.InvalidateBodyTopology();
}


PhysicsAuthoredBodyRegistration PhysicsEngine::RegisterAuthoredBody( const PhysicsBodyCreateDesc& bodyDesc,
                                                                     PhysicsColliderCreateDesc colliderDesc )
{
    // Invariant: authored registration must never let an invalid variant reach
    // std::visit, whose exception-disabled failure otherwise loses the owning
    // subsystem and descriptor stage from captured automation logs.
    if ( bodyDesc.shape.valueless_by_exception() )
    {
        SB_FATAL( "Physics/PhysicsEngine", "Cannot register authored body: input collision shape is valueless." );
    }
    PhysicsBodyCreateDesc authoredDesc = bodyDesc;
    if ( authoredDesc.shape.valueless_by_exception() )
    {
        SB_FATAL( "Physics/PhysicsEngine", "Cannot register authored body: copied collision shape is valueless." );
    }
    ApplyAuthoredBodyPolicy( authoredDesc );
    m_authoredBodyDescs.push_back( authoredDesc );
    const PhysicsBodyHandle body = m_bodyStore.CreateBodyRecord( authoredDesc, m_world.IsPhysicsSleepEnabled() );
    const PhysicsBodyRecord* record = m_bodyStore.RecordForHandle( body );
    if ( !record )
    {
        m_authoredBodyDescs.pop_back();
        return {};
    }

    colliderDesc.body = body;
    colliderDesc.sceneObjectId = record->sceneObjectId;
    ApplyAuthoredColliderPolicy( colliderDesc );
    const PhysicsColliderHandle collider =
        m_colliderStore.CreateColliderRecord( MakeColliderRecordFromDesc( colliderDesc, *record ) );
    if ( !collider.IsValid() )
    {
        // Invariant: registration is all-or-nothing even if a future collider
        // capacity rule rejects after body append. Retiring the handle here
        // prevents a partial live body from escaping the physics boundary.
        (void)m_bodyStore.DestroyBodyRecord( body );
        m_authoredBodyDescs.pop_back();
        return {};
    }
    m_world.InvalidateBodyTopology();
    return { body, collider };
}


bool PhysicsEngine::DestroyAuthoredBody( PhysicsBodyHandle body )
{
    const int bodyRow = m_bodyStore.ModelIndexForHandle( body );
    const PhysicsColliderHandle collider = m_colliderStore.HandleForBodyHandle( body );
    if ( bodyRow < 0 || static_cast<std::size_t>( bodyRow ) >= m_authoredBodyDescs.size() || !collider.IsValid() ||
         !m_colliderStore.Contains( collider ) )
    {
        return false;
    }

    m_world.DestroyPointJointsForBody( body );
    if ( !m_colliderStore.DestroyColliderRecord( collider ) )
    {
        return false;
    }
    if ( !m_bodyStore.DestroyBodyRecord( body ) )
    {
        SB_FATAL( "Physics/PhysicsEngine", "Body destruction failed after paired collider removal." );
    }

    const std::size_t row = static_cast<std::size_t>( bodyRow );
    if ( row + 1u != m_authoredBodyDescs.size() )
    {
        m_authoredBodyDescs[row] = std::move( m_authoredBodyDescs.back() );
    }
    m_authoredBodyDescs.pop_back();
    m_world.InvalidateBodyTopology();
    return true;
}


bool PhysicsEngine::UpdateAuthoredBody( const PhysicsBodyUpdateDesc& update )
{
    const int bodyRow = m_bodyStore.ModelIndexForHandle( update.body );
    const PhysicsBodyRecord* body = m_bodyStore.RecordForHandle( update.body );
    if ( bodyRow < 0 || !body || bodyRow >= static_cast<int>( m_authoredBodyDescs.size() ) ||
         m_authoredBodyDescs.size() != static_cast<std::size_t>( m_bodyStore.Count() ) )
    {
        return false;
    }

    // Invariant: authored edits start from live store state. Solver movement
    // between tool commands must not be overwritten by an old cold descriptor.
    PhysicsBodyCreateDesc desc = m_authoredBodyDescs[static_cast<std::size_t>( bodyRow )];
    const PhysicsBodyHotFieldsConstView hotFields = m_bodyStore.HotFields();
    const std::size_t hotIndex = static_cast<std::size_t>( bodyRow );
    desc.sceneObjectId = body->sceneObjectId;
    desc.position = PhysicsBodyPosition( hotFields, hotIndex );
    desc.orientation = PhysicsBodyOrientation( hotFields, hotIndex );
    desc.linearVelocity = PhysicsBodyLinearVelocity( hotFields, hotIndex );
    desc.angularVelocity = PhysicsBodyAngularVelocity( hotFields, hotIndex );
    desc.rotationalInertia = body->rotationalInertia;
    desc.mass = body->mass;
    desc.motionKind = hotFields.fixed[hotIndex] != 0u ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic;
    desc.startsAsleep = hotFields.awake[hotIndex] == 0u;

    if ( update.updateMask & PHYSICS_BODY_UPDATE_POSE )
    {
        desc.position = update.position;
        desc.orientation = update.orientation;
    }
    if ( update.updateMask & PHYSICS_BODY_UPDATE_VELOCITY )
    {
        desc.linearVelocity = update.linearVelocity;
        desc.angularVelocity = update.angularVelocity;
    }
    if ( update.updateMask & PHYSICS_BODY_UPDATE_MASS )
    {
        desc.mass = update.mass;
        desc.rotationalInertia = update.rotationalInertia;
    }
    if ( update.updateMask & PHYSICS_BODY_UPDATE_MOTION_KIND )
    {
        desc.motionKind = update.motionKind;
    }
    if ( update.updateMask & PHYSICS_BODY_UPDATE_SLEEP_STATE )
    {
        desc.startsAsleep = update.sleeping;
    }
    if ( update.updateMask & PHYSICS_BODY_UPDATE_DIAGNOSTIC_NAME )
    {
        desc.diagnosticName = update.diagnosticName;
    }

    ApplyAuthoredBodyPolicy( desc );
    m_authoredBodyDescs[static_cast<std::size_t>( bodyRow )] = desc;
    m_bodyStore.RefreshRecordFromDescriptorAt( desc, bodyRow );
    if ( update.updateMask & PHYSICS_BODY_UPDATE_SLEEP_STATE )
    {
        if ( update.sleeping )
        {
            SeedBodyAsleep( update.body );
        }
        else
        {
            WakeBody( update.body );
        }
    }
    m_world.InvalidateBodyTopology();
    return true;
}


bool PhysicsEngine::UpdateAuthoredBodyAndCollider( const PhysicsBodyUpdateDesc& update,
                                                   PhysicsColliderCreateDesc colliderDesc )
{
    const PhysicsBodyRecord* body = m_bodyStore.RecordForHandle( update.body );
    const PhysicsColliderHandle collider = m_colliderStore.HandleForBodyHandle( update.body );
    const ColliderRecord* existingCollider = m_colliderStore.RecordForHandle( collider );
    if ( !body || !existingCollider )
    {
        return false;
    }

    colliderDesc.body = update.body;
    colliderDesc.sceneObjectId = body->sceneObjectId;
    ApplyAuthoredColliderPolicy( colliderDesc );
    if ( colliderDesc.contactMaterialName[0] == '\0' && existingCollider->contactMaterialName[0] != '\0' )
    {
        strncpy_s( colliderDesc.contactMaterialName,
                   sizeof( colliderDesc.contactMaterialName ),
                   existingCollider->contactMaterialName,
                   _TRUNCATE );
    }
    if ( !UpdateAuthoredBody( update ) )
    {
        return false;
    }

    body = m_bodyStore.RecordForHandle( update.body );
    if ( !body ||
         !m_colliderStore.UpdateRecordForHandle( collider, MakeColliderRecordFromDesc( colliderDesc, *body ) ) )
    {
        // Lane F: preflighted fixed-capacity rows disappearing during one
        // synchronous owner command is internal handle-map corruption.
        SB_FATAL( "Physics/PhysicsEngine", "Coordinated body/collider update lost a preflighted row." );
    }

    const int bodyRow = m_bodyStore.ModelIndexForHandle( update.body );
    PhysicsBodyCreateDesc& authored = m_authoredBodyDescs[static_cast<std::size_t>( bodyRow )];
    authored.shape = colliderDesc.shape;
    authored.boundingRadius = Math::CollisionDetection::GetShapeBoundingRadius( authored.shape );
    authored.volume = Math::CollisionDetection::GetShapeVolume( authored.shape );
    authored.projectedSurfaceArea = Math::CollisionDetection::GetShapeProjectedSurfaceArea( authored.shape );
    authored.dragCoefficient = Math::CollisionDetection::GetShapeDragCoefficient( authored.shape );
    authored.usesWorldInertia = !std::holds_alternative<BoundingSphere>( authored.shape );
    ApplyAuthoredBodyPolicy( authored );
    m_bodyStore.RefreshRecordFromDescriptorAt( authored, bodyRow );
    return true;
}


void PhysicsEngine::ClearPendingBodyImpulses()
{
    m_bodyStore.ClearPendingImpulses();
}


bool PhysicsEngine::TrimBodiesToCount( PhysicsBodyCount bodyCount )
{
    const bool trimmed = m_bodyStore.TrimToCount( CountAsInt( bodyCount ) );
    if ( trimmed )
    {
        m_world.InvalidateBodyTopology();
    }
    return trimmed;
}


bool PhysicsEngine::TrimCollidersToCount( PhysicsColliderCount colliderCount )
{
    return m_colliderStore.TrimToCount( CountAsInt( colliderCount ) );
}


bool PhysicsEngine::RestoreReplayBodyState( PhysicsBodyHandle body,
                                            PhysicsSceneObjectId sceneObjectId,
                                            bool fixed,
                                            const Math::Vector::Vector3& position,
                                            const Math::Orientation::Quaternion& orientation,
                                            const Math::Vector::Vector3& linearVelocity,
                                            const Math::Vector::Vector3& angularVelocity,
                                            float mass,
                                            float inverseMass,
                                            const Math::Vector::Vector3& rotationalInertia,
                                            const Math::Vector::Vector3& inverseRotationalInertia )
{
    const bool restored = m_bodyStore.RestoreReplayBodyState( body,
                                                              sceneObjectId,
                                                              fixed,
                                                              position,
                                                              orientation,
                                                              linearVelocity,
                                                              angularVelocity,
                                                              mass,
                                                              inverseMass,
                                                              rotationalInertia,
                                                              inverseRotationalInertia );
    if ( restored )
    {
        m_world.InvalidateBodyTopology();
    }
    return restored;
}


bool PhysicsEngine::RefreshColliderSnapshot()
{
    return m_colliderStore.RefreshBodyBindings( m_bodyStore );
}


#ifdef _DEBUG
void PhysicsEngine::ValidatePhysicsStoreMappings( int modelCount ) const
{
    assert( m_bodyStore.Count() == modelCount );
    assert( m_colliderStore.Count() == modelCount );

    const auto bodies = m_bodyStore.Records();
    const auto colliders = m_colliderStore.Records();
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const PhysicsBodyRecord& body = bodies[index];
        const ColliderRecord& collider = colliders[index];
        const PhysicsBodyHandle bodyHandle = m_bodyStore.HandleForModelIndex( i );
        const PhysicsColliderHandle colliderHandle = m_colliderStore.HandleForBodyHandle( bodyHandle );

        assert( bodyHandle.IsValid() );
        assert( colliderHandle.IsValid() );
        assert( body.handle == bodyHandle );
        assert( collider.handle == colliderHandle );
        assert( collider.body == bodyHandle );
        assert( m_bodyStore.ModelIndexForHandle( bodyHandle ) == i );
        assert( m_colliderStore.ModelIndexForHandle( colliderHandle ) == i );
        assert( body.sceneObjectId == collider.sceneObjectId );
    }
}
#endif


void PhysicsEngine::Step( float fChangeInTime,
                          const PhysicsWorldForces& worldForces,
                          Threading::WorkerPool& workerPool,
                          const char* const* diagnosticNames,
                          int diagnosticNameCount,
                          const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
{
    Step( fChangeInTime,
          worldForces,
          ExternalForceFrameInput{},
          workerPool,
          diagnosticNames,
          diagnosticNameCount,
          diagnosticsCsvWriter );
}


void PhysicsEngine::Step( float fChangeInTime,
                          const PhysicsWorldForces& worldForces,
                          const ExternalForceFrameInput& externalForces,
                          Threading::WorkerPool& workerPool,
                          const char* const* diagnosticNames,
                          int diagnosticNameCount,
                          const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
{
    m_lastWorldForces = worldForces;
    m_hasLastWorldForces = true;

    m_world.RunPhysics( m_bodyStore,
                        m_colliderStore,
                        fChangeInTime,
                        m_runtimeSettings,
                        worldForces,
                        externalForces,
                        workerPool );

    ApplyFixedTreeReleaseEvents( worldForces );

    m_world.EmitStepDiagnostics( m_bodyStore,
                                 m_colliderStore,
                                 fChangeInTime,
                                 diagnosticNames,
                                 diagnosticNameCount,
                                 diagnosticsCsvWriter );

    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
}


void PhysicsEngine::ApplyFixedTreeReleaseEvents( const PhysicsWorldForces& worldForces )
{
    const std::span<const PhysicsFixedTreeReleaseEvent> releaseEvents = m_world.GetFixedTreeReleaseEvents();
    if ( releaseEvents.empty() )
    {
        return;
    }

    // Why: fixed-tree release changes live simulation state, then wake
    // propagation may touch neighbouring bodies. Keep both operations on the
    // body store before Debug diagnostics or presentation sampling reads it.
    for ( const PhysicsFixedTreeReleaseEvent& event : releaseEvents )
    {
        m_bodyStore.ReleaseAttachedFixedTreeParts( event, m_fixedTreeReleaseWakeBodies );
        for ( int index : m_fixedTreeReleaseWakeBodies )
        {
            m_world.WakeModel( m_bodyStore, m_colliderStore, worldForces, index );
        }
    }
}


bool PhysicsEngine::ReleaseFixedBodyAndAttachedTreeParts( PhysicsBodyHandle sourceBody,
                                                          float releaseImpulseStrength,
                                                          const Math::Vector::Vector3& seedLinearVelocity,
                                                          const Math::Vector::Vector3& seedAngularVelocity )
{
    const int sourceIndex = m_bodyStore.ModelIndexForHandle( sourceBody );
    PhysicsBodyRecord* sourceRecord = m_bodyStore.MutableRecordForHandle( sourceBody );
    if ( sourceIndex < 0 || !sourceRecord )
    {
        return false;
    }

    bool sourceReleased = false;
    const PhysicsBodyHotFieldsConstView hotFields = m_bodyStore.HotFields();
    if ( hotFields.fixed[static_cast<std::size_t>( sourceIndex )] != 0u )
    {
        // Hazard: authored fixed props only become dynamic when their store
        // policy accepts the tool impulse. The source body receives the actual
        // launcher impulse separately, so its release preserves current velocity
        // while attached parts inherit the seeded breakaway velocity.
        if ( !sourceRecord->releasesFromFixedOnContact ||
             releaseImpulseStrength < sourceRecord->contactReleaseImpulseThreshold )
        {
            return false;
        }
        const Math::Vector::Vector3 sourceLinearVelocity =
            PhysicsBodyLinearVelocity( hotFields, static_cast<std::size_t>( sourceIndex ) );
        const Math::Vector::Vector3 sourceAngularVelocity =
            PhysicsBodyAngularVelocity( hotFields, static_cast<std::size_t>( sourceIndex ) );
        m_bodyStore.ReleaseFixedBody( sourceIndex, sourceLinearVelocity, sourceAngularVelocity );
        sourceReleased = true;
    }

    const PhysicsFixedTreeReleaseEvent event = { sourceIndex, seedLinearVelocity, seedAngularVelocity };
    m_bodyStore.ReleaseAttachedFixedTreeParts( event, m_fixedTreeReleaseWakeBodies );

    const auto wakeReleasedIndex = [&]( int index )
    {
        if ( m_hasLastWorldForces )
        {
            m_world.WakeModel( m_bodyStore, m_colliderStore, m_lastWorldForces, index );
        }
        else
        {
            m_world.WakeModel( m_bodyStore, index );
        }
    };

    if ( sourceReleased )
    {
        wakeReleasedIndex( sourceIndex );
    }
    for ( int index : m_fixedTreeReleaseWakeBodies )
    {
        wakeReleasedIndex( index );
    }
    if ( sourceReleased || !m_fixedTreeReleaseWakeBodies.empty() )
    {
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }
    return true;
}


// Invariant: steady-state wake commands mutate PhysicsBodyStore records
// selected by handles. Legacy model-index callers must refresh topology before
// they enter this handle-owned command.
void PhysicsEngine::WakeBody( PhysicsBodyHandle body )
{
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 )
    {
        return;
    }
    if ( m_hasLastWorldForces )
    {
        m_world.WakeModel( m_bodyStore, m_colliderStore, m_lastWorldForces, index );
    }
    else
    {
        m_world.WakeModel( m_bodyStore, index );
    }
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    // Why: wake is solver sleep/island state. Rebuilding render projection here
    // would add work without changing pose; the normal step boundary owns later
    // presentation updates that actually change body state.
}


bool PhysicsEngine::SetBodyVelocity( PhysicsBodyHandle body,
                                     const Math::Vector::Vector3& linearVelocity,
                                     const Math::Vector::Vector3& angularVelocity,
                                     bool wakeIfMoving )
{
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 || !m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity ) )
    {
        return false;
    }

    const bool shouldWake = wakeIfMoving && ( !linearVelocity.IsCloseToZero() || !angularVelocity.IsCloseToZero() );
    if ( shouldWake )
    {
        if ( m_hasLastWorldForces )
        {
            m_world.WakeModel( m_bodyStore, m_colliderStore, m_lastWorldForces, index );
        }
        else
        {
            m_world.WakeModel( m_bodyStore, index );
        }
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }

    // Invariant: callers that start from model indices perform any count-gated
    // topology refresh before resolving the handle. This command does not borrow
    // authoring state or reload same-count body rows on the edit path.
    return true;
}


void PhysicsEngine::SeedBodyAsleep( PhysicsBodyHandle body )
{
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 || !m_world.IsPhysicsSleepEnabled() )
    {
        return;
    }

    // Why: sleep seeding is solver state, not presentation. Seed both the
    // dense body store and PhysicsWorld's sleep counters, then leave
    // presentation projection to the next normal step boundary.
    if ( m_bodyStore.SeedBodyAsleep( body ) )
    {
        m_world.SeedModelAsleep( m_bodyStore, index );
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }
}


void PhysicsEngine::SetPendingBodyImpulse( PhysicsBodyHandle body,
                                           const Math::Vector::Vector3& impulse,
                                           const Math::Vector::Vector3& localApplicationPoint )
{
    // Why: initial authored/generated impulses are one-shot physics state.
    // Writing them into the body store avoids routing setup through the
    // collection-owned model-index command wrappers.
    m_bodyStore.SetPendingBodyImpulse( body, impulse, localApplicationPoint );
}


void PhysicsEngine::ApplyBodyImpulse( PhysicsBodyHandle body,
                                      const Math::Vector::Vector3& impulse,
                                      const Math::Vector::Vector3& localApplicationPoint )
{
    SetPendingBodyImpulse( body, impulse, localApplicationPoint );
    WakeBody( body );
}


void PhysicsEngine::SetSleepEnabled( bool enabled )
{
    m_world.SetPhysicsSleepEnabled( enabled );
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
}


bool PhysicsEngine::IsSleepEnabled() const
{
    return m_world.IsPhysicsSleepEnabled();
}


void PhysicsEngine::BeginCollisionVisualFrame( PhysicsBodyCount bodyCount )
{
    m_world.BeginCollisionVisualFrame( CountAsInt( bodyCount ) );
}


void PhysicsEngine::EndCollisionVisualFrame()
{
    m_world.EndCollisionVisualFrame();
}


void PhysicsEngine::ClearPointJointConstraints()
{
    m_world.ClearPointJointConstraints();
}


PhysicsConstraintHandle PhysicsEngine::CreatePointJoint( const PhysicsPointJointCreateDesc& desc )
{
    // Why: stale body handles should fail at the scene/store boundary before
    // the solver receives an append-only point-joint row.
    if ( !m_bodyStore.Contains( desc.bodyA ) || !m_bodyStore.Contains( desc.bodyB ) || desc.bodyA == desc.bodyB )
    {
        return PhysicsConstraintHandle{};
    }

    return m_world.CreatePointJoint( desc );
}


bool PhysicsEngine::UpdatePointJoint( const PhysicsPointJointUpdateDesc& desc )
{
    const bool updatesBodies = ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_BODIES ) != 0u;
    if ( updatesBodies &&
         ( !m_bodyStore.Contains( desc.bodyA ) || !m_bodyStore.Contains( desc.bodyB ) || desc.bodyA == desc.bodyB ) )
    {
        return false;
    }
    const bool updated = m_world.UpdatePointJoint( desc );
    if ( updated && updatesBodies )
    {
        m_world.InvalidateBodyTopology();
    }
    return updated;
}


bool PhysicsEngine::DestroyConstraint( PhysicsConstraintHandle constraint )
{
    const bool destroyed = m_world.DestroyConstraint( constraint );
    if ( destroyed )
    {
        m_world.InvalidateBodyTopology();
    }
    return destroyed;
}


PhysicsRayCastHit PhysicsEngine::RayCast( const PhysicsRayCastDesc& desc ) const
{
    PhysicsRayCastHit closestHit;
    if ( desc.maxDistance < 0.0f )
    {
        return closestHit;
    }

    const float directionLength = VectorMag( desc.direction );
    if ( directionLength == 0.0f )
    {
        return closestHit;
    }
    const Vector3 direction = desc.direction / directionLength;
    float closestDistance = ( std::numeric_limits<float>::max )();
    const auto hotFields = m_bodyStore.HotFields();

    // Concept: the public ray is a conservative store query, not a second
    // narrowphase. It returns stable physics identities and leaves exact
    // collision/contact generation to the shipping solver.
    for ( const ColliderRecord& collider : m_colliderStore.Records() )
    {
        const PhysicsBodyRecord* body = m_bodyStore.RecordForHandle( collider.body );
        const int bodyIndex = m_bodyStore.ModelIndexForHandle( collider.body );
        if ( !body || bodyIndex < 0 ||
             !BodyPassesQueryFilters( hotFields,
                                      static_cast<std::size_t>( bodyIndex ),
                                      desc.includeFixedBodies,
                                      desc.includeSleepingBodies,
                                      IsSleepEnabled() ) )
        {
            continue;
        }

        float distance = 0.0f;
        const Vector3 center = ColliderWorldCenter( hotFields, static_cast<std::size_t>( bodyIndex ), collider );
        if ( !IntersectRaySphere( desc.origin, direction, center, EffectiveColliderRadius( collider ), distance ) ||
             distance > desc.maxDistance || distance >= closestDistance )
        {
            continue;
        }

        closestDistance = distance;
        closestHit.body = body->handle;
        closestHit.collider = collider.handle;
        closestHit.sceneObjectId = collider.sceneObjectId.IsValid() ? collider.sceneObjectId : body->sceneObjectId;
        closestHit.distance = distance;
        closestHit.point = desc.origin + direction * distance;
        closestHit.normal = closestHit.point - center;
        const float normalLength = VectorMag( closestHit.normal );
        closestHit.normal = normalLength > 0.0f ? closestHit.normal / normalLength : -direction;
        closestHit.hit = true;
    }
    return closestHit;
}


PhysicsBroadphaseQueryResultView PhysicsEngine::QueryBroadphaseCells( const PhysicsBroadphaseCellQueryDesc& desc ) const
{
    m_broadphaseQueryScratch.clear();
    const auto bodies = m_bodyStore.Records();
    const auto hotFields = m_bodyStore.HotFields();
    const auto colliders = m_colliderStore.Records();
    for ( std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex )
    {
        const PhysicsBodyRecord& body = bodies[bodyIndex];
        if ( !BodyPassesQueryFilters( hotFields,
                                      bodyIndex,
                                      desc.includeFixedBodies,
                                      desc.includeSleepingBodies,
                                      IsSleepEnabled() ) )
        {
            continue;
        }

        bool overlaps = hotFields.boundingRadius[bodyIndex] > 0.0f &&
                        SphereOverlapsAabb( PhysicsBodyPosition( hotFields, bodyIndex ),
                                            hotFields.boundingRadius[bodyIndex],
                                            desc.min,
                                            desc.max );
        for ( const ColliderRecord& collider : colliders )
        {
            if ( overlaps )
            {
                break;
            }
            if ( collider.body == body.handle )
            {
                overlaps = SphereOverlapsAabb( ColliderWorldCenter( hotFields, bodyIndex, collider ),
                                               EffectiveColliderRadius( collider ),
                                               desc.min,
                                               desc.max );
            }
        }
        if ( overlaps )
        {
            m_broadphaseQueryScratch.push_back( body.handle );
        }
    }

    PhysicsBroadphaseQueryResultView view;
    view.bodies = m_broadphaseQueryScratch.empty() ? nullptr : m_broadphaseQueryScratch.data();
    view.bodyCount = static_cast<uint32_t>( m_broadphaseQueryScratch.size() );
    return view;
}


void PhysicsEngine::CaptureReplaySolverSnapshot( PhysicsSolverSnapshot& outSnapshot, PhysicsBodyCount bodyCount ) const
{
    m_world.CaptureReplaySolverSnapshot( outSnapshot, CountAsInt( bodyCount ) );
}


bool PhysicsEngine::RestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, PhysicsBodyCount bodyCount )
{
    const bool restored = m_world.RestoreReplaySolverSnapshot( snapshot, CountAsInt( bodyCount ) );
    if ( restored )
    {
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }
    return restored;
}


SkullbonezCore::Physics::PhysicsDiagnosticsView PhysicsEngine::GetDiagnosticsView() const
{
    return m_world.GetDiagnosticsView();
}

uint64_t PhysicsEngine::CollectPhysicsWorldMemoryBytes() const
{
    return m_world.CollectMemoryBytes();
}


uint64_t PhysicsEngine::CollectDebugAndBroadphaseMemoryBytes() const
{
    return m_world.CollectDebugAndBroadphaseMemoryBytes();
}


bool PhysicsEngine::ShouldEmitStepDiagnostics() const
{
    return m_world.ShouldEmitStepDiagnostics();
}


bool PhysicsEngine::ShouldEmitCollisionTimeDiagnostics() const
{
    return m_world.ShouldEmitCollisionTimeDiagnostics();
}

const PhysicsBodyStore& PhysicsEngine::ReadBodies( const PhysicsEngine& engine )
{
    return engine.m_bodyStore;
}


const ColliderStore& PhysicsEngine::ReadColliders( const PhysicsEngine& engine )
{
    return engine.m_colliderStore;
}


const SkullbonezCore::Math::CollisionDetection::SpatialGrid&
PhysicsEngine::ReadSpatialGrid( const PhysicsEngine& engine )
{
    return engine.m_world.GetSpatialGrid();
}


std::span<const int> PhysicsEngine::ReadFixedContactHighlightBodies( const PhysicsEngine& engine )
{
    return engine.m_world.GetFixedContactHighlightBodies();
}


const std::vector<int64_t>& PhysicsEngine::ReadCollisionCellKeys( const PhysicsEngine& engine )
{
    return engine.m_world.GetCollisionCellKeys();
}


const std::vector<uint8_t>& PhysicsEngine::ReadCollisionVisualContacts( const PhysicsEngine& engine )
{
    return engine.m_world.GetCollisionVisualContacts();
}


std::span<const uint8_t> PhysicsEngine::ReadSleepStates( const PhysicsEngine& engine )
{
    return engine.m_world.GetSleepStates();
}


std::span<const int> PhysicsEngine::ReadSleepIslandVisualIds( const PhysicsEngine& engine )
{
    return engine.m_world.GetSleepIslandVisualIds();
}


std::span<const uint8_t> PhysicsEngine::ReadSleepSupportedStates( const PhysicsEngine& engine )
{
    return engine.m_world.GetSleepSupportedStates();
}


std::span<const uint8_t> PhysicsEngine::ReadSleepInhibitedStates( const PhysicsEngine& engine )
{
    return engine.m_world.GetSleepInhibitedStates();
}


const std::vector<PhysicsDebugContact>& PhysicsEngine::ReadDebugContacts( const PhysicsEngine& engine )
{
    return engine.m_world.GetPhysicsDebugContacts();
}


const std::vector<PhysicsPipelineRecord>& PhysicsEngine::ReadPipelineTrace( const PhysicsEngine& engine )
{
    return engine.m_world.GetPhysicsPipelineTrace();
}


const std::vector<PointJointConstraint>& PhysicsEngine::ReadPointJointConstraints( const PhysicsEngine& engine )
{
    return engine.m_world.GetPointJointConstraints();
}

#ifdef _DEBUG
void PhysicsEngine::SetPhysicsRegressionLogPath( const char* path )
{
    m_world.SetPhysicsRegressionLogPath( path );
}


void PhysicsEngine::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_world.SetPhysicsCollisionTimeLogPath( path );
}


void PhysicsEngine::SetPhysicsDiagnosticsPath( const char* path )
{
    m_world.SetPhysicsDiagnosticsPath( path );
}


void PhysicsEngine::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_world.SetPhysicsDiagnosticsRunId( runId );
}


bool PhysicsEngine::SetDiagnosticsSuppressed( bool suppressed )
{
    return m_world.SetDiagnosticsSuppressed( suppressed );
}
#endif
