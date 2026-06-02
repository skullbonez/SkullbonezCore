#pragma once

#include "SkullbonezCommon.h"

#include <cmath>


namespace SkullbonezCore
{
namespace Rendering
{
namespace PrimitiveMeshes
{
/* -- Primitive Mesh Builder --------------------------------------------------------------------------------------------------------------------------------------

    Shared CPU-side triangle emitters for the engine's built-in unit primitives.

    These helpers intentionally stop at canonical primitive geometry:
      - position
      - normal
      - simple UVs

    The normal renderer and collision visualizer still pack that data into different GPU layouts.
    Normal rendering needs pos3 + normal3 + uv2 with a matrix-only instance payload.
    Collision visualization needs pos3 + normal3 with a matrix + rgba instance payload.

    Keeping the primitive coordinates here avoids drift in sphere orientation, cube winding, and
    tessellation while preserving each renderer's specialized shader and instance format.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct VertexPNUV
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};


inline int SphereTriangleVertexCount( int slices, int stacks )
{
    return slices * stacks * 6;
}


inline constexpr int BoxTriangleVertexCount()
{
    return 36;
}


template <typename EmitVertex>
inline void EmitUnitSphere( int slices, int stacks, EmitVertex emitVertex )
{
    // The sphere is generated as triangle-expanded UV-sphere quads. Its local frame matches the
    // existing Skullbonez visual/physics convention: theta=0 points down negative Z, which is the
    // same orientation both the normal renderer and collision visualizer already used.
    auto emit = [&]( float x, float y, float z, float u, float v )
    {
        emitVertex( VertexPNUV{ x, y, z, x, y, z, u, v } );
    };

    for ( int i = 0; i < stacks; ++i )
    {
        const float phi0 = _PI * static_cast<float>( i ) / static_cast<float>( stacks );
        const float phi1 = _PI * static_cast<float>( i + 1 ) / static_cast<float>( stacks );

        for ( int j = 0; j < slices; ++j )
        {
            const float theta0 = _2PI * static_cast<float>( j ) / static_cast<float>( slices );
            const float theta1 = _2PI * static_cast<float>( j + 1 ) / static_cast<float>( slices );

            const float x00 = sinf( phi0 ) * sinf( theta0 ), y00 = cosf( phi0 ), z00 = -sinf( phi0 ) * cosf( theta0 );
            const float x01 = sinf( phi0 ) * sinf( theta1 ), y01 = cosf( phi0 ), z01 = -sinf( phi0 ) * cosf( theta1 );
            const float x10 = sinf( phi1 ) * sinf( theta0 ), y10 = cosf( phi1 ), z10 = -sinf( phi1 ) * cosf( theta0 );
            const float x11 = sinf( phi1 ) * sinf( theta1 ), y11 = cosf( phi1 ), z11 = -sinf( phi1 ) * cosf( theta1 );

            const float u0 = static_cast<float>( j ) / static_cast<float>( slices );
            const float v0 = static_cast<float>( i ) / static_cast<float>( stacks );
            const float u1 = static_cast<float>( j + 1 ) / static_cast<float>( slices );
            const float v1 = static_cast<float>( i + 1 ) / static_cast<float>( stacks );

            emit( x00, y00, z00, u0, v0 );
            emit( x11, y11, z11, u1, v1 );
            emit( x10, y10, z10, u0, v1 );

            emit( x00, y00, z00, u0, v0 );
            emit( x01, y01, z01, u1, v0 );
            emit( x11, y11, z11, u1, v1 );
        }
    }
}


template <typename EmitVertex>
inline void EmitUnitBox( EmitVertex emitVertex )
{
    struct CubeFace
    {
        float nx, ny, nz;
        float v0[3], v1[3], v2[3], v3[3];
    };

    // Each face lists four corners in the order used to emit two outward-facing triangles:
    // v0, v1, v2 and v0, v2, v3.
    // clang-format off
    CubeFace faces[6] = {
        { 1, 0, 0, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}, { 1,-1, 1} },
        {-1, 0, 0, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}, {-1,-1,-1} },
        { 0, 1, 0, {-1, 1,-1}, {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1} },
        { 0,-1, 0, {-1,-1, 1}, {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1} },
        { 0, 0, 1, {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1} },
        { 0, 0,-1, { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1} }
    };
    // clang-format on

    const float uv[4][2] = { { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } };

    auto emit = [&]( const float* v, const CubeFace& face, const float* texCoord )
    {
        emitVertex( VertexPNUV{ v[0], v[1], v[2], face.nx, face.ny, face.nz, texCoord[0], texCoord[1] } );
    };

    for ( int f = 0; f < 6; ++f )
    {
        const CubeFace& face = faces[f];
        const float* v[4] = { face.v0, face.v1, face.v2, face.v3 };

        emit( v[0], face, uv[0] );
        emit( v[1], face, uv[1] );
        emit( v[2], face, uv[2] );

        emit( v[0], face, uv[0] );
        emit( v[2], face, uv[2] );
        emit( v[3], face, uv[3] );
    }
}
} // namespace PrimitiveMeshes
} // namespace Rendering
} // namespace SkullbonezCore
