/*
File: SkullbonezSource/SkullbonezPersistentContactSolver.cpp
Purpose:
  Solves object/object and object/terrain persistent contact rows.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  OBB (Oriented Bounding Box): Box with rotation, used for exact object-space
  collision tests.
  PGS (Projected Gauss-Seidel): Iterative constraint-solver method used for
  bounded contact impulses.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezPersistentContactSolver.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezPersistentContactSolver.h"

#include "SkullbonezConfig.h"
#include "SkullbonezContactSolverCommon.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezObjectContactManifold.h"
#include "SkullbonezPhysicsWorld.h"
#include "SkullbonezProfiler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <type_traits>

using namespace SkullbonezCore::GameObjects;
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
} // namespace

void PersistentContactSolver::Solve( PhysicsWorld& world, GameModelCollection& collection, float dt )
{
    using PersistentContact = PhysicsWorld::PersistentContact;
    using PersistentContactCacheEntry = PhysicsWorld::PersistentContactCacheEntry;
    using PersistentContactSolverStats = PhysicsWorld::PersistentContactSolverStats;
    using SolverBodyState = PhysicsWorld::SolverBodyState;

    auto& m_gameModels = collection.m_gameModels;
    auto& m_soaIsFixed = collection.m_soaCache.isFixed;
    auto& m_candidatePairs = world.m_candidatePairs;
    auto& m_sleepState = world.m_sleepState;
    auto& m_sleepSupportEdges = world.m_sleepSupportEdges;
    auto& m_persistentContacts = world.m_persistentContacts;
    auto& m_persistentContactCache = world.m_persistentContactCache;
    auto& m_persistentContactSolverStats = world.m_persistentContactSolverStats;
    auto& m_persistentContactCounts = world.m_persistentContactCounts;
    auto& m_persistentRestingContactCounts = world.m_persistentRestingContactCounts;
    auto& m_solverBodies = world.m_solverBodies;
    auto& m_physicsDebugContacts = world.m_physicsDebugContacts;
    auto& m_terrainContactManifolds = world.m_terrainContactManifolds;
    auto& m_terrainRestApplied = world.m_terrainRestApplied;
    auto& m_sleepSupportedThisFrame = world.m_sleepSupportedThisFrame;
    auto RecordPhysicsPipelineStage = [&]( const PhysicsPipelineRecord& record )
    { world.RecordPhysicsPipelineStage( record ); };
    auto MarkCollisionVisualContact = [&]( int index )
    { world.MarkCollisionVisualContact( index ); };
    auto MarkFixedContact = [&]( int index )
    { world.MarkFixedContact( collection, index ); };
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts" );

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
    const int modelCount = static_cast<int>( m_gameModels.size() );
    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactSolverStats.cachePreviousRows = static_cast<int>( m_persistentContactCache.size() );
    m_persistentContactCounts.assign( modelCount, 0 );
    m_persistentRestingContactCounts.assign( modelCount, 0 );
    if ( modelCount <= 0 ||
         ( m_candidatePairs.empty() && m_terrainContactManifolds.empty() ) )
    {
        m_persistentContacts.clear();
        m_persistentContactCache.clear();
        m_physicsDebugContacts.clear();
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
    const float contactSlop = (std::max)( 0.0f, Cfg().persistentContactSlop );

    // CATTO REF:
    //   Catto 2005, PDF p. 8, Section 3.6, Equation 15 and PDF p. 10,
    //   Section 4.2, Equation 20. Reason: convert penetration error into a
    //   target separating velocity so overlap decays over several frames.
    // Baumgarte bias is a gentle "please separate" velocity for bodies that are
    // already interpenetrating. It removes overlap over several ticks instead of
    // teleporting everything apart in one harsh correction.
    const float baumgarteBeta = (std::max)( 0.0f, Cfg().persistentContactBaumgarteBeta );

    // ENGINE-SPECIFIC:
    //   Catto uses the bias term for penetration correction. This partial
    //   post-solve correction is local visual cleanup for the current approximate
    //   object manifolds; it is intentionally partial so stacks do not pop.
    // A final direct positional correction catches the remaining overlap after the
    // velocity solve. The percent is deliberately partial so stacks do not pop.
    const float positionCorrectionPercent = (std::max)( 0.0f, (std::min)( Cfg().persistentContactPositionCorrectionPercent, 1.0f ) );
    const float maxBaumgarteBias = (std::max)( 0.0f, Cfg().terrainMaxBaumgarteBias );

    // CATTO REF:
    //   Catto 2005, PDF p. 15, Section 7, and PDF pp. 16-17, Section 7.2,
    //   Algorithm 4. Reason: repeat cheap row solves until the coupled contact
    //   system is visually good enough.
    // Projected Gauss-Seidel works by revisiting every contact repeatedly. Each
    // visit improves the answer a little; twelve passes is a compromise between
    // stack stability and keeping the physics hot path affordable.
    const int solverIterations = (std::max)( 1, Cfg().persistentContactSolverIterations );
    const float invDt = ( dt > TOLERANCE ) ? ( 1.0f / dt ) : 120.0f;

    // CATTO REF:
    //   Catto 2005, PDF pp. 18-19, Section 8.1/8.2 and Algorithm 5 store lambda
    //   with a contact identifier and retrieve it for matching contacts next
    //   frame.
    // ENGINE-SPECIFIC:
    //   This key is a compact pair+feature id. Full 32-bit feature IDs are kept
    //   so authored hull face/edge identifiers are not truncated before warm
    //   starting. MAX_GAME_MODELS is 8192, so 15 bits per body leaves 32 bits
    //   for the feature and one high kind bit for terrain rows.
    // Catto's cache needs a stable name for "body A touching body B at this
    // contact feature". Box and hull manifolds assign distinct feature ids per row.
    auto makeKey = []( int a, int b, uint32_t featureId ) -> int64_t
    {
        constexpr uint64_t BODY_MASK = 0x7fffull;
        if ( b == TERRAIN_BODY_INDEX )
        {
            uint64_t packed = ( 1ull << 62 ) |
                              ( ( static_cast<uint64_t>( static_cast<uint32_t>( a ) ) & BODY_MASK ) << 32 ) |
                              static_cast<uint64_t>( featureId );
            return static_cast<int64_t>( packed );
        }

        int lo = ( a < b ) ? a : b;
        int hi = ( a < b ) ? b : a;
        uint64_t packed = ( ( static_cast<uint64_t>( static_cast<uint32_t>( lo ) ) & BODY_MASK ) << 47 ) |
                          ( ( static_cast<uint64_t>( static_cast<uint32_t>( hi ) ) & BODY_MASK ) << 32 ) |
                          static_cast<uint64_t>( featureId );
        return static_cast<int64_t>( packed );
    };

    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/BodySetup" );
        m_solverBodies.assign( modelCount, SolverBodyState() );

        // CATTO REF:
        //   Catto 2005, PDF p. 7, Algorithms 1-2 and PDF p. 16, Algorithm 4 work on
        //   sparse body velocity blocks. Algorithm 4 names the mutable velocity-like
        //   work vector "a".
        // ENGINE-SPECIFIC:
        //   We keep compact per-body solver state here and write back once after PGS.
        //   That preserves Catto's sparse-row shape while avoiding repeated GameModel
        //   getter/setter churn inside the row loop.
        for ( int i = 0; i < modelCount; ++i )
        {
            GameModel& model = m_gameModels[i];
            SolverBodyState& body = m_solverBodies[i];
            if ( m_sleepState[i] || m_soaIsFixed[i] )
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
                body.linearVelocity = model.GetVelocity();
                body.angularVelocity = model.GetAngularVelocity();
                body.invMass = model.GetInvertedMass();
                body.invInertia = model.GetInvertedRotationalInertia();
                body.useWorldInertia = model.UsesWorldInertia();
            }
            if ( body.useWorldInertia )
            {
                Quaternion orientation = model.GetOrientation();
                body.orientation = orientation.GetOrientationMatrix();
            }
        }
    }

    if ( m_persistentContactCache.size() > 1 )
    {
        std::sort( m_persistentContactCache.begin(),
                   m_persistentContactCache.end(),
                   []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                   {
                       return lhs.key < rhs.key;
                   } );
#ifdef _DEBUG
        assert( std::is_sorted( m_persistentContactCache.begin(),
                                m_persistentContactCache.end(),
                                []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                                {
                                    return lhs.key < rhs.key;
                                } ) &&
                "persistent contact cache must be sorted before lower_bound lookup" );
#endif
    }

    // CATTO REF:
    //   Catto 2005, PDF p. 12, Section 5, unnumbered inertia transform before
    //   Equations 26-28: I_world^-1 = R * I_body^-1 * R^T.
    // Inertia is rotational mass. Boxes need world-space inertia because their
    // local inertia axes rotate with orientation; spheres remain isotropic.
    auto applyInvInertia = [&]( int body, const Vector3& v ) -> Vector3
    {
        if ( body == TERRAIN_BODY_INDEX )
        {
            return ZERO_VECTOR;
        }

        const SolverBodyState& solverBody = m_solverBodies[body];
        if ( !solverBody.useWorldInertia )
        {
            return Vector::VectorMultiply( solverBody.invInertia, v );
        }

        Vector3 bodyV = solverBody.orientation.TransposeMultiply( v );
        return solverBody.orientation * Vector::VectorMultiply( solverBody.invInertia, bodyV );
    };

    // CATTO REF:
    //   Catto 2005, PDF p. 5, Section 3.3, Equation 7 says constraint forces are
    //   Fc = J^T*lambda. PDF p. 8, Algorithm 2 shows accumulating those row
    //   contributions into body force/torque blocks.
    // REASON:
    //   Applying an impulse to a contact row changes linear velocity by
    //   invMass*impulse and angular velocity by I^-1*(r cross impulse). Body A
    //   receives the opposite impulse from body B.
    // Apply one impulse to both bodies using Newton's third law: equal and
    // opposite pushes. A receives -impulse, B receives +impulse. The cross
    // products turn off-center pushes into spin changes.
    auto applyImpulse = [&]( const PersistentContact& c, const Vector3& impulse )
    {
        SolverBodyState& a = m_solverBodies[c.bodyA];

        a.linearVelocity -= impulse * a.invMass;
        a.angularVelocity -= applyInvInertia( c.bodyA, Vector::CrossProduct( c.rA, impulse ) );
        if ( c.bodyB != TERRAIN_BODY_INDEX )
        {
            SolverBodyState& b = m_solverBodies[c.bodyB];
            b.linearVelocity += impulse * b.invMass;
            b.angularVelocity += applyInvInertia( c.bodyB, Vector::CrossProduct( c.rB, impulse ) );
        }
    };

    auto conservativeContactRadius = []( const GameModel& model ) -> float
    {
        // Broadphase radii must include any local shape offset. If a shape is
        // not centered on the body origin, the "safe maybe touching" sphere has
        // to reach from the origin all the way to the farthest shifted point.
        const CollisionShape& shape = model.GetCollisionShape();
        float radius = GetShapeBoundingRadius( shape );
        const Vector3& offset = GetShapePosition( shape );
        float offsetSq = Vector::VectorMagSquared( offset );
        if ( offsetSq > TOLERANCE * TOLERANCE )
        {
            radius += sqrtf( offsetSq );
        }
        return radius;
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
            m_sleepSupportEdges.emplace_back( aIndex, bIndex );
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SleepSupportEdge;
            record.bodyA = aIndex;
            record.bodyB = bIndex;
            record.normal = normal;
            record.point = ( m_gameModels[aIndex].GetPosition() + m_gameModels[bIndex].GetPosition() ) * 0.5f;
            record.scalarA = normal.y;
            RecordPhysicsPipelineStage( record );
        }
        else if ( normal.y < -supportNormalY )
        {
            m_sleepSupportEdges.emplace_back( bIndex, aIndex );
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SleepSupportEdge;
            record.bodyA = bIndex;
            record.bodyB = aIndex;
            record.normal = -normal;
            record.point = ( m_gameModels[aIndex].GetPosition() + m_gameModels[bIndex].GetPosition() ) * 0.5f;
            record.scalarA = -normal.y;
            RecordPhysicsPipelineStage( record );
        }
    };

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
        if ( c.isTerrain ||
             c.supportsRestingPolicy ||
             c.manifoldPointCount != 1 ||
             c.accN <= TOLERANCE ||
             fabsf( c.normal.y ) <= supportNormalY )
        {
            return;
        }

        const int supportedIndex = ( c.normal.y > 0.0f ) ? c.bodyB : c.bodyA;
        if ( supportedIndex < 0 ||
             supportedIndex >= modelCount ||
             supportedIndex >= static_cast<int>( m_solverBodies.size() ) ||
             m_soaIsFixed[supportedIndex] ||
             m_sleepState[supportedIndex] )
        {
            return;
        }
        if ( !std::get_if<ConvexHullShape>( &m_gameModels[supportedIndex].GetCollisionShape() ) )
        {
            return;
        }
        if ( supportedIndex < static_cast<int>( m_persistentRestingContactCounts.size() ) &&
             m_persistentRestingContactCounts[supportedIndex] > 0 )
        {
            return;
        }
        if ( supportedIndex < static_cast<int>( m_sleepSupportedThisFrame.size() ) &&
             m_sleepSupportedThisFrame[supportedIndex] != 0 )
        {
            return;
        }

        SolverBodyState& body = m_solverBodies[supportedIndex];
        if ( body.invMass <= 0.0f )
        {
            return;
        }

        const float sleepLinear = (std::max)( 0.0f, Cfg().physicsSleepLinearSpeed );
        const float sleepAngular = (std::max)( 0.0f, Cfg().physicsSleepAngularSpeed );
        const float speedSq = body.linearVelocity * body.linearVelocity;
        const float omegaSq = body.angularVelocity * body.angularVelocity;
        if ( speedSq > sleepLinear * sleepLinear ||
             omegaSq > sleepAngular * sleepAngular )
        {
            return;
        }

        const Vector3 supportNormal = ( c.normal.y > 0.0f ) ? c.normal : -c.normal;
        const Vector3 supportArm = ( c.normal.y > 0.0f ) ? c.rB : c.rA;
        const Vector3 lever = supportArm - supportNormal * ( supportArm * supportNormal );
        const float radius = conservativeContactRadius( m_gameModels[supportedIndex] );
        const float leverTolerance = (std::max)( 0.001f, radius * 0.0002f );
        if ( Vector::VectorMagSquared( lever ) > leverTolerance * leverTolerance )
        {
            return;
        }

        const float loadFloor = (std::max)( 1.0e-4f, m_gameModels[supportedIndex].GetMass() * fabsf( Cfg().gravity ) * dt * 0.01f );
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
        const float nudgeSpeed = sleepAngular > 0.0f
                                     ? (std::max)( TOLERANCE, (std::min)( sleepAngular * 0.25f, 0.08f ) )
                                     : 0.02f;
        if ( omegaSq > nudgeSpeed * nudgeSpeed )
        {
            return;
        }

        const uint32_t seed = c.featureId ^
                              ( static_cast<uint32_t>( c.bodyA + 1 ) * 0x9e3779b9u ) ^
                              ( static_cast<uint32_t>( c.bodyB + 17 ) * 0x85ebca6bu );
        const Vector3 axis = deterministicTangentAxis( supportNormal, seed );
        const float axisMagSq = axis * axis;
        if ( axisMagSq <= TOLERANCE * TOLERANCE )
        {
            return;
        }

        const float alongAxis = body.angularVelocity * axis;
        const float target = ( alongAxis < 0.0f ) ? -nudgeSpeed : nudgeSpeed;
        body.angularVelocity += axis * ( target - alongAxis );
    };

    // CATTO REF:
    //   Catto 2005, PDF p. 9, Section 4 "Contact Model" and Equation 16 require
    //   a contact point, a normal, and separation/penetration for each row.
    // ENGINE-SPECIFIC:
    //   Broadphase still uses conservative bounding radii, but the authoritative
    //   object contact geometry now comes from shape-pair manifolds: exact
    //   sphere/sphere, closest-point sphere/box, and SAT/clipped OBB contacts.
    // First pass: turn broadphase candidate pairs into Catto-style contact rows.
    // Each manifold point becomes one persistent row with its own feature id so
    // warm starting can remember face contacts instead of one pair-wide fallback.
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds" );
        m_persistentContacts.reserve( m_candidatePairs.size() * 4 );
        for ( const auto& cp : m_candidatePairs )
        {
            int aIndex = cp.first;
            int bIndex = cp.second;
            if ( aIndex == bIndex ||
                 aIndex < 0 || bIndex < 0 ||
                 aIndex >= modelCount || bIndex >= modelCount ||
                 ( m_sleepState[aIndex] && m_sleepState[bIndex] ) ||
                 ( m_soaIsFixed[aIndex] && m_soaIsFixed[bIndex] ) )
            {
                continue;
            }

            if ( bIndex < aIndex )
            {
                std::swap( aIndex, bIndex );
            }

            GameModel& a = m_gameModels[aIndex];
            GameModel& b = m_gameModels[bIndex];

            Vector3 centerDelta = b.GetPosition() - a.GetPosition();
            float contactDistance = conservativeContactRadius( a ) + conservativeContactRadius( b ) + Cfg().contactEpsilon;
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
                PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds/ExactObjectManifold" );
                manifoldBuilt = BuildObjectContactManifold( a, b, aIndex, bIndex, Cfg().contactEpsilon, manifold );
            }
            if ( manifoldBuilt )
            {
                PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds/AddRows" );
                contactNormal = manifold.normal;
                const CollisionShape& shapeA = a.GetCollisionShape();
                const CollisionShape& shapeB = b.GetCollisionShape();
                const bool hasConvexHull = std::get_if<ConvexHullShape>( &shapeA ) || std::get_if<ConvexHullShape>( &shapeB );
                const bool hasSphere = std::get_if<BoundingSphere>( &shapeA ) || std::get_if<BoundingSphere>( &shapeB );
                hasRestingFootprint = !hasConvexHull || hasSphere || manifold.pointCount >= 2;
                for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
                {
                    const ObjectContactPoint& point = manifold.points[pointIndex];

                    // CATTO REF:
                    //   rA/rB are the r1/r2 contact arms in Catto 2005, PDF p. 6,
                    //   Equations 9-11 and PDF p. 9, Equations 16-18.
                    PersistentContact c;
                    c.bodyA = aIndex;
                    c.bodyB = bIndex;
                    c.featureId = point.featureId;
                    c.key = makeKey( aIndex, bIndex, c.featureId );
                    c.normal = manifold.normal;
                    c.rA = point.rA;
                    c.rB = point.rB;
                    c.penetration = point.penetration;
                    c.supportsRestingPolicy = hasRestingFootprint;
                    c.normalCoupledFriction = !hasRestingFootprint;
                    c.manifoldPointCount = manifold.pointCount;
                    m_persistentContacts.push_back( c );
                    ++m_persistentContactCounts[aIndex];
                    ++m_persistentContactCounts[bIndex];
                    if ( c.supportsRestingPolicy )
                    {
                        ++m_persistentRestingContactCounts[aIndex];
                        ++m_persistentRestingContactCounts[bIndex];
                    }

                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::ManifoldRow;
                    record.bodyA = aIndex;
                    record.bodyB = bIndex;
                    record.featureId = point.featureId;
                    record.point = point.point;
                    record.normal = manifold.normal;
                    record.scalarA = point.penetration;
                    record.scalarB = static_cast<float>( pointIndex );
                    record.scalarC = static_cast<float>( manifold.pointCount );
                    RecordPhysicsPipelineStage( record );
                }
                hasContact = manifold.pointCount > 0;
            }

            if ( !hasContact )
            {
                continue;
            }

            MarkCollisionVisualContact( aIndex );
            MarkCollisionVisualContact( bIndex );
            appendSleepSupportEdge( aIndex, bIndex, contactNormal, hasRestingFootprint );
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Terrain" );
        PROFILE_SCOPED( "Frame/Physics/Terrain/Rows" );

        // Convert terrain manifolds into the same PersistentContact rows used by
        // object/object contacts. Terrain uses TERRAIN_BODY_INDEX for body B, so
        // later solver phases treat it as infinite mass, zero velocity, and no
        // writeback. From this point on, terrain response is ordinary shared-row
        // normal/friction solving.
        size_t terrainRowCount = 0;
        for ( const Physics::TerrainContactManifold& manifold : m_terrainContactManifolds )
        {
            terrainRowCount += manifold.pointCount;
        }
        m_persistentContacts.reserve( m_persistentContacts.size() + terrainRowCount );

        for ( const Physics::TerrainContactManifold& manifold : m_terrainContactManifolds )
        {
            // Skip invalid/no-op manifolds before they affect profiler counts,
            // pipeline records, or the warm-start cache. Sleeping bodies do not
            // need fresh terrain rows; their accepted support state is already
            // represented by the sleep island data.
            if ( manifold.bodyA < 0 ||
                 manifold.bodyA >= modelCount ||
                 manifold.pointCount == 0 ||
                 ( manifold.bodyA < static_cast<int>( m_sleepState.size() ) && m_sleepState[manifold.bodyA] ) )
            {
                continue;
            }

            Physics::PhysicsPipelineRecord manifoldRecord;
            manifoldRecord.stage = Physics::PhysicsPipelineStage::TerrainManifold;
            manifoldRecord.bodyA = manifold.bodyA;
            manifoldRecord.bodyB = TERRAIN_BODY_INDEX;
            manifoldRecord.point = manifold.points[0].point;
            manifoldRecord.normal = manifold.normal;
            manifoldRecord.scalarA = static_cast<float>( manifold.pointCount );
            manifoldRecord.scalarB = manifold.supportsRestingPolicy ? 1.0f : 0.0f;
            manifoldRecord.scalarC = manifold.timeOfImpact;
            RecordPhysicsPipelineStage( manifoldRecord );

            // Stable terrain support receives a gravity-sized normal seed so a
            // resting body does not sink a little before the solver rediscovers
            // the support force. Edge/point terrain contacts deliberately get
            // zero here: they still resolve impact and penetration, but cannot
            // become sleep anchors or rest-friction anchors.
            const float warmStartTotal = manifold.supportsRestingPolicy
                                             ? m_gameModels[manifold.bodyA].GetMass() * fabsf( Cfg().gravity ) * fabsf( manifold.normal.y ) * dt
                                             : 0.0f;
            const float warmStartPerContact = warmStartTotal / static_cast<float>( manifold.pointCount );

            for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
            {
                const Physics::TerrainContactPoint& point = manifold.points[pointIndex];

                PersistentContact c;
                c.bodyA = manifold.bodyA;
                c.bodyB = TERRAIN_BODY_INDEX;
                c.featureId = point.featureId;
                c.key = makeKey( c.bodyA, c.bodyB, c.featureId );

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
                m_persistentContacts.push_back( c );

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
                RecordPhysicsPipelineStage( rowRecord );
            }
        }
    }

    if ( m_persistentContacts.empty() )
    {
        m_persistentContactCache.clear();
        m_physicsDebugContacts.clear();
        return;
    }
    m_persistentContactSolverStats.rowCount = static_cast<int>( m_persistentContacts.size() );
    const SolverBodyState staticTerrainBody;

    // Second pass: precompute each row. This is the "setup" part of the paper:
    // CATTO REF:
    //   Catto 2005, PDF p. 17, Algorithm 4 initializes d_i from Jsp*Bsp before
    //   iteration. PDF p. 14, Equations 34-35 define B = M^-1*J^T. The code
    //   below expands that sparse matrix math into scalar effective masses.
    // The setup below builds friction axes, effective masses, bias, friction
    // limits, and pulls the previous frame's accumulated impulses from the cache.
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/Precompute" );
        for ( PersistentContact& c : m_persistentContacts )
        {
            GameModel& a = m_gameModels[c.bodyA];
            const SolverBodyState& bodyA = m_solverBodies[c.bodyA];
            const SolverBodyState& bodyB = c.isTerrain ? staticTerrainBody : m_solverBodies[c.bodyB];

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
            auto applyInvInertiaA = [&]( const Vector3& v ) -> Vector3
            {
                return applyInvInertia( c.bodyA, v );
            };
            auto applyInvInertiaB = [&]( const Vector3& v ) -> Vector3
            {
                return c.isTerrain ? ZERO_VECTOR : applyInvInertia( c.bodyB, v );
            };
            c.normalMass = Physics::ContactSolver::ComputeTwoBodyEffectiveMass(
                bodyA.invMass,
                bodyB.invMass,
                c.normal,
                c.rA,
                c.rB,
                applyInvInertiaA,
                applyInvInertiaB );
            c.tangentMass1 = Physics::ContactSolver::ComputeTwoBodyEffectiveMass(
                bodyA.invMass,
                bodyB.invMass,
                c.tangent1,
                c.rA,
                c.rB,
                applyInvInertiaA,
                applyInvInertiaB );
            c.tangentMass2 = Physics::ContactSolver::ComputeTwoBodyEffectiveMass(
                bodyA.invMass,
                bodyB.invMass,
                c.tangent2,
                c.rA,
                c.rB,
                applyInvInertiaA,
                applyInvInertiaB );
            if ( !c.allowsTangentFriction )
            {
                c.tangentMass1 = 0.0f;
                c.tangentMass2 = 0.0f;
            }

            Vector3 velA = bodyA.linearVelocity + Vector::CrossProduct( bodyA.angularVelocity, c.rA );
            Vector3 velB = c.isTerrain ? ZERO_VECTOR : bodyB.linearVelocity + Vector::CrossProduct( bodyB.angularVelocity, c.rB );
            float vn = ( velB - velA ) * c.normal;

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
                const float terrainSlop = (std::max)( 0.0f, Cfg().terrainContactSlop );
                if ( !c.supportsRestingPolicy &&
                     c.penetration <= terrainSlop &&
                     vn > -Cfg().contactRestitutionThreshold )
                {
                    c.normalMass = 0.0f;
                    c.tangentMass1 = 0.0f;
                    c.tangentMass2 = 0.0f;
                }
                else if ( fabsf( vn ) < Cfg().contactRestitutionThreshold )
                {
                    float penetrationError = c.penetration - terrainSlop;
                    if ( penetrationError > 0.0f )
                    {
                        const float terrainBeta = (std::max)( 0.0f, Cfg().terrainContactBaumgarteBeta );
                        const float maxTerrainBias = (std::max)( 0.0f, Cfg().terrainMaxBaumgarteBias );
                        c.bias = terrainBeta * penetrationError * invDt;
                        if ( c.bias > maxTerrainBias )
                        {
                            c.bias = maxTerrainBias;
                        }
                    }
                }
                else if ( vn < -Cfg().contactRestitutionThreshold )
                {
                    const uint8_t pointCount = c.manifoldPointCount > 0 ? c.manifoldPointCount : 1;
                    c.bias = ( -a.GetCoefficientRestitution() * vn ) / static_cast<float>( pointCount );
                }
            }
            else if ( vn < -Cfg().contactRestitutionThreshold )
            {
                GameModel& b = m_gameModels[c.bodyB];
                float restitution = sqrtf( a.GetCoefficientRestitution() * b.GetCoefficientRestitution() );
                c.bias = -restitution * vn;
            }
            else if ( vn >= -Cfg().contactRestitutionThreshold )
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

            uint16_t countA = ( m_persistentContactCounts[c.bodyA] > 0 ) ? m_persistentContactCounts[c.bodyA] : 1;
            float contactMass = a.GetMass() / static_cast<float>( countA );
            if ( !c.isTerrain )
            {
                GameModel& b = m_gameModels[c.bodyB];
                uint16_t countB = ( m_persistentContactCounts[c.bodyB] > 0 ) ? m_persistentContactCounts[c.bodyB] : 1;
                float contactMassB = b.GetMass() / static_cast<float>( countB );
                if ( contactMassB < contactMass )
                {
                    contactMass = contactMassB;
                }
            }
            // CATTO REF:
            //   Catto 2005, PDF p. 12, Section 4.3, Equations 24-25 bound tangent
            //   lambdas by +/-mu*m_c*g. Reason: avoid coupling tangent friction to
            //   solved normal force while keeping static friction usable in games.
            c.frictionLimit = c.isTerrain
                                  ? ( c.allowsTangentFriction ? Cfg().frictionCoeff * c.terrainWarmStart : 0.0f )
                                  : ( c.normalCoupledFriction ? 0.0f : Cfg().frictionCoeff * contactMass * fabsf( Cfg().gravity ) * dt );

            // CATTO REF:
            //   Catto 2005, PDF pp. 18-19, Section 8.1 and Algorithm 5. Reason:
            //   retrieve cached lambda for matching contact identifiers and use it
            //   as the initial lambda_0 for Algorithm 4.
            // Warm starting: if this same pair+feature was touching last frame,
            // start from the cached solution instead of zero.  The cache is sorted so
            // lookup does not linearly scan every previous-frame contact.
            const bool canUseCachedWarmStart = c.supportsRestingPolicy;
            auto cachedIt = canUseCachedWarmStart
                                ? std::lower_bound(
                                      m_persistentContactCache.begin(),
                                      m_persistentContactCache.end(),
                                      c.key,
                                      []( const PersistentContactCacheEntry& entry, int64_t key )
                                      {
                                          return entry.key < key;
                                      } )
                                : m_persistentContactCache.end();
            if ( canUseCachedWarmStart && cachedIt != m_persistentContactCache.end() && cachedIt->key == c.key )
            {
                ++m_persistentContactSolverStats.cacheHits;
                c.accN = ( cachedIt->accN > 0.0f ) ? cachedIt->accN : 0.0f;
                c.accT1 = cachedIt->accT1;
                c.accT2 = cachedIt->accT2;
                const float cachedFrictionLimit = !c.allowsTangentFriction
                                                      ? 0.0f
                                                      : ( c.isTerrain
                                                              ? Cfg().frictionCoeff * ( ( c.accN > c.terrainWarmStart ) ? c.accN : c.terrainWarmStart )
                                                              : ( c.normalCoupledFriction ? Cfg().frictionCoeff * c.accN : c.frictionLimit ) );
                Physics::ContactSolver::ClampFrictionVector( c.accT1, c.accT2, cachedFrictionLimit );
                c.warmStarted = c.accN > 0.0f || fabsf( c.accT1 ) > 0.0f || fabsf( c.accT2 ) > 0.0f;
            }
            else if ( canUseCachedWarmStart )
            {
                ++m_persistentContactSolverStats.cacheMisses;
            }

            if ( c.isTerrain && c.terrainWarmStart > c.accN )
            {
                c.accN = c.terrainWarmStart;
                c.warmStarted = c.accN > 0.0f || c.warmStarted;
            }

            if ( c.warmStarted )
            {
                ++m_persistentContactSolverStats.warmStartedRows;
            }

            {
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::WarmStart;
                record.bodyA = c.bodyA;
                record.bodyB = c.bodyB;
                record.featureId = c.featureId;
                record.point = m_gameModels[c.bodyA].GetPosition() + c.rA;
                record.normal = c.normal;
                record.scalarA = c.warmStarted ? 1.0f : 0.0f;
                record.scalarB = c.accN;
                record.scalarC = c.frictionLimit;
                RecordPhysicsPipelineStage( record );
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
                applyImpulse( c, warmImpulse );
            }
        }
    }

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
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/SolveRows" );
        for ( int iter = 0; iter < solverIterations; ++iter )
        {
            m_persistentContactSolverStats.solverIterations = iter + 1;
            float iterImpulseSq = 0.0f;
            for ( PersistentContact& c : m_persistentContacts )
            {
                SolverBodyState& a = m_solverBodies[c.bodyA];
                const SolverBodyState& b = c.isTerrain ? staticTerrainBody : m_solverBodies[c.bodyB];

                Vector3 velA = a.linearVelocity + Vector::CrossProduct( a.angularVelocity, c.rA );
                Vector3 velB = c.isTerrain ? ZERO_VECTOR : b.linearVelocity + Vector::CrossProduct( b.angularVelocity, c.rB );
                float vn = ( velB - velA ) * c.normal;
                float lambdaN = c.normalMass * ( c.bias - vn );
                float oldAccN = c.accN;

                // CATTO REF:
                //   Catto 2005, PDF p. 8, Section 3.5, Equation 14 and PDF p. 9,
                //   Equation 19 set the normal lower bound to zero.
                // Normal impulses are one-way. Contacts can push bodies apart, but
                // they cannot glue bodies together, so the accumulated value is >= 0.
                c.accN = ( oldAccN + lambdaN > 0.0f ) ? oldAccN + lambdaN : 0.0f;
                float deltaN = c.accN - oldAccN;
                applyImpulse( c, c.normal * deltaN );

                velA = a.linearVelocity + Vector::CrossProduct( a.angularVelocity, c.rA );
                velB = c.isTerrain ? ZERO_VECTOR : b.linearVelocity + Vector::CrossProduct( b.angularVelocity, c.rB );
                float vt1 = ( velB - velA ) * c.tangent1;
                float vt2 = ( velB - velA ) * c.tangent2;
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
                const float frictionLimit = !c.allowsTangentFriction
                                                ? 0.0f
                                                : ( c.isTerrain
                                                        ? Cfg().frictionCoeff * ( ( c.accN > c.terrainWarmStart ) ? c.accN : c.terrainWarmStart )
                                                        : ( c.normalCoupledFriction ? Cfg().frictionCoeff * c.accN : c.frictionLimit ) );
                Physics::ContactSolver::ClampFrictionVector( c.accT1, c.accT2, frictionLimit );
                float deltaT1 = c.accT1 - oldAccT1;
                float deltaT2 = c.accT2 - oldAccT2;
                applyImpulse( c, c.tangent1 * deltaT1 + c.tangent2 * deltaT2 );

                iterImpulseSq += deltaN * deltaN + deltaT1 * deltaT1 + deltaT2 * deltaT2;

                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::SolverIteration;
                record.bodyA = c.bodyA;
                record.bodyB = c.bodyB;
                record.iteration = iter;
                record.featureId = c.featureId;
                record.point = m_gameModels[c.bodyA].GetPosition() + c.rA;
                record.normal = c.normal;
                record.scalarA = deltaN;
                record.scalarB = c.accN;
                record.scalarC = sqrtf( c.accT1 * c.accT1 + c.accT2 * c.accT2 );
                RecordPhysicsPipelineStage( record );
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
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/PointSupportInstability" );
        for ( const PersistentContact& c : m_persistentContacts )
        {
            applyPointSupportInstability( c );
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Terrain" );
        PROFILE_SCOPED( "Frame/Physics/Terrain/RestPolicy" );

        // This is intentionally separate from the row solver. The rows above
        // handle physical contact response; this pass applies engine rest policy
        // only for manifolds that the terrain classifier marked as stable
        // support. That separation keeps unstable edge/corner terrain contacts
        // from gaining rolling damping or sleep privileges just because their
        // impact rows solved successfully.
        std::fill_n( m_terrainRestApplied.begin(), static_cast<size_t>( modelCount ), static_cast<uint8_t>( 0 ) );
        for ( const Physics::TerrainContactManifold& manifold : m_terrainContactManifolds )
        {
            const int bodyIndex = manifold.bodyA;
            if ( bodyIndex < 0 ||
                 bodyIndex >= modelCount ||
                 m_terrainRestApplied[bodyIndex] ||
                 !manifold.supportsRestingPolicy ||
                 m_sleepState[bodyIndex] ||
                 m_soaIsFixed[bodyIndex] )
            {
                continue;
            }

            m_terrainRestApplied[bodyIndex] = 1;
            GameModel& model = m_gameModels[bodyIndex];
            SolverBodyState& body = m_solverBodies[bodyIndex];
            float normalForce = model.GetMass() * fabsf( Cfg().gravity ) * fabsf( manifold.normal.y );
            float omegaMagSq = body.angularVelocity * body.angularVelocity;
            if ( omegaMagSq > TOLERANCE * TOLERANCE )
            {
                // Approximate rolling friction as a torque opposite angular
                // velocity. The effective radius is exact for spheres and a
                // conservative average extent for boxes, enough to bleed tiny
                // residual spin without adding a shape-specific response path.
                float omegaMag = sqrtf( omegaMagSq );
                float rEff = std::visit( []( const auto& shape ) -> float
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
                    } },
                                         model.GetCollisionShape() );

                constexpr float muRolling = 0.02f;
                float rollingTorqueMag = muRolling * normalForce * rEff;
                const Vector3& inertia = model.GetRotationalInertia();
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
            if ( ( body.linearVelocity * body.linearVelocity ) < sleepLinear * sleepLinear &&
                 ( body.angularVelocity * body.angularVelocity ) < sleepAngular * sleepAngular )
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
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/WriteBack" );
        for ( int i = 0; i < modelCount; ++i )
        {
            if ( m_sleepState[i] || m_soaIsFixed[i] )
            {
                continue;
            }

            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::VelocityWriteback;
            record.bodyA = i;
            record.point = m_gameModels[i].GetPosition();
            record.scalarA = Vector::VectorMag( m_solverBodies[i].linearVelocity );
            record.scalarB = Vector::VectorMag( m_solverBodies[i].angularVelocity );
            RecordPhysicsPipelineStage( record );

            m_gameModels[i].SetLinearVelocity( m_solverBodies[i].linearVelocity );
            m_gameModels[i].SetAngularVelocity( m_solverBodies[i].angularVelocity );
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/DebugContacts" );
        m_physicsDebugContacts.clear();
        m_physicsDebugContacts.reserve( m_persistentContacts.size() );
        for ( const PersistentContact& c : m_persistentContacts )
        {
            if ( c.accN > 0.0f )
            {
                if ( m_gameModels[c.bodyA].IsFixed() )
                {
                    MarkFixedContact( c.bodyA );
                }
                if ( c.bodyB != TERRAIN_BODY_INDEX && m_gameModels[c.bodyB].IsFixed() )
                {
                    MarkFixedContact( c.bodyB );
                }
            }

            Physics::PhysicsDebugContact out;
            out.bodyA = c.bodyA;
            out.bodyB = c.bodyB;
            out.featureId = c.featureId;
            out.point = m_gameModels[c.bodyA].GetPosition() + c.rA;
            out.normal = c.isTerrain ? c.terrainNormal : c.normal;
            out.tangent1 = c.tangent1;
            out.tangent2 = c.tangent2;
            out.penetration = c.penetration;
            out.normalImpulse = c.accN;
            m_physicsDebugContacts.push_back( out );
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
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/PositionCorrection" );
        for ( const PersistentContact& c : m_persistentContacts )
        {
            const float rowContactSlop = c.isTerrain ? (std::max)( 0.0f, Cfg().terrainContactSlop ) : contactSlop;
            if ( c.penetration <= rowContactSlop )
            {
                continue;
            }

            GameModel& a = m_gameModels[c.bodyA];
            float invMassA = ( m_sleepState[c.bodyA] || a.IsFixed() ) ? 0.0f : a.GetInvertedMass();
            float invMassB = 0.0f;
            GameModel* b = nullptr;
            if ( c.bodyB != TERRAIN_BODY_INDEX )
            {
                b = &m_gameModels[c.bodyB];
                invMassB = ( m_sleepState[c.bodyB] || b->IsFixed() ) ? 0.0f : b->GetInvertedMass();
            }
            float totalInvMass = invMassA + invMassB;
            if ( totalInvMass <= TOLERANCE )
            {
                continue;
            }

            const float rowPositionCorrectionPercent = c.isTerrain ? 0.4f : positionCorrectionPercent;
            Vector3 correction = c.normal * ( ( c.penetration - rowContactSlop ) * rowPositionCorrectionPercent / totalInvMass );
            float correctionMagnitude = Vector::VectorMag( correction );
            ++m_persistentContactSolverStats.positionCorrectionRows;
            m_persistentContactSolverStats.positionCorrectionTotal += correctionMagnitude;
            if ( correctionMagnitude > m_persistentContactSolverStats.positionCorrectionMax )
            {
                m_persistentContactSolverStats.positionCorrectionMax = correctionMagnitude;
            }
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::PositionCorrection;
            record.bodyA = c.bodyA;
            record.bodyB = c.bodyB;
            record.featureId = c.featureId;
            record.point = a.GetPosition() + c.rA;
            record.normal = c.normal;
            record.scalarA = correctionMagnitude;
            record.scalarB = c.penetration;
            record.scalarC = rowContactSlop;
            RecordPhysicsPipelineStage( record );
            a.SetPosition( a.GetPosition() - correction * invMassA );
            if ( b )
            {
                b->SetPosition( b->GetPosition() + correction * invMassB );
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
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/CacheStore" );
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

            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::CacheStore;
            record.bodyA = c.bodyA;
            record.bodyB = c.bodyB;
            record.featureId = c.featureId;
            record.point = m_gameModels[c.bodyA].GetPosition() + c.rA;
            record.normal = c.normal;
            record.scalarA = c.accN;
            record.scalarB = c.accT1;
            record.scalarC = c.accT2;
            RecordPhysicsPipelineStage( record );
        }

        if ( m_persistentContactCache.size() > 1 )
        {
            std::sort( m_persistentContactCache.begin(),
                       m_persistentContactCache.end(),
                       []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                       {
                           return lhs.key < rhs.key;
                       } );
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/FixedContactRelease" );
        auto releaseFixedContactBody = [&]( int fixedIndex, int otherIndex, const PersistentContact& c, bool fixedIsBodyA )
        {
            if ( fixedIndex < 0 ||
                 fixedIndex >= modelCount ||
                 otherIndex < 0 ||
                 otherIndex >= modelCount )
            {
                return;
            }

            GameModel& fixedModel = m_gameModels[fixedIndex];
            GameModel& otherModel = m_gameModels[otherIndex];
            if ( !fixedModel.IsFixed() ||
                 !fixedModel.ReleasesFromFixedOnContact() ||
                 otherModel.IsFixed() ||
                 otherModel.ReleasesFromFixedOnContact() ||
                 c.accN < fixedModel.GetContactReleaseImpulseThreshold() )
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

            const float mass = (std::max)( 0.001f, fixedModel.GetMass() );
            const float impulseSpeed = c.accN / mass;
            const Vector3 otherVelocity = otherModel.GetVelocity();
            const float carriedSpeed = (std::max)( 0.0f, otherVelocity * releaseDir );
            const float releaseSpeed = std::clamp( (std::max)( impulseSpeed, carriedSpeed * 0.35f ), 1.5f, 36.0f );

            Vector3 tangentVelocity = otherVelocity - releaseDir * ( otherVelocity * releaseDir );
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
                const float radius = (std::max)( 0.25f, fixedModel.GetBoundingRadius() );
                angularVelocity = spinAxis * ( std::clamp( releaseSpeed / radius, 0.0f, 8.0f ) / spinAxisMag );
            }

            fixedModel.SetFixed( false );
            fixedModel.SetLinearVelocity( releaseDir * releaseSpeed + tangentVelocity );
            fixedModel.SetAngularVelocity( angularVelocity );
            world.WakeModel( collection, fixedIndex );
            collection.ReleaseAttachedFixedTreeParts( fixedIndex, fixedModel.GetVelocity(), fixedModel.GetAngularVelocity() );
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
}
