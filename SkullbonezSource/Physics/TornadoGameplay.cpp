/*
File: SkullbonezSource/Physics/TornadoGameplay.cpp
Purpose:
  Implements deterministic tornado capture, ejection, and fixed-tree release.

Summary:
  Tornado gameplay samples either one configured field or the active procedural
  tornado system, mutates dense physics body rows, and emits plain body-index
  wake work for PhysicsWorld to apply through its existing sleep/contact logic.

Glossary:
  Best config: The active vortex with the largest acceleration at a body; its
    thresholds drive capture/ejection policy for that body.
  Deterministic slot: Body-index/capture-time phase bucket that staggers eject
    impulses without random numbers.
  Wake output: Ordered model indices that PhysicsWorld should wake after fixed
    releases and before per-body tornado acceleration.

Invariants:
  - The tornado system clock is advanced exactly once per fixed tick through
    BeginStep().
  - Fixed-tree release decisions run before per-body tornado acceleration.
  - Per-body worker execution touches only model-indexed arrays owned by the
    current fixed tick, preserving deterministic output.

Related:
  - SkullbonezSource/Physics/TornadoGameplay.h
  - SkullbonezSource/Physics/TornadoField.cpp
*/
#include "TornadoGameplay.h"
#include "Stages/PhysicsSleepController.h"

#include "../Core/Config.h"
#include "../Core/Profiler.h"
#include "../Core/WorkerPool.h"
#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsWorldForces.h"

#include <algorithm>
#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;

