//
// File: SkullbonezTests/TestBounds.cpp
// Purpose:
//   Lock BoundingSphere, BoundingBox, and shared collider-transform contracts.
//
// Summary:
//   Swept shape methods answer whether shapes could touch during a movement
//   segment before later narrowphase manifold work. Companion fixtures pin
//   finite default descriptors, offset-inclusive body bounds, and byte-exact
//   render translation from the shared rotated local-collider transform.
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
//   - Debug, Profile, and Release consume the same staged render-translation
//     arithmetic so build configuration cannot alter its float bits.
//
// Related:
//   - SkullbonezSource/Physics/BoundingSphere.h
//   - SkullbonezSource/Physics/BoundingBox.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/CollisionShape.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Maths/Quaternion.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using SkullbonezCore::Geometry::Ray;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::CollisionDetection::GetShapeBodyOriginBoundingRadius;
using SkullbonezCore::Math::CollisionDetection::GetWorldShapeCenter;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
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

    CheckNear( focus.TestCollision( target, Motion( Vector3( 4.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                    Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 4.0f, 0.0f, 0.0f ) ) ),
               0.5f );
    CHECK( focus.TestCollision( target, Motion( Vector3( 5.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 2.0f, 0.0f, 0.0f ) ) ) == NO_COLLISION );
    CheckNear( focus.TestCollision( target, Motion( Vector3( 4.0f, 2.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                    Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 4.0f, 0.0f, 0.0f ) ) ),
               1.0f );
}


TEST_CASE( "Bounds: sphere static overlap is left for later resolution" )
{
    const BoundingSphere focus( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const BoundingSphere touchingTarget( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const BoundingSphere overlappingTarget( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );

    CHECK( focus.TestCollision( touchingTarget, Motion( Vector3( 2.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) ) == NO_COLLISION );
    CHECK( focus.TestCollision( overlappingTarget, Motion( Vector3( 1.5f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) ) == NO_COLLISION );
}


TEST_CASE( "Bounds: box broadphase uses bounding-radius overlap and sweep times" )
{
    const BoundingBox focus( Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const BoundingBox target( Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const float combinedRadius = focus.GetBoundingRadius() + target.GetBoundingRadius();

    CHECK( focus.TestCollision( target, Motion( Vector3( combinedRadius, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) ) == 0.0f );
    CHECK( focus.TestCollision( target, Motion( Vector3( combinedRadius + 0.01f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
                                Motion( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) ) == NO_COLLISION );
    CheckNear( focus.TestCollision( target, Motion( Vector3( 10.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
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


TEST_CASE( "Bounds: default sphere and rotated local offsets share one transform rule" )
{
    const BoundingSphere defaultSphere;
    CHECK( defaultSphere.GetPosition().x == 0.0f );
    CHECK( defaultSphere.GetPosition().y == 0.0f );
    CHECK( defaultSphere.GetPosition().z == 0.0f );
    CHECK( std::isfinite( defaultSphere.GetRadius() ) );
    CHECK( std::isfinite( defaultSphere.GetVolume() ) );
    CHECK( std::isfinite( defaultSphere.GetProjectedSurfaceArea() ) );
    CHECK( std::isfinite( defaultSphere.GetDragCoefficient() ) );

    const CollisionShape defaultShape = defaultSphere;
    const auto defaultDescriptor = SkullbonezCore::Physics::MakePhysicsBodyCreateDesc(
        SkullbonezCore::Physics::PhysicsSceneObjectId( 1u ), defaultShape, Vector3( 0.0f, 0.0f, 0.0f ),
        Quaternion(), Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
        Vector3( 0.0f, 0.0f, 0.0f ), 1.0f, 0.0f, SkullbonezCore::Physics::PhysicsBodyMotionKind::Dynamic );
    CHECK( std::isfinite( defaultDescriptor.boundingRadius ) );
    CHECK( std::isfinite( defaultDescriptor.volume ) );
    CHECK( std::isfinite( defaultDescriptor.projectedSurfaceArea ) );
    CHECK( std::isfinite( defaultDescriptor.dragCoefficient ) );

    const Vector3 localOffset( 1.0f, -2.0f, 0.5f );
    const BoundingSphere sphere( 2.0f, localOffset );
    const BoundingBox box( Vector3( 2.0f, 1.0f, 0.5f ), localOffset );
    const Quaternion orientation( 0.0f, 0.0f, 0.70710677f, 0.70710677f );
    const Vector3 bodyPosition( 10.0f, 20.0f, 30.0f );
    const Matrix4 rotation = Matrix4::FromQuaternion( orientation );
    const Vector3 expectedCenter = GetWorldShapeCenter( sphere, bodyPosition, orientation.GetOrientationMatrix() );

    const Matrix4 sphereModel = sphere.GetModelMatrix( bodyPosition, rotation );
    const Matrix4 boxModel = box.GetModelMatrix( bodyPosition, rotation );
    CheckNear( sphereModel.m[12], expectedCenter.x );
    CheckNear( sphereModel.m[13], expectedCenter.y );
    CheckNear( sphereModel.m[14], expectedCenter.z );
    CheckNear( boxModel.m[12], expectedCenter.x );
    CheckNear( boxModel.m[13], expectedCenter.y );
    CheckNear( boxModel.m[14], expectedCenter.z );

    const CollisionShape shape = sphere;
    CheckNear( GetShapeBodyOriginBoundingRadius( shape ), 2.0f + std::sqrt( 5.25f ) );
}


TEST_CASE( "Bounds: render translations have optimizer-independent exact bits" )
{
    const float rotationValues[16] = { 0.1234567f, -0.7654321f, 0.3333333f, 0.0f,
                                       0.2468135f, 0.1357911f, -0.2222222f, 0.0f,
                                       -0.3141592f, 0.2718281f, 0.4444444f, 0.0f,
                                       0.0f,       0.0f,       0.0f,       1.0f };
    const Matrix4 rotation( rotationValues );
    const Vector3 bodyPosition( 10.25f, -20.5f, 30.75f );
    const Vector3 localPosition( 1.1f, -2.2f, 0.3f );
    const BoundingSphere sphere( 2.0f, localPosition );
    const BoundingBox box( Vector3( 2.0f, 1.0f, 0.5f ), localPosition );
    const Matrix4 sphereModel = sphere.GetModelMatrix( bodyPosition, rotation );
    const Matrix4 boxModel = box.GetModelMatrix( bodyPosition, rotation );

    const auto bits = []( float value )
    {
        uint32_t result = 0u;
        std::memcpy( &result, &value, sizeof( result ) );
        return result;
    };

    // These constants are the shared helper's deliberately staged IEEE-754
    // result. Every configuration must produce this same evidence row.
    CHECK( bits( sphereModel.m[12] ) == 0x411bfa1fu );
    CHECK( bits( sphereModel.m[13] ) == 0xc1ac792du );
    CHECK( bits( sphereModel.m[14] ) == 0x41fde93fu );
    CHECK( bits( boxModel.m[12] ) == 0x411bfa1fu );
    CHECK( bits( boxModel.m[13] ) == 0xc1ac792du );
    CHECK( bits( boxModel.m[14] ) == 0x41fde93fu );
}
