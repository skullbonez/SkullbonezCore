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
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

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
    const std::string current = "hull_version 2";
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
    const ConvexHullShape hull = ConvexHullShape::LoadFromFile( diagnostics, kPyramidHullPath );

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

    const Vector3 inertiaAtTwoKg = hull.ComputeBoxApproxInertia( 2.0f );
    CheckVectorNear( inertiaAtTwoKg, Vector3( 33.3333334f, 33.3333334f, 33.3333334f ), 0.0001f );
}


TEST_CASE( "ConvexHull: pyramid fixture exposes baked vertices and face spans" )
{
    const ConvexHullShape hull = ConvexHullShape::LoadFromFile( diagnostics, kPyramidHullPath );

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
    const ConvexHullShape hull = ConvexHullShape::LoadFromFile( diagnostics, kPyramidHullPath );

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


TEST_CASE( "ConvexHull: previous version upgrades and future version fails recoverably" )
{
    TemporaryHullFixture fixture;
    REQUIRE( WriteHullVersionFixture( 1 ) );
    ConvexHullShape previous;
    REQUIRE( ConvexHullShape::TryLoadFromFile( diagnostics, kVersionFixturePath, previous ).Ok() );
    CHECK( previous.GetVertexCount() == 5 );
    CHECK( previous.GetDefaultMass() == doctest::Approx( 300.0f ) );

    REQUIRE( WriteHullVersionFixture( 3 ) );
    ConvexHullShape future;
    const auto result = ConvexHullShape::TryLoadFromFile( diagnostics, kVersionFixturePath, future );
    CHECK_FALSE( result.Ok() );
    CHECK( std::string( result.ErrorOwner() ) == "Physics/ConvexHullShape" );
    CHECK( std::string( result.ErrorMessage() ).find( "version 3" ) != std::string::npos );
    CHECK( std::string( result.ErrorMessage() ).find( "current version 2" ) != std::string::npos );
}
