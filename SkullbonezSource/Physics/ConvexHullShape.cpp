/*
File: SkullbonezSource/Physics/ConvexHullShape.cpp
Purpose:
  Loads, validates, and exposes immutable convex hull collision geometry.

Mental model:
  Hull assets are serialized collision data. Runtime loading copies baked
  topology and mass properties from disk; authoring-time tools own derivation.

Glossary:
  Convex hull: Collision shape whose vertices form one outward-facing closed
  solid with no inward dents.
  Deterministic topology: Vertex, face, and edge ordering that stays stable so
  physics validation can compare byte-exact output.
  Support mapping: Query that returns the hull point farthest along a direction,
  used by convex collision tests.

Invariants:
  - Runtime hull loading trusts baked topology order and validates shape data
    rather than deriving new topology at load time.
  - Missing legacy mass metadata uses a legacy default with a warning so
    old assets remain loadable without silently changing validation behavior.

Related:
  - SkullbonezSource/Physics/ConvexHullShape.h
  - SkullbonezSource/Physics/ObjectContactManifold.cpp
*/
#include "ConvexHullShape.h"

#include "BoundingBox.h"
#include "BoundingSphere.h"
#include "../Core/FatalError.h"

#include <cerrno>
#include <cstdarg>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;
using SkullbonezCore::Basics::SbResult;
namespace Physics = SkullbonezCore::Physics;

namespace
{
constexpr float COMPATIBILITY_HULL_DEFAULT_MASS = 24.0f;
constexpr const char* HULL_LOAD_OWNER = "Physics/ConvexHullShape";

struct HullFile
{
    FILE* handle = nullptr;

    ~HullFile()
    {
        Close();
    }

