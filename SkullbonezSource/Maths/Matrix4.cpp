/*
File: SkullbonezSource/Maths/Matrix4.cpp
Purpose:
  Implements the engine matrix type and common transform operations.

Summary:
  Matrix4 implements column-major transforms, deterministic look-at fallbacks,
  DX12 depth projection, and shadow construction shared by simulation and
  rendering callers.

Glossary:
  Clip-space depth: Projected depth range consumed by the graphics pipeline;
    DX12 callers use zero through one.

Invariants:
  - Matrix storage is column-major and must match shader constant upload layout.
  - DX12 projection callers use the ZeroToOne variants so clip-space depth is
    in [0,1].
  - LookAt rejects a coincident eye/target and replaces a zero or parallel up
    vector with a deterministic orthogonal basis.
  - ShadowFromNormal uses world X for an antiparallel normal so both Debug and
    fused shipping paths remain finite.

Related:
  - SkullbonezSource/Maths/Matrix4.h
  - Agentic/Reference/engine-glossary.md
*/
#include "Matrix4.h"
#include "Quaternion.h"
#include <cmath>
#include <immintrin.h> // SSE intrinsics (_mm_loadu_ps, _mm_set1_ps, _mm_mul_ps, _mm_add_ps, _mm_storeu_ps)


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Vector;


Matrix4::Matrix4()
{
    m[0] = 1.0f;
    m[4] = 0.0f;
    m[8] = 0.0f;
    m[12] = 0.0f;
    m[1] = 0.0f;
    m[5] = 1.0f;
    m[9] = 0.0f;
    m[13] = 0.0f;
    m[2] = 0.0f;
    m[6] = 0.0f;
    m[10] = 1.0f;
    m[14] = 0.0f;
    m[3] = 0.0f;
    m[7] = 0.0f;
    m[11] = 0.0f;
    m[15] = 1.0f;
}


Matrix4::Matrix4( const float* values )
{
    for ( int i = 0; i < 16; ++i )
    {
        m[i] = values[i];
    }
}


Matrix4 Matrix4::PerspectiveZeroToOne( float fovDegrees, float aspect, float nearPlane, float farPlane )
{
    Matrix4 result;
    float fovRad = fovDegrees * ( _PI / 180.0f );
    float tanHalf = tanf( fovRad * 0.5f );

    for ( int i = 0; i < 16; ++i )
    {
        result.m[i] = 0.0f;
    }

    result.m[0] = 1.0f / ( aspect * tanHalf );
    result.m[5] = 1.0f / tanHalf;
    result.m[10] = farPlane / ( nearPlane - farPlane );
    result.m[11] = -1.0f;
    result.m[14] = ( nearPlane * farPlane ) / ( nearPlane - farPlane );

    return result;
}


Matrix4 Matrix4::Ortho( float left, float right, float bottom, float top, float nearPlane, float farPlane )
{
    Matrix4 result;

    for ( int i = 0; i < 16; ++i )
    {
        result.m[i] = 0.0f;
    }

    result.m[0] = 2.0f / ( right - left );
    result.m[5] = 2.0f / ( top - bottom );
    result.m[10] = -2.0f / ( farPlane - nearPlane );
    result.m[12] = -( right + left ) / ( right - left );
    result.m[13] = -( top + bottom ) / ( top - bottom );
    result.m[14] = -( farPlane + nearPlane ) / ( farPlane - nearPlane );
    result.m[15] = 1.0f;

    return result;
}


Matrix4 Matrix4::OrthoZeroToOne( float left, float right, float bottom, float top, float nearPlane, float farPlane )
{
    Matrix4 result;

    for ( int i = 0; i < 16; ++i )
    {
        result.m[i] = 0.0f;
    }

    result.m[0] = 2.0f / ( right - left );
    result.m[5] = 2.0f / ( top - bottom );
    result.m[10] = 1.0f / ( nearPlane - farPlane );
    result.m[12] = -( right + left ) / ( right - left );
    result.m[13] = -( top + bottom ) / ( top - bottom );
    result.m[14] = nearPlane / ( nearPlane - farPlane );
    result.m[15] = 1.0f;

    return result;
}


