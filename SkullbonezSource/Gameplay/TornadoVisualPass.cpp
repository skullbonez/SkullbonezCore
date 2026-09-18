// Builds a tapered condensation column and ground-following dust from bounded
// camera-facing cloud quads. Their clock comes from the selected presentation
// state; the visual recipe never mutates the force field or simulation.
#include "TornadoGameplay.h"

#include "../Core/FatalError.h"
#include "../Rendering/RenderCommandTypes.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "../Rendering/DX12/Dx12Diagnostics.h"
#include "../Rendering/RenderGpuTimingOwner.h"
#include "../Rendering/RenderRasterBindingContract.h"

#include <algorithm>
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
constexpr float FX_KIND_CLOUD = 2.0f;
constexpr float FX_KIND_DUST = 1.0f;
constexpr Rendering::PassRasterStateBucket
    VISUAL_RASTER = Rendering::MakePassRasterStateBucket( 0, { true, false, true, Rendering::BlendFactor::SrcAlpha, Rendering::BlendFactor::OneMinusSrcAlpha, Rendering::CullMode::None } );

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

struct FxColor
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
};

struct FxUv
{
    float u = 0.0f;
    float v = 0.0f;
};

struct FxVertex
{
    Vector3 position;
    FxColor color;
    FxUv uv;
    float kind = 0.0f;
    float terrainHeight = 0.0f;

    void AppendTo( std::vector<float>& vertices ) const
    {
        // Invariant: this method is the single serialization order shared with
        // the 11-float tornado shader input layout.
        vertices.push_back( position.x );
        vertices.push_back( position.y );
        vertices.push_back( position.z );
        vertices.push_back( color.red );
        vertices.push_back( color.green );
        vertices.push_back( color.blue );
        vertices.push_back( color.alpha );
        vertices.push_back( uv.u );
        vertices.push_back( uv.v );
        vertices.push_back( kind );
        vertices.push_back( terrainHeight );
    }
};

void EmitFxVertex( std::vector<float>& vertices, const FxVertex& vertex )
{
    vertex.AppendTo( vertices );
}

// Kind fractions carry a stable per-puff noise seed; the integer selects the
// generic cloud/dust shader. All six vertices share that seed and ground height.
void EmitCloudQuad( std::vector<float>& vertices, const Vector3& center, const Vector3& right, const Vector3& up, const FxColor& color, float kind, float groundHeight )
{
    const Vector3 a = center - right - up;
    const Vector3 b = center + right - up;
    const Vector3 c = center + right + up;
    const Vector3 d = center - right + up;
    EmitFxVertex( vertices, { a, color, { 0, 0 }, kind, groundHeight } );
    EmitFxVertex( vertices, { b, color, { 1, 0 }, kind, groundHeight } );
    EmitFxVertex( vertices, { c, color, { 1, 1 }, kind, groundHeight } );
    EmitFxVertex( vertices, { a, color, { 0, 0 }, kind, groundHeight } );
    EmitFxVertex( vertices, { c, color, { 1, 1 }, kind, groundHeight } );
    EmitFxVertex( vertices, { d, color, { 0, 1 }, kind, groundHeight } );
}

