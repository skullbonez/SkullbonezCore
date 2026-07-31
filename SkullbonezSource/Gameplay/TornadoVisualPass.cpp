/*
File: SkullbonezSource/Gameplay/TornadoVisualPass.cpp
Purpose:
  Builds and submits tornado production-art ribbons and dust.

Summary:
  The pass resolves one presentation clock, builds active gameplay vortices,
  expands their fixed visual recipe into transient vertices, and executes from
  the graph-owned world-extension callback.

Glossary:
  Dust band: Ground-following quad ring encoded with terrain-height attributes.
  Callback payload: Stack record borrowed by RenderGraph for dry-run and live
    execution before the registration scope returns.

Invariants:
  - Vertex generation retains the established float layout and authored order.
  - Live time advances only outside replay selection and while live advance is
    not held.
  - The callback retains no graph, frame, surface, or backend borrow.

Related:
  - SkullbonezSource/Gameplay/TornadoVisualPass.h
  - SkullbonezData/shaders/transient_colored_triangles.hlsl
  - Agentic/Reference/engine-glossary.md
*/
#include "TornadoGameplay.h"

#include "../Core/FatalError.h"
#include "../Rendering/RenderCommandTypes.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "../Rendering/DX12/Dx12Diagnostics.h"
#include "../Rendering/RenderGpuTimingOwner.h"
#include "../Rendering/RenderRasterBindingContract.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace SkullbonezCore::Gameplay
{
namespace
{
using Math::Vector::CrossProduct;
using Math::Vector::Vector3;
using Math::Vector::VectorMagSquared;

constexpr int RENDER_TEXTURE_SLOT_COUNT = Rendering::TEXTURE_SLOT_COUNT;
constexpr float FX_KIND_RIBBON = 0.0f;
constexpr float FX_KIND_DUST = 1.0f;
constexpr Rendering::PassRasterStateBucket
    VISUAL_RASTER = Rendering::MakePassRasterStateBucket( 0, { true, false, true, Rendering::BlendFactor::SrcAlpha,
                                                               Rendering::BlendFactor::OneMinusSrcAlpha,
                                                               Rendering::CullMode::None } );

float Clamp01( float value )
{
    return std::clamp( value, 0.0f, 1.0f );
}

float HashUnitFloat( uint32_t value )
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>( value & 0x00ffffffu ) / static_cast<float>( 0x01000000u );
}

Vector3 NormalizeOr( Vector3 value, const Vector3& fallback )
{

    if ( VectorMagSquared( value ) <= 1.0e-8f )
    {
        return fallback;
    }

    value.Normalise();
    return value;
}

Vector3 CylindricalOffset( float radius, float angle )
{
    return Vector3( cosf( angle ) * radius, 0.0f, sinf( angle ) * radius );
}

void EmitFxVertex( std::vector<float>& vertices, const Vector3& position, float r, float g, float b, float a, float u,
                   float v, float fxKind, float terrainY )
{
    vertices.push_back( position.x );
    vertices.push_back( position.y );
    vertices.push_back( position.z );
    vertices.push_back( r );
    vertices.push_back( g );
    vertices.push_back( b );
    vertices.push_back( a );
    vertices.push_back( u );
    vertices.push_back( v );
    vertices.push_back( fxKind );
    vertices.push_back( terrainY );
}

void ClearAllRenderTextureSlots( Rendering::Dx12TextureOwner& renderTextures )
{

    for ( int slot = 0; slot < RENDER_TEXTURE_SLOT_COUNT; ++slot )
    {
        renderTextures.BindTexture( 0, slot );
    }
}
} // namespace

const TornadoVisualSettings& TornadoGameplay::VisualSettings() const
{
    return m_visualPass.Settings();
}

void TornadoGameplay::SetVisualSettings( const TornadoVisualSettings& settings )
{
    m_visualPass.SetSettings( settings );
}

bool TornadoGameplay::VisualAutoEnableWithTornado() const
{
    return m_visualPass.AutoEnableWithTornado();
}

