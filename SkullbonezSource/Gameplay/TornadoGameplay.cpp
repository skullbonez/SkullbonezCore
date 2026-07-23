/*
File: SkullbonezSource/Gameplay/TornadoGameplay.cpp
Purpose:
  Advances tornado content and publishes bounded force and debug values.

Summary:
  The owner retains authored field/system configuration, procedural time,
  per-body exposure state, and preallocated debug-line scratch. Fixed ticks
  publish Physics-owned cylindrical values; render preparation publishes packed
  line vertices without transferring gameplay state ownership.

Glossary:
  Active vortex: Authored vortex after spawn, growth, shrink, drift, and
    repulsion have been evaluated for the current gameplay clock.
  Force frame: Synchronous spans borrowed by Physics for exactly one Step call.
  Debug line row: Two position/color vertices packed as xyz/rgb floats for one
    synchronous late-frame draw.

Invariants:
  - The system clock advances exactly once per BuildForceFrame call.
  - Field conversion retains active/source order and exact float values.
  - The fixed 64-field array never allocates during steady gameplay.
  - Debug buffers are preallocated for the fixed 64-vortex sampling ceiling.

Related:
  - SkullbonezSource/Gameplay/TornadoGameplay.h
  - SkullbonezSource/Physics/Stages/ExternalForceStage.cpp
*/
#include "TornadoGameplay.h"

#include "../Core/FatalError.h"
#include "../Core/SceneCapacity.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Gameplay
{
namespace
{
template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}
} // namespace

TornadoVisualPass::TornadoVisualPass()
{
    m_activeVisualVortices.reserve( MAX_TORNADO_ACTIVE_FORCE_FIELDS );
}

void TornadoVisualPass::ReserveCapacity()
{
    // Runtime allocation policy: the live SceneWorld pays the complete visual
    // maximum before steady gameplay. Replay-only TornadoGameplay owners do
    // not carry this 32 MiB presentation buffer.
    m_vertices.reserve( MAX_VISUAL_FLOAT_CAPACITY );
}

TornadoGameplay::TornadoGameplay()
{
    ReserveBodyCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_debugLineVertices.reserve( MAX_ACTIVE_FORCE_FIELDS * 12u * 4u * 5u * 6u * 6u );
    m_debugVortices.reserve( MAX_ACTIVE_FORCE_FIELDS );
}

void TornadoGameplay::SetParallelForceEvaluation( bool enabled )
{
    m_parallelForceEvaluation = enabled;
}

bool TornadoGameplay::ParallelForceEvaluation() const
{
    return m_parallelForceEvaluation;
}

void TornadoGameplay::ReserveBodyCapacity( int capacity )
{
    const std::size_t reserveCount = static_cast<std::size_t>( (std::max)( 0, capacity ) );
    m_captureSeconds.reserve( reserveCount );
    m_ejectCooldownSeconds.reserve( reserveCount );
}

void TornadoGameplay::ReserveVisualCapacity()
{
    m_visualPass.ReserveCapacity();
}

void TornadoGameplay::Clear()
{
    m_captureSeconds.clear();
    m_ejectCooldownSeconds.clear();
    m_forceFieldCount = 0u;
    m_debugLineVertices.clear();
    m_debugVortices.clear();
    m_field.SetConfig( TornadoFieldConfig{} );
    m_system.SetConfig( TornadoSystemConfig{} );
    m_system.ResetElapsedSeconds();
}

void TornadoGameplay::SetFieldConfig( const TornadoFieldConfig& config )
{
    m_field.SetConfig( config );
    if ( !m_field.GetConfig().enabled )
    {
        m_captureSeconds.clear();
        m_ejectCooldownSeconds.clear();
    }
}

const TornadoFieldConfig& TornadoGameplay::GetFieldConfig() const
{
    return m_field.GetConfig();
}

void TornadoGameplay::SetSystemConfig( const TornadoSystemConfig& config )
{
    if ( config.vortices.size() > MAX_ACTIVE_FORCE_FIELDS )
    {
        // Lane F: authored parsing must reject oversize content before owner
        // mutation. Reaching this typed command with too many rows means that
        // recoverable preflight was bypassed.
        SB_FATAL( "Gameplay/TornadoGameplay",
                  "External force field capacity exceeded. requested=%zu capacity=%zu",
                  config.vortices.size(),
                  MAX_ACTIVE_FORCE_FIELDS );
    }
    m_system.SetConfig( config );
    if ( !m_system.IsEnabled() && !m_field.GetConfig().enabled )
    {
        m_captureSeconds.clear();
        m_ejectCooldownSeconds.clear();
    }
}