Matrix4 Matrix4::LookAt( const Vector3& eye, const Vector3& center, const Vector3& up )
{
    Vector3 f = center - eye;

    if ( VectorMag( f ) < 1e-6f )
    {
        return Matrix4();
    }

    f.Normalise();

    Vector3 u = up;

    if ( !u.TryNormalise() )
    {

        // Fallback: authored cameras may omit up; world +Y is deterministic
        // and the parallel-axis branch below still handles a top-down view.
        u = Vector3( 0.0f, 1.0f, 0.0f );
    }

    Vector3 s = CrossProduct( f, u );

    // f and u are parallel (e.g. top-down camera) — pick arbitrary perpendicular
    if ( VectorMag( s ) < 1e-6f )
    {
        u = ( fabsf( f.x ) < 0.9f ) ? Vector3( 1.0f, 0.0f, 0.0f ) : Vector3( 0.0f, 0.0f, 1.0f );
        s = CrossProduct( f, u );
    }

    s.Normalise();

    u = CrossProduct( s, f );

    Matrix4 result;
    result.m[0] = s.x;
    result.m[4] = s.y;
    result.m[8] = s.z;
    result.m[1] = u.x;
    result.m[5] = u.y;
    result.m[9] = u.z;
    result.m[2] = -f.x;
    result.m[6] = -f.y;
    result.m[10] = -f.z;
    result.m[12] = -( Dot( s, eye ) );
    result.m[13] = -( Dot( u, eye ) );
    result.m[14] = ( Dot( f, eye ) );

    return result;
}


Matrix4 Matrix4::Translate( const Vector3& v )
{
    return Translate( v.x, v.y, v.z );
}


Matrix4 Matrix4::Translate( float x, float y, float z )
{
    Matrix4 result;
    result.m[12] = x;
    result.m[13] = y;
    result.m[14] = z;
    return result;
}


Matrix4 Matrix4::Scale( float x, float y, float z )
{
    Matrix4 result;
    result.m[0] = x;
    result.m[5] = y;
    result.m[10] = z;
    return result;
}


Matrix4 Matrix4::Scale( float uniform )
{
    return Scale( uniform, uniform, uniform );
}


Matrix4 Matrix4::FromQuaternion( const Quaternion& q )
{

    // Unit quaternion q = (qx, qy, qz, qw) becomes a 4x4 column-major rotation matrix.
    //
    // Derivation:
    //   Applying rotation q to a vector v uses the sandwich product: v' = q * (0,v) * q'
    //   Expanding that product and collecting terms yields 9 bilinear expressions in the
    //   quaternion components.  Every term carries a factor of 2 because q encodes half-angles
    //   (the rotation angle θ is stored as sin(θ/2) and cos(θ/2) in the vector and scalar parts).
    //
    //   Pre-computing the 9 products (xx2 = 2·qx·qx, etc.) avoids 9 redundant multiplies in
    //   the array initialiser below.
    //
    // Column-major memory layout:
    //   col0 = local X axis (right)   = m[0..3]
    //   col1 = local Y axis (up)      = m[4..7]
    //   col2 = local Z axis (forward) = m[8..11]
    //   col3 = translation            = m[12..15]
    //
    // Resulting matrix (row-by-row for readability, stored column-major):
    //   [ 1-(yy2+zz2)   xy2-wz2     xz2+wy2    0 ]
    //   [   xy2+wz2   1-(xx2+zz2)   yz2-wx2    0 ]
    //   [   xz2-wy2     yz2+wx2   1-(xx2+yy2)  0 ]
    //   [      0           0           0        1 ]
    //
    // Quaternion and RotationMatrix expose the same canonical active rotation.
    // Keep this column-major form equivalent to Quaternion::GetOrientationMatrix.

    float qx, qy, qz, qw;
    q.GetComponents( qx, qy, qz, qw );

    const float xx2 = 2.0f * qx * qx, yy2 = 2.0f * qy * qy, zz2 = 2.0f * qz * qz;
    const float xy2 = 2.0f * qx * qy, xz2 = 2.0f * qx * qz, yz2 = 2.0f * qy * qz;
    const float wx2 = 2.0f * qw * qx, wy2 = 2.0f * qw * qy, wz2 = 2.0f * qw * qz;

    const float r[16] = {
        1.0f - ( yy2 + zz2 ),
        xy2 + wz2,
        xz2 - wy2,
        0.0f, // col0: local X (right)
        xy2 - wz2,
        1.0f - ( xx2 + zz2 ),
        yz2 + wx2,
        0.0f, // col1: local Y (up)
        xz2 + wy2,
        yz2 - wx2,
        1.0f - ( xx2 + yy2 ),
        0.0f, // col2: local Z (forward)
        0.0f,
        0.0f,
        0.0f,
        1.0f // col3: translation (identity)
    };

    return Matrix4( r );
}


