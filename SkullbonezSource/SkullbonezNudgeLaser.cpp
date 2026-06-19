/*
File: SkullbonezSource/SkullbonezNudgeLaser.cpp
Purpose:
  Draws nudge-mode laser shots as short-lived camera-facing ribbons.

Mental model:
  Each shot is a fixed world-space segment. Rendering billboards a wide outer
  ribbon and a narrow hot core toward the current camera so the feedback stays
  visible even when fired straight out of the crosshair.

Related:
  - SkullbonezSource/SkullbonezNudgeLaser.h
  - SkullbonezSource/SkullbonezRunPasses.cpp
*/
#include "SkullbonezNudgeLaser.h"

#include "SkullbonezAssetSystem.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezIShader.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Rendering;

namespace
{
constexpr float LASER_LIFETIME_SECONDS = 0.20f;
constexpr float LASER_START_LEAD = 2.0f;
constexpr float LASER_START_DOWN_OFFSET = 0.55f;
constexpr float LASER_OUTER_HALF_WIDTH = 0.34f;
constexpr float LASER_CORE_HALF_WIDTH = 0.095f;
constexpr float LASER_IMPACT_HALF_SIZE = 1.15f;
constexpr float LASER_MIN_SEGMENT_LENGTH = 0.25f;

Vector3 NormalizeOr( const Vector3& value, const Vector3& fallback )
{
    const float lenSq = VectorMagSquared( value );
    if ( lenSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }
    return value * ( 1.0f / sqrtf( lenSq ) );
}
} // namespace

NudgeLaser::NudgeLaser()
{
    m_vertices.reserve( static_cast<std::size_t>( MAX_VERTICES ) * 7u );
}

NudgeLaser::~NudgeLaser()
{
    ResetResources();
}

void NudgeLaser::ResetResources()
{
    if ( IsGfxReady() && m_dynamicVB != 0 )
    {
        Gfx().DestroyDynamicVB( m_dynamicVB );
    }

    m_dynamicVB = 0;
    m_shader.reset();
}

void NudgeLaser::Clear()
{
    m_shots = {};
    m_nextShot = 0;
}

void NudgeLaser::EnsureResources()
{
    if ( !IsGfxReady() )
    {
        return;
    }

    if ( !m_shader )
    {
        m_shader = SkullbonezCore::Assets::CreateShaderFromActiveAssets( "shader.nudge_laser" );
    }

    if ( m_dynamicVB == 0 )
    {
        const int attribs[] = { 3, 4 };
        m_dynamicVB = Gfx().CreateDynamicVB( attribs, 2, MAX_VERTICES );
    }
}

void NudgeLaser::Fire( const Vector3& rayOrigin,
                       const Vector3& rayDirection,
                       const Vector3& cameraUp,
                       float distance,
                       bool hit )
{
    const Vector3 forward = NormalizeOr( rayDirection, Vector3( 0.0f, 0.0f, 1.0f ) );
    const Vector3 up = NormalizeOr( cameraUp, Vector3( 0.0f, 1.0f, 0.0f ) );
    const float clampedDistance = (std::max)( distance, LASER_START_LEAD + LASER_MIN_SEGMENT_LENGTH );
    const float startLead = (std::min)( LASER_START_LEAD, clampedDistance * 0.35f );

    Shot& shot = m_shots[static_cast<std::size_t>( m_nextShot ) % MAX_SHOTS];
    shot.start = rayOrigin + forward * startLead - up * LASER_START_DOWN_OFFSET;
    shot.end = rayOrigin + forward * clampedDistance;
    shot.ageSeconds = 0.0f;
    shot.lifetimeSeconds = LASER_LIFETIME_SECONDS;
    shot.active = true;
    shot.hit = hit;
    m_nextShot = ( m_nextShot + 1 ) % static_cast<int>( MAX_SHOTS );
}

void NudgeLaser::Update( float dt )
{
    if ( dt <= 0.0f )
    {
        return;
    }

    for ( Shot& shot : m_shots )
    {
        if ( !shot.active )
        {
            continue;
        }
        shot.ageSeconds += dt;
        if ( shot.ageSeconds >= shot.lifetimeSeconds )
        {
            shot.active = false;
        }
    }
}

void NudgeLaser::EmitVertex( const Vector3& p, float r, float g, float b, float a )
{
    m_vertices.insert( m_vertices.end(), { p.x, p.y, p.z, r, g, b, a } );
}

void NudgeLaser::EmitQuad( const Vector3& a,
                           const Vector3& b,
                           const Vector3& c,
                           const Vector3& d,
                           float r,
                           float g,
                           float bl,
                           float alpha )
{
    EmitVertex( a, r, g, bl, alpha );
    EmitVertex( b, r, g, bl, alpha );
    EmitVertex( c, r, g, bl, alpha );
    EmitVertex( a, r, g, bl, alpha );
    EmitVertex( c, r, g, bl, alpha );
    EmitVertex( d, r, g, bl, alpha );
}

