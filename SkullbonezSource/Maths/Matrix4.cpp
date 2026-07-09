/*
File: SkullbonezSource/Maths/Matrix4.cpp
Purpose:
  Implements the engine matrix type and common transform operations.

Mental model:
  Matrix4.cpp implements the engine matrix type and common transform
  operations. As an implementation unit, keep edits anchored on units, basis
  conventions, and numerical assumptions and on the glossary/invariants below.

Glossary:
  Engine module: A source file with one focused responsibility inside the
  SkullbonezCore runtime.

Invariants:
  - Matrix storage is column-major and must match shader constant upload layout.
  - DX12 projection callers use the ZeroToOne variants so clip-space depth is
    in [0,1].

Related:
  - SkullbonezSource/Maths/Matrix4.h
  - Agentic/Reference/comment-style-guide.md
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


Matrix4 Matrix4::Perspective( float fovDegrees, float aspect, float nearPlane, float farPlane )
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
    result.m[10] = -( farPlane + nearPlane ) / ( farPlane - nearPlane );
    result.m[11] = -1.0f;
    result.m[14] = -( 2.0f * farPlane * nearPlane ) / ( farPlane - nearPlane );

    return result;
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
    u.Normalise();

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
    result.m[12] = -( s * eye );
    result.m[13] = -( u * eye );
    result.m[14] = ( f * eye );

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


Matrix4 Matrix4::Scale( const Vector3& v )
{
    return Scale( v.x, v.y, v.z );
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


Matrix4 Matrix4::RotateAxis( float angleDeg, float axisX, float axisY, float axisZ )
{
    float rad = angleDeg * ( 3.14159265f / 180.0f );
    float c = cosf( rad );
    float s = sinf( rad );
    float t = 1.0f - c;

    // Normalise axis
    float mag = sqrtf( axisX * axisX + axisY * axisY + axisZ * axisZ );
    if ( mag > 0.0f )
    {
        axisX /= mag;
        axisY /= mag;
        axisZ /= mag;
    }

    Matrix4 result;
    result.m[0] = t * axisX * axisX + c;
    result.m[1] = t * axisX * axisY + s * axisZ;
    result.m[2] = t * axisX * axisZ - s * axisY;
    result.m[3] = 0.0f;

    result.m[4] = t * axisX * axisY - s * axisZ;
    result.m[5] = t * axisY * axisY + c;
    result.m[6] = t * axisY * axisZ + s * axisX;
    result.m[7] = 0.0f;

    result.m[8] = t * axisX * axisZ + s * axisY;
    result.m[9] = t * axisY * axisZ - s * axisX;
    result.m[10] = t * axisZ * axisZ + c;
    result.m[11] = 0.0f;

    result.m[12] = 0.0f;
    result.m[13] = 0.0f;
    result.m[14] = 0.0f;
    result.m[15] = 1.0f;
    return result;
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
    // This engine uses an anti-Hamilton quaternion convention — GetOrientationMatrix() returns
    // the transpose of a standard active rotation.  Do NOT change the sign convention here.

    float qx, qy, qz, qw;
    q.GetComponents( qx, qy, qz, qw );

    const float xx2 = 2.0f * qx * qx, yy2 = 2.0f * qy * qy, zz2 = 2.0f * qz * qz;
    const float xy2 = 2.0f * qx * qy, xz2 = 2.0f * qx * qz, yz2 = 2.0f * qy * qz;
    const float wx2 = 2.0f * qw * qx, wy2 = 2.0f * qw * qy, wz2 = 2.0f * qw * qz;

    const float r[16] = {
        1.0f - ( yy2 + zz2 ),
        xy2 - wz2,
        xz2 + wy2,
        0.0f, // col0: local X (right)
        xy2 + wz2,
        1.0f - ( xx2 + zz2 ),
        yz2 - wx2,
        0.0f, // col1: local Y (up)
        xz2 - wy2,
        yz2 + wx2,
        1.0f - ( xx2 + yy2 ),
        0.0f, // col2: local Z (forward)
        0.0f,
        0.0f,
        0.0f,
        1.0f // col3: translation (identity)
    };
    return Matrix4( r );
}


Matrix4 Matrix4::ShadowFromNormal( float tx, float ty, float tz, const Vector3& N, float scale )
{
    // Fused single-pass evaluation of: T(tx,ty,tz) * RotFromUpToN * Scale(scale)
    //
    // WHY: The old shadow path called GetTerrainNormalAt (a second LocatePolygon walk),
    // then built a rotation from world-up to N by computing acosf(N.y), converting the
    // result to degrees, passing it into RotateAxis (which called cosf+sinf on the angle
    // we just computed from acosf — immediately undoing the trig), then performed three
    // separate Matrix4 multiplications.  This function fuses the entire chain into one
    // pass with one sqrtf and zero transcendentals.
    //
    // ROTATION DERIVATION:
    //   We need a rotation R such that R * (0,1,0) = N.
    //   The rotation axis is the cross product of world-up and N:
    //     axis = (0,1,0) × N = (0*N.z - 1*N.y, 1*N.x - 0*N.z, 0*N.y - 0*N.x)
    //                        = (-N.y*0... wait: (0,1,0)×(Nx,Ny,Nz) = (1*Nz-0*Ny, 0*Nx-0*Nz, 0*Ny-1*Nx)
    //                        = (N.z, 0, -N.x)
    //   Magnitude of axis = sqrt(N.z² + N.x²) = sinA  (since |N|=1 and cosA = N.y)
    //   Normalised axis:  ax = N.z/sinA,  ay = 0,  az = -N.x/sinA
    //   cosA = N·up = N.y,  sinA = sqrt(N.x² + N.z²)
    //
    // RODRIGUES FORMULA for unit-axis (ax, 0, az), cos=c, sin=s, t=(1-c):
    //   R = [ t·ax²+c     t·ax·ay - s·az    t·ax·az + s·ay ]
    //       [ t·ay·ax+s·az   t·ay²+c      t·ay·az - s·ax   ]
    //       [ t·az·ax - s·ay  t·az·ay+s·ax    t·az²+c      ]
    //
    //   With ay = 0, simplify each element:
    //     R[0][0] = t·ax²+c              R[0][1] = -s·az = N.x/sinA·sinA = N.x   R[0][2] = t·ax·az
    //     R[1][0] = s·az  = -N.x         R[1][1] =  c = N.y                       R[1][2] = -s·ax = -N.z
    //     R[2][0] = t·az·ax              R[2][1] =  s·ax = N.z                    R[2][2] = t·az²+c
    //
    //   Observation: column 1 of R = (-s·az, c, s·ax) = (N.x, N.y, N.z) = N.
    //   Column 1 IS the terrain normal — already held in N, no multiply needed.
    //   s·az and s·ax also simplify to -N.x and N.z respectively (substituted directly).
    //
    //   Multiplying each direction column by 'scale' and setting col3 = (tx,ty,tz,1)
    //   gives the complete TRS matrix in memory layout:
    //     col0 = (t·ax²+c, -N.x,    t·ax·az) * scale
    //     col1 = (N.x,      N.y,    N.z    ) * scale   ← the terrain normal itself
    //     col2 = (t·ax·az, -N.z,   t·az²+c ) * scale
    //     col3 = (tx, ty, tz, 1)
    //
    // COST: 1 sqrtf + ~20 FP ops.  Old path: acosf + 2× (cosf+sinf) + 3× Matrix4 multiply.
    //
    // The Debug path below performs each step individually for debugger visibility;
    // it produces numerically identical results.
#ifdef _DEBUG
    // Debug: step-by-step — shows the mathematical composition being fused.
    // The acosf→degrees→RotateAxis round-trip recovers cosf/sinf from the angle we
    // derived from acosf — the exact waste the release path eliminates.
    Matrix4 model = Translate( tx, ty, tz );
    const float cosA = N.y;
    if ( cosA < 0.9999f )
    {
        float axisX = N.z;
        float axisZ = -N.x;
        float axisMag = sqrtf( axisX * axisX + axisZ * axisZ );
        axisX /= axisMag;
        axisZ /= axisMag;
        float angleDeg = acosf( cosA ) * ( 180.0f / 3.14159265f );
        model = model * RotateAxis( angleDeg, axisX, 0.0f, axisZ );
    }
    return model * Scale( scale );
#else
    // c = cosA = N·up = N.y (dot product of two unit vectors, one of which is (0,1,0))
    const float c = N.y;
    float res[16];
    if ( c >= 0.9999f )
    {
        // Terrain is within ~0.8° of flat — rotation is effectively identity.
        // R = I, so the TRS result is just a uniform scale placed at (tx,ty,tz).
        res[0] = scale;
        res[1] = 0.0f;
        res[2] = 0.0f;
        res[3] = 0.0f;
        res[4] = 0.0f;
        res[5] = scale;
        res[6] = 0.0f;
        res[7] = 0.0f;
        res[8] = 0.0f;
        res[9] = 0.0f;
        res[10] = scale;
        res[11] = 0.0f;
    }
    else
    {
        // sinA = |axis| = sqrt(N.x² + N.z²).  With |N|=1 and c=N.y: sinA = sqrt(1-c²).
        const float sinA = sqrtf( N.x * N.x + N.z * N.z );
        const float t = 1.0f - c; // Rodrigues (1-cos) factor
        // Normalised axis components: ax = N.z/sinA,  az = -N.x/sinA
        const float ax = N.z / sinA;
        const float az = -N.x / sinA;
        // Rodrigues diagonal and off-diagonal terms (factored to avoid repeating ax²,az²,ax·az)
        const float tax2 = t * ax * ax; // t·ax²     → contributes to R[0][0]
        const float taz2 = t * az * az; // t·az²     → contributes to R[2][2]
        const float taxz = t * ax * az; // t·ax·az   → off-diagonal shared by R[0][2] and R[2][0]

        // Column-major assignment  (res[col*4 + row]):
        // col0 = (t·ax²+c,  -N.x,  t·ax·az, 0) * scale
        res[0] = ( tax2 + c ) * scale; // R[0][0] = t·ax²+c
        res[1] = -N.x * scale;         // R[1][0] = s·az  = -(N.x/sinA)·sinA = -N.x
        res[2] = taxz * scale;         // R[2][0] = t·ax·az
        res[3] = 0.0f;
        // col1 = N * scale  (the terrain normal IS the rotated-up axis — see derivation above)
        res[4] = N.x * scale; // R[0][1] = -s·az = N.x
        res[5] = N.y * scale; // R[1][1] = c     = N.y
        res[6] = N.z * scale; // R[2][1] = s·ax  = (N.z/sinA)·sinA = N.z
        res[7] = 0.0f;
        // col2 = (t·ax·az,  -N.z,  t·az²+c, 0) * scale
        res[8] = taxz * scale;          // R[0][2] = t·ax·az
        res[9] = -N.z * scale;          // R[1][2] = -s·ax = -N.z
        res[10] = ( taz2 + c ) * scale; // R[2][2] = t·az²+c
        res[11] = 0.0f;
    }
    // col3: translation (homogeneous row = 1)
    res[12] = tx;
    res[13] = ty;
    res[14] = tz;
    res[15] = 1.0f;
    return Matrix4( res );
#endif
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

    const __m128 lhsC0 = _mm_loadu_ps( m + 0 );  // LHS column 0: m[0..3]
    const __m128 lhsC1 = _mm_loadu_ps( m + 4 );  // LHS column 1: m[4..7]
    const __m128 lhsC2 = _mm_loadu_ps( m + 8 );  // LHS column 2: m[8..11]
    const __m128 lhsC3 = _mm_loadu_ps( m + 12 ); // LHS column 3: m[12..15]

    float r[16];
    for ( int c = 0; c < 4; ++c )
    {
        // Broadcast each RHS scalar for output column c, scale the matching LHS column,
        // accumulate four contributions with two paired adds (avoids a 4-way add chain).
        _mm_storeu_ps( r + c * 4,
                       _mm_add_ps( _mm_add_ps( _mm_mul_ps( lhsC0, _mm_set1_ps( rhs.m[c * 4 + 0] ) ),
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

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
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