namespace SkullbonezCore
{
namespace Physics
{
namespace
{
constexpr float TORNADO_EJECTION_PHASE_HZ = 10.0f;

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

Vector3 ClampVectorMagnitude( const Vector3& value, float maxMagnitude )
{
    if ( maxMagnitude <= TOLERANCE )
    {
        return ZERO_VECTOR;
    }

    const float magSq = value * value;
    const float maxSq = maxMagnitude * maxMagnitude;
    if ( magSq <= maxSq || magSq <= TOLERANCE * TOLERANCE )
    {
        return value;
    }

    return value * ( maxMagnitude / sqrtf( magSq ) );
}

bool IsUnderwaterSleepLocked( std::span<const uint8_t> underwaterSleepLocked, int bodyCount, int index )
{
    if ( index < 0 || index >= bodyCount || index >= static_cast<int>( underwaterSleepLocked.size() ) )
    {
        return false;
    }
    return underwaterSleepLocked[static_cast<size_t>( index )] != 0;
}
} // namespace


TornadoGameplay::TornadoGameplay()
{
    ReserveBodyCapacity( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
}


void TornadoGameplay::ReserveBodyCapacity( int capacity )
{
    const size_t reserveCount = static_cast<size_t>( (std::max)( 0, capacity ) );
    m_captureSeconds.reserve( reserveCount );
    m_ejectCooldownSeconds.reserve( reserveCount );
    m_fixedTreeReleaseWakeScratch.reserve( reserveCount );
    m_releaseWakeBodies.reserve( reserveCount );
}


void TornadoGameplay::Clear()
{
    m_captureSeconds.clear();
    m_ejectCooldownSeconds.clear();
    m_fixedTreeReleaseWakeScratch.clear();
    m_releaseWakeBodies.clear();
    m_system.SetConfig( TornadoSystemConfig() );
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
    m_system.SetConfig( systemConfig );
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


TornadoGameplayStepState TornadoGameplay::BeginStep( float dt )
{
    TornadoGameplayStepState state;
    state.stepSeconds = (std::max)( 0.0f, dt );
    state.useSystem = m_system.IsEnabled();
    if ( state.useSystem )
    {
        m_system.Tick( state.stepSeconds );
    }

    const std::vector<TornadoActiveVortex>& activeVortices = m_system.ActiveVortices();
    state.active =
        ( state.useSystem && !activeVortices.empty() ) || ( !state.useSystem && m_field.GetConfig().enabled );
    return state;
}


const std::vector<int>& TornadoGameplay::ReleaseFixedBodies( const TornadoGameplayStepState& stepState,
                                                             PhysicsBodyStore& bodyStore )
{
    m_releaseWakeBodies.clear();
    m_fixedTreeReleaseWakeScratch.clear();
    if ( !stepState.active || !stepState.useSystem )
    {
        return m_releaseWakeBodies;
    }

    const auto bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( hotFields );
    for ( int i = 0; i < bodyStore.Count(); ++i )
    {
        const PhysicsBodyRecord& record = bodyRecords[static_cast<size_t>( i )];
        if ( hotFields.fixed[static_cast<size_t>( i )] == 0u || !record.releasesFromFixedOnContact )
        {
            continue;
        }

        TornadoFieldConfig bestConfig;
        float bestAccelerationSq = 0.0f;
        const Vector3 acceleration = SampleAcceleration( stepState,
                                                         PhysicsBodyPosition( hotRead, static_cast<size_t>( i ) ),
                                                         bestConfig,
                                                         bestAccelerationSq );
        const float releaseAcceleration = (std::max)( 16.0f, record.contactReleaseImpulseThreshold * 32.0f );
        if ( bestAccelerationSq < releaseAcceleration * releaseAcceleration )
        {
            continue;
        }

        const Vector3 seedLinearVelocity =
            ClampVectorMagnitude( acceleration * 0.08f, (std::max)( 10.0f, bestConfig.maxDeltaVelocity * 1.5f ) );
        const Vector3 seedAngularVelocity( seedLinearVelocity.z * 0.08f, 0.0f, -seedLinearVelocity.x * 0.08f );
        // Why: fixed-tree release must happen before broadphase so later fixed
        // checks see dynamic rows. PhysicsWorld applies the ordered wake output
        // immediately after this release stage.
        bodyStore.ReleaseFixedBody( i, seedLinearVelocity, seedAngularVelocity );
        m_releaseWakeBodies.push_back( i );

        bodyStore.ReleaseAttachedFixedTreeParts(
            PhysicsFixedTreeReleaseEvent{ i, seedLinearVelocity, seedAngularVelocity },
            m_fixedTreeReleaseWakeScratch );
        for ( int releasedIndex : m_fixedTreeReleaseWakeScratch )
        {
            m_releaseWakeBodies.push_back( releasedIndex );
        }
        m_fixedTreeReleaseWakeScratch.clear();
    }
    return m_releaseWakeBodies;
}


void TornadoGameplay::ApplyBodyForces( const TornadoGameplayStepState& stepState,
                                       const TornadoBodyForceContext& context )
{
    if ( !stepState.active )
    {
        return;
    }

    const auto bodyRecords = context.bodyStore.Records();
    const PhysicsBodyHotFieldsView hotFields = context.bodyStore.MutableHotFields();
    const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( hotFields );
    const int modelCount = (std::min)( { context.bodyStore.Count(),
                                         static_cast<int>( bodyRecords.size() ),
                                         context.colliderStore.Count() } );
    EnsureStateBuffers( modelCount );

    const auto applyTornadoAt = [&]( int i )
    {
        const size_t bodyIndex = static_cast<size_t>( i );
        if ( hotFields.fixed[bodyIndex] != 0u ||
             IsUnderwaterSleepLocked( context.underwaterSleepLocked, modelCount, i ) )
        {
            m_captureSeconds[static_cast<size_t>( i )] = 0.0f;
            m_ejectCooldownSeconds[static_cast<size_t>( i )] = 0.0f;
            return;
        }

        const Vector3 position = PhysicsBodyPosition( hotRead, bodyIndex );
        TornadoFieldConfig bestConfig;
        float bestAccelerationSq = 0.0f;
        Vector3 acceleration = SampleAcceleration( stepState, position, bestConfig, bestAccelerationSq );
        const float dx = position.x - bestConfig.center.x;
        const float dz = position.z - bestConfig.center.z;
        const float horizontalSq = dx * dx + dz * dz;
        const float horizontal = sqrtf( horizontalSq );
        const float height = (std::max)( bestConfig.height, 1.0f );
        const float height01 = ( position.y - bestConfig.center.y ) / height;
        if ( bestAccelerationSq <= TOLERANCE * TOLERANCE )
        {
            m_captureSeconds[static_cast<size_t>( i )] = 0.0f;
            m_ejectCooldownSeconds[static_cast<size_t>( i )] =
                (std::max)( 0.0f, m_ejectCooldownSeconds[static_cast<size_t>( i )] - stepState.stepSeconds );
            return;
        }

        if ( context.sleepState[i] )
        {
            context.wakeAccess.WakeBody( i );
        }

        Vector3 velocity = PhysicsBodyLinearVelocity( hotRead, bodyIndex );
        m_captureSeconds[static_cast<size_t>( i )] += stepState.stepSeconds;
        m_ejectCooldownSeconds[static_cast<size_t>( i )] =
            (std::max)( 0.0f, m_ejectCooldownSeconds[static_cast<size_t>( i )] - stepState.stepSeconds );

        const float ejectBand = std::clamp( bestConfig.ejectBand, 0.0f, 1.0f );
        const float minCaptureSeconds = (std::max)( 0.0f, bestConfig.minCaptureSeconds );
        const float cooldownSeconds = (std::max)( 0.0f, bestConfig.ejectCooldownSeconds );
        const float maxDeltaVelocity = (std::max)( 1.0f, bestConfig.maxDeltaVelocity );
        const float minTangentialSpeed = (std::max)( 18.0f, bestConfig.swirlAcceleration * 0.12f );
        Vector3 outward;
        if ( horizontal > TOLERANCE )
        {
            outward = Vector3( dx / horizontal, 0.0f, dz / horizontal );
        }
        else
        {
            switch ( i & 3 )
            {
            case 0:
                outward = Vector3( 1.0f, 0.0f, 0.0f );
                break;
            case 1:
                outward = Vector3( 0.0f, 0.0f, 1.0f );
                break;
            case 2:
                outward = Vector3( -1.0f, 0.0f, 0.0f );
                break;
            default:
                outward = Vector3( 0.0f, 0.0f, -1.0f );
                break;
            }
        }

        const Vector3 tangent( -outward.z, 0.0f, outward.x );
        const float tangentialSpeed = fabsf( velocity * tangent );
        const int captureBucket =
            static_cast<int>( m_captureSeconds[static_cast<size_t>( i )] * TORNADO_EJECTION_PHASE_HZ );
        const bool deterministicSlot = ( ( i + captureBucket ) % 3 ) == 0;
        if ( height01 >= ejectBand && m_captureSeconds[static_cast<size_t>( i )] >= minCaptureSeconds &&
             m_ejectCooldownSeconds[static_cast<size_t>( i )] <= 0.0f && tangentialSpeed >= minTangentialSpeed &&
             deterministicSlot )
        {
            acceleration +=
                outward * bestConfig.ejectAcceleration + Vector3( 0.0f, bestConfig.ejectUpAcceleration, 0.0f );
            m_captureSeconds[static_cast<size_t>( i )] = 0.0f;
            m_ejectCooldownSeconds[static_cast<size_t>( i )] = cooldownSeconds;
        }

        velocity += ClampVectorMagnitude( acceleration * stepState.stepSeconds, maxDeltaVelocity );
        hotFields.linearVelocityX[bodyIndex] = velocity.x;
        hotFields.linearVelocityY[bodyIndex] = velocity.y;
        hotFields.linearVelocityZ[bodyIndex] = velocity.z;
    };

    if ( context.runtimeConfig.physicsExecution.parallel &&
         context.runtimeConfig.physicsExecution.parallelTornadoField )
    {
        context.workerPool.ParallelForNoAlloc( 0,
                                               modelCount,
                                               applyTornadoAt,
                                               context.minParallelBodies,
                                               context.workerMarkerPath,
                                               context.workerMarkerHash );
    }
    else
    {
        for ( int i = 0; i < modelCount; ++i )
        {
            applyTornadoAt( i );
        }
    }
}


uint64_t TornadoGameplay::CollectMemoryBytes() const
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytes( m_captureSeconds );
    bytes += VectorCapacityBytes( m_ejectCooldownSeconds );
    bytes += VectorCapacityBytes( m_fixedTreeReleaseWakeScratch );
    bytes += VectorCapacityBytes( m_releaseWakeBodies );
    bytes += static_cast<uint64_t>( m_field.DynamicMemoryBytes() );
    bytes += static_cast<uint64_t>( m_system.DynamicMemoryBytes() );
    return bytes;
}


uint64_t TornadoGameplay::CollectDebugMemoryBytes() const
{
    return static_cast<uint64_t>( m_field.DynamicMemoryBytes() );
}


Vector3 TornadoGameplay::SampleAcceleration( const TornadoGameplayStepState& stepState,
                                             const Vector3& position,
                                             TornadoFieldConfig& outBestConfig,
                                             float& outBestAccelerationSq ) const
{
    Vector3 acceleration = ZERO_VECTOR;
    outBestConfig = m_field.GetConfig();
    outBestAccelerationSq = 0.0f;
    if ( stepState.useSystem )
    {
        for ( const TornadoActiveVortex& vortex : m_system.ActiveVortices() )
        {
            const Vector3 sample = TornadoField::SampleAccelerationForConfig( vortex.field, position );
            const float sampleSq = sample * sample;
            acceleration += sample;
            if ( sampleSq > outBestAccelerationSq )
            {
                outBestAccelerationSq = sampleSq;
                outBestConfig = vortex.field;
            }
        }
    }
    else
    {
        acceleration = m_field.SampleAcceleration( position );
        outBestAccelerationSq = acceleration * acceleration;
    }
    return acceleration;
}


void TornadoGameplay::EnsureStateBuffers( int modelCount )
{
    if ( static_cast<int>( m_captureSeconds.size() ) != modelCount )
    {
        m_captureSeconds.assign( modelCount, 0.0f );
    }
    if ( static_cast<int>( m_ejectCooldownSeconds.size() ) != modelCount )
    {
        m_ejectCooldownSeconds.assign( modelCount, 0.0f );
    }
}
} // namespace Physics
} // namespace SkullbonezCore
