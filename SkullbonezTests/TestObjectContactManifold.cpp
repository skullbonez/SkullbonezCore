//
// File: SkullbonezTests/TestObjectContactManifold.cpp
// Purpose:
//   Locks geometric and deterministic object-contact manifolds at the narrowphase boundary.
//
// Summary:
//   Hand-derived shape fixtures pin the normal, penetration, row count, and
//   point placement supplied to the persistent solver. Face clipping may
//   produce more candidates than the four-row solver budget, so reduction must
//   also retain the deepest row, useful patch coverage, and stable feature ids.
//   Frame-pair and cache fixtures make that identity lifetime visible through
//   the solver boundary rather than treating it as an encoding detail. Planted
//   faults prove the same geometry and identity predicates reject bad output.
//
// Glossary:
//   Contact candidate: A clipped point that is eligible for a solver row.
//   Feature id: Deterministic key used to match the same contact across steps.
//   Degenerate slab: A box shape with a zero half-extent on one axis.
//
// Invariants:
//   - An object manifold contains at most four finite points.
//   - Every sphere, box, and convex-hull family is checked against geometry
//     derived from the authored pose and dimensions, never captured output.
//   - The first reduced point is the deepest candidate; remaining rows favor
//     spatial coverage and retain deterministic feature ids.
//   - Rebuilding an unchanged contact produces identical row order and ids.
//   - Sub-slop pose changes retain feature identity, while a true feature
//     change misses the persistent-contact cache.
//   - Candidate selection is insertion-order independent: deepest geometry,
//     then tangent spread, then feature identity owns every tie.
//   - Every geometry/identity predicate used by a positive fixture also rejects
//     a focused planted fault; negative controls do not substitute a second oracle.
//   - Every sphere, box, and convex-hull pairing publishes finite contacts,
//     while a separated pair remains contact-free.
//
// Related:
//   - SkullbonezSource/Physics/ObjectContactManifold.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestResultLoadFixtures.h"

#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Physics/ObjectContactManifold.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h"
#include "TestCollisionShapeFixtures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::CrossProduct;
using SkullbonezCore::Math::Vector::Dot;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuildObjectContactManifold;
using SkullbonezCore::Physics::ObjectContactBodyView;
using SkullbonezCore::Physics::ObjectContactCandidate;
using SkullbonezCore::Physics::ObjectContactCandidateSelection;
using SkullbonezCore::Physics::ObjectContactManifold;
using SkullbonezCore::Physics::PersistentContactCacheEntry;
using SkullbonezCore::Physics::PersistentContactCacheList;
using SkullbonezCore::Physics::PersistentContactSolveTransaction;
using SkullbonezCore::Physics::SelectObjectContactCandidateIndices;
using SkullbonezCore::Physics::SweepObjectContact;
using SkullbonezTests::CollisionShapeFixtures::BoxShape;
using SkullbonezTests::CollisionShapeFixtures::SphereShape;

