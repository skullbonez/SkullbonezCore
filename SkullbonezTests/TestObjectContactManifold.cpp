//
// File: SkullbonezTests/TestObjectContactManifold.cpp
// Purpose:
//   Locks deterministic box-contact manifold reduction at the narrowphase boundary.
//
// Mental model:
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
//
// Related:
//   - SkullbonezSource/Physics/ObjectContactManifold.cpp
//   - Agentic/Plans/TODO/behavioral-test-depth.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/ObjectContactManifold.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuildObjectContactManifold;
using SkullbonezCore::Physics::ObjectContactBodyView;
using SkullbonezCore::Physics::ObjectContactManifold;

namespace
{
constexpr float kContactSkin = 0.001f;

CollisionShape MakeBox( const Vector3& halfExtents = Vector3( 1.0f, 1.0f, 1.0f ) )
{
    return CollisionShape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
}

ObjectContactBodyView MakeBody( const Vector3& position,
                                const Vector3& rotationAxis = Vector3( 1.0f, 0.0f, 0.0f ),
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

ObjectContactManifold BuildBoxManifold( const ObjectContactBodyView& a,
                                        const CollisionShape& shapeA,
                                        const ObjectContactBodyView& b,
                                        const CollisionShape& shapeB )
{
    ObjectContactManifold manifold;
    const bool hit = BuildObjectContactManifold( a, shapeA, b, shapeB, 11, 29, kContactSkin, manifold );
    REQUIRE( hit );
    return manifold;
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

    const ObjectContactManifold coplanar =
        BuildBoxManifold( base, unitBox, MakeBody( Vector3( 0.0f, 2.0f, 0.0f ) ), unitBox );
    CheckFiniteManifold( coplanar );

    // A zero-height slab is a useful editor/import boundary case. Narrowphase
    // must return bounded data rather than introducing NaNs into solver rows.
    const CollisionShape slab = MakeBox( Vector3( 0.75f, 0.0f, 0.75f ) );
    const ObjectContactManifold degenerate =
        BuildBoxManifold( base, unitBox, MakeBody( Vector3( 0.0f, 1.0f, 0.0f ) ), slab );
    CheckFiniteManifold( degenerate );
}
