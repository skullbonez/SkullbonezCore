//
// File: SkullbonezTests/TestBounds.cpp
// Purpose:
//   Lock the first focused tests for BoundingSphere and BoundingBox broadphase contracts.
//
// Mental model:
//   These shape methods are broadphase filters. They answer "could these shapes
//   touch during this movement segment?" by returning a first contact time in
//   [0,1] or NO_COLLISION; exact face, edge, and resting contacts are built by
//   later narrowphase manifold code.
//
// Glossary:
//   Broadphase: Cheap candidate pass that may return false positives but must
//     avoid false negatives.
//   Bounding radius: Conservative sphere radius used to approximate a shape.
//   Swept test: Collision query over a finite movement vector for one frame.
//
// Invariants:
//   - Sphere-sphere static overlap returns NO_COLLISION here; static contact is
//     resolved by later overlap handling.
//   - Box-involved static overlap returns 0.0f because those overloads perform a
//     bounding-radius overlap check.
//   - Box face/edge precision is not owned by these broadphase helpers.
//
// Related:
//   - SkullbonezSource/Physics/BoundingSphere.h
//   - SkullbonezSource/Physics/BoundingBox.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"

#include <cmath>

using SkullbonezCore::Geometry::Ray;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr float kEpsilon = 0.00001f;

void CheckNear( float actual, float expected, float epsilon = kEpsilon )
{
    CHECK( std::fabs( actual - expected ) <= epsilon );
}

Ray Motion( const Vector3& origin, const Vector3& movement )
{
    return Ray( origin, movement );
}
} // namespace


TEST_CASE( "Bounds: sphere broadphase sweep distinguishes hit, miss, and tangent" )
{
    const BoundingSphere focus( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const BoundingSphere target( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );

    CheckNear( focus.TestCollision( target,
                                    Motion( Vector3( 4.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                    Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 4.0f, 0.0f, 0.0f ) ) ),
               0.5f );
    CHECK( focus.TestCollision( target,
                                Motion( Vector3( 5.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 2.0f, 0.0f, 0.0f ) ) ) ==
           NO_COLLISION );
    CheckNear( focus.TestCollision( target,
                                    Motion( Vector3( 4.0f, 2.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                    Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 4.0f, 0.0f, 0.0f ) ) ),
               1.0f );
}


TEST_CASE( "Bounds: sphere static overlap is left for later resolution" )
{
    const BoundingSphere focus( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const BoundingSphere touchingTarget( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const BoundingSphere overlappingTarget( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );

    CHECK( focus.TestCollision( touchingTarget,
                                Motion( Vector3( 2.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) ) ==
           NO_COLLISION );
    CHECK( focus.TestCollision( overlappingTarget,
                                Motion( Vector3( 1.5f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) ) ==
           NO_COLLISION );
}


TEST_CASE( "Bounds: box broadphase uses bounding-radius overlap and sweep times" )
{
    const BoundingBox focus( Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const BoundingBox target( Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const float combinedRadius = focus.GetBoundingRadius() + target.GetBoundingRadius();

    CHECK( focus.TestCollision( target,
                                Motion( Vector3( combinedRadius, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) ) == 0.0f );
    CHECK( focus.TestCollision( target,
                                Motion( Vector3( combinedRadius + 0.01f, 0.0f, 0.0f ),
                                        Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) ) ==
           NO_COLLISION );
    CheckNear( focus.TestCollision( target,
                                    Motion( Vector3( 10.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                    Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 10.0f, 0.0f, 0.0f ) ) ),
               ( 10.0f - combinedRadius ) / 10.0f );
}


TEST_CASE( "Bounds: sphere-box broadphase is symmetric for shared setup" )
{
    const BoundingSphere sphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const BoundingBox box( Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const Ray sphereMotion = Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 4.0f, 0.0f, 0.0f ) );
    const Ray boxMotion = Motion( Vector3( 4.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const float expected = ( 4.0f - ( sphere.GetRadius() + box.GetBoundingRadius() ) ) / 4.0f;

    CheckNear( sphere.TestCollision( box, boxMotion, sphereMotion ), expected );
    CheckNear( box.TestCollision( sphere, sphereMotion, boxMotion ), expected );
}