namespace
{
constexpr float kContactSkin = 0.001f;
constexpr std::array<uint32_t, 4> kSpreadReductionFeatureIds = { 100u, 20u, 30u, 40u };

CollisionShape MakeBox( const Vector3& halfExtents = Vector3( 1.0f, 1.0f, 1.0f ) )
{
    return CollisionShape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
}

std::array<Vector3, 4> UnitBoxFacePatch( float contactPlaneX )
{
    return { Vector3( contactPlaneX, -1.0f, -1.0f ), Vector3( contactPlaneX, -1.0f, 1.0f ),
             Vector3( contactPlaneX, 1.0f, -1.0f ), Vector3( contactPlaneX, 1.0f, 1.0f ) };
}

ObjectContactBodyView MakeBody( const Vector3& position, Vector3 rotationAxis = Vector3( 1.0f, 0.0f, 0.0f ),
                                float rotationRadians = 0.0f )
{
    ObjectContactBodyView body;
    body.position = position;

    if ( rotationRadians != 0.0f )
    {

        // Invariant: tilted manifold fixtures describe a direction, not an
        // angle scale. Normalize arbitrary diagonals before axis-angle rotation.
        rotationAxis.Normalise();
        body.orientation.RotateAboutAxis( rotationAxis, rotationRadians );
    }

    return body;
}

ObjectContactManifold BuildManifold( const ObjectContactBodyView& a, const CollisionShape& shapeA,
                                     const ObjectContactBodyView& b, const CollisionShape& shapeB,
                                     float contactSkin = kContactSkin )
{
    ObjectContactManifold manifold;
    const bool hit = BuildObjectContactManifold( a, shapeA, b, shapeB, 11, 29, contactSkin, manifold );
    REQUIRE( hit );
    return manifold;
}

bool VectorNear( const Vector3& actual, const Vector3& expected, float tolerance = 2.0e-4f )
{
    return fabsf( actual.x - expected.x ) <= tolerance && fabsf( actual.y - expected.y ) <= tolerance &&
           fabsf( actual.z - expected.z ) <= tolerance;
}

void CheckVectorNear( const Vector3& actual, const Vector3& expected, float tolerance = 2.0e-4f )
{
    CHECK( VectorNear( actual, expected, tolerance ) );
}

template <std::size_t PointCount>
bool PointSetMatches( const ObjectContactManifold& manifold, const std::array<Vector3, PointCount>& expected,
                      float tolerance = 2.0e-4f )
{

    if ( static_cast<std::size_t>( manifold.pointCount ) != PointCount )
    {
        return false;
    }

    std::array<bool, PointCount> matched = {};

    for ( const Vector3& expectedPoint : expected )
    {
        bool found = false;

        for ( std::size_t actualIndex = 0; actualIndex < PointCount; ++actualIndex )
        {

            if ( !matched[actualIndex] && VectorNear( manifold.points[actualIndex].point, expectedPoint, tolerance ) )
            {
                matched[actualIndex] = true;
                found = true;
                break;
            }
        }

        if ( !found )
        {
            return false;
        }
    }

    return true;
}

template <std::size_t PointCount>
void CheckPointSet( const ObjectContactManifold& manifold, const std::array<Vector3, PointCount>& expected,
                    float tolerance = 2.0e-4f )
{
    CHECK( PointSetMatches( manifold, expected, tolerance ) );
}

bool UniformPenetrationMatches( const ObjectContactManifold& manifold, float expected, float tolerance = 2.0e-4f )
{

    if ( manifold.pointCount == 0 )
    {
        return false;
    }

    for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
    {

        if ( fabsf( manifold.points[pointIndex].penetration - expected ) > tolerance )
        {
            return false;
        }
    }

    return true;
}

void CheckUniformPenetration( const ObjectContactManifold& manifold, float expected, float tolerance = 2.0e-4f )
{
    CHECK( UniformPenetrationMatches( manifold, expected, tolerance ) );
}

bool FeatureIdsEqual( const ObjectContactManifold& first, const ObjectContactManifold& second )
{

    if ( first.pointCount == 0 || first.pointCount != second.pointCount )
    {
        return false;
    }

    for ( uint8_t pointIndex = 0; pointIndex < first.pointCount; ++pointIndex )
    {

        if ( first.points[pointIndex].featureId != second.points[pointIndex].featureId )
        {
            return false;
        }
    }

    return true;
}

void CheckFeatureIdsEqual( const ObjectContactManifold& first, const ObjectContactManifold& second )
{
    CHECK( FeatureIdsEqual( first, second ) );
}

std::array<ObjectContactCandidate, 6> MakeSpreadReductionCandidates()
{
    return {
        ObjectContactCandidate { Vector3( 0.0f, 0.0f, 0.0f ), 0.40f, 100u },
        ObjectContactCandidate { Vector3( 2.0f, 0.0f, 0.0f ), 0.20f, 20u },
        ObjectContactCandidate { Vector3( -2.0f, 0.0f, 0.0f ), 0.20f, 30u },
        ObjectContactCandidate { Vector3( 0.0f, 2.0f, 0.0f ), 0.20f, 40u },
        ObjectContactCandidate { Vector3( 0.0f, -2.0f, 0.0f ), 0.20f, 50u },
        ObjectContactCandidate { Vector3( 1.9f, 0.1f, 0.0f ), 0.30f, 10u },
    };
}

template <std::size_t CandidateCount>
bool SelectionMatchesFeatureIds( const std::array<ObjectContactCandidate, CandidateCount>& candidates,
                                 const ObjectContactCandidateSelection& selection,
                                 const std::array<uint32_t, 4>& expectedFeatureIds )
{

    if ( static_cast<std::size_t>( selection.count ) != expectedFeatureIds.size() )
    {
        return false;
    }

    for ( uint8_t selectedIndex = 0; selectedIndex < selection.count; ++selectedIndex )
    {
        const int candidateIndex = selection.indices[selectedIndex];

        if ( candidateIndex < 0 || candidateIndex >= static_cast<int>( CandidateCount ) ||
             candidates[static_cast<std::size_t>( candidateIndex )].featureId != expectedFeatureIds[selectedIndex] )
        {
            return false;
        }
    }

    return true;
}

std::array<Vector3, 3> WorldAxes( const ObjectContactBodyView& body )
{
    const auto rotation = body.orientation.GetOrientationMatrix();
    return { rotation * Vector3( 1.0f, 0.0f, 0.0f ), rotation * Vector3( 0.0f, 1.0f, 0.0f ),
             rotation * Vector3( 0.0f, 0.0f, 1.0f ) };
}

float ExtentComponent( const Vector3& halfExtents, int axis )
{

    if ( axis == 0 )
    {
        return halfExtents.x;
    }

    if ( axis == 1 )
    {
        return halfExtents.y;
    }

    return halfExtents.z;
}

float ProjectionRadius( const std::array<Vector3, 3>& axes, const Vector3& halfExtents, const Vector3& normal )
{
    float radius = 0.0f;

    for ( int axis = 0; axis < 3; ++axis )
    {
        radius += ExtentComponent( halfExtents, axis ) * fabsf( Dot( axes[axis], normal ) );
    }

    return radius;
}

Vector3 SupportVertexOffset( const std::array<Vector3, 3>& axes, const Vector3& halfExtents, const Vector3& direction )
{
    Vector3 offset( 0.0f, 0.0f, 0.0f );

    for ( int axis = 0; axis < 3; ++axis )
    {
        const float sign = Dot( axes[axis], direction ) >= 0.0f ? 1.0f : -1.0f;
        offset += axes[axis] * ( sign * ExtentComponent( halfExtents, axis ) );
    }

    return offset;
}

Vector3 SupportEdgeCenterOffset( const std::array<Vector3, 3>& axes, const Vector3& halfExtents, int edgeAxis,
                                 const Vector3& direction )
{
    Vector3 offset( 0.0f, 0.0f, 0.0f );

    for ( int axis = 0; axis < 3; ++axis )
    {

        if ( axis == edgeAxis )
        {
            continue;
        }

        const float sign = Dot( axes[axis], direction ) >= 0.0f ? 1.0f : -1.0f;
        offset += axes[axis] * ( sign * ExtentComponent( halfExtents, axis ) );
    }

    return offset;
}

void ClosestSegmentPoints( const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2, Vector3& closestA,
                           Vector3& closestB )
{

    // Invariant: the positive edge fixture uses non-parallel, interior-intersection
    // segments. Its nonzero denominator keeps this analytic calculation out of
    // the production helper and independent of production edge selection.
    const Vector3 d1 = q1 - p1;
    const Vector3 d2 = q2 - p2;
    const Vector3 r = p1 - p2;
    const float a = Dot( d1, d1 );
    const float e = Dot( d2, d2 );
    const float b = Dot( d1, d2 );
    const float c = Dot( d1, r );
    const float f = Dot( d2, r );
    const float denominator = a * e - b * b;
    const float s = (std::max)( 0.0f, (std::min)( 1.0f, ( b * f - c * e ) / denominator ) );
    const float t = (std::max)( 0.0f, (std::min)( 1.0f, ( b * s + f ) / e ) );
    closestA = p1 + d1 * s;
    closestB = p2 + d2 * t;
}

const ConvexHullShape& BrickHull()
{
    static ConvexHullShape hull;
    static const bool
        loaded = SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics,
                                                                         "SkullbonezData/hulls/building_brick_unit.hull",
                                                                         hull );

    REQUIRE( loaded );
    return hull;
}

Vector3 BrickHalfExtents()
{

    // Invariant: these are the exact baked vertex maxima in
    // building_brick_unit.hull. Keeping them explicit makes every expected
    // point configuration-derived and causes authored-asset drift to fail.
    return Vector3( 1.45f, 0.72f, 0.34f );
}

void CheckDerivedEdgeEdgeConfiguration( const CollisionShape& shapeA, const CollisionShape& shapeB,
                                        const Vector3& halfExtents )
{
    constexpr float overlap = 0.05f;
    ObjectContactBodyView bodyA = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ), 0.35f );
    ObjectContactBodyView bodyB = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 1.0f ), 0.50f );
    const auto axesA = WorldAxes( bodyA );
    const auto axesB = WorldAxes( bodyB );
    Vector3 expectedNormal = CrossProduct( axesA[0], axesB[0] );
    expectedNormal.Normalise();

    // Concept: place the two long-axis support edges with exactly `overlap`
    // along their shared separating axis. Every face axis then has a wider
    // overlap, so the one-row result is analytically edge/edge rather than a
    // face fixture that merely happens to return one point.
    const float radiusA = ProjectionRadius( axesA, halfExtents, expectedNormal );
    const float radiusB = ProjectionRadius( axesB, halfExtents, expectedNormal );
    bodyB.position = expectedNormal * ( radiusA + radiusB - overlap );

    const Vector3 edgeCenterA = bodyA.position + SupportEdgeCenterOffset( axesA, halfExtents, 0, expectedNormal );
    const Vector3 edgeCenterB = bodyB.position + SupportEdgeCenterOffset( axesB, halfExtents, 0, -expectedNormal );
    const Vector3 edgeHalfA = axesA[0] * halfExtents.x;
    const Vector3 edgeHalfB = axesB[0] * halfExtents.x;
    Vector3 closestA;
    Vector3 closestB;
    ClosestSegmentPoints( edgeCenterA - edgeHalfA, edgeCenterA + edgeHalfA, edgeCenterB - edgeHalfB, edgeCenterB + edgeHalfB,
                          closestA, closestB );

    const ObjectContactManifold manifold = BuildManifold( bodyA, shapeA, bodyB, shapeB );
    CheckVectorNear( manifold.normal, expectedNormal );
    CheckUniformPenetration( manifold, overlap );
    CheckPointSet( manifold, std::array<Vector3, 1> { ( closestA + closestB ) * 0.5f } );
}