    void Close()
    {
        if ( handle )
        {
            fclose( handle );
            handle = nullptr;
        }
    }
};

SbResult HullLoadFailure( const char* format, ... )
{
    char message[512];
    va_list args;
    va_start( args, format );
    std::vsnprintf( message, sizeof( message ), format ? format : "Convex hull load failed.", args );
    va_end( args );
    return SbResult::Failure( HULL_LOAD_OWNER, "%s", message );
}

float ClampPositive( float value, float fallback )
{
    return value > TOLERANCE ? value : fallback;
}

Vector3 BoxApproxUnitInertia( const Vector3& halfExtents )
{
    const float hx2 = halfExtents.x * halfExtents.x;
    const float hy2 = halfExtents.y * halfExtents.y;
    const float hz2 = halfExtents.z * halfExtents.z;
    return Vector3( ( hy2 + hz2 ) / 3.0f, ( hx2 + hz2 ) / 3.0f, ( hx2 + hy2 ) / 3.0f );
}

void ScaleAxisComponent( Vector3& v, int axis, float factor )
{
    switch ( axis )
    {
    case 0:
        v.x *= factor;
        break;
    case 1:
        v.y *= factor;
        break;
    case 2:
        v.z *= factor;
        break;
    default:
        break;
    }
}

SbResult TryNormalized( const Vector3& v, const char* context, Vector3& out )
{
    const float magSq = VectorMagSquared( v );
    if ( magSq <= 1.0e-10f )
    {
        return HullLoadFailure( "%s", context );
    }
    out = v / sqrtf( magSq );
    return SbResult::Success();
}

SbResult ParseFiniteFloat( const char* value, const char* path, int lineNumber, const char* field, float& out )
{
    errno = 0;
    char* end = nullptr;
    const double parsed = strtod( value, &end );
    if ( end == value || *end != '\0' || errno == ERANGE || !std::isfinite( parsed ) || parsed < -FLT_MAX ||
         parsed > FLT_MAX )
    {
        return HullLoadFailure( "Invalid %s at %s:%d.  (ConvexHullShape::LoadFromFile)", field, path, lineNumber );
    }
    out = static_cast<float>( parsed );
    return SbResult::Success();
}

SbResult ParseUint16( const char* value, const char* path, int lineNumber, const char* field, uint16_t& out )
{
    errno = 0;
    char* end = nullptr;
    const long parsed = strtol( value, &end, 10 );
    if ( end == value || *end != '\0' || errno == ERANGE || parsed < 0 || parsed > 65535 )
    {
        return HullLoadFailure( "Invalid %s at %s:%d.  (ConvexHullShape::LoadFromFile)", field, path, lineNumber );
    }
    out = static_cast<uint16_t>( parsed );
    return SbResult::Success();
}

SbResult RequireNoExtraTokens( char* context, const char* path, int lineNumber, const char* directive )
{
    // Hazard: hull files are deterministic physics inputs. Extra tokens usually
    // mean the bake format changed or the asset is corrupted, so fail loudly.
    if ( strtok_s( nullptr, " \t\r\n", &context ) )
    {
        return HullLoadFailure( "Unexpected extra value in %s at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                directive,
                                path,
                                lineNumber );
    }
    return SbResult::Success();
}

void WarnMissingDefaultMassMetadata( const char* path )
{
    fprintf( stderr,
             "[hull][legacy] %s missing default_mass; using legacy mass default %.3f at load. Re-bake with "
             "tools\\bake_hulls.bat --write.\n",
             path,
             COMPATIBILITY_HULL_DEFAULT_MASS );
}

float SweptBoundingRadiusCollision( float focusRadius,
                                    const Vector3& focusOffset,
                                    float targetRadius,
                                    const Vector3& targetOffset,
                                    const Ray& targetRay,
                                    const Ray& focusRay )
{
    const float combinedRadius = focusRadius + targetRadius;
    const float combinedRadiusSq = combinedRadius * combinedRadius;
    const Vector3 totalMovement = targetRay.vector3 - focusRay.vector3;
    const float totalMovementSq = VectorMagSquared( totalMovement );

    if ( totalMovementSq < TOLERANCE )
    {
        const Vector3 delta = ( targetRay.origin + targetOffset ) - ( focusRay.origin + focusOffset );
        return VectorMagSquared( delta ) <= combinedRadiusSq ? 0.0f : NO_COLLISION;
    }

    const Vector3 d = ( focusRay.origin + focusOffset ) - ( targetRay.origin + targetOffset );
    const float totalMovementMag = sqrtf( totalMovementSq );
    const Vector3 moveDir = totalMovement / totalMovementMag;
    const float dDotMoveDir = d * moveDir;
    const float discriminant = dDotMoveDir * dDotMoveDir - ( VectorMagSquared( d ) - combinedRadiusSq );
    if ( discriminant < 0.0f )
    {
        return NO_COLLISION;
    }

    const float t = ( dDotMoveDir - sqrtf( discriminant ) ) / totalMovementMag;
    return ( t < 0.0f || t > 1.0f ) ? NO_COLLISION : t;
}

void CopyHullName( char ( &out )[64], const char* path, const char* authoredName )
{
    if ( authoredName && authoredName[0] != '\0' )
    {
        strncpy_s( out, authoredName, _TRUNCATE );
        return;
    }

    const char* slash = strrchr( path, '\\' );
    const char* slash2 = strrchr( path, '/' );
    const char* base = slash && slash2 ? ( slash > slash2 ? slash + 1 : slash2 + 1 )
                                       : ( slash ? slash + 1 : ( slash2 ? slash2 + 1 : path ) );
    strncpy_s( out, base ? base : "convex_hull", _TRUNCATE );
}

SbResult ParseVec3( char*& context, const char* path, int lineNumber, const char* field, Vector3& out )
{
    char* sx = strtok_s( nullptr, " \t\r\n", &context );
    char* sy = strtok_s( nullptr, " \t\r\n", &context );
    char* sz = strtok_s( nullptr, " \t\r\n", &context );
    if ( !sx || !sy || !sz )
    {
        return HullLoadFailure( "Invalid %s at %s:%d.  (ConvexHullShape::LoadFromFile)", field, path, lineNumber );
    }
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    SbResult result = ParseFiniteFloat( sx, path, lineNumber, field, x );
    if ( !result.ok )
    {
        return result;
    }
    result = ParseFiniteFloat( sy, path, lineNumber, field, y );
    if ( !result.ok )
    {
        return result;
    }
    result = ParseFiniteFloat( sz, path, lineNumber, field, z );
    if ( !result.ok )
    {
        return result;
    }
    out = Vector3( x, y, z );
    return SbResult::Success();
}
} // namespace