void TornadoGameplay::ToggleVisualEnabled()
{
    m_visualPass.SetEnabled( !m_visualPass.Settings().enabled );
}

void TornadoGameplay::SetVisualEnabled( bool enabled )
{
    m_visualPass.SetEnabled( enabled );
}

Rendering::WorldRenderExtensionRegistration TornadoGameplay::PrepareVisualFrame( const TornadoVisualTimeCandidates& time )
{
    return m_visualPass.PrepareFrame( m_field.GetConfig(), m_system.GetConfig(), m_system.GetElapsedSeconds(), time );
}

void TornadoGameplay::ReleaseVisualResources()
{
    m_visualPass.ReleaseResources();
}

const TornadoVisualSettings& TornadoVisualPass::Settings() const
{
    return m_settings;
}

void TornadoVisualPass::SetSettings( const TornadoVisualSettings& settings )
{
    m_settings = settings;
}

void TornadoVisualPass::SetEnabled( bool enabled )
{
    m_settings.enabled = enabled;
}

bool TornadoVisualPass::AutoEnableWithTornado() const
{
    return m_settings.autoEnableWithTornado;
}

Rendering::WorldRenderExtensionRegistration TornadoVisualPass::PrepareFrame( const TornadoFieldConfig& field,
                                                                             const TornadoSystemConfig& system,
                                                                             float systemElapsedSeconds,
                                                                             const TornadoVisualTimeCandidates& time )
{
    m_frame.field = &field;
    m_frame.system = &system;
    m_frame.time = time;
    m_frame.systemElapsedSeconds = systemElapsedSeconds;
    EnsureTransientCapacity();
    return Rendering::WorldRenderExtensionRegistration::Bind<TornadoVisualPass, &TornadoVisualPass::RegisterGraphPass>( *this );
}

void TornadoVisualPass::ReleaseResources()
{
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_activeVisualVortices.clear();
    m_activeVisualVortices.shrink_to_fit();
    m_liveVisualTimeSeconds = 0.0f;
    m_lastLiveVisualSourceSeconds = 0.0;
    m_hasLiveVisualTime = false;
    m_frame = {};
}

bool TornadoVisualPass::RegisterGraphPass( TornadoVisualPass& pass, Rendering::WorldRenderExtensionScope& scope )
{
    GraphCallbackData callbackData;
    callbackData.pass = &pass;
    callbackData.frame = &scope.Frame();
    scope.AppendGraphicsPass<&TornadoVisualPass::ExecuteGraphPass>( "TornadoVisualPass", callbackData,
                                                                    "Frame/Render/TornadoVisual" );
    const bool rendered = callbackData.rendered;

    // Lifetime: no frame/configuration borrow survives the synchronous graph
    // range. Persistent visual clock and owned capacity remain valid.
    pass.m_frame = {};

    return rendered;
}

void TornadoVisualPass::ExecuteGraphPass( const Rendering::RenderGraphPassContext& /*context*/, GraphCallbackData& data )
{

    if ( !data.pass || !data.frame )
    {
        SB_FATAL( "Gameplay/TornadoVisualPass", "Graph callback missing frame execution data." );
    }

    data.rendered = data.pass->Render( *data.frame );
}

void TornadoVisualPass::EnsureTransientCapacity()
{
    assert( m_frame.system && "TornadoVisualPass requires a system snapshot" );
    const int ribbonCount = std::clamp( m_settings.ribbonCount, 0, 16 );
    const int ribbonSegments = std::clamp( m_settings.ribbonSegments, 2, 96 );
    const int particleCount = std::clamp( m_settings.particleCount, 0, 256 );
    constexpr int dustBands = 3;
    constexpr int dustSegments = 56;
    const int authoredVortexCount = m_frame.system->enabled
                                        ? (std::max)( 1, static_cast<int>( m_frame.system->vortices.size() ) )
                                        : 1;

    const int vertexCount = authoredVortexCount *
                            ( ribbonCount * ribbonSegments * 6 + dustBands * dustSegments * 6 + particleCount * 6 );

    const std::size_t floatCapacity = static_cast<std::size_t>( (std::max)( vertexCount, 0 ) ) * VISUAL_FLOATS_PER_VERTEX;

    if ( floatCapacity > m_vertices.capacity() )
    {
        SB_FATAL( "Gameplay/TornadoVisualPass", "Transient vertex capacity exceeded. requested=%zu capacity=%zu",
                  floatCapacity, m_vertices.capacity() );
    }
}

