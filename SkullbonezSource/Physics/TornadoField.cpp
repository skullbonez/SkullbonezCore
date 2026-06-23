/*
File: SkullbonezSource/Physics/TornadoField.cpp
Purpose:
  Computes a procedural tornado force field for generated physics scenes.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/TornadoField.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "TornadoField.h"
#include "../Core/Common.h"
#include "../Rendering/IRenderBackend.h"
#include "../Core/Profiler.h"
#include <algorithm>
#include <cmath>


using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Rendering::Gfx;
namespace Vector = SkullbonezCore::Math::Vector;


static float SmoothStep01( float edge0, float edge1, float value )
{
    if ( fabsf( edge1 - edge0 ) <= TOLERANCE )
    {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    float t = std::clamp( ( value - edge0 ) / ( edge1 - edge0 ), 0.0f, 1.0f );
    return t * t * ( 3.0f - 2.0f * t );
}


TornadoField::TornadoField()
{
    m_lineData.reserve( 12 * 4 * 5 * 6 * 6 );
}


void TornadoField::SetConfig( const TornadoFieldConfig& config )
{
    m_config = config;
    m_config.radius = (std::max)( 1.0f, m_config.radius );
    m_config.height = (std::max)( 1.0f, m_config.height );
    m_config.ejectAcceleration = (std::max)( 0.0f, m_config.ejectAcceleration );
    m_config.ejectUpAcceleration = (std::max)( 0.0f, m_config.ejectUpAcceleration );
    m_config.ejectBand = std::clamp( m_config.ejectBand, 0.0f, 1.0f );
    m_config.minCaptureSeconds = (std::max)( 0.0f, m_config.minCaptureSeconds );
    m_config.ejectCooldownSeconds = (std::max)( 0.0f, m_config.ejectCooldownSeconds );
    m_config.maxDeltaVelocity = (std::max)( 1.0f, m_config.maxDeltaVelocity );
}


Vector3 TornadoField::SampleAcceleration( const Vector3& position ) const
{
    const float radius = (std::max)( m_config.radius, 1.0f );
    const float height = (std::max)( m_config.height, 1.0f );
    const float dx = position.x - m_config.center.x;
    const float dz = position.z - m_config.center.z;
    const float horizontalSq = dx * dx + dz * dz;
    const float horizontal = sqrtf( horizontalSq );
    const float radial01 = horizontal / radius;
    const float height01 = ( position.y - m_config.center.y ) / height;

    if ( radial01 > 1.0f || height01 < -0.10f || height01 > 1.05f )
    {
        return ZERO_VECTOR;
    }

    Vector3 inward =
        horizontal > TOLERANCE ? Vector3( -dx / horizontal, 0.0f, -dz / horizontal ) : Vector3( 1.0f, 0.0f, 0.0f );
    Vector3 tangent( -inward.z, 0.0f, inward.x );

    const float radialMask = 0.18f + 0.82f * SmoothStep01( 1.0f, 0.0f, radial01 );
    const float columnMask = SmoothStep01( 0.0f, 0.12f, height01 ) * SmoothStep01( 1.0f, 0.78f, height01 );
    const float swirlMask = columnMask * ( 0.45f + 0.55f * radialMask );

    return inward * ( m_config.inwardAcceleration * radialMask ) +
           tangent * ( m_config.swirlAcceleration * swirlMask ) +
           Vector3( 0.0f, m_config.liftAcceleration * columnMask, 0.0f );
}


void TornadoField::RenderVectors( const Matrix4& viewProj )
{
    if ( !m_config.visualizeVelocityField || !Gfx().GetCapabilities().supportsDebugLines )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Render/TornadoFieldVectors" );
    m_lineData.clear();

    auto emit = [&]( const Vector3& a, const Vector3& b, float r, float g, float bColor )
    {
        m_lineData.push_back( a.x );
        m_lineData.push_back( a.y );
        m_lineData.push_back( a.z );
        m_lineData.push_back( r );
        m_lineData.push_back( g );
        m_lineData.push_back( bColor );
        m_lineData.push_back( b.x );
        m_lineData.push_back( b.y );
        m_lineData.push_back( b.z );
        m_lineData.push_back( r );
        m_lineData.push_back( g );
        m_lineData.push_back( bColor );
    };

    constexpr int ANGLE_STEPS = 12;
    constexpr int RADIUS_STEPS = 4;
    constexpr int HEIGHT_STEPS = 5;
    constexpr float PI = 3.1415926535f;
    const float maxFieldSpeed = (std::max)( 1.0f,
                                            sqrtf( m_config.inwardAcceleration * m_config.inwardAcceleration +
                                                   m_config.swirlAcceleration * m_config.swirlAcceleration +
                                                   m_config.liftAcceleration * m_config.liftAcceleration ) );

    for ( int h = 0; h < HEIGHT_STEPS; ++h )
    {
        const float height01 = 0.12f + static_cast<float>( h ) * ( 0.78f / static_cast<float>( HEIGHT_STEPS - 1 ) );
        const float y = m_config.center.y + m_config.height * height01;
        for ( int rIndex = 0; rIndex < RADIUS_STEPS; ++rIndex )
        {
            const float radial01 =
                0.22f + static_cast<float>( rIndex ) * ( 0.72f / static_cast<float>( RADIUS_STEPS - 1 ) );
            const float radius = m_config.radius * radial01;
            for ( int aIndex = 0; aIndex < ANGLE_STEPS; ++aIndex )
            {
                const float angle = ( static_cast<float>( aIndex ) / static_cast<float>( ANGLE_STEPS ) ) * PI * 2.0f;
                Vector3 start( m_config.center.x + cosf( angle ) * radius,
                               y,
                               m_config.center.z + sinf( angle ) * radius );
                Vector3 field = SampleAcceleration( start );
                const float speed = Vector::VectorMag( field );
                if ( speed <= TOLERANCE )
                {
                    continue;
                }

                const float t = std::clamp( speed / maxFieldSpeed, 0.0f, 1.0f );
                const float red = t;
                const float green = 1.0f - t;
                const float arrowLength = 9.0f + 23.0f * t;
                Vector3 dir = field / speed;
                Vector3 end = start + dir * arrowLength;
                emit( start, end, red, green, 0.0f );

                Vector3 side( -dir.z, 0.0f, dir.x );
                const float sideMag = Vector::VectorMag( side );
                if ( sideMag > TOLERANCE )
                {
                    side /= sideMag;
                }
                else
                {
                    side = Vector3( 1.0f, 0.0f, 0.0f );
                }
                Vector3 headBase = end - dir * 4.4f;
                emit( end, headBase + side * 2.4f, red, green, 0.0f );
                emit( end, headBase - side * 2.4f, red, green, 0.0f );
            }
        }
    }

    if ( !m_lineData.empty() )
    {
        const int vertCount = static_cast<int>( m_lineData.size() / 6 );
        Gfx().DrawLinesColored( m_lineData.data(), vertCount, viewProj.Data() );
    }
}
