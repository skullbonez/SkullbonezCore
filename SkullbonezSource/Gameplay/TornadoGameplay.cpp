/*
File: SkullbonezSource/Gameplay/TornadoGameplay.cpp
Purpose:
  Advances tornado content and publishes bounded external-force frame values.

Summary:
  The owner retains authored field/system configuration, procedural time, and
  per-body exposure state. Each fixed tick it converts active vortices to the
  Physics-owned cylindrical value vocabulary without changing order or math.

Glossary:
  Active vortex: Authored vortex after spawn, growth, shrink, drift, and
    repulsion have been evaluated for the current gameplay clock.
  Force frame: Synchronous spans borrowed by Physics for exactly one Step call.

Invariants:
  - The system clock advances exactly once per BuildForceFrame call.
  - Field conversion retains active/source order and exact float values.
  - The fixed 64-field array never allocates during steady gameplay.

Related:
  - SkullbonezSource/Gameplay/TornadoGameplay.h
  - SkullbonezSource/Physics/Stages/ExternalForceStage.cpp
*/
#include "TornadoGameplay.h"

#include "../Core/FatalError.h"
#include "../Core/SceneCapacity.h"

#include <algorithm>

namespace SkullbonezCore::Gameplay
{
namespace
{
template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}
} // namespace

TornadoGameplay::TornadoGameplay()
{
    ReserveBodyCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
}

void TornadoGameplay::ReserveBodyCapacity( int capacity )
{
    const std::size_t reserveCount = static_cast<std::size_t>( (std::max)( 0, capacity ) );
    m_captureSeconds.reserve( reserveCount );
    m_ejectCooldownSeconds.reserve( reserveCount );
}

void TornadoGameplay::Clear()
{
    m_captureSeconds.clear();
    m_ejectCooldownSeconds.clear();
    m_forceFieldCount = 0u;
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

void TornadoGameplay::SetReplayState( const std::vector<float>& captureSeconds,
                                      const std::vector<float>& ejectCooldownSeconds,
                                      const TornadoFieldConfig& fieldConfig,
                                      const TornadoSystemConfig& systemConfig,
                                      float systemElapsedSeconds )
{
    m_captureSeconds = captureSeconds;
    m_ejectCooldownSeconds = ejectCooldownSeconds;
    m_field.SetConfig( fieldConfig );
    SetSystemConfig( systemConfig );
    m_system.SetElapsedSeconds( systemElapsedSeconds );
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
        stepSeconds };
}

uint64_t TornadoGameplay::CollectMemoryBytes() const
{
    return VectorCapacityBytes( m_captureSeconds ) + VectorCapacityBytes( m_ejectCooldownSeconds ) +
           static_cast<uint64_t>( m_field.DynamicMemoryBytes() ) +
           static_cast<uint64_t>( m_system.DynamicMemoryBytes() ) + sizeof( m_forceFields );
}

uint64_t TornadoGameplay::CollectDebugMemoryBytes() const
{
    return static_cast<uint64_t>( m_field.DynamicMemoryBytes() );
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
} // namespace SkullbonezCore::Gameplay