const TornadoSystemConfig& TornadoGameplay::GetSystemConfig() const
{
    return m_system.GetConfig();
}

float TornadoGameplay::GetSystemElapsedSeconds() const
{
    return m_system.GetElapsedSeconds();
}

bool TornadoGameplay::ToggleEnabled()
{
    return m_system.HasAuthoredVortices() ? m_system.ToggleEnabled() : m_field.ToggleEnabled();
}

void TornadoGameplay::ToggleFieldVectors()
{
    if ( m_system.HasAuthoredVortices() )
    {
        m_system.ToggleVelocityFieldVisualization();
    }
    else
    {
        m_field.ToggleVelocityFieldVisualization();
    }
}

void TornadoGameplay::SetFieldRadius( float value )
{
    SetFieldValue( &TornadoFieldConfig::radius, value );
}

void TornadoGameplay::SetFieldHeight( float value )
{
    SetFieldValue( &TornadoFieldConfig::height, value );
}

void TornadoGameplay::SetFieldInwardAcceleration( float value )
{
    SetFieldValue( &TornadoFieldConfig::inwardAcceleration, value );
}

void TornadoGameplay::SetFieldSwirlAcceleration( float value )
{
    SetFieldValue( &TornadoFieldConfig::swirlAcceleration, value );
}

void TornadoGameplay::SetFieldLiftAcceleration( float value )
{
    SetFieldValue( &TornadoFieldConfig::liftAcceleration, value );
}

void TornadoGameplay::SetFieldValue( float TornadoFieldConfig::* field, float value )
{
    if ( m_system.HasAuthoredVortices() )
    {
        m_system.SetFieldValue( field, value );
    }
    else
    {
        m_field.SetFieldValue( field, value );
    }
}

void TornadoGameplay::SetReplayState( const std::vector<float>& captureSeconds,
                                      const std::vector<float>& ejectCooldownSeconds,
                                      const TornadoFieldConfig& fieldConfig,
                                      const TornadoSystemConfig& systemConfig,
                                      float systemElapsedSeconds )
{
    if ( captureSeconds.size() > m_captureSeconds.capacity() ||
         ejectCooldownSeconds.size() > m_ejectCooldownSeconds.capacity() )
    {
        SB_FATAL( "Gameplay/TornadoGameplay",
                  "Replay timer storage exceeded. capture=%zu/%zu cooldown=%zu/%zu",
                  captureSeconds.size(),
                  m_captureSeconds.capacity(),
                  ejectCooldownSeconds.size(),
                  m_ejectCooldownSeconds.capacity() );
    }
    m_field.SetConfig( fieldConfig );
    SetSystemConfig( systemConfig );
    m_system.SetElapsedSeconds( systemElapsedSeconds );
    // Invariant: SetSystemConfig clears live timers when every tornado source is
    // disabled. Replay restoration must apply its retained per-body timers after
    // that normalization so the recaptured solver snapshot remains byte-faithful.
    m_captureSeconds = captureSeconds;
    m_ejectCooldownSeconds = ejectCooldownSeconds;
}

const std::vector<float>& TornadoGameplay::CaptureSeconds() const
{
    return m_captureSeconds;
}

const std::vector<float>& TornadoGameplay::EjectCooldownSeconds() const
{
    return m_ejectCooldownSeconds;
}

void TornadoGameplay::CaptureReplayState( TornadoGameplayReplayState& outState ) const
{
    outState.field = m_field.GetConfig();
    outState.system = m_system.GetConfig();
    outState.systemElapsedSeconds = m_system.GetElapsedSeconds();
    outState.captureSeconds = m_captureSeconds;
    outState.ejectCooldownSeconds = m_ejectCooldownSeconds;
}

void TornadoGameplay::RestoreReplayState( const TornadoGameplayReplayState& state )
{
    SetReplayState( state.captureSeconds,
                    state.ejectCooldownSeconds,
                    state.field,
                    state.system,
                    state.systemElapsedSeconds );
}

