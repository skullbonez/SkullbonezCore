/*
File: SkullbonezSource/Physics/TornadoField.cpp
Purpose:
  Computes a procedural tornado force field for generated physics scenes.

Mental model:
  TornadoField.cpp computes a procedural tornado force field for generated
  physics scenes. As an implementation unit, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below.

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
#include <algorithm>
#include <cmath>


using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
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

    Vector3 inward =
        horizontal > TOLERANCE ? Vector3( -dx / horizontal, 0.0f, -dz / horizontal ) : Vector3( 1.0f, 0.0f, 0.0f );
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


Vector3 TornadoField::SampleAcceleration( const Vector3& position ) const
{
    return SampleAccelerationForConfigImpl( m_config, position );
}


Vector3 TornadoField::SampleAccelerationForConfig( const TornadoFieldConfig& config, const Vector3& position )
{
    return SampleAccelerationForConfigImpl( config, position );
}


std::size_t TornadoField::DynamicMemoryBytes() const
{
    return sizeof( *this );
}


void TornadoSystem::SetConfig( const TornadoSystemConfig& config )
{
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

    if ( m_activeVortices.capacity() < m_config.vortices.size() )
    {
        m_activeVortices.reserve( m_config.vortices.size() );
    }
    RebuildActiveVortices();
}


bool TornadoSystem::IsEnabled() const
{
    return m_config.enabled && !m_config.vortices.empty();
}


std::size_t TornadoSystem::DynamicMemoryBytes() const
{
    return m_config.vortices.capacity() * sizeof( TornadoVortexConfig ) +
           m_activeVortices.capacity() * sizeof( TornadoActiveVortex );
}


void TornadoSystem::ResetElapsedSeconds()
{
    m_elapsedSeconds = 0.0f;
    RebuildActiveVortices();
}


void TornadoSystem::SetElapsedSeconds( float seconds )
{
    m_elapsedSeconds = (std::max)( 0.0f, seconds );
    RebuildActiveVortices();
}


void TornadoSystem::Tick( float dt )
{
    m_elapsedSeconds += (std::max)( 0.0f, dt );
    RebuildActiveVortices();
}


void TornadoSystem::BuildActiveVortices( const TornadoSystemConfig& config,
                                         float elapsedSeconds,
                                         std::vector<TornadoActiveVortex>& outVortices )
{
    outVortices.clear();
    if ( !config.enabled )
    {
        return;
    }

    elapsedSeconds = (std::max)( 0.0f, elapsedSeconds );
    const float twoPi = 6.28318530718f;
    for ( int i = 0; i < static_cast<int>( config.vortices.size() ); ++i )
    {
        const TornadoVortexConfig& source = config.vortices[static_cast<size_t>( i )];
        if ( !source.field.enabled || elapsedSeconds < source.spawnSeconds )
        {
            continue;
        }

        const float age = elapsedSeconds - source.spawnSeconds;
        if ( source.timeToLiveSeconds > 0.0f &&
             age > source.timeToLiveSeconds + (std::max)( source.shrinkSeconds, 0.001f ) )
        {
            continue;
        }

        const float grow = source.growSeconds > 0.0f ? std::clamp( age / source.growSeconds, 0.0f, 1.0f ) : 1.0f;
        float shrink = 1.0f;
        if ( source.timeToLiveSeconds > 0.0f && age > source.timeToLiveSeconds )
        {
            const float shrinkAge = age - source.timeToLiveSeconds;
            shrink =
                source.shrinkSeconds > 0.0f ? std::clamp( 1.0f - shrinkAge / source.shrinkSeconds, 0.0f, 1.0f ) : 0.0f;
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
        active.ageSeconds = age;
        active.sourceIndex = i;

        if ( source.driftRadius > 0.0f && source.driftSpeed > 0.0f )
        {
            const float phase = source.driftPhase + age * source.driftSpeed;
            const float wobble = source.driftPhase * 0.73f + age * source.driftSpeed * 0.67f + twoPi * 0.19f;
            active.field.center.x += cosf( phase ) * source.driftRadius + cosf( wobble ) * source.driftRadius * 0.34f;
            active.field.center.z += sinf( phase ) * source.driftRadius + sinf( wobble ) * source.driftRadius * 0.34f;
        }

        outVortices.push_back( active );
    }

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
            const float distanceSq = delta * delta;
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
                const float fallbackPhase =
                    static_cast<float>( a * 41 + b * 97 ) * 0.61803398875f + elapsedSeconds * 0.17f;
                direction = Vector3( cosf( fallbackPhase ), 0.0f, sinf( fallbackPhase ) );
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


Vector3 TornadoSystem::SampleAcceleration( const Vector3& position ) const
{
    Vector3 acceleration = ZERO_VECTOR;
    for ( const TornadoActiveVortex& vortex : m_activeVortices )
    {
        acceleration += SampleAccelerationForConfigImpl( vortex.field, position );
    }
    return acceleration;
}
