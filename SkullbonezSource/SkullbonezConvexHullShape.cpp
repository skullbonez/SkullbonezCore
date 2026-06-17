/*
File: SkullbonezSource/SkullbonezConvexHullShape.cpp
Purpose:
  Loads, validates, and exposes immutable convex hull collision geometry.

Mental model:
  Hull assets are authoring data. This file freezes them into deterministic
  topology once, before the fixed-step physics loop reads the shape.

Related:
  - SkullbonezSource/SkullbonezConvexHullShape.h
  - SkullbonezSource/SkullbonezObjectContactManifold.cpp
*/
#include "SkullbonezConvexHullShape.h"

#include "SkullbonezBoundingBox.h"
#include "SkullbonezBoundingSphere.h"

#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;

namespace
{
struct ParsedFace
{
    uint16_t start = 0;
    uint8_t count = 0;
};

float ClampPositive( float value, float fallback )
{
    return value > TOLERANCE ? value : fallback;
}

Vector3 NormalizedOrThrow( const Vector3& v, const char* context )
{
    const float magSq = VectorMagSquared( v );
    if ( magSq <= 1.0e-10f )
    {
        throw std::runtime_error( context );
    }
    return v / sqrtf( magSq );
}

float ParseFiniteFloatOrThrow( const char* value, const char* path, int lineNumber )
{
    errno = 0;
    char* end = nullptr;
    const double parsed = strtod( value, &end );
    if ( end == value || *end != '\0' || errno == ERANGE || !std::isfinite( parsed ) || parsed < -FLT_MAX || parsed > FLT_MAX )
    {
        char msg[384];
        sprintf_s( msg, sizeof( msg ), "Invalid vertex coordinate at %s:%d.  (ConvexHullShape::LoadFromFile)", path, lineNumber );
        throw std::runtime_error( msg );
    }
    return static_cast<float>( parsed );
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
} // namespace

ConvexHullShape::ConvexHullShape()
{
    strcpy_s( m_name, sizeof( m_name ), "convex_hull" );
}

ConvexHullShape ConvexHullShape::LoadFromFile( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        throw std::runtime_error( "Convex hull path is empty.  (ConvexHullShape::LoadFromFile)" );
    }

    FILE* file = nullptr;
    if ( fopen_s( &file, path, "r" ) != 0 || !file )
    {
        char msg[384];
        sprintf_s( msg, sizeof( msg ), "Unable to open convex hull asset: %s  (ConvexHullShape::LoadFromFile)", path );
        throw std::runtime_error( msg );
    }

    ConvexHullShape hull;
    std::array<Vector3, MAX_VERTICES> vertices = {};
    std::array<uint16_t, MAX_FACE_INDICES> indices = {};
    std::array<ParsedFace, MAX_FACES> parsedFaces = {};
    uint16_t vertexCount = 0;
    uint16_t faceCount = 0;
    uint16_t faceIndexCount = 0;
    char authoredName[64] = {};
    int lineNumber = 0;
    bool sawVersion = false;

