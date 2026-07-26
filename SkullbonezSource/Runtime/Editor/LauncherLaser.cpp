/*
File: SkullbonezSource/Runtime/Editor/LauncherLaser.cpp
Purpose:
  Draws launcher-mode laser shots as short-lived camera-facing ribbons.

Summary:
  Each shot is a fixed world-space segment. Rendering billboards a wide outer
  ribbon and a narrow hot core toward the current camera so the feedback stays
  visible even when fired straight out of the crosshair.

Glossary:
  Billboard: Camera-facing quad built from a world-space segment and view
    direction.
  Ribbon: Thin quad strip used to render one laser streak.
  Afterimage: Fading visual trail that remains briefly after the shot.
  Resource builder: Cold renderer owner borrowed only while compiling the
    laser shader.
  Geometry owner: Renderer owner borrowed while creating or destroying the
    laser vertex buffer.
  Render command context: Per-frame renderer capability borrowed only while
    drawing laser vertices and temporarily changing draw state.
  Shader handle: Runtime id that resolves to a renderer-owned shader resource.

Invariants:
  - Laser shots are visual feedback only; physics impulses happen elsewhere.
  - Expired shots must stop drawing without changing launcher hit history.

Related:
  - SkullbonezSource/Runtime/Editor/LauncherLaser.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
*/
#include "LauncherLaser.h"

#include "../../Assets/AssetSystem.h"
#include "../../Rendering/RenderCommandTypes.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/DX12/ShaderDX12.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Rendering;

