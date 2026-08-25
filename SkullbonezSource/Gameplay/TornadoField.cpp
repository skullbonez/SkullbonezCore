/*
File: SkullbonezSource/Gameplay/TornadoField.cpp
Purpose:
  Evolves Gameplay-owned tornado fields and samples their acceleration.

Summary:
  Gameplay-owned vortices advance through deterministic growth, drift, and
  pair-repulsion math. The resulting active rows retain source order for
  Physics and presentation.

Glossary:
  Strength envelope: Product of the growth and shrink factors for one vortex.
  Pair repulsion: Deterministic separation applied to overlapping active
    vortex centers after their independent drift is evaluated.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.

Related:
  - SkullbonezSource/Gameplay/TornadoField.h
  - Agentic/Reference/physics-overview.md
*/
#include "TornadoField.h"
#include "../Core/Common.h"
#include "../Core/FatalError.h"
#include <algorithm>
#include <cmath>


using namespace SkullbonezCore::Gameplay;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
namespace Vector = SkullbonezCore::Math::Vector;

// Invariant: times above this first fixed-step stall boundary always use the
// precise lifecycle path, even when a restored double lands on the float lattice.
static constexpr double TORNADO_FLOAT_FIXED_STEP_BOUNDARY_SECONDS = 262144.0;


static float SmoothStep01( float edge0, float edge1, float value )
{
    if ( fabsf( edge1 - edge0 ) <= TOLERANCE )
    {
        return value >= edge1 ? 1.0f : 0.0f;
    }

    float t = std::clamp( ( value - edge0 ) / ( edge1 - edge0 ), 0.0f, 1.0f );
    return t * t * ( 3.0f - 2.0f * t );
}