void NudgeLaser::EmitRibbon( const Vector3& a,
                             const Vector3& b,
                             const Vector3& widthAxis,
                             float halfWidth,
                             float r,
                             float g,
                             float bl,
                             float alpha )
{
    const Vector3 w = widthAxis * halfWidth;
    EmitQuad( a - w, b - w, b + w, a + w, r, g, bl, alpha );
}

void NudgeLaser::EmitShot( const Shot& shot,
                           const Vector3& cameraEye,
                           const Vector3& cameraUp )
{
    if ( !shot.active || shot.lifetimeSeconds <= TOLERANCE )
    {
        return;
    }

    const Vector3 segment = shot.end - shot.start;
    const float segmentLenSq = VectorMagSquared( segment );
    if ( segmentLenSq <= LASER_MIN_SEGMENT_LENGTH * LASER_MIN_SEGMENT_LENGTH )
    {
        return;
    }

    const float fade = std::clamp( 1.0f - shot.ageSeconds / shot.lifetimeSeconds, 0.0f, 1.0f );
    if ( fade <= 0.0f )
    {
        return;
    }

    const Vector3 dir = segment * ( 1.0f / sqrtf( segmentLenSq ) );
    const Vector3 midpoint = ( shot.start + shot.end ) * 0.5f;
    Vector3 cameraVector = NormalizeOr( cameraEye - midpoint, NormalizeOr( cameraUp, Vector3( 0.0f, 1.0f, 0.0f ) ) );
    Vector3 widthAxis = CrossProduct( dir, cameraVector );
    if ( VectorMagSquared( widthAxis ) <= TOLERANCE * TOLERANCE )
    {
        widthAxis = CrossProduct( dir, NormalizeOr( cameraUp, Vector3( 0.0f, 1.0f, 0.0f ) ) );
    }
    widthAxis = NormalizeOr( widthAxis, Vector3( 1.0f, 0.0f, 0.0f ) );

    EmitRibbon( shot.start, shot.end, widthAxis, LASER_OUTER_HALF_WIDTH, 0.08f, 0.88f, 1.0f, 0.23f * fade );
    EmitRibbon( shot.start, shot.end, widthAxis, LASER_CORE_HALF_WIDTH, 0.98f, 0.94f, 0.30f, 0.88f * fade );

    if ( shot.hit )
    {
        const Vector3 flareUp = NormalizeOr( CrossProduct( widthAxis, dir ), NormalizeOr( cameraUp, Vector3( 0.0f, 1.0f, 0.0f ) ) );
        EmitRibbon( shot.end - widthAxis * LASER_IMPACT_HALF_SIZE,
                    shot.end + widthAxis * LASER_IMPACT_HALF_SIZE,
                    flareUp,
                    LASER_CORE_HALF_WIDTH * 1.25f,
                    1.0f,
                    0.46f,
                    0.12f,
                    0.86f * fade );
        EmitRibbon( shot.end - flareUp * LASER_IMPACT_HALF_SIZE,
                    shot.end + flareUp * LASER_IMPACT_HALF_SIZE,
                    widthAxis,
                    LASER_CORE_HALF_WIDTH * 1.25f,
                    1.0f,
                    0.84f,
                    0.22f,
                    0.78f * fade );
    }
}

void NudgeLaser::Render( const Matrix4& viewProjection,
                         const Vector3& cameraEye,
                         const Vector3& cameraUp )
{
    if ( !IsGfxReady() )
    {
        return;
    }

    m_vertices.clear();
    for ( const Shot& shot : m_shots )
    {
        EmitShot( shot, cameraEye, cameraUp );
    }
    if ( m_vertices.empty() )
    {
        return;
    }

    EnsureResources();
    if ( !m_shader || m_dynamicVB == 0 )
    {
        return;
    }

    const bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    const bool depthWriteWasEnabled = Gfx().IsDepthWriteEnabled();
    const bool blendWasEnabled = Gfx().IsBlendEnabled();
    BlendFactor blendSrc = BlendFactor::One;
    BlendFactor blendDst = BlendFactor::Zero;
    Gfx().GetBlendFunc( blendSrc, blendDst );

    Gfx().SetDepthTest( false );
    Gfx().SetDepthWrite( false );
    Gfx().SetBlend( true );
    Gfx().SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );

    m_shader->Use();
    m_shader->SetMat4( "uViewProj", viewProjection );
    Gfx().UploadAndDrawDynamicVB( m_dynamicVB, m_vertices.data(), static_cast<int>( m_vertices.size() / 7 ) );

    Gfx().SetBlendFunc( blendSrc, blendDst );
    Gfx().SetBlend( blendWasEnabled );
    Gfx().SetDepthWrite( depthWriteWasEnabled );
    Gfx().SetDepthTest( depthWasEnabled );
}