    char line[512];
    while ( fgets( line, sizeof( line ), file ) )
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
            if ( !version || strcmp( version, "1" ) != 0 )
            {
                char msg[384];
                sprintf_s( msg, sizeof( msg ), "Invalid hull_version at %s:%d.  (ConvexHullShape::LoadFromFile)", path, lineNumber );
                fclose( file );
                throw std::runtime_error( msg );
            }
            sawVersion = true;
            continue;
        }

        if ( strcmp( token, "name" ) == 0 )
        {
            char* name = strtok_s( nullptr, " \t\r\n", &context );
            if ( name )
            {
                strncpy_s( authoredName, sizeof( authoredName ), name, _TRUNCATE );
            }
            continue;
        }

        if ( strcmp( token, "vertex" ) == 0 )
        {
            if ( vertexCount >= MAX_VERTICES )
            {
                char msg[384];
                sprintf_s( msg, sizeof( msg ), "Convex hull exceeds %u vertices at %s:%d.  (ConvexHullShape::LoadFromFile)", MAX_VERTICES, path, lineNumber );
                fclose( file );
                throw std::runtime_error( msg );
            }

            char* sx = strtok_s( nullptr, " \t\r\n", &context );
            char* sy = strtok_s( nullptr, " \t\r\n", &context );
            char* sz = strtok_s( nullptr, " \t\r\n", &context );
            if ( !sx || !sy || !sz )
            {
                char msg[384];
                sprintf_s( msg, sizeof( msg ), "Invalid vertex at %s:%d.  (ConvexHullShape::LoadFromFile)", path, lineNumber );
                fclose( file );
                throw std::runtime_error( msg );
            }

            vertices[vertexCount++] = Vector3( ParseFiniteFloatOrThrow( sx, path, lineNumber ),
                                               ParseFiniteFloatOrThrow( sy, path, lineNumber ),
                                               ParseFiniteFloatOrThrow( sz, path, lineNumber ) );
            continue;
        }

        if ( strcmp( token, "face" ) == 0 )
        {
            if ( faceCount >= MAX_FACES )
            {
                char msg[384];
                sprintf_s( msg, sizeof( msg ), "Convex hull exceeds %u faces at %s:%d.  (ConvexHullShape::LoadFromFile)", MAX_FACES, path, lineNumber );
                fclose( file );
                throw std::runtime_error( msg );
            }

            ParsedFace face;
            face.start = faceIndexCount;
            char* value = nullptr;
            while ( ( value = strtok_s( nullptr, " \t\r\n", &context ) ) != nullptr )
            {
                if ( face.count >= MAX_FACE_VERTICES || faceIndexCount >= MAX_FACE_INDICES )
                {
                    char msg[384];
                    sprintf_s( msg, sizeof( msg ), "Convex hull face exceeds first-pass limits at %s:%d.  (ConvexHullShape::LoadFromFile)", path, lineNumber );
                    fclose( file );
                    throw std::runtime_error( msg );
                }
                errno = 0;
                char* end = nullptr;
                const long parsed = strtol( value, &end, 10 );
                if ( end == value || *end != '\0' || errno == ERANGE || parsed < 0 || parsed > 65535 )
                {
                    char msg[384];
                    sprintf_s( msg, sizeof( msg ), "Invalid face index at %s:%d.  (ConvexHullShape::LoadFromFile)", path, lineNumber );
                    fclose( file );
                    throw std::runtime_error( msg );
                }
                indices[faceIndexCount++] = static_cast<uint16_t>( parsed );
                ++face.count;
            }

            if ( face.count < 3 )
            {
                char msg[384];
                sprintf_s( msg, sizeof( msg ), "Convex hull face needs at least three vertices at %s:%d.  (ConvexHullShape::LoadFromFile)", path, lineNumber );
                fclose( file );
                throw std::runtime_error( msg );
            }
            parsedFaces[faceCount++] = face;
            continue;
        }

        char msg[384];
        sprintf_s( msg, sizeof( msg ), "Unknown hull directive '%s' at %s:%d.  (ConvexHullShape::LoadFromFile)", token, path, lineNumber );
        fclose( file );
        throw std::runtime_error( msg );
    }

    fclose( file );

    if ( !sawVersion || vertexCount < 4 || faceCount < 4 )
    {
        char msg[384];
        sprintf_s( msg, sizeof( msg ), "Convex hull asset must declare hull_version 1, at least 4 vertices, and at least 4 faces: %s  (ConvexHullShape::LoadFromFile)", path );
        throw std::runtime_error( msg );
    }

    Vector3 centroid = ZERO_VECTOR;
    Vector3 minV( FLT_MAX, FLT_MAX, FLT_MAX );
    Vector3 maxV( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    for ( uint16_t i = 0; i < vertexCount; ++i )
    {
        centroid += vertices[i];
        minV.x = (std::min)( minV.x, vertices[i].x );
        minV.y = (std::min)( minV.y, vertices[i].y );
        minV.z = (std::min)( minV.z, vertices[i].z );
        maxV.x = (std::max)( maxV.x, vertices[i].x );
        maxV.y = (std::max)( maxV.y, vertices[i].y );
        maxV.z = (std::max)( maxV.z, vertices[i].z );
        hull.m_boundingRadius = (std::max)( hull.m_boundingRadius, sqrtf( VectorMagSquared( vertices[i] ) ) );
    }
    centroid /= static_cast<float>( vertexCount );

    hull.m_vertexCount = vertexCount;
    for ( uint16_t i = 0; i < vertexCount; ++i )
    {
        hull.m_vertices[i] = vertices[i];
    }

    hull.m_faceCount = faceCount;
    for ( uint16_t f = 0; f < faceCount; ++f )
    {
        const ParsedFace& parsed = parsedFaces[f];
        for ( uint8_t i = 0; i < parsed.count; ++i )
        {
            const uint16_t index = indices[parsed.start + i];
            if ( index >= vertexCount )
            {
                char msg[384];
                sprintf_s( msg, sizeof( msg ), "Convex hull face references vertex %u but only %u vertices exist: %s  (ConvexHullShape::LoadFromFile)", index, vertexCount, path );
                throw std::runtime_error( msg );
            }
        }

        const Vector3& a = vertices[indices[parsed.start + 0]];
        const Vector3& b = vertices[indices[parsed.start + 1]];
        const Vector3& c = vertices[indices[parsed.start + 2]];
        Vector3 normal = NormalizedOrThrow( CrossProduct( b - a, c - a ), "Degenerate convex hull face.  (ConvexHullShape::LoadFromFile)" );
        bool flip = ( normal * ( centroid - a ) ) > 0.0f;
        if ( flip )
        {
            normal = -normal;
        }
        const float planeOffset = normal * a;
        for ( uint8_t i = 0; i < parsed.count; ++i )
        {
            const Vector3& faceVertex = vertices[indices[parsed.start + i]];
            if ( fabsf( ( normal * faceVertex ) - planeOffset ) > 1.0e-3f )
            {
                char msg[384];
                sprintf_s( msg, sizeof( msg ), "Convex hull face %u is not planar in %s.  (ConvexHullShape::LoadFromFile)", f, path );
                throw std::runtime_error( msg );
            }
        }

        ConvexHullFace face;
        face.normalLocal = normal;
        face.planeOffsetLocal = planeOffset;
        face.firstIndex = hull.m_faceIndexCount;
        face.indexCount = parsed.count;
        hull.m_faces[f] = face;

        for ( uint8_t i = 0; i < parsed.count; ++i )
        {
            const uint8_t source = flip ? static_cast<uint8_t>( parsed.count - 1 - i ) : i;
            hull.m_faceIndices[hull.m_faceIndexCount++] = indices[parsed.start + source];
        }
    }

    for ( uint16_t f = 0; f < hull.m_faceCount; ++f )
    {
        const ConvexHullFace& face = hull.m_faces[f];
        for ( uint16_t v = 0; v < hull.m_vertexCount; ++v )
        {
            const float signedDistance = ( face.normalLocal * hull.m_vertices[v] ) - face.planeOffsetLocal;
            if ( signedDistance > 1.0e-3f )
            {
                char msg[384];
                sprintf_s( msg, sizeof( msg ), "Convex hull is not convex near face %u in %s.  (ConvexHullShape::LoadFromFile)", f, path );
                throw std::runtime_error( msg );
            }
        }
    }

    for ( uint16_t f = 0; f < hull.m_faceCount; ++f )
    {
        const ConvexHullFace& face = hull.m_faces[f];
        for ( uint8_t i = 0; i < face.indexCount; ++i )
        {
            uint16_t a = hull.m_faceIndices[face.firstIndex + i];
            uint16_t b = hull.m_faceIndices[face.firstIndex + ( ( i + 1 ) % face.indexCount )];
            const uint16_t lo = (std::min)( a, b );
            const uint16_t hi = (std::max)( a, b );

            int existing = -1;
            for ( uint16_t e = 0; e < hull.m_edgeCount; ++e )
            {
                if ( hull.m_edges[e].vertexA == lo && hull.m_edges[e].vertexB == hi )
                {
                    existing = e;
                    break;
                }
            }

            if ( existing < 0 )
            {
                if ( hull.m_edgeCount >= MAX_EDGES )
                {
                    char msg[384];
                    sprintf_s( msg, sizeof( msg ), "Convex hull exceeds %u edges in %s.  (ConvexHullShape::LoadFromFile)", MAX_EDGES, path );
                    throw std::runtime_error( msg );
                }
                ConvexHullEdge edge;
                edge.vertexA = lo;
                edge.vertexB = hi;
                edge.faceA = f;
                edge.faceB = 0xffffu;
                hull.m_edges[hull.m_edgeCount++] = edge;
            }
            else
            {
                ConvexHullEdge& edge = hull.m_edges[existing];
                if ( edge.faceB != 0xffffu )
                {
                    char msg[384];
                    sprintf_s( msg, sizeof( msg ), "Convex hull edge has more than two faces in %s.  (ConvexHullShape::LoadFromFile)", path );
                    throw std::runtime_error( msg );
                }
                edge.faceB = f;
            }
        }
    }

    for ( uint16_t e = 0; e < hull.m_edgeCount; ++e )
    {
        if ( hull.m_edges[e].faceB == 0xffffu )
        {
            char msg[384];
            sprintf_s( msg, sizeof( msg ), "Convex hull edge has only one adjacent face in %s.  (ConvexHullShape::LoadFromFile)", path );
            throw std::runtime_error( msg );
        }
    }

    hull.m_inertiaHalfExtents = ( maxV - minV ) * 0.5f;
    hull.m_inertiaHalfExtents.x = ClampPositive( hull.m_inertiaHalfExtents.x, hull.m_boundingRadius );
    hull.m_inertiaHalfExtents.y = ClampPositive( hull.m_inertiaHalfExtents.y, hull.m_boundingRadius );
    hull.m_inertiaHalfExtents.z = ClampPositive( hull.m_inertiaHalfExtents.z, hull.m_boundingRadius );
    hull.m_volume = (std::max)( 1.0e-4f, 8.0f * hull.m_inertiaHalfExtents.x * hull.m_inertiaHalfExtents.y * hull.m_inertiaHalfExtents.z * 0.55f );
    hull.m_projectedSurfaceArea = ( 4.0f * hull.m_inertiaHalfExtents.x * hull.m_inertiaHalfExtents.y +
                                    4.0f * hull.m_inertiaHalfExtents.x * hull.m_inertiaHalfExtents.z +
                                    4.0f * hull.m_inertiaHalfExtents.y * hull.m_inertiaHalfExtents.z ) /
                                  3.0f;
    CopyHullName( hull.m_name, path, authoredName );
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

const Vector3& ConvexHullShape::GetInertiaHalfExtents() const
{
    return m_inertiaHalfExtents;
}

Vector3 ConvexHullShape::ComputeBoxApproxInertia( float mass ) const
{
    const float hx2 = m_inertiaHalfExtents.x * m_inertiaHalfExtents.x;
    const float hy2 = m_inertiaHalfExtents.y * m_inertiaHalfExtents.y;
    const float hz2 = m_inertiaHalfExtents.z * m_inertiaHalfExtents.z;
    const float m3 = mass / 3.0f;
    return Vector3( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );
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
    return SweptBoundingRadiusCollision( GetBoundingRadius(), GetPosition(), target.GetRadius(), target.GetPosition(), targetRay, focusRay );
}

float ConvexHullShape::TestCollision( const BoundingBox& target, const Ray& targetRay, const Ray& focusRay ) const
{
    return SweptBoundingRadiusCollision( GetBoundingRadius(), GetPosition(), target.GetBoundingRadius(), target.GetPosition(), targetRay, focusRay );
}

float ConvexHullShape::TestCollision( const ConvexHullShape& target, const Ray& targetRay, const Ray& focusRay ) const
{
    return SweptBoundingRadiusCollision( GetBoundingRadius(), GetPosition(), target.GetBoundingRadius(), target.GetPosition(), targetRay, focusRay );
}