void CheckContactPair( const ObjectContactBodyView& a, const CollisionShape& shapeA, const ObjectContactBodyView& b,
                       const CollisionShape& shapeB )
{
    ObjectContactManifold manifold;
    REQUIRE( BuildObjectContactManifold( a, shapeA, b, shapeB, 3, 7, 0.02f, manifold ) );
    REQUIRE( manifold.pointCount > 0u );
    CHECK( manifold.bodyA == 3 );
    CHECK( manifold.bodyB == 7 );
    CHECK( std::isfinite( manifold.normal.x ) );
    CHECK( std::isfinite( manifold.normal.y ) );
    CHECK( std::isfinite( manifold.normal.z ) );

    for ( uint8_t point = 0; point < manifold.pointCount; ++point )
    {
        CHECK( std::isfinite( manifold.points[point].penetration ) );
        CHECK( manifold.points[point].penetration >= -0.02f );
    }
}

void CheckFiniteManifold( const ObjectContactManifold& manifold )
{
    REQUIRE( manifold.pointCount > 0 );
    REQUIRE( manifold.pointCount <= 4 );
    CHECK( std::isfinite( manifold.normal.x ) );
    CHECK( std::isfinite( manifold.normal.y ) );
    CHECK( std::isfinite( manifold.normal.z ) );

    for ( uint8_t i = 0; i < manifold.pointCount; ++i )
    {
        const auto& point = manifold.points[i];
        CHECK( std::isfinite( point.point.x ) );
        CHECK( std::isfinite( point.point.y ) );
        CHECK( std::isfinite( point.point.z ) );
        CHECK( std::isfinite( point.penetration ) );
        CHECK( point.penetration >= 0.0f );
    }
}
} // namespace


TEST_CASE( "Object contact manifold: unchanged box stack keeps four stable face rows" )
{
    const CollisionShape box = MakeBox();
    const ObjectContactBodyView lower = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    const ObjectContactBodyView upper = MakeBody( Vector3( 0.0f, 1.5f, 0.0f ) );

    const ObjectContactManifold firstStep = BuildManifold( lower, box, upper, box );
    const ObjectContactManifold secondStep = BuildManifold( lower, box, upper, box );

    REQUIRE( firstStep.pointCount == 4 );
    REQUIRE( secondStep.pointCount == firstStep.pointCount );
    CheckFiniteManifold( firstStep );
    CheckFiniteManifold( secondStep );

    for ( uint8_t i = 0; i < firstStep.pointCount; ++i )
    {
        CHECK( secondStep.points[i].featureId == firstStep.points[i].featureId );
        CHECK( secondStep.points[i].point.x == doctest::Approx( firstStep.points[i].point.x ) );
        CHECK( secondStep.points[i].point.y == doctest::Approx( firstStep.points[i].point.y ) );
        CHECK( secondStep.points[i].point.z == doctest::Approx( firstStep.points[i].point.z ) );
        CHECK( secondStep.points[i].penetration == doctest::Approx( firstStep.points[i].penetration ) );

        for ( uint8_t j = static_cast<uint8_t>( i + 1 ); j < firstStep.pointCount; ++j )
        {
            CHECK( firstStep.points[i].featureId != firstStep.points[j].featureId );
        }
    }
}


TEST_CASE( "Object contact manifold: reduced tilted face starts with deepest retained point" )
{
    const CollisionShape box = MakeBox();
    const ObjectContactBodyView reference = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );

    // These deterministic poses exercise clipped face polygons whose depths are
    // not uniform. A first-four truncation leaves a shallower row at index zero;
    // deepest-first reduction makes the solver's most important row explicit.
    const ObjectContactBodyView tilted[] = {
        MakeBody( Vector3( 0.10f, 1.55f, -0.10f ), Vector3( 1.0f, 0.0f, 1.0f ), 0.22f ),
        MakeBody( Vector3( -0.15f, 1.60f, 0.12f ), Vector3( 1.0f, 0.0f, -0.5f ), 0.28f ),
        MakeBody( Vector3( 0.05f, 1.50f, 0.08f ), Vector3( 0.7f, 0.0f, 1.0f ), 0.18f ),
    };

    bool observedFourPointDepthVariation = false;

    for ( const ObjectContactBodyView& incident : tilted )
    {
        const ObjectContactManifold manifold = BuildManifold( reference, box, incident, box );
        CheckFiniteManifold( manifold );

        if ( manifold.pointCount != 4 )
        {
            continue;
        }

        float deepestReturned = manifold.points[0].penetration;
        float shallowestReturned = manifold.points[0].penetration;

        for ( uint8_t i = 1; i < manifold.pointCount; ++i )
        {
            deepestReturned = (std::max)( deepestReturned, manifold.points[i].penetration );
            shallowestReturned = (std::min)( shallowestReturned, manifold.points[i].penetration );
        }

        if ( deepestReturned - shallowestReturned > 1.0e-4f )
        {
            observedFourPointDepthVariation = true;
            CHECK( manifold.points[0].penetration == doctest::Approx( deepestReturned ).epsilon( 1.0e-5 ) );
        }
    }

    REQUIRE( observedFourPointDepthVariation );
}


TEST_CASE( "Object contact manifold: coplanar face and degenerate slab stay finite and nonempty" )
{
    const CollisionShape unitBox = MakeBox();
    const ObjectContactBodyView base = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );

    const ObjectContactManifold coplanar = BuildManifold( base, unitBox, MakeBody( Vector3( 0.0f, 2.0f, 0.0f ) ), unitBox );

    CheckFiniteManifold( coplanar );

    // A zero-height slab is a useful editor/import boundary case. Narrowphase
    // must return bounded data rather than introducing NaNs into solver rows.
    const CollisionShape slab = MakeBox( Vector3( 0.75f, 0.0f, 0.75f ) );
    const ObjectContactManifold degenerate = BuildManifold( base, unitBox, MakeBody( Vector3( 0.0f, 1.0f, 0.0f ) ), slab );

    CheckFiniteManifold( degenerate );
}