Vector3 SampleAccelerationForConfigImpl( const TornadoFieldConfig& config, const Vector3& position )
{
    const float radius = (std::max)( config.radius, 1.0f );
    const float height = (std::max)( config.height, 1.0f );
    const float dx = position.x - config.center.x;
    const float dz = position.z - config.center.z;
    const float horizontalSq = dx * dx + dz * dz;
    const float horizontal = sqrtf( horizontalSq );
    const float radial01 = horizontal / radius;
    const float height01 = ( position.y - config.center.y ) / height;

    if ( radial01 > 1.0f || height01 < -0.10f || height01 > 1.05f )
    {
        return ZERO_VECTOR;
    }

    Vector3 inward = horizontal > TOLERANCE ? Vector3( -dx / horizontal, 0.0f, -dz / horizontal )
                                            : Vector3( 1.0f, 0.0f, 0.0f );

    Vector3 tangent( -inward.z, 0.0f, inward.x );

    const float radialMask = 0.18f + 0.82f * SmoothStep01( 1.0f, 0.0f, radial01 );
    const float columnMask = SmoothStep01( 0.0f, 0.12f, height01 ) * SmoothStep01( 1.0f, 0.78f, height01 );
    const float swirlMask = columnMask * ( 0.45f + 0.55f * radialMask );

    return inward * ( config.inwardAcceleration * radialMask ) + tangent * ( config.swirlAcceleration * swirlMask ) +
           Vector3( 0.0f, config.liftAcceleration * columnMask, 0.0f );
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

bool TornadoField::ToggleEnabled()
{
    m_config.enabled = !m_config.enabled;
    return m_config.enabled;
}

void TornadoField::ToggleVelocityFieldVisualization()
{
    m_config.visualizeVelocityField = !m_config.visualizeVelocityField;
}

void TornadoField::SetFieldValue( float TornadoFieldConfig::* field, float value )
{
    m_config.*field = value;
    SetConfig( m_config );
}


Vector3 TornadoField::SampleAccelerationForConfig( const TornadoFieldConfig& config, const Vector3& position )
{
    return SampleAccelerationForConfigImpl( config, position );
}


std::size_t TornadoField::DynamicMemoryBytes() const
{
    return sizeof( *this );
}


TornadoSystem::TornadoSystem()
{
    // Lifetime: both vectors reach their authored hard cap during owner
    // construction. Scene edits, idle UI, and replay restore may change size
    // but cannot grow storage after steady gameplay begins.
    m_config.vortices.reserve( MAX_TORNADO_ACTIVE_FORCE_FIELDS );
    m_activeVortices.reserve( MAX_TORNADO_ACTIVE_FORCE_FIELDS );
}

void TornadoSystem::SetConfig( const TornadoSystemConfig& config )
{
    if ( config.vortices.size() > m_config.vortices.capacity() )
    {
        SB_FATAL( "Gameplay/TornadoSystem", "Authored vortex storage exceeded. requested=%zu capacity=%zu",
                  config.vortices.size(), m_config.vortices.capacity() );
    }

    m_config = config;

    for ( TornadoVortexConfig& vortex : m_config.vortices )
    {
        vortex.field.radius = (std::max)( 1.0f, vortex.field.radius );
        vortex.field.height = (std::max)( 1.0f, vortex.field.height );
        vortex.field.ejectAcceleration = (std::max)( 0.0f, vortex.field.ejectAcceleration );
        vortex.field.ejectUpAcceleration = (std::max)( 0.0f, vortex.field.ejectUpAcceleration );
        vortex.field.ejectBand = std::clamp( vortex.field.ejectBand, 0.0f, 1.0f );
        vortex.field.minCaptureSeconds = (std::max)( 0.0f, vortex.field.minCaptureSeconds );
        vortex.field.ejectCooldownSeconds = (std::max)( 0.0f, vortex.field.ejectCooldownSeconds );
        vortex.field.maxDeltaVelocity = (std::max)( 1.0f, vortex.field.maxDeltaVelocity );
        vortex.spawnSeconds = (std::max)( 0.0f, vortex.spawnSeconds );
        vortex.timeToLiveSeconds = (std::max)( 0.0f, vortex.timeToLiveSeconds );
        vortex.growSeconds = (std::max)( 0.0f, vortex.growSeconds );
        vortex.shrinkSeconds = (std::max)( 0.0f, vortex.shrinkSeconds );
        vortex.driftRadius = (std::max)( 0.0f, vortex.driftRadius );
        vortex.driftSpeed = (std::max)( 0.0f, vortex.driftSpeed );
        vortex.repulsionRadius = (std::max)( 0.0f, vortex.repulsionRadius );
        vortex.repulsionStrength = (std::max)( 0.0f, vortex.repulsionStrength );
    }

    RebuildActiveVortices();
}


bool TornadoSystem::IsEnabled() const
{
    return m_config.enabled && !m_config.vortices.empty();
}

bool TornadoSystem::HasAuthoredVortices() const
{
    return !m_config.vortices.empty();
}

bool TornadoSystem::ToggleEnabled()
{
    m_config.enabled = !m_config.enabled;
    RebuildActiveVortices();
    return m_config.enabled;
}

void TornadoSystem::ToggleVelocityFieldVisualization()
{
    m_config.visualizeVelocityField = !m_config.visualizeVelocityField;
}

void TornadoSystem::SetFieldValue( float TornadoFieldConfig::* field, float value )
{
    for ( TornadoVortexConfig& vortex : m_config.vortices )
    {
        vortex.field.*field = value;
    }

    SetConfig( m_config );
}


std::size_t TornadoSystem::DynamicMemoryBytes() const
{
    return m_config.vortices.capacity() * sizeof( TornadoVortexConfig ) +
           m_activeVortices.capacity() * sizeof( TornadoActiveVortex );
}


void TornadoSystem::ResetElapsedSeconds()
{
    m_elapsedSeconds = 0.0;
    m_preciseElapsedContinuation = false;
    RebuildActiveVortices();
}


void TornadoSystem::SetElapsedSeconds( double seconds )
{
    m_elapsedSeconds = (std::max)( 0.0, seconds );
    m_preciseElapsedContinuation =
        m_elapsedSeconds > TORNADO_FLOAT_FIXED_STEP_BOUNDARY_SECONDS ||
        static_cast<double>( static_cast<float>( m_elapsedSeconds ) ) != m_elapsedSeconds;
    RebuildActiveVortices();
}


void TornadoSystem::Tick( float dt )
{
    const float elapsedDelta = (std::max)( 0.0f, dt );

    if ( elapsedDelta > 0.0f )
    {
        if ( !m_preciseElapsedContinuation )
        {
            const float currentElapsed = static_cast<float>( m_elapsedSeconds );
            const float nextElapsed = currentElapsed + elapsedDelta;

            if ( nextElapsed != currentElapsed )
            {
                // Compatibility: retain the original float addition while it
                // can represent progress, preserving existing deterministic bytes.
                m_elapsedSeconds = static_cast<double>( nextElapsed );
            }
            else
            {
                // Hazard: at long runtimes a fixed-step delta becomes smaller
                // than half a float ULP. Continue from that exact boundary in double.
                m_preciseElapsedContinuation = true;
                m_elapsedSeconds += static_cast<double>( elapsedDelta );
            }
        }
        else
        {
            m_elapsedSeconds += static_cast<double>( elapsedDelta );
        }
    }

    RebuildActiveVortices();
}


void TornadoSystem::BuildActiveVortices( const TornadoSystemConfig& config, double elapsedSeconds,
                                         std::vector<TornadoActiveVortex>& outVortices )
{
    outVortices.clear();

    if ( !config.enabled )
    {
        return;
    }

    elapsedSeconds = (std::max)( 0.0, elapsedSeconds );
    const float compatibleElapsedSeconds = static_cast<float>( elapsedSeconds );
    const bool requiresPreciseLifecycle =
        elapsedSeconds > TORNADO_FLOAT_FIXED_STEP_BOUNDARY_SECONDS ||
        static_cast<double>( compatibleElapsedSeconds ) != elapsedSeconds;
    const float twoPi = 6.28318530718f;

    // Invariant: append order is authored source order. Physics consumes this
    // exact order for left-to-right floating-point accumulation.
    for ( int i = 0; i < static_cast<int>( config.vortices.size() ); ++i )
    {
        const TornadoVortexConfig& source = config.vortices[static_cast<size_t>( i )];

        if ( !source.field.enabled || elapsedSeconds < static_cast<double>( source.spawnSeconds ) )
        {
            continue;
        }

        const float compatibleAge = compatibleElapsedSeconds - source.spawnSeconds;
        const double preciseAge = elapsedSeconds - static_cast<double>( source.spawnSeconds );

        if ( source.timeToLiveSeconds > 0.0f &&
             ( requiresPreciseLifecycle
                   ? preciseAge > static_cast<double>( source.timeToLiveSeconds ) +
                                      static_cast<double>( (std::max)( source.shrinkSeconds, 0.001f ) )
                   : compatibleAge > source.timeToLiveSeconds + (std::max)( source.shrinkSeconds, 0.001f ) ) )
        {
            continue;
        }

        const float grow = source.growSeconds > 0.0f
                               ? ( requiresPreciseLifecycle
                                       ? static_cast<float>( std::clamp( preciseAge / static_cast<double>( source.growSeconds ),
                                                                        0.0, 1.0 ) )
                                       : std::clamp( compatibleAge / source.growSeconds, 0.0f, 1.0f ) )
                               : 1.0f;
        float shrink = 1.0f;

        if ( source.timeToLiveSeconds > 0.0f &&
             ( requiresPreciseLifecycle ? preciseAge > static_cast<double>( source.timeToLiveSeconds )
                                        : compatibleAge > source.timeToLiveSeconds ) )
        {
            if ( requiresPreciseLifecycle )
            {
                const double shrinkAge = preciseAge - static_cast<double>( source.timeToLiveSeconds );
                shrink = source.shrinkSeconds > 0.0f
                             ? static_cast<float>( std::clamp( 1.0 - shrinkAge / static_cast<double>( source.shrinkSeconds ),
                                                              0.0, 1.0 ) )
                             : 0.0f;
            }
            else
            {
                const float shrinkAge = compatibleAge - source.timeToLiveSeconds;
                shrink = source.shrinkSeconds > 0.0f
                             ? std::clamp( 1.0f - shrinkAge / source.shrinkSeconds, 0.0f, 1.0f )
                             : 0.0f;
            }
        }

        const float strength = grow * shrink;

        if ( strength <= 0.001f )
        {
            continue;
        }

        TornadoActiveVortex active;
        active.field = source.field;
        active.field.visualizeVelocityField = config.visualizeVelocityField || source.field.visualizeVelocityField;
        active.field.radius = (std::max)( 1.0f, source.field.radius * strength );
        active.field.height = (std::max)( 1.0f, source.field.height * ( 0.35f + 0.65f * strength ) );
        active.field.inwardAcceleration = source.field.inwardAcceleration * strength;
        active.field.swirlAcceleration = source.field.swirlAcceleration * strength;
        active.field.liftAcceleration = source.field.liftAcceleration * strength;
        active.field.ejectAcceleration = source.field.ejectAcceleration * strength;
        active.field.ejectUpAcceleration = source.field.ejectUpAcceleration * strength;
        active.strength = strength;
        active.ageSeconds = requiresPreciseLifecycle ? preciseAge : static_cast<double>( compatibleAge );
        active.sourceIndex = i;

        if ( source.driftRadius > 0.0f && source.driftSpeed > 0.0f )
        {
            if ( requiresPreciseLifecycle )
            {
                const double phase = static_cast<double>( source.driftPhase ) + preciseAge * source.driftSpeed;
                const double wobble = static_cast<double>( source.driftPhase ) * 0.73 +
                                      preciseAge * static_cast<double>( source.driftSpeed ) * 0.67 +
                                      static_cast<double>( twoPi ) * 0.19;
                active.field.center.x += static_cast<float>( std::cos( phase ) * source.driftRadius +
                                                             std::cos( wobble ) * source.driftRadius * 0.34 );
                active.field.center.z += static_cast<float>( std::sin( phase ) * source.driftRadius +
                                                             std::sin( wobble ) * source.driftRadius * 0.34 );
            }
            else
            {
                const float phase = source.driftPhase + compatibleAge * source.driftSpeed;
                const float wobble = source.driftPhase * 0.73f + compatibleAge * source.driftSpeed * 0.67f + twoPi * 0.19f;
                active.field.center.x += cosf( phase ) * source.driftRadius + cosf( wobble ) * source.driftRadius * 0.34f;
                active.field.center.z += sinf( phase ) * source.driftRadius + sinf( wobble ) * source.driftRadius * 0.34f;
            }
        }

        outVortices.push_back( active );
    }

    // Why: pair iteration is ascending and updates both centers immediately;
    // changing this to a parallel or unordered reduction changes later pairs.
    for ( int a = 0; a < static_cast<int>( outVortices.size() ); ++a )
    {
        const TornadoVortexConfig& configA = config.vortices[static_cast<size_t>( outVortices[a].sourceIndex )];

        for ( int b = a + 1; b < static_cast<int>( outVortices.size() ); ++b )
        {
            const TornadoVortexConfig& configB = config.vortices[static_cast<size_t>( outVortices[b].sourceIndex )];
            const float repulsionRadius = (std::max)( configA.repulsionRadius, configB.repulsionRadius );
            const float repulsionStrength = (std::max)( configA.repulsionStrength, configB.repulsionStrength );

            if ( repulsionRadius <= TOLERANCE || repulsionStrength <= TOLERANCE )
            {
                continue;
            }

            Vector3 delta = outVortices[b].field.center - outVortices[a].field.center;
            delta.y = 0.0f;
            const float distanceSq = Dot( delta, delta );

            if ( distanceSq >= repulsionRadius * repulsionRadius )
            {
                continue;
            }

            Vector3 direction( 1.0f, 0.0f, 0.0f );
            float distance = 0.0f;

            if ( distanceSq > TOLERANCE * TOLERANCE )
            {
                distance = sqrtf( distanceSq );
                direction = delta / distance;
            }
            else
            {
                if ( requiresPreciseLifecycle )
                {
                    const double fallbackPhase = static_cast<double>( a * 41 + b * 97 ) * 0.61803398875 +
                                                 elapsedSeconds * 0.17;
                    direction = Vector3( static_cast<float>( std::cos( fallbackPhase ) ), 0.0f,
                                         static_cast<float>( std::sin( fallbackPhase ) ) );
                }
                else
                {
                    const float fallbackPhase = static_cast<float>( a * 41 + b * 97 ) * 0.61803398875f +
                                                compatibleElapsedSeconds * 0.17f;
                    direction = Vector3( cosf( fallbackPhase ), 0.0f, sinf( fallbackPhase ) );
                }
            }

            const float push = ( repulsionRadius - distance ) * repulsionStrength * 0.5f;
            outVortices[a].field.center -= direction * push;
            outVortices[b].field.center += direction * push;
        }
    }
}


void TornadoSystem::RebuildActiveVortices()
{
    BuildActiveVortices( m_config, m_elapsedSeconds, m_activeVortices );
}