ConvexHullShape::ConvexHullShape()
{
    strcpy_s( m_name, sizeof( m_name ), "convex_hull" );
}

SbResult ConvexHullShape::TryLoadFromFile( const char* path, ConvexHullShape& outHull )
{
    if ( !path || path[0] == '\0' )
    {
        return HullLoadFailure( "Convex hull path is empty.  (ConvexHullShape::LoadFromFile)" );
    }

    HullFile file;
    if ( fopen_s( &file.handle, path, "r" ) != 0 || !file.handle )
    {
        return HullLoadFailure( "Unable to open convex hull asset: %s  (ConvexHullShape::LoadFromFile)", path );
    }

    ConvexHullShape hull;
    char authoredName[64] = {};
    int lineNumber = 0;
    bool sawVersion = false;
    bool sawSourceHash = false;
    bool sawCenterOfMass = false;
    bool sawVolume = false;
    bool sawBoundingRadius = false;
    bool sawInertiaHalfExtents = false;
    bool sawUnitInertia = false;
    bool sawProjectedSurfaceArea = false;
    bool sawDefaultMass = false;

    char line[512];
    while ( fgets( line, sizeof( line ), file.handle ) )
    {
        ++lineNumber;
        char* hash = strchr( line, '#' );
        if ( hash )
        {
            *hash = '\0';
        }

        char* context = nullptr;
        char* token = strtok_s( line, " \t\r\n", &context );
        if ( !token )
        {
            continue;
        }

        if ( strcmp( token, "hull_version" ) == 0 )
        {
            char* version = strtok_s( nullptr, " \t\r\n", &context );
            if ( !version || strcmp( version, "2" ) != 0 )
            {
                return HullLoadFailure(
                    "Convex hull asset must be serialized hull_version 2: %s:%d.  (ConvexHullShape::LoadFromFile)",
                    path,
                    lineNumber );
            }
            sawVersion = true;
            const SbResult extraResult = RequireNoExtraTokens( context, path, lineNumber, "hull_version" );
            if ( !extraResult.ok )
            {
                return extraResult;
            }
            continue;
        }

        if ( !sawVersion )
        {
            return HullLoadFailure(
                "Convex hull asset must start with hull_version 2: %s:%d.  (ConvexHullShape::LoadFromFile)",
                path,
                lineNumber );
        }

        if ( strcmp( token, "name" ) == 0 )
        {
            char* name = strtok_s( nullptr, " \t\r\n", &context );
            if ( name )
            {
                strncpy_s( authoredName, sizeof( authoredName ), name, _TRUNCATE );
            }
            const SbResult extraResult = RequireNoExtraTokens( context, path, lineNumber, "name" );
            if ( !extraResult.ok )
            {
                return extraResult;
            }
            continue;
        }

        if ( strcmp( token, "source_hash" ) == 0 )
        {
            char* hashValue = strtok_s( nullptr, " \t\r\n", &context );
            if ( !hashValue || hashValue[0] == '\0' )
            {
                return HullLoadFailure( "Invalid source_hash at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        path,
                                        lineNumber );
            }
            sawSourceHash = true;
            const SbResult extraResult = RequireNoExtraTokens( context, path, lineNumber, "source_hash" );
            if ( !extraResult.ok )
            {
                return extraResult;
            }
            continue;
        }

        if ( strcmp( token, "source_vertex" ) == 0 )
        {
            Vector3 sourceVertex = ZERO_VECTOR;
            SbResult parseResult = ParseVec3( context, path, lineNumber, "source_vertex", sourceVertex );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = RequireNoExtraTokens( context, path, lineNumber, "source_vertex" );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            continue;
        }

        if ( strcmp( token, "source_face" ) == 0 )
        {
            int sourceFaceCount = 0;
            char* value = nullptr;
            while ( ( value = strtok_s( nullptr, " \t\r\n", &context ) ) != nullptr )
            {
                uint16_t sourceIndex = 0;
                const SbResult parseResult = ParseUint16( value, path, lineNumber, "source_face", sourceIndex );
                if ( !parseResult.ok )
                {
                    return parseResult;
                }
                ++sourceFaceCount;
            }
            if ( sourceFaceCount < 3 )
            {
                return HullLoadFailure(
                    "source_face needs at least three vertices at %s:%d.  (ConvexHullShape::LoadFromFile)",
                    path,
                    lineNumber );
            }
            continue;
        }

        if ( strcmp( token, "center_of_mass" ) == 0 )
        {
            SbResult parseResult =
                ParseVec3( context, path, lineNumber, "center_of_mass", hull.m_authoredCenterOfMass );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = RequireNoExtraTokens( context, path, lineNumber, "center_of_mass" );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            sawCenterOfMass = true;
            continue;
        }

        if ( strcmp( token, "default_density" ) == 0 || strcmp( token, "default_mass" ) == 0 )
        {
            char* value = strtok_s( nullptr, " \t\r\n", &context );
            if ( !value )
            {
                return HullLoadFailure( "Invalid %s at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        token,
                                        path,
                                        lineNumber );
            }
            float parsed = 0.0f;
            SbResult parseResult = ParseFiniteFloat( value, path, lineNumber, token, parsed );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = RequireNoExtraTokens( context, path, lineNumber, token );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            if ( parsed <= 0.0f )
            {
                return HullLoadFailure( "%s must be positive at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        token,
                                        path,
                                        lineNumber );
            }
            if ( strcmp( token, "default_mass" ) == 0 )
            {
                hull.m_defaultMass = Physics::ClampPositiveMass( parsed );
                sawDefaultMass = true;
            }
            // default_density is accepted as baked provenance; default_mass is runtime-authoritative.
            continue;
        }

        if ( strcmp( token, "volume" ) == 0 || strcmp( token, "bounding_radius" ) == 0 ||
             strcmp( token, "projected_surface_area" ) == 0 )
        {
            char* value = strtok_s( nullptr, " \t\r\n", &context );
            if ( !value )
            {
                return HullLoadFailure( "Invalid %s at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        token,
                                        path,
                                        lineNumber );
            }
            float parsed = 0.0f;
            SbResult parseResult = ParseFiniteFloat( value, path, lineNumber, token, parsed );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = RequireNoExtraTokens( context, path, lineNumber, token );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            if ( parsed <= 0.0f )
            {
                return HullLoadFailure( "%s must be positive at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        token,
                                        path,
                                        lineNumber );
            }
            if ( strcmp( token, "volume" ) == 0 )
            {
                hull.m_volume = parsed;
                sawVolume = true;
            }
            else if ( strcmp( token, "bounding_radius" ) == 0 )
            {
                hull.m_boundingRadius = parsed;
                sawBoundingRadius = true;
            }
            else
            {
                hull.m_projectedSurfaceArea = parsed;
                sawProjectedSurfaceArea = true;
            }
            continue;
        }

        if ( strcmp( token, "inertia_half_extents" ) == 0 || strcmp( token, "unit_inertia" ) == 0 )
        {
            const bool isHalfExtents = strcmp( token, "inertia_half_extents" ) == 0;
            Vector3 parsed = ZERO_VECTOR;
            SbResult parseResult = ParseVec3( context, path, lineNumber, token, parsed );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = RequireNoExtraTokens( context, path, lineNumber, token );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            if ( isHalfExtents )
            {
                hull.m_inertiaHalfExtents = parsed;
            }
            else
            {
                hull.m_unitInertia = parsed;
            }
            const Vector3& checked = isHalfExtents ? hull.m_inertiaHalfExtents : hull.m_unitInertia;
            if ( checked.x <= 0.0f || checked.y <= 0.0f || checked.z <= 0.0f )
            {
                return HullLoadFailure( "%s must be positive at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        token,
                                        path,
                                        lineNumber );
            }
            if ( isHalfExtents )
            {
                sawInertiaHalfExtents = true;
            }
            else
            {
                sawUnitInertia = true;
            }
            continue;
        }

        if ( strcmp( token, "vertex" ) == 0 )
        {
            if ( hull.m_vertexCount >= MAX_VERTICES )
            {
                return HullLoadFailure( "Convex hull exceeds %u vertices at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        MAX_VERTICES,
                                        path,
                                        lineNumber );
            }

            SbResult parseResult =
                ParseVec3( context, path, lineNumber, "vertex", hull.m_vertices[hull.m_vertexCount] );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = RequireNoExtraTokens( context, path, lineNumber, "vertex" );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            ++hull.m_vertexCount;
            continue;
        }

        if ( strcmp( token, "face" ) == 0 )
        {
            if ( hull.m_faceCount >= MAX_FACES )
            {
                return HullLoadFailure( "Convex hull exceeds %u faces at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        MAX_FACES,
                                        path,
                                        lineNumber );
            }

            char* nx = strtok_s( nullptr, " \t\r\n", &context );
            char* ny = strtok_s( nullptr, " \t\r\n", &context );
            char* nz = strtok_s( nullptr, " \t\r\n", &context );
            char* offset = strtok_s( nullptr, " \t\r\n", &context );
            if ( !nx || !ny || !nz || !offset )
            {
                return HullLoadFailure( "Invalid face at %s:%d.  (ConvexHullShape::LoadFromFile)", path, lineNumber );
            }

            ConvexHullFace face;
            float normalX = 0.0f;
            float normalY = 0.0f;
            float normalZ = 0.0f;
            SbResult parseResult = ParseFiniteFloat( nx, path, lineNumber, "face.normal", normalX );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = ParseFiniteFloat( ny, path, lineNumber, "face.normal", normalY );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = ParseFiniteFloat( nz, path, lineNumber, "face.normal", normalZ );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = ParseFiniteFloat( offset, path, lineNumber, "face.planeOffset", face.planeOffsetLocal );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            face.normalLocal = Vector3( normalX, normalY, normalZ );

            face.firstIndex = hull.m_faceIndexCount;
            char* value = nullptr;
            while ( ( value = strtok_s( nullptr, " \t\r\n", &context ) ) != nullptr )
            {
                if ( face.indexCount >= MAX_FACE_VERTICES || hull.m_faceIndexCount >= MAX_FACE_INDICES )
                {
                    return HullLoadFailure(
                        "Convex hull face exceeds serialized limits at %s:%d.  (ConvexHullShape::LoadFromFile)",
                        path,
                        lineNumber );
                }
                uint16_t faceIndex = 0;
                parseResult = ParseUint16( value, path, lineNumber, "face.index", faceIndex );
                if ( !parseResult.ok )
                {
                    return parseResult;
                }
                hull.m_faceIndices[hull.m_faceIndexCount++] = faceIndex;
                ++face.indexCount;
            }

            if ( face.indexCount < 3 )
            {
                return HullLoadFailure(
                    "Convex hull face needs at least three vertices at %s:%d.  (ConvexHullShape::LoadFromFile)",
                    path,
                    lineNumber );
            }
            if ( VectorMagSquared( face.normalLocal ) <= 1.0e-10f )
            {
                return HullLoadFailure(
                    "Convex hull face normal is degenerate at %s:%d.  (ConvexHullShape::LoadFromFile)",
                    path,
                    lineNumber );
            }
            hull.m_faces[hull.m_faceCount++] = face;
            continue;
        }

        if ( strcmp( token, "edge" ) == 0 )
        {
            if ( hull.m_edgeCount >= MAX_EDGES )
            {
                return HullLoadFailure( "Convex hull exceeds %u edges in %s:%d.  (ConvexHullShape::LoadFromFile)",
                                        MAX_EDGES,
                                        path,
                                        lineNumber );
            }

            char* a = strtok_s( nullptr, " \t\r\n", &context );
            char* b = strtok_s( nullptr, " \t\r\n", &context );
            char* faceA = strtok_s( nullptr, " \t\r\n", &context );
            char* faceB = strtok_s( nullptr, " \t\r\n", &context );
            if ( !a || !b || !faceA || !faceB )
            {
                return HullLoadFailure( "Invalid edge at %s:%d.  (ConvexHullShape::LoadFromFile)", path, lineNumber );
            }

            ConvexHullEdge edge;
            SbResult parseResult = ParseUint16( a, path, lineNumber, "edge.vertexA", edge.vertexA );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = ParseUint16( b, path, lineNumber, "edge.vertexB", edge.vertexB );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = ParseUint16( faceA, path, lineNumber, "edge.faceA", edge.faceA );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = ParseUint16( faceB, path, lineNumber, "edge.faceB", edge.faceB );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            parseResult = RequireNoExtraTokens( context, path, lineNumber, "edge" );
            if ( !parseResult.ok )
            {
                return parseResult;
            }
            hull.m_edges[hull.m_edgeCount++] = edge;
            continue;
        }

        return HullLoadFailure( "Unknown hull directive '%s' at %s:%d.  (ConvexHullShape::LoadFromFile)",
                                token,
                                path,
                                lineNumber );
    }

    file.Close();

    if ( !sawVersion || !sawSourceHash || !sawCenterOfMass || !sawVolume || !sawBoundingRadius ||
         !sawInertiaHalfExtents || !sawUnitInertia || !sawProjectedSurfaceArea || hull.m_vertexCount < 4 ||
         hull.m_faceCount < 4 || hull.m_edgeCount < 6 )
    {
        return HullLoadFailure(
            "Convex hull asset is missing required baked hull_version 2 data: %s  (ConvexHullShape::LoadFromFile)",
            path );
    }

    if ( !sawDefaultMass )
    {
        hull.m_defaultMass = COMPATIBILITY_HULL_DEFAULT_MASS;
        WarnMissingDefaultMassMetadata( path );
    }

    for ( uint16_t f = 0; f < hull.m_faceCount; ++f )
    {
        const ConvexHullFace& face = hull.m_faces[f];
        if ( face.firstIndex + face.indexCount > hull.m_faceIndexCount )
        {
            return HullLoadFailure(
                "Convex hull face %u has invalid index range in %s.  (ConvexHullShape::LoadFromFile)",
                f,
                path );
        }
        for ( uint8_t i = 0; i < face.indexCount; ++i )
        {
            const uint16_t index = hull.m_faceIndices[face.firstIndex + i];
            if ( index >= hull.m_vertexCount )
            {
                return HullLoadFailure(
                    "Convex hull face %u references invalid vertex %u in %s.  (ConvexHullShape::LoadFromFile)",
                    f,
                    index,
                    path );
            }
        }
    }

    for ( uint16_t e = 0; e < hull.m_edgeCount; ++e )
    {
        const ConvexHullEdge& edge = hull.m_edges[e];
        if ( edge.vertexA >= hull.m_vertexCount || edge.vertexB >= hull.m_vertexCount ||
             edge.faceA >= hull.m_faceCount || edge.faceB >= hull.m_faceCount )
        {
            return HullLoadFailure(
                "Convex hull edge %u references invalid topology in %s.  (ConvexHullShape::LoadFromFile)",
                e,
                path );
        }
    }
    CopyHullName( hull.m_name, path, authoredName );
    outHull = hull;
    return SbResult::Success();
}

