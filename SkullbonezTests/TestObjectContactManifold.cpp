//
// File: SkullbonezTests/TestObjectContactManifold.cpp
// Purpose:
//   Locks deterministic box-contact manifold reduction at the narrowphase boundary.
//
// Summary:
//   Box clipping may produce more contact candidates than the four rows the
//   persistent solver can consume. Reduction must keep the deepest candidate,
//   retain useful face coverage, and produce stable feature ids for warm starting.
//
// Glossary:
//   Contact candidate: A clipped point that is eligible for a solver row.
//   Feature id: Deterministic key used to match the same contact across steps.
//   Degenerate slab: A box shape with a zero half-extent on one axis.
//
// Invariants:
//   - A box manifold contains at most four finite points.
//   - The first reduced point is the deepest candidate; remaining rows favor
//     spatial coverage and retain deterministic feature ids.
//   - Rebuilding an unchanged contact produces identical row order and ids.
//   - Every sphere, box, and convex-hull pairing publishes finite contacts,
//     while a separated pair remains contact-free.
//
// Related:
//   - SkullbonezSource/Physics/ObjectContactManifold.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Physics/ObjectContactManifold.h"
#include "TestCollisionShapeFixtures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuildObjectContactManifold;
using SkullbonezCore::Physics::ObjectContactBodyView;
using SkullbonezCore::Physics::ObjectContactManifold;
using SkullbonezCore::Physics::SweepObjectContact;
using SkullbonezTests::CollisionShapeFixtures::BoxShape;
using SkullbonezTests::CollisionShapeFixtures::SphereShape;

namespace
{
constexpr float kContactSkin = 0.001f;

CollisionShape MakeBox( const Vector3& halfExtents = Vector3( 1.0f, 1.0f, 1.0f ) )
{
    return CollisionShape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
}

ObjectContactBodyView MakeBody( const Vector3& position, const Vector3& rotationAxis = Vector3( 1.0f, 0.0f, 0.0f ),
                                float rotationRadians = 0.0f )
{
    ObjectContactBodyView body;
    body.position = position;

    if ( rotationRadians != 0.0f )
    {
        body.orientation.RotateAboutAxis( rotationAxis, rotationRadians );
    }

    return body;
}

ObjectContactManifold BuildBoxManifold( const ObjectContactBodyView& a, const CollisionShape& shapeA,
                                        const ObjectContactBodyView& b, const CollisionShape& shapeB )
{
    ObjectContactManifold manifold;
    const bool hit = BuildObjectContactManifold( a, shapeA, b, shapeB, 11, 29, kContactSkin, manifold );
    REQUIRE( hit );
    return manifold;
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

    const ObjectContactManifold firstStep = BuildBoxManifold( lower, box, upper, box );
    const ObjectContactManifold secondStep = BuildBoxManifold( lower, box, upper, box );

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
        const ObjectContactManifold manifold = BuildBoxManifold( reference, box, incident, box );
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

    const ObjectContactManifold coplanar = BuildBoxManifold( base, unitBox, MakeBody( Vector3( 0.0f, 2.0f, 0.0f ) ),
                                                             unitBox );

    CheckFiniteManifold( coplanar );

    // A zero-height slab is a useful editor/import boundary case. Narrowphase
    // must return bounded data rather than introducing NaNs into solver rows.
    const CollisionShape slab = MakeBox( Vector3( 0.75f, 0.0f, 0.75f ) );
    const ObjectContactManifold degenerate = BuildBoxManifold( base, unitBox, MakeBody( Vector3( 0.0f, 1.0f, 0.0f ) ),
                                                               slab );

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
    const ObjectContactManifold baseline = BuildBoxManifold( lower, box, upper, box );

    for ( int repeat = 0; repeat < 10; ++repeat )
    {
        const ObjectContactManifold current = BuildBoxManifold( lower, box, upper, box );
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


TEST_CASE( "Coverage floor contract: every object manifold shape pair publishes contacts" )
{
    const CollisionShape sphere = SphereShape( 2.0f );
    const CollisionShape box = BoxShape( Vector3( 2.0f, 2.0f, 2.0f ) );
    const CollisionShape hull = SkullbonezCore::Math::CollisionDetection::ConvexHullShape::
        LoadFromFile( diagnostics, "SkullbonezData/hulls/pyramid.hull" );

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