Matrix4 Matrix4::operator*( const Matrix4& rhs ) const
{
#ifdef _DEBUG

    // Debug: scalar triple-loop — each intermediate value is individually inspectable.
    // result[col][row] = sum_k( lhs[k][row] * rhs[col][k] )
    Matrix4 result;

    for ( int col = 0; col < 4; ++col )
    {
        for ( int row = 0; row < 4; ++row )
        {
            result.m[col * 4 + row] = 0.0f;

            for ( int k = 0; k < 4; ++k )
            {
                result.m[col * 4 + row] += m[k * 4 + row] * rhs.m[col * 4 + k];
            }
        }
    }

    return result;
#else

    // Release/Profile: column-major 4×4 × 4×4 using SSE.
    //
    // STRATEGY: for each output column c, compute all 4 rows simultaneously using
    // the column-outer-product sum:
    //
    //   out_col_c = LHS.col0 * rhs[c*4+0]
    //             + LHS.col1 * rhs[c*4+1]
    //             + LHS.col2 * rhs[c*4+2]
    //             + LHS.col3 * rhs[c*4+3]
    //
    // where each LHS.colN is a __m128 holding all 4 rows of that column, and
    // rhs[c*4+k] is broadcast (replicated into all 4 lanes) so the multiply
    // scales every row of that column by the same scalar.
    //
    // SSE INTRINSICS USED:
    //   _mm_loadu_ps(ptr)          — load 4 floats from unaligned memory into a register
    //   _mm_set1_ps(scalar)        — broadcast a single float into all 4 lanes
    //   _mm_mul_ps(a, b)           — lane-wise multiply:  (a0*b0, a1*b1, a2*b2, a3*b3)
    //   _mm_add_ps(a, b)           — lane-wise add:       (a0+b0, a1+b1, a2+b2, a3+b3)
    //   _mm_storeu_ps(ptr, reg)    — store 4 floats to unaligned memory
    //
    // The four LHS columns are loaded once outside the loop so they stay in registers
    // across all four output-column iterations, avoiding 16 redundant loads.

    const __m128 lhsC0 = _mm_loadu_ps( m + 0 ); // LHS column 0: m[0..3]
    const __m128 lhsC1 = _mm_loadu_ps( m + 4 ); // LHS column 1: m[4..7]

    const __m128 lhsC2 = _mm_loadu_ps( m + 8 ); // LHS column 2: m[8..11]

    const __m128 lhsC3 = _mm_loadu_ps( m + 12 ); // LHS column 3: m[12..15]

    float r[16];

    for ( int c = 0; c < 4; ++c )
    {

        // Broadcast each RHS scalar for output column c, scale the matching LHS column,
        // accumulate four contributions with two paired adds (avoids a 4-way add chain).
        _mm_storeu_ps( r + c * 4, _mm_add_ps( _mm_add_ps( _mm_mul_ps( lhsC0, _mm_set1_ps( rhs.m[c * 4 + 0] ) ),
                                                          _mm_mul_ps( lhsC1, _mm_set1_ps( rhs.m[c * 4 + 1] ) ) ),
                                              _mm_add_ps( _mm_mul_ps( lhsC2, _mm_set1_ps( rhs.m[c * 4 + 2] ) ),
                                                          _mm_mul_ps( lhsC3, _mm_set1_ps( rhs.m[c * 4 + 3] ) ) ) ) );
    }

    return Matrix4( r );
#endif
}


Matrix4& Matrix4::operator*=( const Matrix4& rhs )
{
    *this = *this * rhs;
    return *this;
}


const float* Matrix4::Data() const
{
    return m;
}


Matrix4 Matrix4::Inverse() const
{

    // This cofactor expansion assumes the engine's column-major storage order.
    float inv[16];
    float det;

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] -
             m[13] * m[7] * m[10];

    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];

    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];

    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];

    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];

    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];

    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];

    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];

    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] +
             m[12] * m[3] * m[6];

    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];

    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] +
              m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] +
             m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] +
              m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if ( fabsf( det ) < 1e-10f )
    {
        return Matrix4(); // Identity fallback keeps singular transforms finite.
    }

    float invDet = 1.0f / det;
    Matrix4 result;

    for ( int i = 0; i < 16; ++i )
    {
        result.m[i] = inv[i] * invDet;
    }

    return result;
}