ConvexHullShape ConvexHullShape::LoadFromFile( const char* path )
{
    ConvexHullShape hull;
    const SbResult result = TryLoadFromFile( path, hull );
    if ( !result.ok )
    {
        SB_FATAL( HULL_LOAD_OWNER, "%s", result.error.message );
    }
    return hull;
}

Matrix4 ConvexHullShape::GetModelMatrix( const Vector3& worldPos, const Matrix4& rotation ) const
{
    return Matrix4::Translate( worldPos ) * rotation * Matrix4::Translate( m_position ) *
           Matrix4::Scale( m_boundingRadius, m_boundingRadius, m_boundingRadius );
}

float ConvexHullShape::GetVolume() const
{
    return m_volume;
}

float ConvexHullShape::GetDefaultMass() const
{
    return Physics::ClampPositiveMass( m_defaultMass );
}

float ConvexHullShape::GetSubmergedVolumePercent( float fluidSurfaceHeight ) const
{
    if ( m_position.y - m_boundingRadius >= fluidSurfaceHeight )
    {
        return 0.0f;
    }
    if ( m_position.y + m_boundingRadius <= fluidSurfaceHeight )
    {
        return 1.0f;
    }
    return 0.5f;
}

float ConvexHullShape::GetDragCoefficient() const
{
    return 1.05f;
}