TEST_CASE( "Object contact manifold: boundary-band feature selection is stable across ten evaluations" )
{
    const CollisionShape box = MakeBox();
    const ObjectContactBodyView lower = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );

    // Concept: exact face contact sits on a feature-selection boundary. The
    // selected side is less important than returning the same ordered rows on
    // every evaluation, because those feature ids key the warm-start cache.
    const ObjectContactBodyView upper = MakeBody( Vector3( 0.0f, 2.0f, 0.0f ) );
    const ObjectContactManifold baseline = BuildManifold( lower, box, upper, box );

    for ( int repeat = 0; repeat < 10; ++repeat )
    {
        const ObjectContactManifold current = BuildManifold( lower, box, upper, box );
        REQUIRE( current.pointCount == baseline.pointCount );
        CHECK( current.normal.x == doctest::Approx( baseline.normal.x ) );
        CHECK( current.normal.y == doctest::Approx( baseline.normal.y ) );
        CHECK( current.normal.z == doctest::Approx( baseline.normal.z ) );

        for ( uint8_t pointIndex = 0; pointIndex < baseline.pointCount; ++pointIndex )
        {
            CHECK( current.points[pointIndex].featureId == baseline.points[pointIndex].featureId );
            CHECK( current.points[pointIndex].penetration == doctest::Approx( baseline.points[pointIndex].penetration ) );
        }
    }
}