Vector3 FunnelAxis( const Vector3& base, float radius, float height, float t, float phase )
{
    // The foot stays anchored while the upper column leans and wanders slowly.
    return base + Vector3( radius * t * ( .07f * sinf( phase * .17f + t * 4 ) + .12f * t ), height * t, radius * t * ( .08f * cosf( phase * .13f + t * 3 ) - .05f * t ) );
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

TornadoVisualSnapshot TornadoGameplay::VisualSnapshot() const
{
    return m_visualPass.Snapshot();
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

TornadoVisualSnapshot TornadoVisualPass::Snapshot() const
{
    return m_snapshot;
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

Rendering::WorldRenderExtensionRegistration
TornadoVisualPass::PrepareFrame( const TornadoFieldConfig& field, const TornadoSystemConfig& system, double systemElapsedSeconds, const TornadoVisualTimeCandidates& time )
{
    m_frame.field = &field;
    m_frame.system = &system;
    m_frame.time = time;
    m_frame.systemElapsedSeconds = systemElapsedSeconds;
    EnsureTransientCapacity();
    return Rendering::WorldRenderExtensionRegistration::Bind<TornadoVisualPass, &TornadoVisualPass::RegisterGraphPass>( *this );
}

bool TornadoVisualPass::RegisterGraphPass( TornadoVisualPass& pass, Rendering::WorldRenderExtensionScope& scope )
{
    GraphCallbackData callbackData;
    callbackData.pass = &pass;
    callbackData.frame = &scope.Frame();
    scope.AppendGraphicsPass<&TornadoVisualPass::ExecuteGraphPass>( "TornadoVisualPass", callbackData, "Frame/Render/TornadoVisual" );
    const bool rendered = callbackData.rendered;

    // Lifetime: no frame/configuration borrow survives the synchronous graph
    // range. Persistent visual clock and owned capacity remain valid.
    pass.m_frame = {};

    return rendered;
}

float TornadoVisualPass::ResolveRotationPhase( double time, float rotationSpeed, int sourceIndex )
{
    constexpr float twoPi = 6.28318530718f;
    constexpr double floatFixedStepBoundary = 262144.0;
    const float compatibleTime = static_cast<float>( time );

    if ( std::fabs( time ) <= floatFixedStepBoundary )
    {
        return compatibleTime * rotationSpeed + static_cast<float>( sourceIndex ) * 1.73f;
    }

    // Hazard: a large absolute phase loses fixed-step changes when narrowed to
    // float. Reduce the precise phase before conversion so visual drift advances.
    return static_cast<float>( std::fmod( time * static_cast<double>( rotationSpeed ) + static_cast<double>( sourceIndex ) * 1.73, static_cast<double>( twoPi ) ) );
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
    RequirePreparedFrame( "EnsureTransientCapacity" );
    const int ribbonCount = std::clamp( m_settings.ribbonCount, 0, 16 );
    const int ribbonSegments = std::clamp( m_settings.ribbonSegments, 2, 96 );
    const int particleCount = std::clamp( m_settings.particleCount, 0, 256 );
    constexpr int dustBands = 3;
    constexpr int dustSegments = 56;
    const int authoredVortexCount = m_frame.system->enabled ? (std::max)( 1, static_cast<int>( m_frame.system->vortices.size() ) ) : 1;

    const int vertexCount = authoredVortexCount * ( ribbonCount * ribbonSegments * 6 + dustBands * dustSegments * 6 + particleCount * 6 );

    const std::size_t floatCapacity = static_cast<std::size_t>( (std::max)( vertexCount, 0 ) ) * VISUAL_FLOATS_PER_VERTEX;

    if ( floatCapacity > m_vertices.capacity() )
    {
        SB_FATAL( "Gameplay/TornadoVisualPass", "Transient vertex capacity exceeded. requested=%zu capacity=%zu", floatCapacity, m_vertices.capacity() );
    }
}

double TornadoVisualPass::ResolveVisualTime()
{
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

    double time = m_liveVisualTimeSeconds;

    if ( candidates.hasPresentation )
    {
        time = candidates.presentationSeconds;
    }
    else if ( candidates.hasSolver )
    {
        time = useTornadoSystem ? candidates.solverSystemSeconds : candidates.solverSeconds;
    }
    else if ( candidates.hasPrediction )
    {
        time = useTornadoSystem ? candidates.predictionSystemSeconds : candidates.predictionSeconds;
    }
    else if ( useTornadoSystem )
    {
        time = m_frame.systemElapsedSeconds;
    }

    // Compatibility: all ordinary-time visuals retain their prior float input
    // bytes. Only the long-runtime range that can lose fixed steps stays double.
    if ( std::fabs( time ) < 262144.0 )
    {
        time = static_cast<double>( static_cast<float>( time ) );
    }

    return time;
}

void TornadoVisualPass::BuildActiveVisualVortices( double time )
{
    m_activeVisualVortices.clear();

    if ( m_frame.system->enabled && !m_frame.system->vortices.empty() )
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
}

void TornadoVisualPass::AppendFunnelGeometry( const TornadoActiveVortex& activeVortex,
                                              double time,
                                              const Rendering::WorldRenderExtensionFrameView& frame,
                                              int ribbonCount,
                                              int ribbonSegments,
                                              float shellAlpha )
{
    const TornadoFieldConfig& field = activeVortex.field;
    const float phase = ResolveRotationPhase( time, m_settings.rotationSpeed, activeVortex.sourceIndex );
    const float ground = frame.surfaceHeight.SampleHeight( field.center.x, field.center.z, field.center.y );
    const Vector3 base( field.center.x, (std::max)( ground, field.center.y ), field.center.z );
    const float height = (std::max)( 1.0f, field.center.y + field.height - base.y );
    const Vector3 forward = NormalizeOr( frame.viewCenter - frame.eye, Vector3( 0, 0, 1 ) );
    const Vector3 right = NormalizeOr( CrossProduct( forward, frame.up ), Vector3( 1, 0, 0 ) );
    const Vector3 up = NormalizeOr( CrossProduct( right, forward ), Vector3( 0, 1, 0 ) );
    const float rise = static_cast<float>( std::fmod( time * std::fabs( m_settings.rotationSpeed ) * .024, 1.0 ) );
    const float widthScale = std::clamp( m_settings.ribbonWidth / 5.5f, .35f, 2.0f );
    constexpr float twoPi = 6.28318530718f;

    // Authored ribbon counts now bound cloud streams. The same six-vertex
    // budget per sample is retained, including maximum-capacity configurations.
    for ( int stream = 0; stream < ribbonCount; ++stream )
    {
        for ( int sample = 0; sample < ribbonSegments; ++sample )
        {
            const uint32_t seed = 41u + static_cast<uint32_t>( stream ) * 977u + static_cast<uint32_t>( sample ) * 131u;
            const float h0 = HashUnitFloat( seed );
            const float h1 = HashUnitFloat( seed + 17u );
            const float h2 = HashUnitFloat( seed + 47u );
            float t = ( static_cast<float>( sample ) + h0 ) / ribbonSegments + rise;
            t -= std::floor( t );
            const float angle = twoPi * ( static_cast<float>( stream ) / (std::max)( ribbonCount, 1 ) + t * 1.3f ) + phase * ( .85f + .12f * h1 ) + h0 * .65f;
            const float columnRadius = field.radius * ( .055f + .34f * t * t + .09f * std::pow( t, 6.0f ) );
            const float orbit = columnRadius * ( .40f + .35f * h1 );
            const Vector3 center = FunnelAxis( base, field.radius, height, t, phase ) + CylindricalOffset( orbit, angle );
            const float size = (std::max)( .5f, columnRadius * ( .58f + .20f * h2 ) * widthScale );
            const float fade = Clamp01( t / .045f ) * Clamp01( ( 1.0f - t ) / .16f );
            const float alpha = shellAlpha * 2.2f * fade * activeVortex.strength;
            const float shade = .80f + .20f * h2;
            const FxColor color { ( .23f + .06f * t ) * shade, ( .245f + .065f * t ) * shade, ( .25f + .075f * t ) * shade, alpha };
            EmitCloudQuad( m_vertices, center, right * size, up * ( size * ( .85f + .22f * h0 ) ), color, FX_KIND_CLOUD + h1 * .49f, ground );
        }
    }
}

void TornadoVisualPass::AppendDustGeometry( const TornadoActiveVortex& activeVortex, double time, const Rendering::WorldRenderExtensionFrameView& frame, int particleCount, float dustAlpha )
{
    const TornadoFieldConfig& field = activeVortex.field;
    const float phase = ResolveRotationPhase( time, m_settings.rotationSpeed, activeVortex.sourceIndex );
    const Vector3 forward = NormalizeOr( frame.viewCenter - frame.eye, Vector3( 0, 0, 1 ) );
    const Vector3 right = NormalizeOr( CrossProduct( forward, frame.up ), Vector3( 1, 0, 0 ) );
    const Vector3 up = NormalizeOr( CrossProduct( right, forward ), Vector3( 0, 1, 0 ) );
    constexpr float twoPi = 6.28318530718f;
    constexpr int dustBands = 3;
    constexpr int dustSegments = 56;

    // Broad overlapping billows replace the flat rotating rings. Each foot
    // samples terrain so its lower half disappears into the surface naturally.
    for ( int band = 0; band < dustBands; ++band )
    {
        const float bandT = static_cast<float>( band ) / ( dustBands - 1 );
        for ( int sample = 0; sample < dustSegments; ++sample )
        {
            const uint32_t seed = 193u + static_cast<uint32_t>( band ) * 811u + static_cast<uint32_t>( sample ) * 71u;
            const float h0 = HashUnitFloat( seed );
            const float h1 = HashUnitFloat( seed + 31u );
            const float angle = twoPi * ( sample + h0 ) / dustSegments + phase * ( 1.0f + .23f * bandT );
            const float orbit = field.radius * ( .10f + .5f * bandT ) * ( .75f + .25f * h1 );
            Vector3 center = field.center + CylindricalOffset( orbit, angle );
            const float ground = frame.surfaceHeight.SampleHeight( center.x, center.z, field.center.y );
            const float size = (std::max)( .5f, field.radius * ( .08f + .07f * ( 1 - bandT ) ) * ( .8f + .4f * h0 ) );
            center.y = ground + size * ( .4f + .35f * h1 );
            const float alpha = dustAlpha * ( .90f - .50f * bandT ) * activeVortex.strength;
            const FxColor color { .29f, .255f, .205f, alpha };
            EmitCloudQuad( m_vertices, center, right * size, up * ( size * .8f ), color, FX_KIND_DUST + h1 * .49f, ground );
        }
    }

    const float rise = static_cast<float>( std::fmod( time * std::fabs( m_settings.rotationSpeed ) * .045, 1.0 ) );
    for ( int particle = 0; particle < particleCount; ++particle )
    {
        const uint32_t seed = 0x9e3779b9u + static_cast<uint32_t>( particle ) * 0x85ebca6bu;
        const float h0 = HashUnitFloat( seed );
        const float h1 = HashUnitFloat( seed ^ 0x68bc21ebu );
        const float h2 = HashUnitFloat( seed ^ 0x02e5be93u );
        float life = h0 + rise;
        life -= std::floor( life );
        const float t = life * life * .78f;
        const float angle = h1 * twoPi + phase * ( 1.0f + h2 * .45f ) + t * twoPi;
        const float orbit = field.radius * ( .12f + .30f * t + .12f * h2 );
        Vector3 center = field.center + CylindricalOffset( orbit, angle );
        const float ground = frame.surfaceHeight.SampleHeight( center.x, center.z, field.center.y );
        center.y = (std::max)( ground, field.center.y ) + field.height * t;
        const float size = (std::max)( .4f, field.radius * ( .025f + .045f * h2 ) );
        const float fade = Clamp01( life / .12f ) * Clamp01( ( 1 - life ) / .25f );
        const FxColor color { .32f, .285f, .235f, dustAlpha * .60f * fade * activeVortex.strength };
        EmitCloudQuad( m_vertices, center, right * size, up * ( size * 1.25f ), color, FX_KIND_DUST + h1 * .49f, ground );
    }
}

bool TornadoVisualPass::Render( const Rendering::WorldRenderExtensionFrameView& frame )
{
    RequirePreparedFrame( "Render" );
    m_snapshot = {};
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

    const double time = ResolveVisualTime();
    BuildActiveVisualVortices( time );
    m_snapshot.seconds = time;
    m_snapshot.activeVortices = static_cast<uint32_t>( m_activeVisualVortices.size() );

    if ( m_activeVisualVortices.empty() )
    {
        return false;
    }

    m_vertices.clear();

    for ( const TornadoActiveVortex& activeVortex : m_activeVisualVortices )
    {
        if ( shellAlpha > 0.0f )
        {
            AppendFunnelGeometry( activeVortex, time, frame, ribbonCount, ribbonSegments, shellAlpha );
        }

        if ( dustAlpha > 0.0f )
        {
            AppendDustGeometry( activeVortex, time, frame, particleCount, dustAlpha );
        }
    }

    if ( m_vertices.empty() )
    {
        return false;
    }

    PROFILE_GPU_BEGIN( &frame.renderGpuTiming, "Frame/Render/TornadoVisual" );
    DRAW_CALL_TRACE_SCOPE( frame.renderDiagnostics, "Frame/Render/TornadoVisual" );
    ClearAllRenderTextureSlots( frame.renderTextures );
    frame.renderGeometry.DrawTransientColoredTriangles( m_vertices, frame.viewProjection, Rendering::TransientTriangleStyle::Color, VISUAL_RASTER );

    m_snapshot.vertices = static_cast<uint32_t>( m_vertices.size() / VISUAL_FLOATS_PER_VERTEX );
    PROFILE_GPU_END( &frame.renderGpuTiming, "Frame/Render/TornadoVisual" );
    return true;
}
} // namespace SkullbonezCore::Gameplay