float ConvexHullShape::GetProjectedSurfaceArea() const
{
    return m_projectedSurfaceArea;
}

float ConvexHullShape::GetBoundingRadius() const
{
    return m_boundingRadius;
}

const Vector3& ConvexHullShape::GetPosition() const
{
    return m_position;
}

const Vector3& ConvexHullShape::GetAuthoredCenterOfMass() const
{
    return m_authoredCenterOfMass;
}

const Vector3& ConvexHullShape::GetInertiaHalfExtents() const
{
    return m_inertiaHalfExtents;
}

Vector3 ConvexHullShape::ComputeBoxApproxInertia( float mass ) const
{
    return m_unitInertia * mass;
}

void ConvexHullShape::ScaleAxis( int axis, float factor )
{
    if ( axis < 0 || axis > 2 || m_vertexCount == 0 || !std::isfinite( factor ) || factor <= TOLERANCE )
    {
        return;
    }

    for ( uint16_t i = 0; i < m_vertexCount; ++i )
    {
        ScaleAxisComponent( m_vertices[i], axis, factor );
    }
    ScaleAxisComponent( m_position, axis, factor );
    ScaleAxisComponent( m_authoredCenterOfMass, axis, factor );

    Vector3 centroid = ZERO_VECTOR;
    Vector3 minV( FLT_MAX, FLT_MAX, FLT_MAX );
    Vector3 maxV( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    m_boundingRadius = 0.0f;
    for ( uint16_t i = 0; i < m_vertexCount; ++i )
    {
        const Vector3& v = m_vertices[i];
        centroid += v;
        minV.x = (std::min)( minV.x, v.x );
        minV.y = (std::min)( minV.y, v.y );
        minV.z = (std::min)( minV.z, v.z );
        maxV.x = (std::max)( maxV.x, v.x );
        maxV.y = (std::max)( maxV.y, v.y );
        maxV.z = (std::max)( maxV.z, v.z );
        m_boundingRadius = (std::max)( m_boundingRadius, sqrtf( VectorMagSquared( v ) ) );
    }
    if ( m_vertexCount > 0 )
    {
        centroid /= static_cast<float>( m_vertexCount );
    }

    for ( uint16_t f = 0; f < m_faceCount; ++f )
    {
        ConvexHullFace& face = m_faces[f];
        if ( face.indexCount < 3 )
        {
            continue;
        }

        const Vector3& a = m_vertices[m_faceIndices[face.firstIndex + 0]];
        const Vector3& b = m_vertices[m_faceIndices[face.firstIndex + 1]];
        const Vector3& c = m_vertices[m_faceIndices[face.firstIndex + 2]];
        Vector3 normal = ZERO_VECTOR;
        const SbResult normalResult =
            TryNormalized( CrossProduct( b - a, c - a ),
                           "Degenerate scaled convex hull face.  (ConvexHullShape::ScaleAxis)",
                           normal );
        if ( !normalResult.ok )
        {
            // Invariant: positive finite copy-scale must preserve baked hull
            // topology. Degeneration here is an engine/data bug, not user input.
            SB_FATAL( HULL_LOAD_OWNER, "%s", normalResult.error.message );
        }
        if ( ( normal * ( centroid - a ) ) > 0.0f )
        {
            normal = -normal;
        }
        face.normalLocal = normal;
        face.planeOffsetLocal = normal * a;
    }

    float projectedX = 0.0f;
    float projectedY = 0.0f;
    float projectedZ = 0.0f;
    for ( uint16_t f = 0; f < m_faceCount; ++f )
    {
        const ConvexHullFace& face = m_faces[f];
        if ( face.indexCount < 3 )
        {
            continue;
        }

        const Vector3& root = m_vertices[m_faceIndices[face.firstIndex]];
        for ( uint8_t i = 1; i + 1 < face.indexCount; ++i )
        {
            const Vector3& b = m_vertices[m_faceIndices[face.firstIndex + i]];
            const Vector3& c = m_vertices[m_faceIndices[face.firstIndex + i + 1]];
            const Vector3 areaVector = CrossProduct( b - root, c - root ) * 0.5f;
            projectedX += fabsf( areaVector.x );
            projectedY += fabsf( areaVector.y );
            projectedZ += fabsf( areaVector.z );
        }
    }

    m_inertiaHalfExtents = ( maxV - minV ) * 0.5f;
    m_inertiaHalfExtents.x = ClampPositive( m_inertiaHalfExtents.x, m_boundingRadius );
    m_inertiaHalfExtents.y = ClampPositive( m_inertiaHalfExtents.y, m_boundingRadius );
    m_inertiaHalfExtents.z = ClampPositive( m_inertiaHalfExtents.z, m_boundingRadius );
    m_unitInertia = BoxApproxUnitInertia( m_inertiaHalfExtents );
    m_volume = (std::max)( 1.0e-4f, m_volume * factor );
    m_defaultMass = Physics::ClampPositiveMass( m_defaultMass * factor );
    m_projectedSurfaceArea = (std::max)( 1.0e-4f, ( projectedX + projectedY + projectedZ ) / 6.0f );
}

uint16_t ConvexHullShape::GetVertexCount() const
{
    return m_vertexCount;
}

uint16_t ConvexHullShape::GetFaceCount() const
{
    return m_faceCount;
}

uint16_t ConvexHullShape::GetEdgeCount() const
{
    return m_edgeCount;
}

const Vector3& ConvexHullShape::GetVertex( uint16_t index ) const
{
    return m_vertices[index];
}

const ConvexHullFace& ConvexHullShape::GetFace( uint16_t index ) const
{
    return m_faces[index];
}

const ConvexHullEdge& ConvexHullShape::GetEdge( uint16_t index ) const
{
    return m_edges[index];
}

uint16_t ConvexHullShape::GetFaceIndex( uint16_t index ) const
{
    return m_faceIndices[index];
}

const char* ConvexHullShape::GetName() const
{
    return m_name;
}

float ConvexHullShape::TestCollision( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const
{
    return SweptBoundingRadiusCollision( GetBoundingRadius(),
                                         GetPosition(),
                                         target.GetRadius(),
                                         target.GetPosition(),
                                         targetRay,
                                         focusRay );
}

float ConvexHullShape::TestCollision( const BoundingBox& target, const Ray& targetRay, const Ray& focusRay ) const
{
    return SweptBoundingRadiusCollision( GetBoundingRadius(),
                                         GetPosition(),
                                         target.GetBoundingRadius(),
                                         target.GetPosition(),
                                         targetRay,
                                         focusRay );
}

float ConvexHullShape::TestCollision( const ConvexHullShape& target, const Ray& targetRay, const Ray& focusRay ) const
{
    return SweptBoundingRadiusCollision( GetBoundingRadius(),
                                         GetPosition(),
                                         target.GetBoundingRadius(),
                                         target.GetPosition(),
                                         targetRay,
                                         focusRay );
}