namespace
{
constexpr float LASER_LIFETIME_SECONDS = 0.34f;
constexpr float LASER_EMITTER_LEAD = 3.0f;
constexpr float LASER_EMITTER_DOWN_OFFSET = 1.25f;
constexpr float LASER_AFTERIMAGE_HALF_WIDTH = 0.62f;
constexpr float LASER_OUTER_HALF_WIDTH = 0.40f;
constexpr float LASER_CORE_HALF_WIDTH = 0.12f;
constexpr float LASER_IMPACT_HALF_SIZE = 1.45f;
constexpr float LASER_IMPACT_DISC_HALF_SIZE = 0.68f;
constexpr float LASER_MIN_SEGMENT_LENGTH = 0.25f;
constexpr PassRasterStateBucket LASER_RASTER_BUCKET = { { 0 },
                                                        { false,
                                                          false,
                                                          true,
                                                          BlendFactor::SrcAlpha,
                                                          BlendFactor::One,
                                                          CullMode::None,
                                                          { false, 0.0f, 0.0f },
                                                          { RenderTargetFormatExpectation::ActivePass,
                                                            RenderTargetFormatExpectation::ActivePass, 1 } } };

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

LauncherLaser::LauncherLaser()
{
    m_vertices.reserve( static_cast<std::size_t>( MAX_VERTICES ) * 7u );
}

LauncherLaser::~LauncherLaser()
{

    // Lifetime: backend-owned handles are explicitly released by Run while the
    // renderer is live. Destruction may happen after backend teardown, so it
    // only clears CPU-owned state.
    ResetResources( nullptr );
}

void LauncherLaser::ResetResources( Rendering::Dx12GeometryOwner* renderGeometry )
{

    if ( renderGeometry && m_dynamicVB != 0 )
    {
        renderGeometry->DestroyDynamicVB( m_dynamicVB );
    }

    m_dynamicVB = 0;
    m_shader.reset();
    m_rasterStatePrepared = false;
}

void LauncherLaser::Clear()
{
    m_shots = {};
    m_nextShot = 0;
}

void LauncherLaser::EnsureResources( Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& renderResources,
                                     Rendering::Dx12GeometryOwner& renderGeometry )
{

    if ( !m_shader )
    {
        m_shader = assets.CreateShader( renderResources, "shader.launcher_laser" );
    }

    if ( m_dynamicVB == 0 )
    {
        const int attribs[] = { 3, 4 };
        m_dynamicVB = renderGeometry.CreateDynamicVB( attribs, 2, MAX_VERTICES );
    }
}

void LauncherLaser::Fire( const Vector3& rayOrigin, const Vector3& rayDirection, const Vector3& cameraUp, float distance,
                          bool hit )
{
    const Vector3 forward = NormalizeOr( rayDirection, Vector3( 0.0f, 0.0f, 1.0f ) );
    const Vector3 up = NormalizeOr( cameraUp, Vector3( 0.0f, 1.0f, 0.0f ) );
    Vector3 right = CrossProduct( forward, up );

    if ( VectorMagSquared( right ) <= TOLERANCE * TOLERANCE )
    {
        right = CrossProduct( forward, Vector3( 0.0f, 0.0f, 1.0f ) );
    }

    right = NormalizeOr( right, Vector3( 1.0f, 0.0f, 0.0f ) );
    const Vector3 stableUp = NormalizeOr( CrossProduct( right, forward ), up );
    const float visualDistance = (std::max)( distance, LASER_MIN_SEGMENT_LENGTH );
    const float startLead = (std::min)( LASER_EMITTER_LEAD, visualDistance * 0.35f );
    const float downOffset = (std::min)( LASER_EMITTER_DOWN_OFFSET, startLead * 0.55f );

    Shot& shot = m_shots[static_cast<std::size_t>( m_nextShot ) % MAX_SHOTS];
    shot.start = rayOrigin + forward * startLead - up * downOffset;
    shot.end = rayOrigin + forward * visualDistance;
    shot.cameraRight = right;
    shot.cameraUp = stableUp;
    shot.ageSeconds = 0.0f;
    shot.lifetimeSeconds = LASER_LIFETIME_SECONDS;
    shot.active = true;
    shot.hit = hit;
    m_nextShot = ( m_nextShot + 1 ) % static_cast<int>( MAX_SHOTS );
}

void LauncherLaser::Update( float dt )
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


bool LauncherLaser::HasActiveShots() const
{

    for ( const Shot& shot : m_shots )
    {

        if ( shot.active )
        {
            return true;
        }
    }

    return false;
}


void LauncherLaser::CaptureShots( std::vector<LauncherLaserShotSnapshot>& outShots, int& outNextShot ) const
{
    outShots.clear();
    outShots.reserve( MAX_SHOTS );

    for ( const Shot& shot : m_shots )
    {
        LauncherLaserShotSnapshot snapshot;
        snapshot.start = shot.start;
        snapshot.end = shot.end;
        snapshot.cameraRight = shot.cameraRight;
        snapshot.cameraUp = shot.cameraUp;
        snapshot.ageSeconds = shot.ageSeconds;
        snapshot.lifetimeSeconds = shot.lifetimeSeconds;
        snapshot.active = shot.active;
        snapshot.hit = shot.hit;
        outShots.push_back( snapshot );
    }

    outNextShot = m_nextShot;
}


void LauncherLaser::RestoreShots( const std::vector<LauncherLaserShotSnapshot>& shots, int nextShot )
{
    m_shots = {};
    const std::size_t copyCount = (std::min)( shots.size(), MAX_SHOTS );

    for ( std::size_t i = 0; i < copyCount; ++i )
    {
        Shot& shot = m_shots[i];
        shot.start = shots[i].start;
        shot.end = shots[i].end;
        shot.cameraRight = shots[i].cameraRight;
        shot.cameraUp = shots[i].cameraUp;
        shot.ageSeconds = shots[i].ageSeconds;
        shot.lifetimeSeconds = shots[i].lifetimeSeconds;
        shot.active = shots[i].active;
        shot.hit = shots[i].hit;
    }

    m_nextShot = nextShot % static_cast<int>( MAX_SHOTS );

    if ( m_nextShot < 0 )
    {
        m_nextShot += static_cast<int>( MAX_SHOTS );
    }
}


void LauncherLaser::EmitVertex( const Vector3& p, float r, float g, float b, float a )
{
    m_vertices.insert( m_vertices.end(), { p.x, p.y, p.z, r, g, b, a } );
}

void LauncherLaser::EmitQuad( const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, float r, float g,
                              float bl, float alpha )
{
    EmitVertex( a, r, g, bl, alpha );
    EmitVertex( b, r, g, bl, alpha );
    EmitVertex( c, r, g, bl, alpha );
    EmitVertex( a, r, g, bl, alpha );
    EmitVertex( c, r, g, bl, alpha );
    EmitVertex( d, r, g, bl, alpha );
}

void LauncherLaser::EmitRibbon( const Vector3& a, const Vector3& b, const Vector3& widthAxis, float halfWidth, float r,
                                float g, float bl, float alpha )
{
    const Vector3 w = widthAxis * halfWidth;
    EmitQuad( a - w, b - w, b + w, a + w, r, g, bl, alpha );
}

void LauncherLaser::EmitBillboardQuad( const Vector3& center, const Vector3& right, const Vector3& up, float halfWidth,
                                       float halfHeight, float r, float g, float bl, float alpha )
{
    const Vector3 x = right * halfWidth;
    const Vector3 y = up * halfHeight;
    EmitQuad( center - x - y, center + x - y, center + x + y, center - x + y, r, g, bl, alpha );
}

void LauncherLaser::EmitShot( const Shot& shot )
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