std::span<const float> TornadoGameplay::BuildDebugLineVertices()
{
    m_debugLineVertices.clear();
    m_debugVortices.clear();
    const TornadoSystemConfig& system = m_system.GetConfig();
    if ( system.enabled && !system.vortices.empty() )
    {
        TornadoSystem::BuildActiveVortices( system, m_system.GetElapsedSeconds(), m_debugVortices );
    }
    else if ( m_field.GetConfig().enabled )
    {
        TornadoActiveVortex active;
        active.field = m_field.GetConfig();
        active.strength = 1.0f;
        active.ageSeconds = m_system.GetElapsedSeconds();
        active.sourceIndex = 0;
        m_debugVortices.push_back( active );
    }

    constexpr int ANGLE_STEPS = 12;
    constexpr int RADIUS_STEPS = 4;
    constexpr int HEIGHT_STEPS = 5;
    constexpr float PI = 3.1415926535f;
    for ( const TornadoActiveVortex& vortex : m_debugVortices )
    {
        const TornadoFieldConfig& fieldConfig = vortex.field;
        if ( !fieldConfig.visualizeVelocityField )
        {
            continue;
        }

        const float maxFieldSpeed = (std::max)( 1.0f,
                                                sqrtf( fieldConfig.inwardAcceleration * fieldConfig.inwardAcceleration +
                                                       fieldConfig.swirlAcceleration * fieldConfig.swirlAcceleration +
                                                       fieldConfig.liftAcceleration * fieldConfig.liftAcceleration ) );
        const auto emit =
            [&]( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float blue )
        {
            m_debugLineVertices.insert( m_debugLineVertices.end(),
                                        { a.x, a.y, a.z, r, g, blue, b.x, b.y, b.z, r, g, blue } );
        };

        for ( int h = 0; h < HEIGHT_STEPS; ++h )
        {
            const float height01 = 0.12f + static_cast<float>( h ) * ( 0.78f / static_cast<float>( HEIGHT_STEPS - 1 ) );
            const float y = fieldConfig.center.y + fieldConfig.height * height01;
            for ( int rIndex = 0; rIndex < RADIUS_STEPS; ++rIndex )
            {
                const float radial01 =
                    0.22f + static_cast<float>( rIndex ) * ( 0.72f / static_cast<float>( RADIUS_STEPS - 1 ) );
                const float radius = fieldConfig.radius * radial01;
                for ( int aIndex = 0; aIndex < ANGLE_STEPS; ++aIndex )
                {
                    const float angle =
                        ( static_cast<float>( aIndex ) / static_cast<float>( ANGLE_STEPS ) ) * PI * 2.0f;
                    Math::Vector::Vector3 start( fieldConfig.center.x + cosf( angle ) * radius,
                                                 y,
                                                 fieldConfig.center.z + sinf( angle ) * radius );
                    Math::Vector::Vector3 field = TornadoField::SampleAccelerationForConfig( fieldConfig, start );
                    const float speed = Math::Vector::VectorMag( field );
                    if ( speed <= TOLERANCE )
                    {
                        continue;
                    }

                    const float t = std::clamp( speed / maxFieldSpeed, 0.0f, 1.0f );
                    const float red = t;
                    const float green = 1.0f - t;
                    const float arrowLength = 9.0f + 23.0f * t;
                    Math::Vector::Vector3 dir = field / speed;
                    Math::Vector::Vector3 end = start + dir * arrowLength;
                    emit( start, end, red, green, 0.0f );

                    Math::Vector::Vector3 side( -dir.z, 0.0f, dir.x );
                    const float sideMag = Math::Vector::VectorMag( side );
                    if ( sideMag > TOLERANCE )
                    {
                        side /= sideMag;
                    }
                    else
                    {
                        side = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
                    }
                    const Math::Vector::Vector3 headBase = end - dir * 4.4f;
                    emit( end, headBase + side * 2.4f, red, green, 0.0f );
                    emit( end, headBase - side * 2.4f, red, green, 0.0f );
                }
            }
        }
    }
    return m_debugLineVertices;
}