bool TornadoVisualPass::Render( const Rendering::WorldRenderExtensionFrameView& frame )
{
    assert( m_frame.field && m_frame.system && "TornadoVisualPass requires prepared gameplay state" );
    const TornadoVisualSettings& visual = m_settings;

    if ( !visual.enabled )
    {
        return false;
    }

    const int ribbonCount = std::clamp( visual.ribbonCount, 0, 16 );
    const int ribbonSegments = std::clamp( visual.ribbonSegments, 2, 96 );
    const int particleCount = std::clamp( visual.particleCount, 0, 256 );
    const float shellAlpha = std::clamp( visual.shellAlpha, 0.0f, 0.30f );
    const float dustAlpha = std::clamp( visual.dustAlpha, 0.0f, 0.30f );

    if ( ( ribbonCount <= 0 || shellAlpha <= 0.0f ) && dustAlpha <= 0.0f )
    {
        return false;
    }

    const TornadoVisualTimeCandidates& candidates = m_frame.time;
    const bool useReplayTime = candidates.hasPresentation || candidates.hasSolver || candidates.hasPrediction;
    const bool useTornadoSystem = m_frame.system->enabled && !m_frame.system->vortices.empty();
    const double sourceSeconds = candidates.simulationSourceSeconds;

    if ( !m_hasLiveVisualTime || sourceSeconds < m_lastLiveVisualSourceSeconds )
    {
        m_liveVisualTimeSeconds = static_cast<float>( sourceSeconds );
        m_hasLiveVisualTime = true;
    }
    else if ( !useReplayTime && !candidates.liveAdvanceHeld )
    {
        m_liveVisualTimeSeconds += static_cast<float>( sourceSeconds - m_lastLiveVisualSourceSeconds );
    }

    m_lastLiveVisualSourceSeconds = sourceSeconds;

    float time = m_liveVisualTimeSeconds;

    if ( candidates.hasPresentation )
    {
        time = static_cast<float>( candidates.presentationSeconds );
    }
    else if ( candidates.hasSolver )
    {
        time = useTornadoSystem ? candidates.solverSystemSeconds : static_cast<float>( candidates.solverSeconds );
    }
    else if ( candidates.hasPrediction )
    {
        time = useTornadoSystem ? candidates.predictionSystemSeconds : static_cast<float>( candidates.predictionSeconds );
    }
    else if ( useTornadoSystem )
    {
        time = m_frame.systemElapsedSeconds;
    }

    m_activeVisualVortices.clear();

    if ( useTornadoSystem )
    {
        TornadoSystem::BuildActiveVortices( *m_frame.system, time, m_activeVisualVortices );
    }
    else if ( m_frame.field->enabled && m_frame.field->radius > 1.0f && m_frame.field->height > 1.0f )
    {
        TornadoActiveVortex active;
        active.field = *m_frame.field;
        active.strength = 1.0f;
        active.ageSeconds = time;
        active.sourceIndex = 0;
        m_activeVisualVortices.push_back( active );
    }

    if ( m_activeVisualVortices.empty() )
    {
        return false;
    }

    m_vertices.clear();
    const Vector3 cameraForward = NormalizeOr( frame.viewCenter - frame.eye, Vector3( 0.0f, 0.0f, 1.0f ) );
    const Vector3 cameraUp = NormalizeOr( frame.up, Vector3( 0.0f, 1.0f, 0.0f ) );
    const Vector3 cameraRight = NormalizeOr( CrossProduct( cameraForward, cameraUp ), Vector3( 1.0f, 0.0f, 0.0f ) );
    const Vector3 billboardUp = NormalizeOr( CrossProduct( cameraRight, cameraForward ), cameraUp );
    const auto terrainHeightFor = [&]( const Vector3& position )
    { return frame.surfaceHeight.SampleHeight( position.x, position.z, position.y - 64.0f ); };

    constexpr float twoPi = 6.28318530718f;

    for ( const TornadoActiveVortex& activeVortex : m_activeVisualVortices )
    {
        const TornadoFieldConfig& field = activeVortex.field;
        const float rotation = time * visual.rotationSpeed + static_cast<float>( activeVortex.sourceIndex ) * 1.73f;
        const float radius = field.radius;
        const float height = field.height;

        if ( shellAlpha > 0.0f )
        {
            constexpr float shellTurns = 2.85f;

            for ( int ribbon = 0; ribbon < ribbonCount; ++ribbon )
            {
                const float ribbonSeed = HashUnitFloat( 41u + static_cast<uint32_t>( ribbon ) * 97u );
                const float phase = static_cast<float>( ribbon ) * twoPi / static_cast<float>( ribbonCount ) + rotation +
                                    ribbonSeed * 0.45f;

                for ( int segment = 0; segment < ribbonSegments; ++segment )
                {
                    const float t0 = static_cast<float>( segment ) / static_cast<float>( ribbonSegments );
                    const float t1 = static_cast<float>( segment + 1 ) / static_cast<float>( ribbonSegments );
                    const float angle0 = phase + t0 * shellTurns * twoPi;
                    const float angle1 = phase + t1 * shellTurns * twoPi;
                    const float radius0 = radius *
                                          ( 0.32f + 0.46f * t0 + 0.035f * sinf( angle0 * 1.7f + ribbonSeed * twoPi ) );

                    const float radius1 = radius *
                                          ( 0.32f + 0.46f * t1 + 0.035f * sinf( angle1 * 1.7f + ribbonSeed * twoPi ) );

                    const Vector3 p0 = field.center + CylindricalOffset( radius0, angle0 ) +
                                       Vector3( 0.0f, t0 * height, 0.0f );

                    const Vector3 p1 = field.center + CylindricalOffset( radius1, angle1 ) +
                                       Vector3( 0.0f, t1 * height, 0.0f );

                    const Vector3 segmentCenter = ( p0 + p1 ) * 0.5f;
                    const Vector3 viewDir = NormalizeOr( frame.eye - segmentCenter, -cameraForward );
                    const Vector3 tangent = NormalizeOr( p1 - p0, Vector3( 0.0f, 1.0f, 0.0f ) );
                    const Vector3 side = NormalizeOr( CrossProduct( viewDir, tangent ), cameraRight );
                    const float width = (std::max)( 1.0f, visual.ribbonWidth * ( 0.78f + 0.34f * t0 ) );
                    const float baseFade = Clamp01( t0 / 0.16f );
                    const float topFade = Clamp01( ( 1.0f - t0 ) / 0.18f );
                    const float gapFade = 0.72f + 0.28f * sinf( phase + t0 * twoPi * 4.0f );
                    const float alpha = shellAlpha * baseFade * topFade * gapFade;
                    const float cool = 0.72f + 0.08f * t0;
                    const Vector3 a = p0 - side * width;
                    const Vector3 b = p1 - side * width;
                    const Vector3 c = p1 + side * width;
                    const Vector3 d = p0 + side * width;

                    // Invariant: every effect quad is emitted as two clockwise
                    // triangles with identical style at all six vertices.
                    EmitFxVertex( m_vertices, a, cool, 0.78f, 0.84f, alpha, 0.0f, 0.0f, FX_KIND_RIBBON, 0.0f );
                    EmitFxVertex( m_vertices, b, cool, 0.78f, 0.84f, alpha, 1.0f, 0.0f, FX_KIND_RIBBON, 0.0f );
                    EmitFxVertex( m_vertices, c, cool, 0.78f, 0.84f, alpha, 1.0f, 1.0f, FX_KIND_RIBBON, 0.0f );
                    EmitFxVertex( m_vertices, a, cool, 0.78f, 0.84f, alpha, 0.0f, 0.0f, FX_KIND_RIBBON, 0.0f );
                    EmitFxVertex( m_vertices, c, cool, 0.78f, 0.84f, alpha, 1.0f, 1.0f, FX_KIND_RIBBON, 0.0f );
                    EmitFxVertex( m_vertices, d, cool, 0.78f, 0.84f, alpha, 0.0f, 1.0f, FX_KIND_RIBBON, 0.0f );
                }
            }
        }

        if ( dustAlpha > 0.0f )
        {
            constexpr int dustBands = 3;
            constexpr int dustSegments = 56;

            for ( int band = 0; band < dustBands; ++band )
            {
                const float bandT = static_cast<float>( band ) / static_cast<float>( dustBands - 1 );
                const float phase = rotation * ( 1.15f + bandT * 0.35f ) + bandT * twoPi * 0.37f;

                for ( int segment = 0; segment < dustSegments; ++segment )
                {

                    if ( ( segment + band * 3 ) % 5 == 0 || ( segment + band ) % 11 == 0 )
                    {
                        continue;
                    }

                    const float t0 = static_cast<float>( segment ) / static_cast<float>( dustSegments );
                    const float t1 = static_cast<float>( segment + 1 ) / static_cast<float>( dustSegments );
                    const float angle0 = phase + t0 * twoPi * 1.18f;
                    const float angle1 = phase + t1 * twoPi * 1.18f;
                    const float bandRadius = radius * ( 0.58f + 0.16f * static_cast<float>( band ) );
                    const float innerRadius = bandRadius - radius * 0.015f;
                    const float outerRadius = bandRadius + radius * ( 0.024f + 0.008f * bandT );
                    const float y0 = field.center.y + height * ( 0.018f + 0.030f * bandT ) + sinf( angle0 * 2.0f ) * 1.6f;

                    const float y1 = field.center.y + height * ( 0.018f + 0.030f * bandT ) + sinf( angle1 * 2.0f ) * 1.6f;

                    const Vector3 a = field.center + CylindricalOffset( innerRadius, angle0 ) +
                                      Vector3( 0.0f, y0 - field.center.y, 0.0f );

                    const Vector3 b = field.center + CylindricalOffset( innerRadius, angle1 ) +
                                      Vector3( 0.0f, y1 - field.center.y, 0.0f );

                    const Vector3 c = field.center + CylindricalOffset( outerRadius, angle1 ) +
                                      Vector3( 0.0f, y1 - field.center.y, 0.0f );

                    const Vector3 d = field.center + CylindricalOffset( outerRadius, angle0 ) +
                                      Vector3( 0.0f, y0 - field.center.y, 0.0f );

                    const float alpha = dustAlpha * ( 0.42f - 0.08f * bandT );
                    const float terrainA = terrainHeightFor( a );
                    const float terrainB = terrainHeightFor( b );
                    const float terrainC = terrainHeightFor( c );
                    const float terrainD = terrainHeightFor( d );
                    EmitFxVertex( m_vertices, a, 0.58f, 0.47f, 0.31f, alpha, 0.0f, 0.0f, FX_KIND_DUST, terrainA );
                    EmitFxVertex( m_vertices, b, 0.58f, 0.47f, 0.31f, alpha, 1.0f, 0.0f, FX_KIND_DUST, terrainB );
                    EmitFxVertex( m_vertices, c, 0.58f, 0.47f, 0.31f, alpha, 1.0f, 1.0f, FX_KIND_DUST, terrainC );
                    EmitFxVertex( m_vertices, a, 0.58f, 0.47f, 0.31f, alpha, 0.0f, 0.0f, FX_KIND_DUST, terrainA );
                    EmitFxVertex( m_vertices, c, 0.58f, 0.47f, 0.31f, alpha, 1.0f, 1.0f, FX_KIND_DUST, terrainC );
                    EmitFxVertex( m_vertices, d, 0.58f, 0.47f, 0.31f, alpha, 0.0f, 1.0f, FX_KIND_DUST, terrainD );
                }
            }

            for ( int particle = 0; particle < particleCount; ++particle )
            {
                const uint32_t seed = 0x9e3779b9u + static_cast<uint32_t>( particle ) * 0x85ebca6bu;
                const float h0 = HashUnitFloat( seed );
                const float h1 = HashUnitFloat( seed ^ 0x68bc21ebu );
                const float h2 = HashUnitFloat( seed ^ 0x02e5be93u );
                const float heightT = powf( h0, 1.45f );
                const float angularSpeed = 0.65f + heightT * 1.25f;
                const float angle = h1 * twoPi + rotation * angularSpeed + heightT * twoPi * 2.2f;
                const float particleRadius = radius * ( 0.55f + 0.43f * h2 );
                const Vector3 center = field.center + CylindricalOffset( particleRadius, angle ) +
                                       Vector3( 0.0f, height * heightT, 0.0f );

                const float size = std::clamp( radius * ( 0.010f + 0.020f * ( 1.0f - heightT ) ), 2.0f, 9.0f );
                const float alpha = dustAlpha * ( 0.38f + 0.42f * ( 1.0f - heightT ) ) * ( 0.55f + 0.45f * h1 );
                const Vector3 right = cameraRight * size;
                const Vector3 up = billboardUp * ( size * ( 0.70f + 0.50f * h2 ) );
                const Vector3 a = center - right - up;
                const Vector3 b = center + right - up;
                const Vector3 c = center + right + up;
                const Vector3 d = center - right + up;
                const float terrainA = terrainHeightFor( a );
                const float terrainB = terrainHeightFor( b );
                const float terrainC = terrainHeightFor( c );
                const float terrainD = terrainHeightFor( d );
                EmitFxVertex( m_vertices, a, 0.68f, 0.52f, 0.34f, alpha, 0.0f, 0.0f, FX_KIND_DUST, terrainA );
                EmitFxVertex( m_vertices, b, 0.68f, 0.52f, 0.34f, alpha, 1.0f, 0.0f, FX_KIND_DUST, terrainB );
                EmitFxVertex( m_vertices, c, 0.68f, 0.52f, 0.34f, alpha, 1.0f, 1.0f, FX_KIND_DUST, terrainC );
                EmitFxVertex( m_vertices, a, 0.68f, 0.52f, 0.34f, alpha, 0.0f, 0.0f, FX_KIND_DUST, terrainA );
                EmitFxVertex( m_vertices, c, 0.68f, 0.52f, 0.34f, alpha, 1.0f, 1.0f, FX_KIND_DUST, terrainC );
                EmitFxVertex( m_vertices, d, 0.68f, 0.52f, 0.34f, alpha, 0.0f, 1.0f, FX_KIND_DUST, terrainD );
            }
        }
    }

    if ( m_vertices.empty() )
    {
        return false;
    }

    PROFILE_GPU_BEGIN( &frame.renderGpuTiming, "Frame/Render/TornadoVisual" );
    DRAW_CALL_TRACE_SCOPE( frame.renderDiagnostics, "Frame/Render/TornadoVisual" );
    ClearAllRenderTextureSlots( frame.renderTextures );
    frame.renderGeometry.DrawTransientColoredTriangles( m_vertices, frame.viewProjection,
                                                        Rendering::TransientTriangleStyle::Color, VISUAL_RASTER );

    PROFILE_GPU_END( &frame.renderGpuTiming, "Frame/Render/TornadoVisual" );
    return true;
}
} // namespace SkullbonezCore::Gameplay