    const float normalizedAge = std::clamp( shot.ageSeconds / shot.lifetimeSeconds, 0.0f, 1.0f );
    const float afterimageFade = std::sqrt( 1.0f - normalizedAge );
    const float coreFade = ( 1.0f - normalizedAge ) * ( 1.0f - normalizedAge );

    if ( afterimageFade <= 0.0f )
    {
        return;
    }

    const Vector3 dir = segment * ( 1.0f / sqrtf( segmentLenSq ) );
    Vector3 screenRight = NormalizeOr( shot.cameraRight, CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) ) );

    if ( VectorMagSquared( screenRight ) <= TOLERANCE * TOLERANCE )
    {
        screenRight = CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) );
    }

    screenRight = NormalizeOr( screenRight, Vector3( 1.0f, 0.0f, 0.0f ) );
    const Vector3 screenUp = NormalizeOr( shot.cameraUp,
                                          NormalizeOr( CrossProduct( screenRight, dir ), Vector3( 0.0f, 1.0f, 0.0f ) ) );

    EmitRibbon( shot.start, shot.end, screenRight, LASER_AFTERIMAGE_HALF_WIDTH, 0.02f, 0.45f, 1.0f, 0.12f * afterimageFade );

    EmitRibbon( shot.start, shot.end, screenUp, LASER_AFTERIMAGE_HALF_WIDTH * 0.55f, 0.06f, 0.82f, 1.0f,
                0.08f * afterimageFade );

    EmitRibbon( shot.start, shot.end, screenRight, LASER_OUTER_HALF_WIDTH, 0.05f, 0.96f, 1.0f, 0.30f * afterimageFade );
    EmitRibbon( shot.start, shot.end, screenUp, LASER_OUTER_HALF_WIDTH * 0.42f, 0.22f, 0.98f, 1.0f, 0.22f * afterimageFade );

    EmitRibbon( shot.start, shot.end, screenRight, LASER_CORE_HALF_WIDTH, 1.0f, 0.95f, 0.28f, 0.98f * coreFade );
    EmitRibbon( shot.start, shot.end, screenUp, LASER_CORE_HALF_WIDTH * 0.72f, 1.0f, 0.58f, 0.16f, 0.82f * coreFade );

    if ( shot.hit )
    {
        EmitBillboardQuad( shot.end, screenRight, screenUp, LASER_IMPACT_DISC_HALF_SIZE, LASER_IMPACT_DISC_HALF_SIZE, 1.0f,
                           0.72f, 0.18f, 0.58f * afterimageFade );

        EmitRibbon( shot.end - screenRight * LASER_IMPACT_HALF_SIZE, shot.end + screenRight * LASER_IMPACT_HALF_SIZE,
                    screenUp, LASER_CORE_HALF_WIDTH * 1.5f, 1.0f, 0.46f, 0.12f, 0.90f * coreFade );

        EmitRibbon( shot.end - screenUp * LASER_IMPACT_HALF_SIZE, shot.end + screenUp * LASER_IMPACT_HALF_SIZE, screenRight,
                    LASER_CORE_HALF_WIDTH * 1.5f, 1.0f, 0.84f, 0.22f, 0.82f * coreFade );
    }
}

void LauncherLaser::Render( const Matrix4& viewProjection, const Vector3& cameraEye, const Vector3& cameraUp,
                            Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& renderResources,
                            Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12GeometryOwner& renderCommands )
{
    static_cast<void>( cameraEye );
    static_cast<void>( cameraUp );

    m_vertices.clear();

    for ( const Shot& shot : m_shots )
    {
        EmitShot( shot );
    }

    if ( m_vertices.empty() )
    {
        return;
    }

    EnsureResources( assets, renderResources, renderGeometry );

    if ( !m_shader || m_dynamicVB == 0 )
    {
        return;
    }

    m_shader->Use();

    if ( !m_rasterStatePrepared )
    {

        // Why: compile the additive overlay recipe before the first submission
        // instead of discovering a new PSO from setter history inside the draw.
        m_rasterStatePrepared = renderCommands.PrecompileDynamicVBRasterState( m_dynamicVB, LASER_RASTER_BUCKET );
    }

    if ( !m_rasterStatePrepared )
    {
        return;
    }

    m_shader->SetMat4( "uViewProj", viewProjection );
    renderCommands.UploadAndDrawDynamicVB( m_dynamicVB, m_vertices, LASER_RASTER_BUCKET );
}