Physics::ExternalForceFrameInput TornadoGameplay::BuildForceFrame( float dt, int bodyCount )
{
    EnsureStateBuffers( bodyCount );
    m_forceFieldCount = 0u;
    const float stepSeconds = (std::max)( 0.0f, dt );
    if ( m_system.IsEnabled() )
    {
        m_system.Tick( stepSeconds );
        for ( const TornadoActiveVortex& vortex : m_system.ActiveVortices() )
        {
            AppendForceField( vortex.field );
        }
    }
    else if ( m_field.GetConfig().enabled )
    {
        AppendForceField( m_field.GetConfig() );
    }

    return Physics::ExternalForceFrameInput{
        std::span<const Physics::ExternalCylindricalForceField>( m_forceFields.data(), m_forceFieldCount ),
        std::span<float>( m_captureSeconds.data(), m_captureSeconds.size() ),
        std::span<float>( m_ejectCooldownSeconds.data(), m_ejectCooldownSeconds.size() ),
        stepSeconds,
        m_parallelForceEvaluation };
}

uint64_t TornadoGameplay::CollectMemoryBytes() const
{
    return VectorCapacityBytes( m_captureSeconds ) + VectorCapacityBytes( m_ejectCooldownSeconds ) +
           static_cast<uint64_t>( m_field.DynamicMemoryBytes() ) +
           static_cast<uint64_t>( m_system.DynamicMemoryBytes() ) + m_visualPass.DynamicMemoryBytes() +
           VectorCapacityBytes( m_debugLineVertices ) + VectorCapacityBytes( m_debugVortices ) +
           sizeof( m_forceFields );
}

uint64_t TornadoGameplay::CollectDebugMemoryBytes() const
{
    return VectorCapacityBytes( m_debugLineVertices ) + VectorCapacityBytes( m_debugVortices );
}

void TornadoGameplay::EnsureStateBuffers( int modelCount )
{
    if ( modelCount < 0 || static_cast<std::size_t>( modelCount ) > m_captureSeconds.capacity() ||
         static_cast<std::size_t>( modelCount ) > m_ejectCooldownSeconds.capacity() )
    {
        // Lane F: SceneWorld must reserve the authored capacity before steady
        // gameplay. Growing either timer vector here would violate the global
        // runtime allocation policy.
        SB_FATAL( "Gameplay/TornadoGameplay",
                  "Body timer capacity exceeded. requested=%d capture_capacity=%zu cooldown_capacity=%zu",
                  modelCount,
                  m_captureSeconds.capacity(),
                  m_ejectCooldownSeconds.capacity() );
    }
    if ( static_cast<int>( m_captureSeconds.size() ) != modelCount )
    {
        m_captureSeconds.assign( modelCount, 0.0f );
    }
    if ( static_cast<int>( m_ejectCooldownSeconds.size() ) != modelCount )
    {
        m_ejectCooldownSeconds.assign( modelCount, 0.0f );
    }
}

void TornadoGameplay::AppendForceField( const TornadoFieldConfig& config )
{
    if ( m_forceFieldCount >= m_forceFields.size() )
    {
        SB_FATAL( "Gameplay/TornadoGameplay",
                  "Active external force field capacity exceeded. requested=%zu capacity=%zu",
                  m_forceFieldCount + 1u,
                  m_forceFields.size() );
    }

    Physics::ExternalCylindricalForceField& field = m_forceFields[m_forceFieldCount++];
    field.center = config.center;
    field.radiusMeters = config.radius;
    field.heightMeters = config.height;
    field.inwardAccelerationMetersPerSecondSquared = config.inwardAcceleration;
    field.tangentialAccelerationMetersPerSecondSquared = config.swirlAcceleration;
    field.liftAccelerationMetersPerSecondSquared = config.liftAcceleration;
    field.outwardEjectAccelerationMetersPerSecondSquared = config.ejectAcceleration;
    field.upwardEjectAccelerationMetersPerSecondSquared = config.ejectUpAcceleration;
    field.ejectHeightFraction = config.ejectBand;
    field.minimumExposureSeconds = config.minCaptureSeconds;
    field.repeatCooldownSeconds = config.ejectCooldownSeconds;
    field.maxDeltaVelocityMetersPerSecond = config.maxDeltaVelocity;
}

uint64_t TornadoVisualPass::DynamicMemoryBytes() const
{
    // Why: the focused CPU test target links Gameplay ownership without the
    // DX12-backed visual implementation, but diagnostics still must account
    // for the visual owner's reserved CPU arena through that stable seam.
    return static_cast<uint64_t>( m_vertices.capacity() ) * sizeof( float ) +
           static_cast<uint64_t>( m_activeVisualVortices.capacity() ) * sizeof( TornadoActiveVortex );
}
} // namespace SkullbonezCore::Gameplay
