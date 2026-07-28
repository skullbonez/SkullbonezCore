/*
File: SkullbonezSource/Physics/PersistentContactSolver.cpp
Purpose:
  Solves object/object and object/terrain persistent contact rows.

Summary:
  PersistentContactSolver.cpp solves object/object and object/terrain
  persistent contact rows. As an implementation unit, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below. The guarded transaction implements phase
  transitions, solver-body arithmetic, and the four row-construction phases;
  Solve still sequences the later solve, publication, and cache phases.

Glossary:
  OBB (Oriented Bounding Box): Box with rotation, used for exact object-space
  collision tests.
  PGS (Projected Gauss-Seidel): Iterative constraint-solver method used for
  bounded contact impulses.
  Warm starting: Reusing an estimated previous support impulse so resting
  contacts do not have to rediscover the full normal force from zero each tick.
  Sleep: Optimization that stops simulating stable bodies until collision,
  movement, or explicit gameplay input wakes them.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Contact row: One solver constraint created from a manifold point and solved
  by the Projected Gauss-Seidel loop.
  Restitution: Bounce response that restores closing speed along the contact
    normal after impact.
  Friction: Tangent impulse that resists sliding along the contact plane.
  Feature ID: Deterministic contact identifier used to match rows across frames

    for warm starting.
  Resting footprint: Stable multi-point support patch that can seed sleep and
  cached support impulses.
  Step policy: Once-per-solve normalized view of authored contact bounds used
  by both object and terrain rows.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Contact setting clamps resolve once before row construction and iteration.
  - Every solve phase crosses the transaction cursor before its pass body runs.

Related:
  - SkullbonezSource/Physics/PersistentContactSolver.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "PersistentContactSolver.h"

#include "ContactSolverCommon.h"
#include "ColliderStore.h"
#include "ObjectContactManifold.h"
#include "PhysicsBodyStore.h"
#include "SleepIslandSystem.h"
#include "Stages/PhysicsContactSolverStage.h"
#include "Stages/PhysicsStepDiagnostics.h"
#include "PhysicsWorldForces.h"
#include "../Core/FatalError.h"
#include "../Core/Profiler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <type_traits>

using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
namespace Math = SkullbonezCore::Math;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr int TERRAIN_BODY_INDEX = -1;
constexpr float TERRAIN_RESTING_SUPPORT_SEED_SCALE = 1.0f;
constexpr float TERRAIN_SHORELINE_SUPPORT_SEED_SCALE = 0.35f;
} // namespace

PersistentContactSolverStepPolicy
PhysicsContactSolverStage::ResolveStepPolicy( const PhysicsRuntimeSettings& settings,
                                              const PhysicsWorldForces& worldForces ) noexcept
{

    // Invariant: these are the historical use-site guards, collected without
    // changing their bounds so every row in the solve shares one interpretation.
    PersistentContactSolverStepPolicy policy;
    policy.objectSlop = (std::max)( 0.0f, settings.solver.slop );
    policy.objectBaumgarteBeta = (std::max)( 0.0f, settings.solver.baumgarteBeta );
    policy.objectPositionCorrectionPercent = (std::max)( 0.0f,
                                                         (std::min)( settings.solver.positionCorrectionPercent, 1.0f ) );

    policy.terrainSlop = (std::max)( 0.0f, settings.terrain.slop );
    policy.terrainBaumgarteBeta = (std::max)( 0.0f, settings.terrain.baumgarteBeta );
    policy.maxBaumgarteBias = (std::max)( 0.0f, settings.terrain.maxBaumgarteBias );
    policy.elasticCollisions = worldForces.mutualGravity.enabled && worldForces.mutualGravity.elasticCollisions;
    policy.rawContactRestitutionThreshold = settings.body.contactRestitutionThreshold;
    policy.contactRestitutionThreshold = policy.elasticCollisions ? 0.0f : policy.rawContactRestitutionThreshold;
    policy.objectFrictionCoefficient = policy.elasticCollisions ? 0.0f : settings.material.objectFrictionCoefficient;
    policy.terrainFrictionCoefficient = settings.material.terrainFrictionCoefficient;
    policy.rollingFrictionCoefficient = (std::max)( 0.0f, settings.material.rollingFrictionCoefficient );
    policy.sleepLinearSpeed = settings.sleep.linearSpeed;
    policy.sleepAngularSpeed = settings.sleep.angularSpeed;
    policy.nonNegativeSleepLinearSpeed = (std::max)( 0.0f, settings.sleep.linearSpeed );
    policy.nonNegativeSleepAngularSpeed = (std::max)( 0.0f, settings.sleep.angularSpeed );
    policy.gravityMagnitude = fabsf( settings.worldForces.gravity );
    policy.contactEpsilon = settings.body.contactEpsilon;
    policy.iterations = (std::max)( 1, settings.solver.iterations );
    return policy;
}

void PersistentContactSolveTransaction::AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase next,
                                                        const char* operation )
{
    const PersistentContactSolvePhaseCursor::Phase current = m_phase.Current();

    if ( !m_phase.TryAdvance( next ) )
    {

        // Lane F: running a contact phase twice, backward, or after skipping a
        // predecessor can publish rows, velocities, or cache state from a
        // partially solved fixed tick.
        SB_FATAL( "Physics/PersistentContactSolveTransaction", "Illegal phase transition. operation=%s current=%u next=%u",
                  operation, static_cast<unsigned int>( current ), static_cast<unsigned int>( next ) );
    }
}

void PersistentContactSolveTransaction::BeginEntryPolicySetup()
{

    if ( m_phase.Current() == PersistentContactSolvePhaseCursor::Phase::Complete && !m_phase.ResetAfterComplete() )
    {
        SB_FATAL( "Physics/PersistentContactSolveTransaction", "Completed solve cursor could not reset." );
    }

    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::EntryPolicySetup, "BeginEntryPolicySetup" );
}

void PersistentContactSolveTransaction::BeginSolveRows()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::SolveRows, "BeginSolveRows" );
}

void PersistentContactSolveTransaction::BeginPointSupportInstability()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::PointSupportInstability, "BeginPointSupportInstability" );
}

void PersistentContactSolveTransaction::BeginTerrainRestPolicy()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::TerrainRestPolicy, "BeginTerrainRestPolicy" );
}

void PersistentContactSolveTransaction::BeginWriteBack()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::WriteBack, "BeginWriteBack" );
}

void PersistentContactSolveTransaction::BeginDebugContacts()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::DebugContacts, "BeginDebugContacts" );
}

void PersistentContactSolveTransaction::BeginPositionCorrection()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::PositionCorrection, "BeginPositionCorrection" );
}

void PersistentContactSolveTransaction::BeginCacheStore()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::CacheStore, "BeginCacheStore" );
}

void PersistentContactSolveTransaction::BeginFixedContactRelease()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::FixedContactRelease, "BeginFixedContactRelease" );
}

void PersistentContactSolveTransaction::Complete()
{
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::Complete, "Complete" );
}

// CATTO REF:
//   Catto 2005, PDF pp. 18-19, Section 8.1/8.2 and Algorithm 5 store lambda
//   with a contact identifier and retrieve it for matching contacts next
//   frame.
// ENGINE-SPECIFIC:
//   This key is a compact pair+feature id. Full 32-bit feature IDs are kept so
//   authored hull face/edge identifiers are not truncated before warm starting.
//   The shared 15-bit body mask leaves 32 bits for the feature and one high kind
//   bit for terrain rows.
int64_t PersistentContactSolveTransaction::MakeKey( int bodyA, int bodyB, uint32_t featureId )
{

    if ( bodyB == TERRAIN_BODY_INDEX )
    {
        const uint64_t packed = ( 1ull << 62 ) |
                                ( ( static_cast<uint64_t>( static_cast<uint32_t>( bodyA ) ) & PERSISTENT_CONTACT_BODY_MASK )
                                  << 32 ) |
                                static_cast<uint64_t>( featureId );

        return static_cast<int64_t>( packed );
    }

    const int lo = ( bodyA < bodyB ) ? bodyA : bodyB;
    const int hi = ( bodyA < bodyB ) ? bodyB : bodyA;
    const uint64_t packed = ( ( static_cast<uint64_t>( static_cast<uint32_t>( lo ) ) & PERSISTENT_CONTACT_BODY_MASK )
                              << 47 ) |
                            ( ( static_cast<uint64_t>( static_cast<uint32_t>( hi ) ) & PERSISTENT_CONTACT_BODY_MASK )
                              << 32 ) |
                            static_cast<uint64_t>( featureId );

    return static_cast<int64_t>( packed );
}

bool PersistentContactSolveTransaction::HasCachedImpulse( const PersistentContactCacheList& cache, int bodyA, int bodyB,
                                                          uint32_t featureId )
{
    const int64_t key = MakeKey( bodyA, bodyB, featureId );
    const auto cachedIt = std::lower_bound( cache.begin(), cache.end(), key,
                                            []( const PersistentContactCacheEntry& entry, int64_t lookupKey )
                                            { return entry.key < lookupKey; } );

    if ( cachedIt == cache.end() || cachedIt->key != key )
    {
        return false;
    }

    return cachedIt->accN > 0.0f || fabsf( cachedIt->accT1 ) > TOLERANCE || fabsf( cachedIt->accT2 ) > TOLERANCE;
}

float PersistentContactSolveTransaction::ConservativeContactRadius( const ColliderRecord& collider )
{

    // Broadphase radii must include any local shape offset. If a shape is not
    // centered on the body origin, the conservative sphere reaches the farthest
    // shifted point.
    const CollisionShapeReference& shape = collider.shape;
    float radius = GetShapeBoundingRadius( shape );
    const Vector3& offset = GetShapePosition( shape );
    const float offsetSq = Vector::VectorMagSquared( offset );

    if ( offsetSq > TOLERANCE * TOLERANCE )
    {
        radius += sqrtf( offsetSq );
    }

    return radius;
}

// CATTO REF:
//   Catto 2005, PDF p. 12, Section 5, unnumbered inertia transform before
//   Equations 26-28: I_world^-1 = R * I_body^-1 * R^T.
// Inertia is rotational mass. Boxes need world-space inertia because their
// local inertia axes rotate with orientation; spheres remain isotropic.
Vector3 PersistentContactSolveTransaction::ApplyInverseInertia( int bodyIndex, const Vector3& value ) const
{

    if ( bodyIndex == TERRAIN_BODY_INDEX )
    {
        return ZERO_VECTOR;
    }

    const SolverBodyState& body = Body( static_cast<std::size_t>( bodyIndex ) );

    if ( !body.useWorldInertia )
    {
        return Vector::VectorMultiply( body.invInertia, value );
    }

    const Vector3 bodyValue = body.orientation.TransposeMultiply( value );
    return body.orientation * Vector::VectorMultiply( body.invInertia, bodyValue );
}

// CATTO REF:
//   Catto 2005, PDF p. 5, Section 3.3, Equation 7 says constraint forces are
//   Fc = J^T*lambda. PDF p. 8, Algorithm 2 shows accumulating those row
//   contributions into body force/torque blocks.
// Why: applying one impulse changes linear velocity by invMass*impulse and
// angular velocity by I^-1*(r cross impulse). Body A receives the equal and
// opposite impulse from body B.
void PersistentContactSolveTransaction::ApplyImpulse( const PersistentContact& contact, const Vector3& impulse )
{
    SolverBodyState& bodyA = Body( static_cast<std::size_t>( contact.bodyA ) );
    bodyA.linearVelocity -= impulse * bodyA.invMass;
    bodyA.angularVelocity -= ApplyInverseInertia( contact.bodyA, Vector::CrossProduct( contact.rA, impulse ) );

    if ( contact.bodyB != TERRAIN_BODY_INDEX )
    {
        SolverBodyState& bodyB = Body( static_cast<std::size_t>( contact.bodyB ) );
        bodyB.linearVelocity += impulse * bodyB.invMass;
        bodyB.angularVelocity += ApplyInverseInertia( contact.bodyB, Vector::CrossProduct( contact.rB, impulse ) );
    }
}

void PersistentContactSolveTransaction::SetupBodies( const PhysicsBodyStore& bodyStore, std::span<const uint8_t> sleepState,
                                                     int modelCount, Core::Profiler* profiler )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/BodySetup" );
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::BodySetup, "SetupBodies" );
    ResetBodies( static_cast<std::size_t>( modelCount ) );

    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsConstView hotRead = bodyStore.HotFields();

    // CATTO REF:
    //   Catto 2005, PDF p. 7, Algorithms 1-2 and PDF p. 16, Algorithm 4 work on
    //   sparse body velocity blocks. Algorithm 4 names the mutable velocity-like
    //   work vector "a".
    // ENGINE-SPECIFIC:
    //   We keep compact per-body solver state here and write back once after PGS.
    //   That preserves Catto's sparse-row shape while avoiding repeated
    //   body-store writes inside the row loop.

    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        const PhysicsBodyRecord& record = bodyRecords[bodyIndex];
        SolverBodyState& body = Body( bodyIndex );

        if ( sleepState[bodyIndex] || hotRead.fixed[bodyIndex] != 0u )
        {

            // Sleeping bodies still provide persistent support to awake bodies,
            // but they behave as static anchors until deliberately woken.
            body.linearVelocity = ZERO_VECTOR;
            body.angularVelocity = ZERO_VECTOR;
            body.invMass = 0.0f;
            body.invInertia = ZERO_VECTOR;
            body.useWorldInertia = false;
        }
        else
        {
            body.linearVelocity = PhysicsBodyLinearVelocity( hotRead, bodyIndex );
            body.angularVelocity = PhysicsBodyAngularVelocity( hotRead, bodyIndex );
            body.invMass = hotRead.inverseMass[bodyIndex];
            body.invInertia = PhysicsBodyInverseInertia( hotRead, bodyIndex );
            body.useWorldInertia = record.usesWorldInertia;
        }

        if ( body.useWorldInertia )
        {
            body.orientation = PhysicsBodyOrientation( hotRead, bodyIndex ).GetOrientationMatrix();
        }
    }
}

void PersistentContactSolveTransaction::BuildManifolds( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                                                        const ColliderStore& colliderStore,
                                                        const PersistentContactSolverStepPolicy& stepPolicy,
                                                        std::span<const std::pair<int, int>> candidatePairs,
                                                        std::span<const uint8_t> sleepState,
                                                        PhysicsCandidatePairList& sleepSupportEdges, int modelCount,
                                                        std::size_t pipelineRecordCapacity, Core::Profiler* profiler )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds" );
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::BuildManifolds, "BuildManifolds" );

    const PhysicsBodyHotFieldsConstView hotRead = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    auto isFixedBody = [&]( int index ) -> bool { return hotRead.fixed[static_cast<std::size_t>( index )] != 0u; };
    auto canRecordPipeline = [&]() { return stage.m_sideEffects.pipelineRecords.size() < pipelineRecordCapacity; };
    auto recordPipeline = [&]( const PhysicsPipelineRecord& record )
    {

        if ( stage.m_sideEffects.pipelineRecords.size() < pipelineRecordCapacity )
        {
            stage.m_sideEffects.pipelineRecords.push_back( record );
        }
    };
    auto contactBodyViewForIndex = [&]( int index ) -> ObjectContactBodyView
    {

        // Why: object manifolds need only pose plus shape. Pose comes from
        // PhysicsBodyStore, while ColliderStore owns the per-kind shape payload
        // borrowed by its collider row; row construction never needs a mutable
        // scene object.
        const std::size_t bodyIndex = static_cast<std::size_t>( index );
        ObjectContactBodyView view;
        view.position = PhysicsBodyPosition( hotRead, bodyIndex );
        view.orientation = PhysicsBodyOrientation( hotRead, bodyIndex );
        return view;
    };
    auto appendSleepSupportEdge = [&]( int aIndex, int bIndex, const Vector3& normal, bool canSeedSupport )
    {
        constexpr float supportNormalY = 0.25f;

        if ( !canSeedSupport )
        {
            return;
        }

        // This records only a possible vertical support relationship. It does
        // not grant sleep support by itself; support must propagate later from
        // terrain or a body that already passed the full sleep gate. That keeps
        // mid-air object-object impacts from becoming false "grounded" evidence.

        if ( normal.y > supportNormalY )
        {
            AppendSleepSupportEdge( sleepSupportEdges, aIndex, bIndex );

            if ( canRecordPipeline() )
            {
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::SleepSupportEdge;
                record.bodyA = aIndex;
                record.bodyB = bIndex;
                record.normal = normal;
                record.point = ( PhysicsBodyPosition( hotRead, static_cast<std::size_t>( aIndex ) ) +
                                 PhysicsBodyPosition( hotRead, static_cast<std::size_t>( bIndex ) ) ) *
                               0.5f;

                record.scalarA = normal.y;
                recordPipeline( record );
            }
        }
        else if ( normal.y < -supportNormalY )
        {
            AppendSleepSupportEdge( sleepSupportEdges, bIndex, aIndex );

            if ( canRecordPipeline() )
            {
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::SleepSupportEdge;
                record.bodyA = bIndex;
                record.bodyB = aIndex;
                record.normal = -normal;
                record.point = ( PhysicsBodyPosition( hotRead, static_cast<std::size_t>( aIndex ) ) +
                                 PhysicsBodyPosition( hotRead, static_cast<std::size_t>( bIndex ) ) ) *
                               0.5f;

                record.scalarA = -normal.y;
                recordPipeline( record );
            }
        }
    };

    // Concept: contact row reduction is a resting-footprint optimization.
    //
    // The full manifold is still authoritative geometry. Reduction only chooses
    // which stable points become solver rows this tick, after broadphase and
    // exact narrowphase have agreed the pair is touching. Determinism still comes
    // from feature IDs and fixed ordering, and the physics baselines remain the
    // validation contract for any behavior drift.
    auto objectContactRowsAreQuiet = [&]( int bodyA, int bodyB, const ObjectContactManifold& manifold ) -> bool
    {
        const SolverBodyState& solverA = Body( static_cast<std::size_t>( bodyA ) );

        const SolverBodyState& solverB = Body( static_cast<std::size_t>( bodyB ) );
        const float linearLimit = (std::max)( stepPolicy.sleepLinearSpeed * 2.0f,
                                              stepPolicy.rawContactRestitutionThreshold * 0.25f );

        const float linearLimitSq = linearLimit * linearLimit;

        for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
        {
            const ObjectContactPoint& point = manifold.points[pointIndex];
            const Vector3 velA = solverA.linearVelocity + Vector::CrossProduct( solverA.angularVelocity, point.rA );
            const Vector3 velB = solverB.linearVelocity + Vector::CrossProduct( solverB.angularVelocity, point.rB );

            if ( Vector::VectorMagSquared( velB - velA ) > linearLimitSq )
            {
                return false;
            }
        }

        const float angularLimit = (std::max)( stepPolicy.sleepAngularSpeed * 2.0f, 0.25f );
        const float angularLimitSq = angularLimit * angularLimit;
        return Vector::VectorMagSquared( solverA.angularVelocity ) <= angularLimitSq &&
               Vector::VectorMagSquared( solverB.angularVelocity ) <= angularLimitSq;
    };
    auto reduceObjectContactRows = [&]( int bodyA, int bodyB, const ObjectContactManifold& manifold,
                                        uint8_t* selectedPointIndices ) -> uint8_t
    {
        auto betterPenetrationTie = [&]( int lhs, int rhs ) -> bool
        {

            if ( rhs < 0 )
            {
                return true;
            }

            const ObjectContactPoint& lhsPoint = manifold.points[lhs];
            const ObjectContactPoint& rhsPoint = manifold.points[rhs];

            if ( fabsf( lhsPoint.penetration - rhsPoint.penetration ) > 1.0e-5f )
            {
                return lhsPoint.penetration > rhsPoint.penetration;
            }

            return lhsPoint.featureId < rhsPoint.featureId;
        };

        int deepest = -1;

        for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
        {

            if ( betterPenetrationTie( pointIndex, deepest ) )
            {
                deepest = pointIndex;
            }
        }

        if ( deepest < 0 )
        {
            return 0;
        }

        uint8_t cachedPointCount = 0;

        for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
        {

            if ( HasCachedImpulse( stage.m_persistentContactCache, bodyA, bodyB, manifold.points[pointIndex].featureId ) )
            {
                ++cachedPointCount;
            }
        }

        if ( cachedPointCount < 2 )
        {
            return manifold.pointCount;
        }

        int secondary = -1;
        bool secondaryUsesCache = false;
        float bestDistanceSq = -1.0f;
        const ObjectContactPoint& primaryPoint = manifold.points[deepest];

        for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
        {

            if ( pointIndex == deepest )
            {
                continue;
            }

            const ObjectContactPoint& candidate = manifold.points[pointIndex];
            const Vector3 pointDelta = candidate.point - primaryPoint.point;
            const float normalDistance = Dot( pointDelta, manifold.normal );
            const Vector3 tangentDelta = pointDelta - manifold.normal * normalDistance;
            const float tangentDistanceSq = Vector::VectorMagSquared( tangentDelta );
            constexpr float duplicatePointDistanceSq = 1.0e-6f;

            if ( tangentDistanceSq <= duplicatePointDistanceSq )
            {
                continue;
            }

            const bool usesCache = HasCachedImpulse( stage.m_persistentContactCache, bodyA, bodyB, candidate.featureId );
            bool replace = usesCache && !secondaryUsesCache;

            if ( usesCache == secondaryUsesCache )
            {
                replace = tangentDistanceSq > bestDistanceSq + 1.0e-5f;

                if ( !replace && fabsf( tangentDistanceSq - bestDistanceSq ) <= 1.0e-5f )
                {
                    replace = betterPenetrationTie( pointIndex, secondary );
                }
            }

            if ( replace )
            {
                secondary = pointIndex;
                secondaryUsesCache = usesCache;
                bestDistanceSq = tangentDistanceSq;
            }
        }

        if ( secondary < 0 )
        {
            return manifold.pointCount;
        }

        selectedPointIndices[0] = static_cast<uint8_t>( deepest );
        selectedPointIndices[1] = static_cast<uint8_t>( secondary );

        if ( selectedPointIndices[1] < selectedPointIndices[0] )
        {
            std::swap( selectedPointIndices[0], selectedPointIndices[1] );
        }

        return 2;
    };

    // CATTO REF:
    //   Catto 2005, PDF p. 9, Section 4 "Contact Model" and Equation 16 require
    //   a contact point, a normal, and separation/penetration for each row.
    // ENGINE-SPECIFIC:
    //   Broadphase still uses conservative bounding radii, but the authoritative
    //   object contact geometry now comes from shape-pair manifolds: exact
    //   sphere/sphere, closest-point sphere contacts, and SAT/clipped box or
    //   convex-hull contacts.
    // First pass: turn broadphase candidate pairs into Catto-style contact rows.
    // Most manifold points become one persistent row each. Quiet multi-point
    // object footprints can use a two-point subset because spread plus cached
    // feature IDs keep the support plane stable while cutting solver work.

    for ( const auto& cp : candidatePairs )
    {
        int aIndex = cp.first;
        int bIndex = cp.second;

        if ( aIndex == bIndex || aIndex < 0 || bIndex < 0 || aIndex >= modelCount || bIndex >= modelCount ||
             ( sleepState[aIndex] && sleepState[bIndex] ) || ( isFixedBody( aIndex ) && isFixedBody( bIndex ) ) )
        {
            continue;
        }

        if ( bIndex < aIndex )
        {
            std::swap( aIndex, bIndex );
        }

        const ColliderRecord& colliderA = colliderRecords[static_cast<std::size_t>( aIndex )];
        const ColliderRecord& colliderB = colliderRecords[static_cast<std::size_t>( bIndex )];
        const ObjectContactBodyView bodyA = contactBodyViewForIndex( aIndex );
        const ObjectContactBodyView bodyB = contactBodyViewForIndex( bIndex );
        Vector3 centerDelta = PhysicsBodyPosition( hotRead, static_cast<std::size_t>( bIndex ) ) -
                              PhysicsBodyPosition( hotRead, static_cast<std::size_t>( aIndex ) );

        float contactDistance = ConservativeContactRadius( colliderA ) + ConservativeContactRadius( colliderB ) +
                                stepPolicy.contactEpsilon;

        if ( Vector::VectorMagSquared( centerDelta ) > contactDistance * contactDistance )
        {
            continue;
        }

        Vector3 contactNormal = ZERO_VECTOR;
        bool hasContact = false;
        bool hasRestingFootprint = true;
        ObjectContactManifold manifold;
        bool manifoldBuilt = false;
        {
            PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds/ExactObjectManifold" );
            manifoldBuilt = BuildObjectContactManifold( profiler, bodyA, colliderA.shape, bodyB, colliderB.shape, aIndex,
                                                        bIndex, stepPolicy.contactEpsilon, manifold );
        }

        if ( manifoldBuilt )
        {
            PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds/AddRows" );
            contactNormal = manifold.normal;
            const CollisionShapeReference& shapeA = colliderA.shape;
            const CollisionShapeReference& shapeB = colliderB.shape;
            const bool shapeAIsBox = GetShapeIf<BoundingBox>( &shapeA ) != nullptr;
            const bool shapeBIsBox = GetShapeIf<BoundingBox>( &shapeB ) != nullptr;
            const bool shapeAIsConvexHull = GetShapeIf<ConvexHullShape>( &shapeA ) != nullptr;
            const bool shapeBIsConvexHull = GetShapeIf<ConvexHullShape>( &shapeB ) != nullptr;
            const bool hasConvexHull = shapeAIsConvexHull || shapeBIsConvexHull;
            const bool hasSphere = GetShapeIf<BoundingSphere>( &shapeA ) || GetShapeIf<BoundingSphere>( &shapeB );
            const bool sameShapeFaceFootprint = ( shapeAIsBox && shapeBIsBox ) ||
                                                ( shapeAIsConvexHull && shapeBIsConvexHull );

            bool boxHasOnlyEdgeSupport = false;

            if ( !hasSphere && manifold.pointCount <= 2 && fabsf( manifold.normal.y ) > 0.25f )
            {
                const int supportedIndex = manifold.normal.y > 0.0f ? bIndex : aIndex;
                const bool supportedBodyIsBox = manifold.normal.y > 0.0f ? shapeBIsBox : shapeAIsBox;

                if ( supportedBodyIsBox )
                {
                    const auto rotation = PhysicsBodyOrientation( hotRead, static_cast<std::size_t>( supportedIndex ) )
                                              .GetOrientationMatrix();

                    const Vector3 supportNormal = manifold.normal.y > 0.0f ? manifold.normal : -manifold.normal;
                    const float faceDotX = fabsf( Dot( ( rotation * Vector3( 1.0f, 0.0f, 0.0f ) ), supportNormal ) );
                    const float faceDotY = fabsf( Dot( ( rotation * Vector3( 0.0f, 1.0f, 0.0f ) ), supportNormal ) );
                    const float faceDotZ = fabsf( Dot( ( rotation * Vector3( 0.0f, 0.0f, 1.0f ) ), supportNormal ) );
                    constexpr float stableFaceDot = 0.95f; // About 18 degrees from face-flat support.
                    boxHasOnlyEdgeSupport = (std::max)( { faceDotX, faceDotY, faceDotZ } ) < stableFaceDot;
                }
            }

            // Invariant: preserve the normal resting/sleep policy for every
            // contact except a vertically supported box balanced on one edge.
            // An edge has at most two manifold rows and no box face aligned
            // with the support normal. Once it topples onto a face, this veto
            // clears and the existing quiet-frame sleep gate applies again.
            // Changing this classification affects byte-exact physics baselines.
            hasRestingFootprint = ( !hasConvexHull || hasSphere || manifold.pointCount >= 2 ) && !boxHasOnlyEdgeSupport;
            uint8_t selectedPointIndices[4] = { 0, 1, 2, 3 };
            uint8_t selectedPointCount = manifold.pointCount;

            if ( manifold.pointCount > 2 && !hasSphere && sameShapeFaceFootprint && hasRestingFootprint &&
                 objectContactRowsAreQuiet( aIndex, bIndex, manifold ) )
            {

                // Why: same-shape box/box and hull/hull face manifolds often
                // produce four rows for one broad contact patch. For quiet
                // support, two well-spread cached points preserve the plane
                // while halving warm-start, friction, and PGS row work. Mixed
                // hull/box, fresh-impact, and sphere contacts keep full rows
                // because their support footprint is less symmetric.
                PROFILE_SCOPED( profiler,
                                "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds/ContactRowReduction" );

                selectedPointCount = reduceObjectContactRows( aIndex, bIndex, manifold, selectedPointIndices );
            }

            for ( uint8_t selectedIndex = 0; selectedIndex < selectedPointCount; ++selectedIndex )
            {
                const uint8_t pointIndex = selectedPointIndices[selectedIndex];
                const ObjectContactPoint& point = manifold.points[pointIndex];

                // CATTO REF:
                //   rA/rB are the r1/r2 contact arms in Catto 2005, PDF p. 6,
                //   Equations 9-11 and PDF p. 9, Equations 16-18.
                PersistentContact c;
                c.bodyA = aIndex;
                c.bodyB = bIndex;
                c.featureId = point.featureId;
                c.key = MakeKey( aIndex, bIndex, c.featureId );
                c.normal = manifold.normal;
                c.rA = point.rA;
                c.rB = point.rB;
                c.penetration = point.penetration;
                c.supportsRestingPolicy = hasRestingFootprint;
                c.normalCoupledFriction = !hasRestingFootprint;
                c.manifoldPointCount = selectedPointCount;
                stage.m_persistentContacts.push_back( c );
                ++stage.m_persistentContactCounts[aIndex];
                ++stage.m_persistentContactCounts[bIndex];

                if ( c.supportsRestingPolicy )
                {
                    ++stage.m_persistentRestingContactCounts[aIndex];
                    ++stage.m_persistentRestingContactCounts[bIndex];
                }

                if ( canRecordPipeline() )
                {
                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::ManifoldRow;
                    record.bodyA = aIndex;
                    record.bodyB = bIndex;
                    record.featureId = point.featureId;
                    record.point = point.point;
                    record.normal = manifold.normal;
                    record.scalarA = point.penetration;
                    record.scalarB = static_cast<float>( pointIndex );
                    record.scalarC = static_cast<float>( selectedPointCount );
                    recordPipeline( record );
                }
            }

            hasContact = selectedPointCount > 0;
        }

        if ( !hasContact )
        {
            continue;
        }

        stage.m_sideEffects.collisionVisualBodies.push_back( aIndex );
        stage.m_sideEffects.collisionVisualBodies.push_back( bIndex );
        appendSleepSupportEdge( aIndex, bIndex, contactNormal, hasRestingFootprint );
    }
}

void PersistentContactSolveTransaction::BuildTerrainRows( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore, const PersistentContactSolverStepPolicy& stepPolicy,
                                                          PhysicsBodyRowList<TerrainContactManifold>& terrainContactManifolds, std::span<const uint8_t> sleepState, int modelCount,
                                                          std::size_t pipelineRecordCapacity, float dt, Core::Profiler* profiler )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Terrain" );
    PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/Rows" );
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::TerrainRows, "BuildTerrainRows" );

    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    auto canRecordPipeline = [&]() { return stage.m_sideEffects.pipelineRecords.size() < pipelineRecordCapacity; };
    auto recordPipeline = [&]( const PhysicsPipelineRecord& record )
    {

        if ( stage.m_sideEffects.pipelineRecords.size() < pipelineRecordCapacity )
        {
            stage.m_sideEffects.pipelineRecords.push_back( record );
        }
    };

    // Convert terrain manifolds into the same PersistentContact rows used by
    // object/object contacts. Terrain uses TERRAIN_BODY_INDEX for body B, so
    // later solver phases treat it as infinite mass, zero velocity, and no
    // writeback. From this point on, terrain response is ordinary shared-row
    // normal/friction solving.

    for ( const Physics::TerrainContactManifold& manifold : terrainContactManifolds )
    {

        // Skip invalid/no-op manifolds before they affect profiler counts,
        // pipeline records, or the warm-start cache. Sleeping bodies do not
        // need fresh terrain rows; their accepted support state is already
        // represented by the sleep island data.

        if ( manifold.bodyA < 0 || manifold.bodyA >= modelCount || manifold.pointCount == 0 ||
             ( manifold.bodyA < static_cast<int>( sleepState.size() ) && sleepState[manifold.bodyA] ) )
        {
            continue;
        }

        if ( canRecordPipeline() )
        {
            Physics::PhysicsPipelineRecord manifoldRecord;
            manifoldRecord.stage = Physics::PhysicsPipelineStage::TerrainManifold;
            manifoldRecord.bodyA = manifold.bodyA;
            manifoldRecord.bodyB = TERRAIN_BODY_INDEX;
            manifoldRecord.point = manifold.points[0].point;
            manifoldRecord.normal = manifold.normal;
            manifoldRecord.scalarA = static_cast<float>( manifold.pointCount );
            manifoldRecord.scalarB = manifold.supportsRestingPolicy ? 1.0f : 0.0f;
            manifoldRecord.scalarC = manifold.timeOfImpact;
            recordPipeline( manifoldRecord );
        }

        // Why: terrain support needs two strengths of warm starting.
        //
        // Stable resting footprints get a full gravity-sized seed so a body
        // already on the ground does not sink before the solver converges.
        // Shoreline edge contacts are not stable enough for sleep or cached
        // friction, but they still need a small one-frame support seed;
        // otherwise a half-wet log or jetty beam falls into the slope, gets
        // pushed out, and repeats as visible bobbing. This seed is not
        // written to the persistent cache, so the contact remains wakeable
        // and cannot become a hidden sleep anchor.
        //
        // Invariant: gravityMagnitude is the magnitude of the engine's
        // vertical -Y gravity, the manifold normal is unit length, and
        // fabs(normal.y) is its supported fraction. The total weight seed is
        // projected onto that normal and divided evenly across every point
        // in the non-empty manifold.
        //
        // Hazard: a future non-vertical gravity vector would make that
        // scalar projection wrong. Directional gravity must replace both
        // terms together rather than reusing this vertical approximation.
        const float supportSeedScale = manifold.supportsRestingPolicy
                                           ? TERRAIN_RESTING_SUPPORT_SEED_SCALE
                                           : ( manifold.inhibitsSleep ? TERRAIN_SHORELINE_SUPPORT_SEED_SCALE : 0.0f );

        const float warmStartTotal = bodyRecords[static_cast<size_t>( manifold.bodyA )].mass * stepPolicy.gravityMagnitude *
                                     fabsf( manifold.normal.y ) * dt * supportSeedScale;

        const float warmStartPerContact = warmStartTotal / static_cast<float>( manifold.pointCount );

        for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
        {
            const Physics::TerrainContactPoint& point = manifold.points[pointIndex];

            PersistentContact c;
            c.bodyA = manifold.bodyA;
            c.bodyB = TERRAIN_BODY_INDEX;
            c.featureId = point.featureId;
            c.key = MakeKey( c.bodyA, c.bodyB, c.featureId );

            // PersistentContact normals point from body A toward body B.
            // Terrain manifold normals point out of the terrain and into
            // body A, so flip them to match the shared solver convention.
            c.normal = -manifold.normal;
            c.tangent1 = manifold.tangent1;
            c.tangent2 = manifold.tangent2;
            c.rA = point.rA;
            c.rB = ZERO_VECTOR;
            c.penetration = point.penetration;
            c.isTerrain = true;
            c.supportsRestingPolicy = manifold.supportsRestingPolicy;
            c.allowsTangentFriction = manifold.allowsTangentFriction;
            c.inhibitsSleep = manifold.inhibitsSleep;
            c.manifoldPointCount = manifold.pointCount;
            c.terrainNormal = manifold.normal;
            c.terrainWarmStart = warmStartPerContact;
            stage.m_persistentContacts.push_back( c );

            if ( canRecordPipeline() )
            {
                Physics::PhysicsPipelineRecord rowRecord;
                rowRecord.stage = Physics::PhysicsPipelineStage::TerrainRow;
                rowRecord.bodyA = c.bodyA;
                rowRecord.bodyB = TERRAIN_BODY_INDEX;
                rowRecord.featureId = c.featureId;
                rowRecord.point = point.point;
                rowRecord.normal = manifold.normal;
                rowRecord.scalarA = point.penetration;
                rowRecord.scalarB = warmStartPerContact;
                rowRecord.scalarC = static_cast<float>( pointIndex );
                recordPipeline( rowRecord );
            }
        }
    }
}

void PersistentContactSolveTransaction::PrecomputeRows( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                                                        const ColliderStore& colliderStore,
                                                        const PersistentContactSolverStepPolicy& stepPolicy,
                                                        std::size_t pipelineRecordCapacity, float dt,
                                                        Core::Profiler* profiler )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/Precompute" );
    AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase::Precompute, "PrecomputeRows" );

    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsConstView hotRead = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    const SolverBodyState staticTerrainBody;
    const float contactSlop = stepPolicy.objectSlop;

    // CATTO REF:
    //   Catto 2005, PDF p. 8, Section 3.6, Equation 15 and PDF p. 10,
    //   Section 4.2, Equation 20. Reason: convert penetration error into a
    //   target separating velocity so overlap decays over several frames.
    // Baumgarte bias is a gentle "please separate" velocity for bodies that are
    // already interpenetrating. It removes overlap over several ticks instead of
    // teleporting everything apart in one harsh correction.
    const float baumgarteBeta = stepPolicy.objectBaumgarteBeta;
    const float maxBaumgarteBias = stepPolicy.maxBaumgarteBias;
    const float invDt = ( dt > TOLERANCE ) ? ( 1.0f / dt ) : 120.0f;
    const bool elasticCollisions = stepPolicy.elasticCollisions;
    const float restitutionThreshold = stepPolicy.contactRestitutionThreshold;
    const float objectFrictionCoeff = stepPolicy.objectFrictionCoefficient;
    auto canRecordPipeline = [&]() { return stage.m_sideEffects.pipelineRecords.size() < pipelineRecordCapacity; };
    auto recordPipeline = [&]( const PhysicsPipelineRecord& record )
    {

        if ( stage.m_sideEffects.pipelineRecords.size() < pipelineRecordCapacity )
        {
            stage.m_sideEffects.pipelineRecords.push_back( record );
        }
    };

    // Second pass: precompute each row. This is the "setup" part of the paper:
    // CATTO REF:
    //   Catto 2005, PDF p. 17, Algorithm 4 initializes d_i from Jsp*Bsp before
    //   iteration. PDF p. 14, Equations 34-35 define B = M^-1*J^T. The code
    //   below expands that sparse matrix math into scalar effective masses.
    // The setup below builds friction axes, effective masses, bias, friction
    // limits, and pulls the previous frame's accumulated impulses from the cache.

    for ( PersistentContact& c : stage.m_persistentContacts )
    {
        const SolverBodyState& bodyA = Body( static_cast<std::size_t>( c.bodyA ) );
        const SolverBodyState& bodyB = c.isTerrain ? staticTerrainBody : Body( static_cast<std::size_t>( c.bodyB ) );

        // CATTO REF:
        //   Catto 2005, PDF pp. 11-12, Section 4.3, Equations 21-23 use two
        //   tangent directions named u1/u2 perpendicular to the contact normal.
        // ENGINE MAPPING:
        //   Skullbonez stores Catto's u1/u2 basis as c.tangent1/c.tangent2.
        //   The normal covers push-apart motion; the two tangent axes cover
        //   sideways sliding in the contact plane.
        Physics::ContactSolver::BuildContactTangents( c.normal, c.tangent1, c.tangent2 );

        // CATTO REF:
        //   Catto 2005, PDF p. 17, Algorithm 4 computes d_i = J_i*B_i. With
        //   B = M^-1*J^T from PDF p. 14, Equations 34-35, this becomes the
        //   familiar point-contact effective mass:
        //       axis dot ((I^-1 * (r cross axis)) cross r) plus invMass terms.
        // Effective mass says how stubborn this contact is. A light body pushed
        // through its center moves easily; a heavy or off-center body resists more
        // because some of the push also has to rotate it.
        auto applyInvInertiaA = [&]( const Vector3& v ) -> Vector3 { return ApplyInverseInertia( c.bodyA, v ); };

        auto applyInvInertiaB = [&]( const Vector3& v ) -> Vector3
        { return c.isTerrain ? ZERO_VECTOR : ApplyInverseInertia( c.bodyB, v ); };

        c.normalMass = Physics::ContactSolver::ComputeTwoBodyEffectiveMass( bodyA.invMass, bodyB.invMass, c.normal, c.rA,
                                                                            c.rB, applyInvInertiaA, applyInvInertiaB );

        c.tangentMass1 = Physics::ContactSolver::ComputeTwoBodyEffectiveMass( bodyA.invMass, bodyB.invMass, c.tangent1, c.rA,
                                                                              c.rB, applyInvInertiaA, applyInvInertiaB );

        c.tangentMass2 = Physics::ContactSolver::ComputeTwoBodyEffectiveMass( bodyA.invMass, bodyB.invMass, c.tangent2, c.rA,
                                                                              c.rB, applyInvInertiaA, applyInvInertiaB );

        if ( !c.allowsTangentFriction )
        {
            c.tangentMass1 = 0.0f;
            c.tangentMass2 = 0.0f;
        }

        Vector3 velA = bodyA.linearVelocity + Vector::CrossProduct( bodyA.angularVelocity, c.rA );
        Vector3 velB = c.isTerrain ? ZERO_VECTOR
                                   : bodyB.linearVelocity + Vector::CrossProduct( bodyB.angularVelocity, c.rB );

        float vn = Dot( ( velB - velA ), c.normal );

        // CATTO REF:
        //   Catto 2005, PDF p. 8, Section 3.6, Equation 15 and PDF p. 10,
        //   Section 4.2, Equation 20 provide the contact bias idea.
        // ENGINE NOTE:
        //   Object/object swept detection no longer applies a competing
        //   immediate impulse. Dynamic bounce therefore belongs in the same
        //   persistent Catto rows as fixed-body impact and resting support.
        c.bias = 0.0f;

        if ( c.isTerrain )
        {
            const float terrainSlop = stepPolicy.terrainSlop;

            if ( !c.supportsRestingPolicy && c.penetration <= terrainSlop && vn > -restitutionThreshold )
            {
                c.normalMass = 0.0f;
                c.tangentMass1 = 0.0f;
                c.tangentMass2 = 0.0f;
            }
            else if ( fabsf( vn ) < restitutionThreshold )
            {
                float penetrationError = c.penetration - terrainSlop;

                if ( penetrationError > 0.0f )
                {
                    c.bias = stepPolicy.terrainBaumgarteBeta * penetrationError * invDt;

                    if ( c.bias > stepPolicy.maxBaumgarteBias )
                    {
                        c.bias = stepPolicy.maxBaumgarteBias;
                    }
                }
            }
            else if ( vn < -restitutionThreshold )
            {
                const uint8_t pointCount = c.manifoldPointCount > 0 ? c.manifoldPointCount : 1;
                const float restitution = elasticCollisions ? 1.0f
                                                            : colliderRecords[static_cast<size_t>( c.bodyA )].restitution;

                c.bias = ( -restitution * vn ) / static_cast<float>( pointCount );
            }
        }
        else if ( vn < -restitutionThreshold )
        {
            float restitution = 1.0f;

            if ( !elasticCollisions )
            {
                const float restitutionA = colliderRecords[static_cast<size_t>( c.bodyA )].restitution;
                const float restitutionB = colliderRecords[static_cast<size_t>( c.bodyB )].restitution;
                restitution = sqrtf( restitutionA * restitutionB );
            }

            c.bias = -restitution * vn;
        }
        else if ( vn >= -restitutionThreshold )
        {
            float penetrationError = c.penetration - contactSlop;

            if ( penetrationError > 0.0f )
            {
                c.bias = baumgarteBeta * penetrationError * invDt;

                if ( maxBaumgarteBias > 0.0f && c.bias > maxBaumgarteBias )
                {
                    c.bias = maxBaumgarteBias;
                }
            }
        }

        uint16_t countA = ( stage.m_persistentContactCounts[c.bodyA] > 0 ) ? stage.m_persistentContactCounts[c.bodyA] : 1;
        float contactMass = bodyRecords[static_cast<size_t>( c.bodyA )].mass / static_cast<float>( countA );

        if ( !c.isTerrain )
        {
            uint16_t countB = ( stage.m_persistentContactCounts[c.bodyB] > 0 ) ? stage.m_persistentContactCounts[c.bodyB]
                                                                               : 1;

            float contactMassB = bodyRecords[static_cast<size_t>( c.bodyB )].mass / static_cast<float>( countB );

            if ( contactMassB < contactMass )
            {
                contactMass = contactMassB;
            }
        }

        // CATTO REF:
        //   Catto 2005, PDF p. 12, Section 4.3, Equations 24-25 bound tangent
        //   lambdas by +/-mu*m_c*g. Reason: avoid coupling tangent friction to
        //   solved normal force while keeping static friction usable in games.
        c.frictionLimit = ( elasticCollisions || !c.allowsTangentFriction )
                              ? 0.0f
                              : ( c.isTerrain ? stepPolicy.terrainFrictionCoefficient * c.terrainWarmStart
                                              : ( c.normalCoupledFriction ? 0.0f
                                                                          : objectFrictionCoeff * contactMass *
                                                                                stepPolicy.gravityMagnitude * dt ) );

        // CATTO REF:
        //   Catto 2005, PDF pp. 18-19, Section 8.1 and Algorithm 5. Reason:
        //   retrieve cached lambda for matching contact identifiers and use it
        //   as the initial lambda_0 for Algorithm 4.
        // Warm starting: if this same pair+feature was touching last frame,
        // start from the cached solution instead of zero.  The cache is sorted so
        // lookup does not linearly scan every previous-frame contact.
        // Why: warm-start cache is a stack-support tool. In elastic space it
        // can preserve last frame's push and make grazing bodies look glued.
        const bool canUseCachedWarmStart = c.supportsRestingPolicy && !elasticCollisions;
        auto cachedIt = canUseCachedWarmStart
                            ? std::lower_bound( stage.m_persistentContactCache.begin(), stage.m_persistentContactCache.end(),
                                                c.key, []( const PersistentContactCacheEntry& entry, int64_t key )
                                                { return entry.key < key; } )
                            : stage.m_persistentContactCache.end();

        if ( canUseCachedWarmStart && cachedIt != stage.m_persistentContactCache.end() && cachedIt->key == c.key )
        {
            ++stage.m_persistentContactSolverStats.cacheHits;
            c.accN = ( cachedIt->accN > 0.0f ) ? cachedIt->accN : 0.0f;
            c.accT1 = cachedIt->accT1;
            c.accT2 = cachedIt->accT2;
            const float cachedFrictionLimit = ( elasticCollisions || !c.allowsTangentFriction )
                                                  ? 0.0f
                                                  : ( c.isTerrain
                                                          ? stepPolicy.terrainFrictionCoefficient *
                                                                ( ( c.accN > c.terrainWarmStart ) ? c.accN
                                                                                                  : c.terrainWarmStart )
                                                          : ( c.normalCoupledFriction ? objectFrictionCoeff * c.accN
                                                                                      : c.frictionLimit ) );

            Physics::ContactSolver::ClampFrictionVector( c.accT1, c.accT2, cachedFrictionLimit );
            c.warmStarted = c.accN > 0.0f || fabsf( c.accT1 ) > 0.0f || fabsf( c.accT2 ) > 0.0f;
        }
        else if ( canUseCachedWarmStart )
        {
            ++stage.m_persistentContactSolverStats.cacheMisses;
        }

        {

            // Concept: impact presentation needs the relative motion that
            // existed before warm-start and solver impulses push through an
            // island. Solved impulse alone also represents support transfer.
            const SolverBodyState& a = Body( static_cast<std::size_t>( c.bodyA ) );
            const SolverBodyState& b = c.isTerrain ? staticTerrainBody : Body( static_cast<std::size_t>( c.bodyB ) );

            const Vector3 contactVelA = a.linearVelocity + Vector::CrossProduct( a.angularVelocity, c.rA );
            const Vector3 contactVelB = c.isTerrain ? ZERO_VECTOR
                                                    : b.linearVelocity + Vector::CrossProduct( b.angularVelocity, c.rB );

            const Vector3 relVel = contactVelB - contactVelA;
            c.preSolveNormalSpeed = Dot( relVel, c.normal );
            c.preSolveClosingSpeed = (std::max)( 0.0f, -c.preSolveNormalSpeed );
            const float slipT1 = Dot( relVel, c.tangent1 );
            const float slipT2 = Dot( relVel, c.tangent2 );
            c.preSolveSlipSpeed = sqrtf( slipT1 * slipT1 + slipT2 * slipT2 );
        }

        if ( c.isTerrain && c.terrainWarmStart > c.accN )
        {
            c.accN = c.terrainWarmStart;
            c.warmStarted = c.accN > 0.0f || c.warmStarted;
        }

        if ( c.warmStarted )
        {
            ++stage.m_persistentContactSolverStats.warmStartedRows;
        }

        if ( canRecordPipeline() )
        {
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::WarmStart;
            record.bodyA = c.bodyA;
            record.bodyB = c.bodyB;
            record.featureId = c.featureId;
            record.point = PhysicsBodyPosition( hotRead, static_cast<size_t>( c.bodyA ) ) + c.rA;
            record.normal = c.normal;
            record.scalarA = c.warmStarted ? 1.0f : 0.0f;
            record.scalarB = c.accN;
            record.scalarC = c.frictionLimit;
            recordPipeline( record );
        }

        if ( c.accN > 0.0f || fabsf( c.accT1 ) > 0.0f || fabsf( c.accT2 ) > 0.0f )
        {

            // CATTO REF:
            //   Catto 2005, PDF p. 17, Algorithm 4 initializes a = B*lambda.
            //   In this implementation, "a" is represented by the mutable solver
            //   velocities, so cached lambda must be applied before iteration.
            // Cached impulses are not just bookkeeping: they must be applied to
            // the bodies before iteration starts, otherwise the solver would clamp
            // against a pretend push that never actually happened.
            Vector3 warmImpulse = c.normal * c.accN + c.tangent1 * c.accT1 + c.tangent2 * c.accT2;
            ApplyImpulse( c, warmImpulse );
        }
    }
}

void PhysicsContactSolverStage::Solve( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                       const PersistentContactSolverStepPolicy& stepPolicy,
                                       std::span<const std::pair<int, int>> candidatePairs,
                                       std::span<const uint8_t> sleepState, PhysicsCandidatePairList& sleepSupportEdges,
                                       PhysicsBodyRowList<TerrainContactManifold>& terrainContactManifolds,
                                       std::span<uint8_t> terrainRestApplied, std::span<uint8_t> sleepSupportedThisFrame,
                                       PhysicsStepDiagnostics& stepDiagnostics, float dt, Core::Profiler* profiler )
{
    m_solveTransaction.BeginEntryPolicySetup();

    // Lifetime: input spans and store views are synchronous borrows for this
    // solve. Side-effect publication completes before any caller may mutate or
    // compact the body, collider, sleep, or manifold backing.
    auto& physicsDebugContacts = stepDiagnostics.MutableDebugContacts();
    auto bodyRecords = bodyStore.MutableRecords();
    auto hotFields = bodyStore.MutableHotFields();
    const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( hotFields );
    const auto colliderRecords = colliderStore.Records();
    auto& sideEffects = m_sideEffects;
    const int bodyStoreCount = bodyStore.Count();
    const int pipelineCapacityValue = stepDiagnostics.RemainingPipelineRecordCapacity();
    PrepareSideEffects( bodyStoreCount, candidatePairs.size(), pipelineCapacityValue );

    // Why: pipeline tracing is capped. Once full, later record calls are no-ops,
    // so the contact hot path should stop building detailed records that would
    // be discarded. Simulation state still follows the same deterministic path.
    const std::size_t pipelineRecordCapacity = static_cast<std::size_t>( pipelineCapacityValue );
    bool pipelineTraceCanRecord = sideEffects.pipelineRecords.size() < pipelineRecordCapacity;
    auto CanRecordPhysicsPipelineStage = [&]() { return pipelineTraceCanRecord; };

    auto RecordPhysicsPipelineStage = [&]( const PhysicsPipelineRecord& record )
    {

        if ( sideEffects.pipelineRecords.size() < pipelineRecordCapacity )
        {
            sideEffects.pipelineRecords.push_back( record );
        }

        pipelineTraceCanRecord = sideEffects.pipelineRecords.size() < pipelineRecordCapacity;
    };

    auto MarkFixedContact = [&]( int index ) { sideEffects.fixedContactBodies.push_back( index ); };

    auto QueueReleaseWake = [&]( int index ) { sideEffects.releaseWakeBodies.push_back( index ); };

    auto QueueFixedTreeRelease = [&]( const PhysicsFixedTreeReleaseEvent& event )
    { sideEffects.fixedTreeReleases.push_back( event ); };

    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts" );

    // Concept: persistent contact rows solve the quiet resting case.
    //
    // One-shot collision impulses are good for impacts but poor at support:
    // bodies already touching each other would have to rediscover "the floor is
    // pushing back" from zero every tick. Catto's temporal coherence caches the
    // previous solution so resting contacts start near their converged impulse.
    //
    // Catto reference:
    //   This whole pass is the engine's closest match to Catto 2005,
    //   Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf:
    //     - Section 4, PDF p. 9: contact point + normal model.
    //     - Section 6, PDF p. 14, Equations 34-35: time-stepped constraint
    //       system JB*lambda = eta.
    //     - Section 7.2, PDF pp. 16-17, Algorithm 4: Projected Gauss-Seidel
    //       over bounded lambda values.
    //     - Section 8, PDF pp. 18-19, Algorithm 5: cache lambda per contact
    //       identifier and reuse it as the next frame's initial guess.
    //
    // Engine-specific policy:
    //   Object-object narrowphase uses Skullbonez shape-pair manifold builders
    //   for the row geometry. The cache and PGS row shape are Catto; the exact
    //   sphere/box/OBB feature encodings are local engine policy.
    const int modelCount = (std::min)( { bodyStoreCount, static_cast<int>( bodyRecords.size() ),
                                         static_cast<int>( colliderRecords.size() ) } );

    auto isFixedBody = [&]( int index ) -> bool { return hotFields.fixed[static_cast<size_t>( index )] != 0u; };

    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactSolverStats.cachePreviousRows = static_cast<int>( m_persistentContactCache.size() );
    m_persistentContactCounts.assign( modelCount, 0 );
    m_persistentRestingContactCounts.assign( modelCount, 0 );

    if ( modelCount <= 0 || ( candidatePairs.empty() && terrainContactManifolds.empty() ) )
    {
        m_persistentContacts.clear();
        m_persistentContactCache.clear();
        physicsDebugContacts.clear();
        m_solveTransaction.Complete();
        return;
    }

    m_persistentContacts.clear();

    // ENGINE-SPECIFIC:
    //   Catto's normal constraint allows penetration and uses bias to resolve
    //   it (PDF p. 9 Section 4; PDF p. 10 Equation 20). This slop is local
    //   tolerance policy: tiny residual overlap is ignored so resting contacts
    //   do not jitter while chasing floating-point dust.
    // Small allowed overlap. Without this tolerance, floating-point noise makes
    // the solver chase microscopic errors and resting bodies visibly tremble.
    const float contactSlop = stepPolicy.objectSlop;

    // ENGINE-SPECIFIC:
    //   Catto uses the bias term for penetration correction. This partial
    //   post-solve correction is local visual cleanup for the current approximate
    //   object manifolds; it is intentionally partial so stacks do not pop.
    // A final direct positional correction catches the remaining overlap after the
    // velocity solve. The percent is deliberately partial so stacks do not pop.
    const float positionCorrectionPercent = stepPolicy.objectPositionCorrectionPercent;

    // CATTO REF:
    //   Catto 2005, PDF p. 15, Section 7, and PDF pp. 16-17, Section 7.2,
    //   Algorithm 4. Reason: repeat cheap row solves until the coupled contact
    //   system is visually good enough.
    // Projected Gauss-Seidel works by revisiting every contact repeatedly. Each
    // visit improves the answer a little; twelve passes is a compromise between
    // stack stability and keeping the physics hot path affordable.
    const int solverIterations = stepPolicy.iterations;

    // Why: mutual-gravity space has no ambient support surface. Contacts should
    // exchange momentum instead of cooling into friction or cached resting rows.
    const bool elasticCollisions = stepPolicy.elasticCollisions;
    const float objectFrictionCoeff = stepPolicy.objectFrictionCoefficient;

    m_solveTransaction.SetupBodies( bodyStore, sleepState, modelCount, profiler );

    if ( m_persistentContactCache.size() > 1 )
    {
        std::sort( m_persistentContactCache.begin(), m_persistentContactCache.end(),
                   []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                   { return lhs.key < rhs.key; } );

#ifdef _DEBUG
        assert( std::is_sorted( m_persistentContactCache.begin(), m_persistentContactCache.end(),
                                []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                                { return lhs.key < rhs.key; } ) &&
                "persistent contact cache must be sorted before lower_bound lookup" );
#endif
    }

    auto deterministicTangentAxis = []( const Vector3& supportNormal, uint32_t seed ) -> Vector3
    {
        Vector3 basis = fabsf( supportNormal.x ) < 0.9f ? Vector3( 1.0f, 0.0f, 0.0f ) : Vector3( 0.0f, 0.0f, 1.0f );

        Vector3 axis = Vector::CrossProduct( supportNormal, basis );
        float axisMag = Vector::VectorMag( axis );

        if ( axisMag <= TOLERANCE )
        {
            basis = Vector3( 0.0f, 0.0f, 1.0f );
            axis = Vector::CrossProduct( supportNormal, basis );
            axisMag = Vector::VectorMag( axis );
        }

        if ( axisMag > TOLERANCE )
        {
            axis /= axisMag;
        }

        if ( ( seed & 0x2u ) != 0u )
        {
            axis = Vector::CrossProduct( supportNormal, axis );
            axisMag = Vector::VectorMag( axis );

            if ( axisMag > TOLERANCE )
            {
                axis /= axisMag;
            }
        }

        if ( ( seed & 0x1u ) != 0u )
        {
            axis = -axis;
        }

        return axis;
    };

    auto applyPointSupportInstability = [&]( const PersistentContact& c )
    {
        constexpr float supportNormalY = 0.25f;

        if ( c.isTerrain || c.supportsRestingPolicy || c.manifoldPointCount != 1 || c.accN <= TOLERANCE ||
             fabsf( c.normal.y ) <= supportNormalY )
        {
            return;
        }

        const int supportedIndex = ( c.normal.y > 0.0f ) ? c.bodyB : c.bodyA;

        if ( supportedIndex < 0 || supportedIndex >= modelCount ||
             supportedIndex >= static_cast<int>( m_solveTransaction.BodyCount() ) || isFixedBody( supportedIndex ) ||
             sleepState[supportedIndex] )
        {
            return;
        }

        if ( supportedIndex >= static_cast<int>( colliderRecords.size() ) ||
             !GetShapeIf<ConvexHullShape>( &colliderRecords[static_cast<size_t>( supportedIndex )].shape ) )
        {
            return;
        }

        if ( supportedIndex < static_cast<int>( m_persistentRestingContactCounts.size() ) &&
             m_persistentRestingContactCounts[supportedIndex] > 0 )
        {
            return;
        }

        if ( supportedIndex < static_cast<int>( sleepSupportedThisFrame.size() ) &&
             sleepSupportedThisFrame[supportedIndex] != 0 )
        {
            return;
        }

        SolverBodyState& body = m_solveTransaction.Body( static_cast<std::size_t>( supportedIndex ) );

        if ( body.invMass <= 0.0f )
        {
            return;
        }

        const float sleepLinear = stepPolicy.nonNegativeSleepLinearSpeed;
        const float sleepAngular = stepPolicy.nonNegativeSleepAngularSpeed;
        const float speedSq = Dot( body.linearVelocity, body.linearVelocity );
        const float omegaSq = Dot( body.angularVelocity, body.angularVelocity );

        if ( speedSq > sleepLinear * sleepLinear || omegaSq > sleepAngular * sleepAngular )
        {
            return;
        }

        const Vector3 supportNormal = ( c.normal.y > 0.0f ) ? c.normal : -c.normal;
        const Vector3 supportArm = ( c.normal.y > 0.0f ) ? c.rB : c.rA;
        const Vector3 lever = supportArm - supportNormal * ( Dot( supportArm, supportNormal ) );
        const float radius = PersistentContactSolveTransaction::ConservativeContactRadius( colliderRecords[static_cast<size_t>( supportedIndex )] );
        const float leverTolerance = (std::max)( 0.001f, radius * 0.0002f );

        if ( Vector::VectorMagSquared( lever ) > leverTolerance * leverTolerance )
        {
            return;
        }

        const float loadFloor = (std::max)( 1.0e-4f, bodyRecords[static_cast<size_t>( supportedIndex )].mass *
                                                         stepPolicy.gravityMagnitude * dt * 0.01f );

        if ( c.accN < loadFloor )
        {
            return;
        }

        // A single convex-hull point contact directly below the COM is an
        // unstable equilibrium, not a credible resting footprint. Exact authored
        // alignments can otherwise preserve it forever, so seed a tiny,
        // deterministic tilt tied to the sleep angular threshold. Once the body
        // is off the point support, ordinary contact geometry and gravity take
        // over.
        const float nudgeSpeed = sleepAngular > 0.0f ? (std::max)( TOLERANCE, (std::min)( sleepAngular * 0.25f, 0.08f ) )
                                                     : 0.02f;

        if ( omegaSq > nudgeSpeed * nudgeSpeed )
        {
            return;
        }

        const uint32_t seed = c.featureId ^ ( static_cast<uint32_t>( c.bodyA + 1 ) * 0x9e3779b9u ) ^
                              ( static_cast<uint32_t>( c.bodyB + 17 ) * 0x85ebca6bu );

        const Vector3 axis = deterministicTangentAxis( supportNormal, seed );
        const float axisMagSq = Dot( axis, axis );

        if ( axisMagSq <= TOLERANCE * TOLERANCE )
        {
            return;
        }

        const float alongAxis = Dot( body.angularVelocity, axis );
        const float target = ( alongAxis < 0.0f ) ? -nudgeSpeed : nudgeSpeed;
        body.angularVelocity += axis * ( target - alongAxis );
    };

    m_solveTransaction.BuildManifolds( *this, bodyStore, colliderStore, stepPolicy, candidatePairs, sleepState,
                                       sleepSupportEdges, modelCount, pipelineRecordCapacity, profiler );

    m_solveTransaction.BuildTerrainRows( *this, bodyStore, stepPolicy, terrainContactManifolds, sleepState, modelCount,
                                         pipelineRecordCapacity, dt, profiler );

    if ( m_persistentContacts.empty() )
    {
        m_persistentContactCache.clear();
        physicsDebugContacts.clear();
        m_solveTransaction.Complete();
        return;
    }

    m_persistentContactSolverStats.rowCount = static_cast<int>( m_persistentContacts.size() );
    const SolverBodyState staticTerrainBody;

    m_solveTransaction.PrecomputeRows( *this, bodyStore, colliderStore, stepPolicy, pipelineRecordCapacity, dt, profiler );

    // Third pass: Projected Gauss-Seidel.
    // CATTO REF:
    //   Catto 2005, PDF pp. 16-17, Section 7.2, Algorithm 4. Reason: compute a
    //   lambda increment per row, clamp accumulated lambda to the row's bounds,
    //   then apply only the actual delta so the running velocity state remains
    //   consistent with B*lambda.
    // In engine terms, each contact computes the extra impulse needed to reduce
    // its current violation, adds that to the accumulated total, clamps the total
    // to valid bounds, then applies only the difference.
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/SolveRows" );
        m_solveTransaction.BeginSolveRows();

        for ( int iter = 0; iter < solverIterations; ++iter )
        {
            m_persistentContactSolverStats.solverIterations = iter + 1;
            float iterImpulseSq = 0.0f;

            for ( PersistentContact& c : m_persistentContacts )
            {
                SolverBodyState& a = m_solveTransaction.Body( static_cast<std::size_t>( c.bodyA ) );
                const SolverBodyState& b = c.isTerrain ? staticTerrainBody
                                                       : m_solveTransaction.Body( static_cast<std::size_t>( c.bodyB ) );

                Vector3 velA = a.linearVelocity + Vector::CrossProduct( a.angularVelocity, c.rA );
                Vector3 velB = c.isTerrain ? ZERO_VECTOR
                                           : b.linearVelocity + Vector::CrossProduct( b.angularVelocity, c.rB );

                float vn = Dot( ( velB - velA ), c.normal );
                float lambdaN = c.normalMass * ( c.bias - vn );
                float oldAccN = c.accN;

                // CATTO REF:
                //   Catto 2005, PDF p. 8, Section 3.5, Equation 14 and PDF p. 9,
                //   Equation 19 set the normal lower bound to zero.
                // Normal impulses are one-way. Contacts can push bodies apart, but
                // they cannot glue bodies together, so the accumulated value is >= 0.
                c.accN = ( oldAccN + lambdaN > 0.0f ) ? oldAccN + lambdaN : 0.0f;
                float deltaN = c.accN - oldAccN;
                m_solveTransaction.ApplyImpulse( c, c.normal * deltaN );

                velA = a.linearVelocity + Vector::CrossProduct( a.angularVelocity, c.rA );
                velB = c.isTerrain ? ZERO_VECTOR : b.linearVelocity + Vector::CrossProduct( b.angularVelocity, c.rB );
                float vt1 = Dot( ( velB - velA ), c.tangent1 );
                float vt2 = Dot( ( velB - velA ), c.tangent2 );
                float lambdaT1 = c.tangentMass1 * ( -vt1 );
                float lambdaT2 = c.tangentMass2 * ( -vt2 );
                float oldAccT1 = c.accT1;
                float oldAccT2 = c.accT2;

                // ENGINE-SPECIFIC:
                //   Catto clamps tangent lambdas independently in PDF p. 12,
                //   Equations 24-25. Skullbonez instead clamps the two accumulated
                //   tangent lambdas as a vector so diagonal friction cannot exceed
                //   the intended budget.
                // Clamp the two tangent accumulators as one 2D friction cone.  The
                // old per-axis clamp allowed diagonal friction to exceed the budget.
                c.accT1 = oldAccT1 + lambdaT1;
                c.accT2 = oldAccT2 + lambdaT2;
                const float frictionLimit = ( elasticCollisions || !c.allowsTangentFriction )
                                                ? 0.0f
                                                : ( c.isTerrain
                                                        ? stepPolicy.terrainFrictionCoefficient *
                                                              ( ( c.accN > c.terrainWarmStart ) ? c.accN
                                                                                                : c.terrainWarmStart )
                                                        : ( c.normalCoupledFriction ? objectFrictionCoeff * c.accN
                                                                                    : c.frictionLimit ) );

                Physics::ContactSolver::ClampFrictionVector( c.accT1, c.accT2, frictionLimit );
                float deltaT1 = c.accT1 - oldAccT1;
                float deltaT2 = c.accT2 - oldAccT2;
                m_solveTransaction.ApplyImpulse( c, c.tangent1 * deltaT1 + c.tangent2 * deltaT2 );

                iterImpulseSq += deltaN * deltaN + deltaT1 * deltaT1 + deltaT2 * deltaT2;

                if ( CanRecordPhysicsPipelineStage() )
                {
                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::SolverIteration;
                    record.bodyA = c.bodyA;
                    record.bodyB = c.bodyB;
                    record.iteration = iter;
                    record.featureId = c.featureId;
                    record.point = PhysicsBodyPosition( hotRead, static_cast<size_t>( c.bodyA ) ) + c.rA;
                    record.normal = c.normal;
                    record.scalarA = deltaN;
                    record.scalarB = c.accN;
                    record.scalarC = sqrtf( c.accT1 * c.accT1 + c.accT2 * c.accT2 );
                    RecordPhysicsPipelineStage( record );
                }
            }

            // ENGINE-SPECIFIC:
            //   Catto lists residual/delta-based termination as a possible
            //   Gauss-Seidel criterion on PDF p. 15, Section 7.1, then uses fixed
            //   iterations for simplicity. This deterministic early-out is a local
            //   optimization using total squared impulse delta.

            if ( iterImpulseSq < 1.0e-6f )
            {
                break;
            }
        }
    }

    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/PointSupportInstability" );
        m_solveTransaction.BeginPointSupportInstability();

        for ( const PersistentContact& c : m_persistentContacts )
        {
            applyPointSupportInstability( c );
        }
    }

    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Terrain" );
        PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/RestPolicy" );
        m_solveTransaction.BeginTerrainRestPolicy();

        // This is intentionally separate from the row solver. The rows above
        // handle physical contact response; this pass applies engine rest policy
        // only for manifolds that the terrain classifier marked as stable
        // support. That separation keeps unstable edge/corner terrain contacts
        // from gaining rolling damping or sleep privileges just because their
        // impact rows solved successfully.
        std::fill_n( terrainRestApplied.begin(), static_cast<size_t>( modelCount ), static_cast<uint8_t>( 0 ) );

        for ( const Physics::TerrainContactManifold& manifold : terrainContactManifolds )
        {
            const int bodyIndex = manifold.bodyA;

            if ( bodyIndex < 0 || bodyIndex >= modelCount || terrainRestApplied[bodyIndex] ||
                 !manifold.supportsRestingPolicy || sleepState[bodyIndex] || isFixedBody( bodyIndex ) )
            {
                continue;
            }

            terrainRestApplied[bodyIndex] = 1;
            const PhysicsBodyRecord& record = bodyRecords[static_cast<size_t>( bodyIndex )];
            SolverBodyState& body = m_solveTransaction.Body( static_cast<std::size_t>( bodyIndex ) );
            float normalForce = record.mass * stepPolicy.gravityMagnitude * fabsf( manifold.normal.y );
            float omegaMagSq = Dot( body.angularVelocity, body.angularVelocity );

            if ( omegaMagSq > TOLERANCE * TOLERANCE )
            {

                // Approximate rolling friction as a torque opposite angular
                // velocity. The effective radius is exact for spheres and a
                // conservative average extent for boxes, enough to bleed tiny
                // residual spin without adding a shape-specific response path.
                float omegaMag = sqrtf( omegaMagSq );
                float rEff = VisitCollisionShape( colliderRecords[static_cast<size_t>( bodyIndex )].shape,
                                                  []( const auto& shape ) -> float
                                                  {
                                                      using ShapeT = std::decay_t<decltype( shape )>;

                                                      if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
                                                      {
                                                          return shape.GetRadius();
                                                      }
                                                      else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
                                                      {
                                                          const Vector3& he = shape.GetHalfExtents();
                                                          return ( he.x + he.y + he.z ) / 3.0f;
                                                      }
                                                      else
                                                      {
                                                          return shape.GetBoundingRadius() * 0.5f;
                                                      }
                                                  } );

                const float muRolling = stepPolicy.rollingFrictionCoefficient;
                float rollingTorqueMag = muRolling * normalForce * rEff;
                const Vector3& inertia = record.rotationalInertia;
                float avgInertia = ( inertia.x + inertia.y + inertia.z ) / 3.0f;

                if ( avgInertia < TOLERANCE )
                {
                    avgInertia = 1.0f;
                }

                float deltaOmega = ( rollingTorqueMag / avgInertia ) * dt;

                if ( deltaOmega >= omegaMag )
                {
                    body.angularVelocity = ZERO_VECTOR;
                }
                else
                {
                    body.angularVelocity -= ( body.angularVelocity / omegaMag ) * deltaOmega;
                }
            }

            constexpr float sleepLinear = 0.05f;
            constexpr float sleepAngular = 0.02f;

            if ( ( Dot( body.linearVelocity, body.linearVelocity ) ) < sleepLinear * sleepLinear &&
                 ( Dot( body.angularVelocity, body.angularVelocity ) ) < sleepAngular * sleepAngular )
            {

                // Snap only near-zero supported motion. This avoids tiny solver
                // residue keeping a legitimately settled terrain body awake,
                // while leaving unsupported impacts and sliding bodies untouched.
                body.linearVelocity = ZERO_VECTOR;
                body.angularVelocity = ZERO_VECTOR;
            }
        }
    }

    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/WriteBack" );
        m_solveTransaction.BeginWriteBack();

        for ( int i = 0; i < modelCount; ++i )
        {

            if ( sleepState[i] || isFixedBody( i ) )
            {
                continue;
            }

            if ( CanRecordPhysicsPipelineStage() )
            {
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::VelocityWriteback;
                record.bodyA = i;
                record.point = PhysicsBodyPosition( hotRead, static_cast<size_t>( i ) );
                record.scalarA = Vector::VectorMag( m_solveTransaction.Body( static_cast<std::size_t>( i ) ).linearVelocity );
                record.scalarB = Vector::VectorMag( m_solveTransaction.Body( static_cast<std::size_t>( i ) ).angularVelocity );
                RecordPhysicsPipelineStage( record );
            }

            const size_t bodyIndex = static_cast<size_t>( i );
            const SolverBodyState& solvedBody = m_solveTransaction.Body( static_cast<std::size_t>( i ) );
            hotFields.linearVelocityX[bodyIndex] = solvedBody.linearVelocity.x;
            hotFields.linearVelocityY[bodyIndex] = solvedBody.linearVelocity.y;
            hotFields.linearVelocityZ[bodyIndex] = solvedBody.linearVelocity.z;
            hotFields.angularVelocityX[bodyIndex] = solvedBody.angularVelocity.x;
            hotFields.angularVelocityY[bodyIndex] = solvedBody.angularVelocity.y;
            hotFields.angularVelocityZ[bodyIndex] = solvedBody.angularVelocity.z;
        }
    }

    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/DebugContacts" );
        m_solveTransaction.BeginDebugContacts();
        physicsDebugContacts.clear();

        for ( const PersistentContact& c : m_persistentContacts )
        {

            if ( c.accN > 0.0f )
            {

                if ( isFixedBody( c.bodyA ) )
                {
                    MarkFixedContact( c.bodyA );
                }

                if ( c.bodyB != TERRAIN_BODY_INDEX && isFixedBody( c.bodyB ) )
                {
                    MarkFixedContact( c.bodyB );
                }
            }

            Physics::PhysicsDebugContact out;
            out.bodyA = c.bodyA;
            out.bodyB = c.bodyB;
            out.featureId = c.featureId;
            out.point = PhysicsBodyPosition( hotRead, static_cast<size_t>( c.bodyA ) ) + c.rA;
            out.normal = c.isTerrain ? c.terrainNormal : c.normal;
            out.tangent1 = c.tangent1;
            out.tangent2 = c.tangent2;
            out.penetration = c.penetration;
            out.normalImpulse = c.accN;
            out.preSolveNormalSpeed = c.preSolveNormalSpeed;
            out.preSolveClosingSpeed = c.preSolveClosingSpeed;
            out.preSolveSlipSpeed = c.preSolveSlipSpeed;
            physicsDebugContacts.push_back( out );
        }
    }

    // ENGINE-SPECIFIC:
    //   Catto's Baumgarte bias handles overlap through velocity-level constraint
    //   correction. This partial positional correction is local cleanup for the
    //   current approximate object contacts.
    // Fourth pass: remove any visible leftover overlap. The velocity solver does
    // most of the work, but this direct correction keeps persistent contacts from
    // sinking deeper into each other over many frames.
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/PositionCorrection" );
        m_solveTransaction.BeginPositionCorrection();

        for ( const PersistentContact& c : m_persistentContacts )
        {
            const float rowContactSlop = c.isTerrain ? stepPolicy.terrainSlop : contactSlop;

            if ( c.penetration <= rowContactSlop )
            {
                continue;
            }

            const size_t bodyAIndex = static_cast<size_t>( c.bodyA );
            float invMassA = ( sleepState[c.bodyA] || hotFields.fixed[bodyAIndex] != 0u )
                                 ? 0.0f
                                 : hotFields.inverseMass[bodyAIndex];

            float invMassB = 0.0f;
            bool hasBodyB = false;

            if ( c.bodyB != TERRAIN_BODY_INDEX )
            {
                const size_t bodyBIndex = static_cast<size_t>( c.bodyB );
                hasBodyB = true;
                invMassB = ( sleepState[c.bodyB] || hotFields.fixed[bodyBIndex] != 0u ) ? 0.0f
                                                                                        : hotFields.inverseMass[bodyBIndex];
            }

            float totalInvMass = invMassA + invMassB;

            if ( totalInvMass <= TOLERANCE )
            {
                continue;
            }

            const float rowPositionCorrectionPercent = c.isTerrain ? 0.4f : positionCorrectionPercent;
            Vector3 correction = c.normal *
                                 ( ( c.penetration - rowContactSlop ) * rowPositionCorrectionPercent / totalInvMass );

            float correctionMagnitude = Vector::VectorMag( correction );
            ++m_persistentContactSolverStats.positionCorrectionRows;
            m_persistentContactSolverStats.positionCorrectionTotal += correctionMagnitude;

            if ( correctionMagnitude > m_persistentContactSolverStats.positionCorrectionMax )
            {
                m_persistentContactSolverStats.positionCorrectionMax = correctionMagnitude;
            }

            if ( CanRecordPhysicsPipelineStage() )
            {
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::PositionCorrection;
                record.bodyA = c.bodyA;
                record.bodyB = c.bodyB;
                record.featureId = c.featureId;
                record.point = PhysicsBodyPosition( hotRead, bodyAIndex ) + c.rA;
                record.normal = c.normal;
                record.scalarA = correctionMagnitude;
                record.scalarB = c.penetration;
                record.scalarC = rowContactSlop;
                RecordPhysicsPipelineStage( record );
            }

            Vector3 positionA = PhysicsBodyPosition( hotRead, bodyAIndex ) - correction * invMassA;
            hotFields.positionX[bodyAIndex] = positionA.x;
            hotFields.positionY[bodyAIndex] = positionA.y;
            hotFields.positionZ[bodyAIndex] = positionA.z;

            if ( hasBodyB )
            {
                const size_t bodyBIndex = static_cast<size_t>( c.bodyB );
                Vector3 positionB = PhysicsBodyPosition( hotRead, bodyBIndex ) + correction * invMassB;
                hotFields.positionX[bodyBIndex] = positionB.x;
                hotFields.positionY[bodyBIndex] = positionB.y;
                hotFields.positionZ[bodyBIndex] = positionB.z;
            }
        }
    }

    // CATTO REF:
    //   Catto 2005, PDF pp. 18-19, Section 8.1 and Algorithm 5: destroy the old
    //   contact cache, create a new one, and store lambda plus the contact
    //   identifier for the next frame.
    // Final pass: store this frame's accumulated pushes for next frame. This is
    // why a settled stack can remain settled; it does not have to rediscover from
    // scratch how much support force each contact needs every tick.
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/CacheStore" );
        m_solveTransaction.BeginCacheStore();
        m_persistentContactCache.clear();

        for ( const PersistentContact& c : m_persistentContacts )
        {

            if ( !c.supportsRestingPolicy )
            {
                continue;
            }

            if ( c.accN <= 0.0f && fabsf( c.accT1 ) <= TOLERANCE && fabsf( c.accT2 ) <= TOLERANCE )
            {
                continue;
            }

            PersistentContactCacheEntry cached;
            cached.key = c.key;
            cached.accN = c.accN;
            cached.accT1 = c.accT1;
            cached.accT2 = c.accT2;
            m_persistentContactCache.push_back( cached );

            if ( CanRecordPhysicsPipelineStage() )
            {
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::CacheStore;
                record.bodyA = c.bodyA;
                record.bodyB = c.bodyB;
                record.featureId = c.featureId;
                record.point = PhysicsBodyPosition( hotRead, static_cast<size_t>( c.bodyA ) ) + c.rA;
                record.normal = c.normal;
                record.scalarA = c.accN;
                record.scalarB = c.accT1;
                record.scalarC = c.accT2;
                RecordPhysicsPipelineStage( record );
            }
        }

        if ( m_persistentContactCache.size() > 1 )
        {
            std::sort( m_persistentContactCache.begin(), m_persistentContactCache.end(),
                       []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                       { return lhs.key < rhs.key; } );
        }
    }

    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/PersistentContacts/FixedContactRelease" );
        m_solveTransaction.BeginFixedContactRelease();
        auto releaseFixedContactBody = [&]( int fixedIndex, int otherIndex, const PersistentContact& c, bool fixedIsBodyA )
        {

            if ( fixedIndex < 0 || fixedIndex >= modelCount || otherIndex < 0 || otherIndex >= modelCount )
            {
                return;
            }

            PhysicsBodyRecord& fixedRecord = bodyRecords[static_cast<size_t>( fixedIndex )];
            const PhysicsBodyRecord& otherRecord = bodyRecords[static_cast<size_t>( otherIndex )];

            if ( !isFixedBody( fixedIndex ) || !fixedRecord.releasesFromFixedOnContact || isFixedBody( otherIndex ) ||
                 otherRecord.releasesFromFixedOnContact || c.accN < fixedRecord.contactReleaseImpulseThreshold )
            {
                return;
            }

            Vector3 releaseDir = fixedIsBodyA ? -c.normal : c.normal;
            const float dirMag = Vector::VectorMag( releaseDir );

            if ( dirMag <= TOLERANCE )
            {
                return;
            }

            releaseDir /= dirMag;

            const float mass = (std::max)( 0.001f, fixedRecord.mass );
            const float impulseSpeed = c.accN / mass;
            const Vector3 otherVelocity = PhysicsBodyLinearVelocity( hotRead, static_cast<size_t>( otherIndex ) );
            const float carriedSpeed = (std::max)( 0.0f, Dot( otherVelocity, releaseDir ) );
            const float releaseSpeed = std::clamp( (std::max)( impulseSpeed, carriedSpeed * 0.35f ), 1.5f, 36.0f );

            Vector3 tangentVelocity = otherVelocity - releaseDir * ( Dot( otherVelocity, releaseDir ) );
            const float tangentSpeed = Vector::VectorMag( tangentVelocity );

            if ( tangentSpeed > releaseSpeed * 0.55f && tangentSpeed > TOLERANCE )
            {
                tangentVelocity *= ( releaseSpeed * 0.55f ) / tangentSpeed;
            }

            const Vector3 arm = fixedIsBodyA ? c.rA : c.rB;
            Vector3 spinAxis = Vector::CrossProduct( arm, releaseDir );
            const float spinAxisMag = Vector::VectorMag( spinAxis );
            Vector3 angularVelocity = ZERO_VECTOR;

            if ( spinAxisMag > TOLERANCE )
            {
                const float radius = (std::max)( 0.25f, hotFields.boundingRadius[static_cast<size_t>( fixedIndex )] );
                angularVelocity = spinAxis * ( std::clamp( releaseSpeed / radius, 0.0f, 8.0f ) / spinAxisMag );
            }

            const Vector3 releasedLinearVelocity = releaseDir * releaseSpeed + tangentVelocity;
            bodyStore.ReleaseFixedBody( fixedIndex, releasedLinearVelocity, angularVelocity );
            QueueReleaseWake( fixedIndex );
            QueueFixedTreeRelease( PhysicsFixedTreeReleaseEvent { fixedIndex, releasedLinearVelocity, angularVelocity } );
        };

        for ( const PersistentContact& c : m_persistentContacts )
        {

            if ( c.isTerrain || c.accN <= TOLERANCE )
            {
                continue;
            }

            releaseFixedContactBody( c.bodyA, c.bodyB, c, true );
            releaseFixedContactBody( c.bodyB, c.bodyA, c, false );
        }
    }

    m_solveTransaction.Complete();
}
