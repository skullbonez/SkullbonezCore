#include "../SkullbonezSource/Maths/SymmetricMatrix3.h"
//
// File: SkullbonezTests/TestConvexHull.cpp
// Purpose:
//   Lock the first focused tests for baked convex hull asset loading.
//
// Summary:
//   Convex hull assets keep editable source geometry plus baked runtime rows.
//   Runtime loads the baked vertices, faces, edges, and mass properties, then
//   narrowphase reads those immutable rows without rebaking the hull.
//
// Glossary:
//   Baked hull: Serialized runtime representation produced by tools/bake_hulls.py.
//   Narrowphase: Precise collision pass that computes contact points, normals,
//     and penetration from candidate shape pairs.
//   Face index stream: Flat list of vertex ids; each face owns a contiguous span.
//   Adjacent faces: The two hull faces sharing one undirected edge.
//
// Invariants:
//   - Loaded faces reference only live vertices and have unit-ish normals.
//   - Loaded edges reference live vertices/faces and both adjacent faces contain
//     both edge endpoints.
//   - Baked mass, volume, radius, and inertia values are positive and stable for
//     committed hull fixtures.
//
// Related:
//   - SkullbonezSource/Physics/ConvexHullShape.h
//   - SkullbonezSource/Physics/ConvexHullShape.cpp
//   - SkullbonezData/hulls/pyramid.hull
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestResultLoadFixtures.h"

#include "../SkullbonezSource/Physics/ConvexHullShape.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

using SkullbonezCore::Math::CollisionDetection::ConvexHullEdge;
using SkullbonezCore::Math::CollisionDetection::ConvexHullFace;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMagSquared;
using SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull;

namespace
{
constexpr const char* kPyramidHullPath = "SkullbonezData/hulls/pyramid.hull";
constexpr const char* kVersionFixturePath = "unit_versioned_pyramid.hull";
constexpr float kEpsilon = 0.00001f;

void CheckNear( float actual, float expected, float epsilon = kEpsilon )
{
    CHECK( std::fabs( actual - expected ) <= epsilon );
}

void CheckVectorNear( const Vector3& actual, const Vector3& expected, float epsilon = kEpsilon )
{
    CheckNear( actual.x, expected.x, epsilon );
    CheckNear( actual.y, expected.y, epsilon );
    CheckNear( actual.z, expected.z, epsilon );
}

bool FaceContainsVertex( const ConvexHullShape& hull, const ConvexHullFace& face, uint16_t vertex )
{

    for ( uint8_t i = 0; i < face.indexCount; ++i )
    {

        if ( hull.GetFaceIndex( face.firstIndex + i ) == vertex )
        {
            return true;
        }
    }

    return false;
}

void CheckEdgeEndpointBelongsToFace( const ConvexHullShape& hull, const ConvexHullEdge& edge, uint16_t faceIndex )
{

    // Invariant: an edge's adjacent face must contain both endpoint vertices or
    // narrowphase feature ids cannot map contacts back to stable hull topology.
    const ConvexHullFace& face = hull.GetFace( faceIndex );
    CHECK( FaceContainsVertex( hull, face, edge.vertexA ) );
    CHECK( FaceContainsVertex( hull, face, edge.vertexB ) );
}

struct TemporaryHullFixture
{
    ~TemporaryHullFixture()
    {
        std::remove( kVersionFixturePath );
    }
};

bool WriteHullVersionFixture( unsigned int version )
{
    std::ifstream input( kPyramidHullPath );
    std::ostringstream contents;
    contents << input.rdbuf();
    std::string text = contents.str();
    const std::string current = "hull_version 3";
    const size_t offset = text.find( current );

    if ( !input || offset == std::string::npos )
    {
        return false;
    }

    text.replace( offset, current.size(), "hull_version " + std::to_string( version ) );
    std::ofstream output( kVersionFixturePath, std::ios::binary );
    output << text;
    return output.good();
}
} // namespace