TEST_CASE( "Object contact manifold geometry: sphere pairs and sphere-box boundaries are analytic" )
{
    const CollisionShape unitSphere = SphereShape( 1.0f );
    const ObjectContactBodyView origin = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold spherePair = BuildManifold( origin, unitSphere, MakeBody( Vector3( 1.5f, 0.0f, 0.0f ) ),
                                                            unitSphere );

    CheckVectorNear( spherePair.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( spherePair, 0.5f );
    CheckPointSet( spherePair, std::array<Vector3, 1> { Vector3( 0.75f, 0.0f, 0.0f ) } );

    const CollisionShape sphere = SphereShape( 0.5f );
    const CollisionShape box = MakeBox();
    const ObjectContactBodyView boxBody = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    struct SphereBoxCase
    {
        float centerX;
        float contactSkin;
        float penetration;
        float pointX;
    };
    const SphereBoxCase cases[] = {
        { 1.4f, kContactSkin, 0.1f, 0.95f }, // Overlapping outside face.
        { 1.5f, kContactSkin, 0.0f, 1.0f },  // Exact surface contact.
        { 1.5005f, 0.001f, 0.0f, 1.00025f }, // Separated, but inside contact skin.
        { 0.8f, kContactSkin, 0.7f, 0.3f },  // Center inside; nearest +X face owns escape.
    };

    for ( const SphereBoxCase& testCase : cases )
    {
        CAPTURE( testCase.centerX );
        const ObjectContactBodyView sphereBody = MakeBody( Vector3( testCase.centerX, 0.0f, 0.0f ) );
        const ObjectContactManifold manifold = BuildManifold( sphereBody, sphere, boxBody, box, testCase.contactSkin );
        CheckVectorNear( manifold.normal, Vector3( -1.0f, 0.0f, 0.0f ) );
        CheckUniformPenetration( manifold, testCase.penetration );
        CheckPointSet( manifold, std::array<Vector3, 1> { Vector3( testCase.pointX, 0.0f, 0.0f ) } );
    }

    const ObjectContactBodyView sphereBody = MakeBody( Vector3( 1.4f, 0.0f, 0.0f ) );
    const ObjectContactManifold reversed = BuildManifold( boxBody, box, sphereBody, sphere );
    CheckVectorNear( reversed.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( reversed, 0.1f );
    CheckPointSet( reversed, std::array<Vector3, 1> { Vector3( 0.95f, 0.0f, 0.0f ) } );
}


TEST_CASE( "Object contact manifold geometry: sphere-hull inside surface and skin contacts are analytic" )
{
    const CollisionShape sphere = SphereShape( 0.25f );
    const CollisionShape hull = BrickHull();
    const ObjectContactBodyView hullBody = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    struct SphereHullCase
    {
        float centerX;
        float contactSkin;
        float penetration;
        float pointX;
    };
    const SphereHullCase cases[] = {
        { 1.65f, kContactSkin, 0.05f, 1.425f }, // Outside overlap against +X face.
        { 1.70f, kContactSkin, 0.0f, 1.45f },   // Exact surface contact.
        { 1.7005f, 0.001f, 0.0f, 1.45025f },    // Skin-only near contact.
        { 1.30f, kContactSkin, 0.40f, 1.25f },  // Inside, nearest +X hull face.
    };

    for ( const SphereHullCase& testCase : cases )
    {
        CAPTURE( testCase.centerX );
        const ObjectContactBodyView sphereBody = MakeBody( Vector3( testCase.centerX, 0.0f, 0.0f ) );
        const ObjectContactManifold manifold = BuildManifold( sphereBody, sphere, hullBody, hull, testCase.contactSkin );
        CheckVectorNear( manifold.normal, Vector3( -1.0f, 0.0f, 0.0f ) );
        CheckUniformPenetration( manifold, testCase.penetration );
        CheckPointSet( manifold, std::array<Vector3, 1> { Vector3( testCase.pointX, 0.0f, 0.0f ) } );
    }

    const ObjectContactBodyView sphereBody = MakeBody( Vector3( 1.65f, 0.0f, 0.0f ) );
    const ObjectContactManifold reversed = BuildManifold( hullBody, hull, sphereBody, sphere );
    CheckVectorNear( reversed.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( reversed, 0.05f );
    CheckPointSet( reversed, std::array<Vector3, 1> { Vector3( 1.425f, 0.0f, 0.0f ) } );

    // A 0.20 diagonal offset from each authored boundary feature leaves 0.05
    // overlap for the radius-0.25 sphere. The contact point is halfway between
    // the feature and sphere surface, hence the 0.025 inward offset.
    const Vector3 edgePoint( 1.45f, 0.72f, 0.0f );
    Vector3 edgeOutward( 1.0f, 1.0f, 0.0f );
    edgeOutward.Normalise();
    const ObjectContactBodyView edgeSphereBody = MakeBody( edgePoint + edgeOutward * 0.20f );
    const ObjectContactManifold edge = BuildManifold( edgeSphereBody, sphere, hullBody, hull );
    CheckVectorNear( edge.normal, -edgeOutward );
    CheckUniformPenetration( edge, 0.05f );
    CheckPointSet( edge, std::array<Vector3, 1> { edgePoint - edgeOutward * 0.025f } );

    const Vector3 vertexPoint( 1.45f, 0.72f, 0.34f );
    Vector3 vertexOutward( 1.0f, 1.0f, 1.0f );
    vertexOutward.Normalise();
    const ObjectContactBodyView vertexSphereBody = MakeBody( vertexPoint + vertexOutward * 0.20f );
    const ObjectContactManifold vertex = BuildManifold( vertexSphereBody, sphere, hullBody, hull );
    CheckVectorNear( vertex.normal, -vertexOutward );
    CheckUniformPenetration( vertex, 0.05f );
    CheckPointSet( vertex, std::array<Vector3, 1> { vertexPoint - vertexOutward * 0.025f } );
}


TEST_CASE( "Object contact manifold geometry: box face patches and deep overlap are hand-derived" )
{
    const CollisionShape box = MakeBox();
    const ObjectContactBodyView bodyA = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    const ObjectContactBodyView bodyB = MakeBody( Vector3( 1.8f, 0.0f, 0.0f ) );
    const ObjectContactManifold face = BuildManifold( bodyA, box, bodyB, box );
    CheckVectorNear( face.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( face, 0.2f );
    CheckPointSet( face, UnitBoxFacePatch( 0.9f ) );

    // Coincident unit boxes overlap by their full two-unit width. Box SAT
    // inspects A's +X axis first among the tied minimum axes, so the centered
    // patch and its deterministic +X normal are configuration-derived.
    const ObjectContactManifold deep = BuildManifold( bodyA, box, bodyA, box );
    CheckVectorNear( deep.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( deep, 2.0f );
    CheckPointSet( deep, UnitBoxFacePatch( 0.0f ) );
}


TEST_CASE( "Object contact manifold geometry: box face-edge vertex-face and edge-edge placements are analytic" )
{
    constexpr float quarterTurn = 0.78539816339f;
    constexpr float overlap = 0.10f;
    const Vector3 unitExtents( 1.0f, 1.0f, 1.0f );
    const CollisionShape unitBox = MakeBox();
    const ObjectContactBodyView reference = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );

    ObjectContactBodyView faceEdge = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ), quarterTurn );
    const auto edgeAxes = WorldAxes( faceEdge );
    const Vector3 targetEdgeCenter( 1.0f - overlap, 0.0f, 0.0f );
    faceEdge.position = targetEdgeCenter - SupportEdgeCenterOffset( edgeAxes, unitExtents, 1, Vector3( -1.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold edge = BuildManifold( reference, unitBox, faceEdge, unitBox );
    CheckVectorNear( edge.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( edge, overlap );
    CheckPointSet( edge, std::array<Vector3, 2> { Vector3( 0.95f, -1.0f, 0.0f ), Vector3( 0.95f, 1.0f, 0.0f ) } );

    ObjectContactBodyView vertex = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), 0.50f );
    const auto vertexAxes = WorldAxes( vertex );
    const Vector3 targetVertex( 1.0f - 0.05f, 0.0f, 0.0f );
    vertex.position = targetVertex - SupportVertexOffset( vertexAxes, unitExtents, Vector3( -1.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold vertexFace = BuildManifold( reference, unitBox, vertex, unitBox );
    CheckVectorNear( vertexFace.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( vertexFace, 0.05f );
    CheckPointSet( vertexFace, std::array<Vector3, 1> { Vector3( 0.975f, 0.0f, 0.0f ) } );

    const Vector3 brickExtents = BrickHalfExtents();
    const CollisionShape brickBox = BoxShape( brickExtents );
    CheckDerivedEdgeEdgeConfiguration( brickBox, brickBox, brickExtents );
}


TEST_CASE( "Object contact manifold geometry: mixed box-hull face contact preserves ordered normals" )
{
    const Vector3 halfExtents = BrickHalfExtents();
    const CollisionShape box = BoxShape( halfExtents );
    const CollisionShape hull = BrickHull();
    const ObjectContactBodyView bodyA = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    const ObjectContactBodyView bodyB = MakeBody( Vector3( 2.80f, 0.0f, 0.0f ) );
    const std::array<Vector3, 4> expected = { Vector3( 1.40f, -0.72f, -0.34f ), Vector3( 1.40f, -0.72f, 0.34f ),
                                              Vector3( 1.40f, 0.72f, -0.34f ), Vector3( 1.40f, 0.72f, 0.34f ) };

    const ObjectContactManifold boxHull = BuildManifold( bodyA, box, bodyB, hull );
    CheckVectorNear( boxHull.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( boxHull, 0.10f );
    CheckPointSet( boxHull, expected );

    const ObjectContactManifold hullBox = BuildManifold( bodyB, hull, bodyA, box );
    CheckVectorNear( hullBox.normal, Vector3( -1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( hullBox, 0.10f );
    CheckPointSet( hullBox, expected );
}


TEST_CASE( "Object contact manifold geometry: mixed box-hull edge and vertex placements are analytic" )
{
    constexpr float quarterTurn = 0.78539816339f;
    const Vector3 halfExtents = BrickHalfExtents();
    const CollisionShape box = BoxShape( halfExtents );
    const CollisionShape hull = BrickHull();
    const ObjectContactBodyView reference = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );

    ObjectContactBodyView faceEdge = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ), quarterTurn );
    const auto edgeAxes = WorldAxes( faceEdge );
    const Vector3 targetEdgeCenter( halfExtents.x - 0.05f, 0.0f, 0.0f );
    faceEdge.position = targetEdgeCenter - SupportEdgeCenterOffset( edgeAxes, halfExtents, 1, Vector3( -1.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold edge = BuildManifold( reference, box, faceEdge, hull );
    CheckVectorNear( edge.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( edge, 0.05f );

    // Mixed polytope clipping makes the same deliberate reference choice as
    // hull/hull: retain the four-row clipped box face rather than the legal
    // two-row incident hull edge alternative.
    CheckPointSet( edge, std::array<Vector3, 4> { Vector3( 1.425f, -0.72f, -0.34f ), Vector3( 1.425f, -0.72f, 0.0f ),
                                                  Vector3( 1.425f, 0.72f, -0.34f ), Vector3( 1.425f, 0.72f, 0.0f ) } );

    ObjectContactBodyView vertex = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), 0.50f );
    const auto vertexAxes = WorldAxes( vertex );
    const Vector3 targetVertex( halfExtents.x - 0.05f, 0.0f, 0.0f );
    vertex.position = targetVertex - SupportVertexOffset( vertexAxes, halfExtents, Vector3( -1.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold vertexFace = BuildManifold( reference, box, vertex, hull );
    CheckVectorNear( vertexFace.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( vertexFace, 0.05f );
    CheckPointSet( vertexFace, std::array<Vector3, 1> { Vector3( 1.425f, 0.0f, 0.0f ) } );

    CheckDerivedEdgeEdgeConfiguration( box, hull, halfExtents );
}


TEST_CASE( "Object contact manifold geometry: hull face patches and deep overlap are hand-derived" )
{
    const Vector3 halfExtents = BrickHalfExtents();
    const CollisionShape hull = BrickHull();
    const ObjectContactBodyView bodyA = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    const ObjectContactBodyView bodyB = MakeBody( Vector3( 2.80f, 0.0f, 0.0f ) );
    const ObjectContactManifold face = BuildManifold( bodyA, hull, bodyB, hull );
    CheckVectorNear( face.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( face, 0.10f );
    CheckPointSet( face, std::array<Vector3, 4> { Vector3( 1.40f, -0.72f, -0.34f ), Vector3( 1.40f, -0.72f, 0.34f ),
                                                  Vector3( 1.40f, 0.72f, -0.34f ), Vector3( 1.40f, 0.72f, 0.34f ) } );

    // The authored brick's thinnest axis is Z. Coincident hulls therefore tie
    // on the two Z faces; source face order selects -Z and centers the patch.
    const ObjectContactManifold deep = BuildManifold( bodyA, hull, bodyA, hull );
    CheckVectorNear( deep.normal, Vector3( 0.0f, 0.0f, -1.0f ) );
    CheckUniformPenetration( deep, halfExtents.z * 2.0f );
    CheckPointSet( deep, std::array<Vector3, 4> { Vector3( -1.45f, -0.72f, 0.0f ), Vector3( -1.45f, 0.72f, 0.0f ),
                                                  Vector3( 1.45f, -0.72f, 0.0f ), Vector3( 1.45f, 0.72f, 0.0f ) } );
}


TEST_CASE( "Object contact manifold geometry: hull face-edge vertex-face and edge-edge placements are analytic" )
{
    constexpr float quarterTurn = 0.78539816339f;
    const Vector3 halfExtents = BrickHalfExtents();
    const CollisionShape hull = BrickHull();
    const ObjectContactBodyView reference = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );

    ObjectContactBodyView faceEdge = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ), quarterTurn );
    const auto edgeAxes = WorldAxes( faceEdge );
    const Vector3 targetEdgeCenter( halfExtents.x - 0.05f, 0.0f, 0.0f );
    faceEdge.position = targetEdgeCenter - SupportEdgeCenterOffset( edgeAxes, halfExtents, 1, Vector3( -1.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold edge = BuildManifold( reference, hull, faceEdge, hull );
    CheckVectorNear( edge.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( edge, 0.05f );

    // The incident hull presents its vertical support edge to A's +X face.
    // Hull clipping evaluates both legal reference faces and retains the
    // four-row alternative: the two -Z corners of A's face plus two crossings
    // at the support-edge plane, all projected halfway in X. That
    // policy-derived patch distinguishes this path from the two-row box
    // face/edge fixture above.
    CheckPointSet( edge, std::array<Vector3, 4> { Vector3( 1.425f, -0.72f, -0.34f ), Vector3( 1.425f, -0.72f, 0.0f ),
                                                  Vector3( 1.425f, 0.72f, -0.34f ), Vector3( 1.425f, 0.72f, 0.0f ) } );

    ObjectContactBodyView vertex = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), 0.50f );
    const auto vertexAxes = WorldAxes( vertex );
    const Vector3 targetVertex( halfExtents.x - 0.05f, 0.0f, 0.0f );
    vertex.position = targetVertex - SupportVertexOffset( vertexAxes, halfExtents, Vector3( -1.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold vertexFace = BuildManifold( reference, hull, vertex, hull );
    CheckVectorNear( vertexFace.normal, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckUniformPenetration( vertexFace, 0.05f );
    CheckPointSet( vertexFace, std::array<Vector3, 1> { Vector3( 1.425f, 0.0f, 0.0f ) } );

    CheckDerivedEdgeEdgeConfiguration( hull, hull, halfExtents );
}


TEST_CASE( "Object contact manifold identity: resting box and hull rows survive sub-slop motion" )
{
    const ObjectContactBodyView lower = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );

    // Invariant: the second pose moves only 1e-5 along the face normal, one
    // hundredth of contact skin. Geometry depth may change, but the same
    // authored faces and vertices must still own every warm-start identity.
    const CollisionShape box = MakeBox();
    const ObjectContactManifold boxFrameN = BuildManifold( lower, box, MakeBody( Vector3( 0.0f, 1.50f, 0.0f ) ), box );
    const ObjectContactManifold boxFrameN1 = BuildManifold( lower, box, MakeBody( Vector3( 0.0f, 1.50001f, 0.0f ) ), box );
    CheckFeatureIdsEqual( boxFrameN, boxFrameN1 );

    const CollisionShape hull = BrickHull();
    const ObjectContactManifold hullFrameN = BuildManifold( lower, hull, MakeBody( Vector3( 0.0f, 1.40f, 0.0f ) ), hull );
    const ObjectContactManifold hullFrameN1 = BuildManifold( lower, hull, MakeBody( Vector3( 0.0f, 1.40001f, 0.0f ) ),
                                                             hull );

    CheckFeatureIdsEqual( hullFrameN, hullFrameN1 );
}


TEST_CASE( "Object contact manifold identity: the 45-degree incident-face boundary changes once" )
{
    constexpr float quarterTurn = 0.78539816339f;
    constexpr float quarterDegree = 0.00436332313f;
    const CollisionShape box = MakeBox();
    const ObjectContactBodyView reference = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    uint32_t previousIncidentCode = 0u;
    int transitionCount = 0;

    for ( int sweepStep = -20; sweepStep <= 20; ++sweepStep )
    {
        CAPTURE( sweepStep );
        const ObjectContactBodyView incident = MakeBody( Vector3( 1.60f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ),
                                                         quarterTurn + quarterDegree * sweepStep );

        const ObjectContactManifold manifold = BuildManifold( reference, box, incident, box );
        REQUIRE( manifold.pointCount > 0 );
        const uint32_t referenceCode = ( manifold.points[0].featureId >> 10 ) & 0x0fu;
        const uint32_t incidentCode = ( manifold.points[0].featureId >> 6 ) & 0x0fu;
        CHECK( referenceCode == 1u ); // Body A's +X face remains the reference.

        for ( uint8_t pointIndex = 1; pointIndex < manifold.pointCount; ++pointIndex )
        {
            CHECK( ( ( manifold.points[pointIndex].featureId >> 10 ) & 0x0fu ) == referenceCode );
            CHECK( ( ( manifold.points[pointIndex].featureId >> 6 ) & 0x0fu ) == incidentCode );
        }

        if ( sweepStep < 0 )
        {
            CHECK( incidentCode == 8u ); // Body B's -X face.
        }
        else if ( sweepStep > 0 )
        {
            CHECK( incidentCode == 12u ); // Body B's -Z face.
        }
        else
        {
            const bool choseEitherTiedFace = incidentCode == 8u || incidentCode == 12u;

            CHECK( choseEitherTiedFace );
        }

        if ( sweepStep > -20 && incidentCode != previousIncidentCode )
        {
            ++transitionCount;
        }

        previousIncidentCode = incidentCode;
    }

    CHECK( transitionCount == 1 );
}


TEST_CASE( "Object contact manifold reduction: deepest feature tie spread and permutations are deterministic" )
{
    const Vector3 normal( 0.0f, 0.0f, 1.0f );
    CHECK( SelectObjectContactCandidateIndices( nullptr, 0, normal ).count == 0 );

    const std::array<ObjectContactCandidate, 2> depthCandidates = {
        ObjectContactCandidate { Vector3( -1.0f, 0.0f, 0.0f ), 0.20f, 1u },
        ObjectContactCandidate { Vector3( 1.0f, 0.0f, 0.0f ), 0.30f, 90u },
    };

    const ObjectContactCandidateSelection depthSelection = SelectObjectContactCandidateIndices( depthCandidates.data(),
                                                                                                static_cast<int>(
                                                                                                    depthCandidates.size() ),
                                                                                                normal );

    REQUIRE( depthSelection.count == 2 );
    CHECK( depthCandidates[depthSelection.indices[0]].featureId == 90u );
    CHECK( SelectObjectContactCandidateIndices( depthCandidates.data(),
                                                SkullbonezCore::Physics::MAX_OBJECT_CONTACT_CANDIDATES + 1, normal )
               .count == 0 );

    const std::array<ObjectContactCandidate, 2> tieCandidates = {
        ObjectContactCandidate { Vector3( -1.0f, 0.0f, 0.0f ), 0.30f, 41u },
        ObjectContactCandidate { Vector3( 1.0f, 0.0f, 0.0f ), 0.30f, 7u },
    };

    const ObjectContactCandidateSelection tieSelection = SelectObjectContactCandidateIndices( tieCandidates.data(),
                                                                                              static_cast<int>(
                                                                                                  tieCandidates.size() ),
                                                                                              normal );

    REQUIRE( tieSelection.count == 2 );
    CHECK( tieCandidates[tieSelection.indices[0]].featureId == 7u );

    const std::array<ObjectContactCandidate, 6> candidates = MakeSpreadReductionCandidates();

    // The center is deepest. Four radius-two points tie on first-step spread
    // and therefore choose feature 20; the opposite and orthogonal points then
    // maximize minimum spread. Feature 10 is deliberately a near neighbor of
    // feature 20, so a neighboring-point reducer would select the wrong set.
    std::array<int, 6> order = { 0, 1, 2, 3, 4, 5 };
    int permutationCount = 0;

    do
    {
        std::array<ObjectContactCandidate, 6> permuted = {};

        for ( std::size_t candidateIndex = 0; candidateIndex < order.size(); ++candidateIndex )
        {
            permuted[candidateIndex] = candidates[static_cast<std::size_t>( order[candidateIndex] )];
        }

        const ObjectContactCandidateSelection selection = SelectObjectContactCandidateIndices( permuted.data(),
                                                                                               static_cast<int>(
                                                                                                   permuted.size() ),
                                                                                               normal );

        CHECK( SelectionMatchesFeatureIds( permuted, selection, kSpreadReductionFeatureIds ) );
        ++permutationCount;
    } while ( std::next_permutation( order.begin(), order.end() ) );

    CHECK( permutationCount == 720 );
}


TEST_CASE( "Object contact manifold identity: a changed narrowphase feature misses the solver cache" )
{
    const CollisionShape sphere = SphereShape( 0.5f );
    const CollisionShape box = MakeBox();
    const ObjectContactBodyView boxBody = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold faceX = BuildManifold( MakeBody( Vector3( 1.40f, 0.0f, 0.0f ) ), sphere, boxBody, box );
    const ObjectContactManifold faceY = BuildManifold( MakeBody( Vector3( 0.0f, 1.40f, 0.0f ) ), sphere, boxBody, box );
    REQUIRE( faceX.pointCount == 1 );
    REQUIRE( faceY.pointCount == 1 );
    REQUIRE( faceX.points[0].featureId != faceY.points[0].featureId );

    // The cache contains a real converged normal impulse under the +X face
    // feature. Holding body ids constant isolates feature identity as the only
    // reason the +Y manifold misses.
    RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
    PersistentContactCacheList cache( "unit.object-manifold-feature-cache",
                                      SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity );
    cache.Reserve( 1u );
    PersistentContactCacheEntry cached;
    cached.key = PersistentContactSolveTransaction::MakeKey( faceX.bodyA, faceX.bodyB, faceX.points[0].featureId );
    cached.accN = 1.0f;
    cache.push_back( cached );

    const std::span<const PersistentContactCacheEntry> cacheRows( cache.data(), cache.size() );
    CHECK( SkullbonezCore::Physics::PersistentContactCacheHasImpulse( cacheRows, faceX.bodyA, faceX.bodyB,
                                                                      faceX.points[0].featureId ) );
    CHECK_FALSE( SkullbonezCore::Physics::PersistentContactCacheHasImpulse( cacheRows, faceY.bodyA, faceY.bodyB,
                                                                            faceY.points[0].featureId ) );
}


TEST_CASE( "Object contact manifold oracles: planted geometry identity and reduction controls fail" )
{
    const CollisionShape box = MakeBox();
    const ObjectContactBodyView origin = MakeBody( Vector3( 0.0f, 0.0f, 0.0f ) );
    const ObjectContactManifold face = BuildManifold( origin, box, MakeBody( Vector3( 1.8f, 0.0f, 0.0f ) ), box );
    const Vector3 expectedNormal( 1.0f, 0.0f, 0.0f );
    const std::array<Vector3, 4> expectedPoints = UnitBoxFacePatch( 0.9f );

    REQUIRE( VectorNear( face.normal, expectedNormal ) );
    REQUIRE( UniformPenetrationMatches( face, 0.2f ) );
    REQUIRE( PointSetMatches( face, expectedPoints ) );

    // Hazard: each copy changes only the named manifold contract. The same
    // predicates used by the positive geometry fixtures must reject it, so a
    // passing control cannot be attributed to an unrelated assertion.
    ObjectContactManifold invertedNormal = face;
    invertedNormal.normal = -invertedNormal.normal;
    CHECK_FALSE( VectorNear( invertedNormal.normal, expectedNormal ) );

    ObjectContactManifold signFlippedPenetration = face;

    for ( uint8_t pointIndex = 0; pointIndex < signFlippedPenetration.pointCount; ++pointIndex )
    {
        signFlippedPenetration.points[pointIndex].penetration *= -1.0f;
    }

    CHECK_FALSE( UniformPenetrationMatches( signFlippedPenetration, 0.2f ) );

    ObjectContactManifold truncatedPatch = face;
    --truncatedPatch.pointCount;
    CHECK_FALSE( PointSetMatches( truncatedPatch, expectedPoints ) );

    const ObjectContactManifold stableFrame = BuildManifold( origin, box, MakeBody( Vector3( 0.0f, 1.50f, 0.0f ) ), box );
    const ObjectContactManifold stableFrameN1 = BuildManifold( origin, box, MakeBody( Vector3( 0.0f, 1.50001f, 0.0f ) ),
                                                               box );

    REQUIRE( FeatureIdsEqual( stableFrame, stableFrameN1 ) );
    ObjectContactManifold unstableFeature = stableFrameN1;
    unstableFeature.points[0].featureId ^= 1u;
    CHECK_FALSE( FeatureIdsEqual( stableFrame, unstableFeature ) );

    const Vector3 reductionNormal( 0.0f, 0.0f, 1.0f );
    const std::array<ObjectContactCandidate, 6> candidates = MakeSpreadReductionCandidates();
    const ObjectContactCandidateSelection productionSelection = SelectObjectContactCandidateIndices( candidates.data(),
                                                                                                     static_cast<int>(
                                                                                                         candidates.size() ),
                                                                                                     reductionNormal );

    REQUIRE( SelectionMatchesFeatureIds( candidates, productionSelection, kSpreadReductionFeatureIds ) );

    // A next-deepest reducer would keep feature 10 immediately beside feature
    // 20 instead of feature 40's orthogonal spread. Plant that exact neighboring
    // selection while preserving the deepest and first spread rows.
    ObjectContactCandidateSelection neighboringSelection;
    neighboringSelection.count = 4;
    neighboringSelection.indices[0] = 0;
    neighboringSelection.indices[1] = 1;
    neighboringSelection.indices[2] = 5;
    neighboringSelection.indices[3] = 2;
    CHECK_FALSE( SelectionMatchesFeatureIds( candidates, neighboringSelection, kSpreadReductionFeatureIds ) );
}


TEST_CASE( "Coverage floor contract: every object manifold shape pair publishes contacts" )
{
    const CollisionShape sphere = SphereShape( 2.0f );
    const CollisionShape box = BoxShape( Vector3( 2.0f, 2.0f, 2.0f ) );
    SkullbonezCore::Math::CollisionDetection::ConvexHullShape hullShape;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics, "SkullbonezData/hulls/pyramid.hull",
                                                                     hullShape ) );

    const CollisionShape hull = hullShape;

    ObjectContactBodyView a;
    a.position = Vector3( 0.0f, 0.0f, 0.0f );
    ObjectContactBodyView b;
    b.position = Vector3( 1.0f, 0.0f, 0.0f );

    CheckContactPair( a, sphere, b, sphere );
    CheckContactPair( a, sphere, b, box );
    CheckContactPair( a, box, b, sphere );
    CheckContactPair( a, box, b, box );
    CheckContactPair( a, sphere, b, hull );
    CheckContactPair( a, hull, b, sphere );
    CheckContactPair( a, box, b, hull );
    CheckContactPair( a, hull, b, box );
    CheckContactPair( a, hull, b, hull );

    ObjectContactBodyView farBody = b;
    farBody.position = Vector3( 30.0f, 0.0f, 0.0f );
    ObjectContactManifold separated;
    CHECK_FALSE( BuildObjectContactManifold( a, sphere, farBody, sphere, 0, 1, 0.0f, separated ) );
    CHECK_FALSE( BuildObjectContactManifold( a, sphere, farBody, hull, 0, 1, 0.0f, separated ) );

    ObjectContactBodyView moving = a;
    moving.position = Vector3( -5.0f, 0.0f, 0.0f );
    ObjectContactBodyView target = a;
    const auto sweep = SweepObjectContact( moving, sphere, Vector3( 10.0f, 0.0f, 0.0f ), target, sphere,
                                           Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );

    CHECK( sweep.hit );
    CHECK( sweep.collisionTime >= 0.0f );
    CHECK( sweep.collisionTime <= 1.0f );
}


TEST_CASE( "Object CCD: rotated local centres determine sphere and box sweep origins" )
{
    const Quaternion orientation( 0.0f, 0.0f, 0.70710677f, 0.70710677f );
    const Vector3 localOffset( 4.0f, 0.0f, 0.0f );
    const Vector3 desiredMovingCenter( -5.0f, 0.0f, 0.0f );
    const Vector3 movingBodyPosition = desiredMovingCenter - orientation.GetOrientationMatrix() * localOffset;
    const CollisionShape movingShapes[] = {
        BoundingSphere( 1.0f, localOffset ),
        BoundingBox( Vector3( 1.0f, 1.0f, 1.0f ), localOffset ),
    };
    const CollisionShape targetShapes[] = {
        BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) ),
        BoundingBox( Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
    };

    for ( int shapeIndex = 0; shapeIndex < 2; ++shapeIndex )
    {
        ObjectContactBodyView moving;
        moving.position = movingBodyPosition;
        moving.orientation = orientation;
        ObjectContactBodyView target;
        target.position = Vector3( 0.0f, 0.0f, 0.0f );

        const auto sweep = SweepObjectContact( moving, movingShapes[shapeIndex], Vector3( 10.0f, 0.0f, 0.0f ), target,
                                               targetShapes[shapeIndex], Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
        REQUIRE( sweep.hit );
        CHECK( sweep.collisionTime >= 0.0f );
        CHECK( sweep.collisionTime <= 1.0f );
    }
}


TEST_CASE( "Object CCD: sphere crosses a thin tall box without a bounding-sphere proxy miss" )
{
    const CollisionShape sphere = BoundingSphere( 0.1f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const CollisionShape thinWall = BoundingBox( Vector3( 0.005f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const Vector3 sphereVelocity( 1.0f, 0.0f, 0.0f );
    const Vector3 stationary( 0.0f, 0.0f, 0.0f );
    ObjectContactBodyView movingSphere;
    movingSphere.position = Vector3( 0.0f, 0.0f, 0.0f );
    ObjectContactBodyView wall;
    wall.position = Vector3( 0.5f, 0.0f, 0.0f );

    const auto sphereFirst = SweepObjectContact( movingSphere, sphere, sphereVelocity, wall, thinWall, stationary, 1.0f );
    const auto wallFirst = SweepObjectContact( wall, thinWall, stationary, movingSphere, sphere, sphereVelocity, 1.0f );

    REQUIRE( sphereFirst.hit );
    REQUIRE( wallFirst.hit );
    CHECK( sphereFirst.collisionTime == doctest::Approx( 0.395f ).epsilon( 0.0001f ) );
    CHECK( wallFirst.collisionTime == doctest::Approx( sphereFirst.collisionTime ).epsilon( 0.0001f ) );

    movingSphere.position.y = 1.2f;
    CHECK_FALSE( SweepObjectContact( movingSphere, sphere, sphereVelocity, wall, thinWall, stationary, 1.0f ).hit );
}


TEST_CASE( "Object CCD: shallow oblique sphere motion reaches a box face" )
{
    const CollisionShape sphere = BoundingSphere( 0.1f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const CollisionShape box = BoundingBox( Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const Vector3 shallowObliqueVelocity( -0.001f, 0.1f, 0.0f );
    const Vector3 stationary( 0.0f, 0.0f, 0.0f );
    ObjectContactBodyView movingSphere;
    movingSphere.position = Vector3( 1.1005f, 0.0f, 0.0f );
    ObjectContactBodyView stationaryBox;

    const auto sweep =
        SweepObjectContact( movingSphere, sphere, shallowObliqueVelocity, stationaryBox, box, stationary, 1.0f );

    REQUIRE( sweep.hit );
    CHECK( sweep.collisionTime == doctest::Approx( 0.5f ).epsilon( 0.0001f ) );
}