TEST_CASE( "ConvexHull: pyramid fixture loads baked identity and mass properties" )
{
    ConvexHullShape hull;
    REQUIRE( TryLoadConvexHull( diagnostics, kPyramidHullPath, hull ) );

    CHECK( std::string( hull.GetName() ) == "pyramid" );
    CHECK( hull.GetVertexCount() == 5 );
    CHECK( hull.GetFaceCount() == 5 );
    CHECK( hull.GetEdgeCount() == 8 );
    CheckVectorNear( hull.GetPosition(), Vector3( 0.0f, 0.0f, 0.0f ) );
    CheckVectorNear( hull.GetAuthoredCenterOfMass(), Vector3( 0.0f, -1.5f, 0.0f ) );
    CheckVectorNear( hull.GetInertiaHalfExtents(), Vector3( 5.0f, 5.0f, 5.0f ) );
    CheckNear( hull.GetVolume(), 333.333333f, 0.0001f );
    CheckNear( hull.GetDefaultMass(), 300.0f );
    CheckNear( hull.GetBoundingRadius(), 7.5f );
    CheckNear( hull.GetProjectedSurfaceArea(), 100.0f );

    const auto inertiaAtTwoKg = hull.ComputeInertia( 2.0f );
    CheckVectorNear( inertiaAtTwoKg.diagonal, Vector3( 17.5f, 20.0f, 17.5f ), 0.0001f );
    CheckVectorNear( inertiaAtTwoKg.offDiagonal, Vector3( 0.0f, 0.0f, 0.0f ) );
}


TEST_CASE( "ConvexHull: pyramid fixture exposes baked vertices and face spans" )
{
    ConvexHullShape hull;
    REQUIRE( TryLoadConvexHull( diagnostics, kPyramidHullPath, hull ) );

    CheckVectorNear( hull.GetVertex( 0 ), Vector3( -5.0f, -2.5f, -5.0f ) );
    CheckVectorNear( hull.GetVertex( 4 ), Vector3( 0.0f, 7.5f, 0.0f ) );

    const ConvexHullFace& base = hull.GetFace( 0 );
    CheckVectorNear( base.normalLocal, Vector3( 0.0f, -1.0f, 0.0f ) );
    CheckNear( base.planeOffsetLocal, 2.5f );
    CHECK( base.firstIndex == 0 );
    CHECK( base.indexCount == 4 );
    CHECK( hull.GetFaceIndex( 0 ) == 0 );
    CHECK( hull.GetFaceIndex( 1 ) == 1 );
    CHECK( hull.GetFaceIndex( 2 ) == 2 );
    CHECK( hull.GetFaceIndex( 3 ) == 3 );
}


TEST_CASE( "ConvexHull: pyramid fixture topology references live vertices and adjacent faces" )
{
    ConvexHullShape hull;
    REQUIRE( TryLoadConvexHull( diagnostics, kPyramidHullPath, hull ) );

    for ( uint16_t faceIndex = 0; faceIndex < hull.GetFaceCount(); ++faceIndex )
    {
        const ConvexHullFace& face = hull.GetFace( faceIndex );
        CHECK( face.indexCount >= 3 );
        CHECK( face.firstIndex + face.indexCount <= ConvexHullShape::MAX_FACE_INDICES );
        CheckNear( VectorMagSquared( face.normalLocal ), 1.0f, 0.00001f );

        for ( uint8_t i = 0; i < face.indexCount; ++i )
        {
            CHECK( hull.GetFaceIndex( face.firstIndex + i ) < hull.GetVertexCount() );
        }
    }

    for ( uint16_t edgeIndex = 0; edgeIndex < hull.GetEdgeCount(); ++edgeIndex )
    {
        const ConvexHullEdge& edge = hull.GetEdge( edgeIndex );
        CHECK( edge.vertexA < hull.GetVertexCount() );
        CHECK( edge.vertexB < hull.GetVertexCount() );
        CHECK( edge.faceA < hull.GetFaceCount() );
        CHECK( edge.faceB < hull.GetFaceCount() );
        CheckEdgeEndpointBelongsToFace( hull, edge, edge.faceA );
        CheckEdgeEndpointBelongsToFace( hull, edge, edge.faceB );
    }
}


TEST_CASE( "ConvexHull: previous version requests rebake and future version fails recoverably" )
{
    TemporaryHullFixture fixture;
    REQUIRE( WriteHullVersionFixture( 2 ) );
    ConvexHullShape previous;
    const auto previousResult = ConvexHullShape::TryLoadFromFile( diagnostics, kVersionFixturePath, previous );
    REQUIRE_FALSE( previousResult.Ok() );
    CHECK( std::string( previousResult.ErrorMessage() ).find( "Re-bake" ) != std::string::npos );

    REQUIRE( WriteHullVersionFixture( 4 ) );
    ConvexHullShape future;
    const auto result = ConvexHullShape::TryLoadFromFile( diagnostics, kVersionFixturePath, future );
    CHECK_FALSE( result.Ok() );
    CHECK( std::string( result.ErrorOwner() ) == "Physics/ConvexHullShape" );
    CHECK( std::string( result.ErrorMessage() ).find( "version 4" ) != std::string::npos );
    CHECK( std::string( result.ErrorMessage() ).find( "current version 3" ) != std::string::npos );
}

TEST_CASE( "Convex hull inertia: full symmetric inverse preserves asymmetric angular response" )
{
    using SkullbonezCore::Math::Transformation::SymmetricMatrix3;
    using SkullbonezCore::Math::Vector::Vector3;
    const SymmetricMatrix3 unit( Vector3( 75.0f / 80.0f, 60.0f / 80.0f, 39.0f / 80.0f ), Vector3( 6.0f / 80.0f, 8.0f / 80.0f, 12.0f / 80.0f ) );
    for ( float mass : { 0.001f, 1.0f, 1000.0f } )
    {
        const auto inertia = unit * mass;
        SymmetricMatrix3 inverse;
        REQUIRE( inertia.TryInversePositiveDefinite( inverse ) );
        for ( const Vector3 impulse : { Vector3( 1, 0, 0 ), Vector3( 0, 1, 0 ), Vector3( 0, 0, 1 ), Vector3( -2, 3, 4 ) } )
        {
            const auto response = inverse * impulse;
            const auto reconstructed = inertia * response;
            CHECK( reconstructed.x == doctest::Approx( impulse.x ).epsilon( 0.00001f ) );
            CHECK( reconstructed.y == doctest::Approx( impulse.y ).epsilon( 0.00001f ) );
            CHECK( reconstructed.z == doctest::Approx( impulse.z ).epsilon( 0.00001f ) );
            CHECK( SkullbonezCore::Math::Vector::Dot( impulse, response ) > 0.0f );
        }
        const auto response = inverse * Vector3( 1, 0, 0 );
        CHECK( response.y != 0.0f );
        CHECK( response.z != 0.0f );
    }
    SymmetricMatrix3 inverse;
    CHECK_FALSE( SymmetricMatrix3().TryInversePositiveDefinite( inverse ) );
    CHECK_FALSE( SymmetricMatrix3( Vector3( 1, 1, 1 ), Vector3( 2, 0, 0 ) ).TryInversePositiveDefinite( inverse ) );
    const SymmetricMatrix3 box( Vector3( 2, 4, 8 ) );
    REQUIRE( box.TryInversePositiveDefinite( inverse ) );
    const auto result = inverse * Vector3( 2, 8, 24 );
    CHECK( result == Vector3( 1, 2, 3 ) );
}


TEST_CASE( "Convex hull inertia: copied scale preserves complete second moments" )
{
    ConvexHullShape base;
    REQUIRE( TryLoadConvexHull( diagnostics, "SkullbonezData/hulls/convex_quality_tetrahedron_ordinary.hull", base ) );
    const auto unit = base.ComputeInertia( 1.0f );
    CheckVectorNear( unit.diagonal, Vector3( 1.2f, 1.2f, 1.2f ) );
    CheckVectorNear( unit.offDiagonal, Vector3( 0.2f, 0.2f, 0.2f ) );
    auto stretched = base;
    stretched.ScaleAxis( 0, 2.0f );
    stretched.ScaleAxis( 1, 0.5f );
    stretched.ScaleAxis( 2, 3.0f );
    const auto stretchedUnit = stretched.ComputeInertia( 1.0f );
    // Axis tetrahedron lengths 8,2,12: diagonal 3*(b²+c²)/80,
    // off-diagonal a*b/80 after integrating about the volume centroid.
    CheckVectorNear( stretchedUnit.diagonal, Vector3( 5.55f, 7.8f, 2.55f ) );
    CheckVectorNear( stretchedUnit.offDiagonal, Vector3( 0.2f, 1.2f, 0.3f ) );
    CheckNear( stretched.GetDefaultMass(), base.GetDefaultMass() * 3.0f );
    auto enlarged = base;
    for ( int axis = 0; axis < 3; ++axis )
    {
        enlarged.ScaleAxis( axis, 2.0f );
    }
    const auto originalInertia = base.ComputeInertia( base.GetDefaultMass() );
    const auto enlargedInertia = enlarged.ComputeInertia( enlarged.GetDefaultMass() );
    CheckVectorNear( enlargedInertia.diagonal, originalInertia.diagonal * 32.0f, 0.001f );
    CheckVectorNear( enlargedInertia.offDiagonal, originalInertia.offDiagonal * 32.0f, 0.001f );
    CheckVectorNear( base.ComputeInertia( 1.0f ).diagonal, unit.diagonal );
}
