/*
File: SkullbonezSource/Physics/Stages/ExternalForceStage.cpp
Purpose:
  Applies bounded gameplay-authored cylindrical forces to dense physics rows.

Summary:
  The stage preserves the established fixed-release, wake, capture, ejection,
  and velocity-write order while consuming only Physics-owned value records.

Glossary:
  Strongest field: The individual field with the largest sampled acceleration;
    its thresholds govern edge ejection while every field contributes force.
  Deterministic slot: A body-row/exposure bucket that staggers ejections without
    random state.

Invariants:
  - Fixed-body release completes before movable-body acceleration.
  - Each worker writes one body row and its matching timer rows only.
  - Field samples accumulate left-to-right in the supplied span order.

Related:
  - SkullbonezSource/Physics/Stages/ExternalForceStage.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#include "ExternalForceStage.h"

#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsWorldForces.h"
#include "../../Core/Common.h"
#include "../../Core/WorkerPool.h"

#include <algorithm>
#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;

namespace SkullbonezCore::Physics
{
namespace
{
constexpr float EJECTION_PHASE_HZ = 10.0f;
constexpr int PHYSICS_PARALLEL_MIN_BODIES = 512;
constexpr const char* EXTERNAL_FORCE_WORKER_MARKER = "Frame/Physics/ExternalForceField/WorkerBodies";
constexpr uint32_t PHYSICS_EXTERNAL_FORCE_WORKER_HASH = HashStr( EXTERNAL_FORCE_WORKER_MARKER );

float SmoothStep01( float edge0, float edge1, float value )
{

    if ( fabsf( edge1 - edge0 ) <= TOLERANCE )
    {
        return value >= edge1 ? 1.0f : 0.0f;
    }

    const float t = std::clamp( ( value - edge0 ) / ( edge1 - edge0 ), 0.0f, 1.0f );
    return t * t * ( 3.0f - 2.0f * t );
}

Vector3 SampleFieldAcceleration( const ExternalCylindricalForceField& field, const Vector3& position )
{
    const float radius = (std::max)( field.radiusMeters, 1.0f );
    const float height = (std::max)( field.heightMeters, 1.0f );
    const float dx = position.x - field.center.x;
    const float dz = position.z - field.center.z;
    const float horizontal = sqrtf( dx * dx + dz * dz );
    const float radial01 = horizontal / radius;
    const float height01 = ( position.y - field.center.y ) / height;

    if ( radial01 > 1.0f || height01 < -0.10f || height01 > 1.05f )
    {
        return ZERO_VECTOR;
    }

    const Vector3 inward = horizontal > TOLERANCE ? Vector3( -dx / horizontal, 0.0f, -dz / horizontal )
                                                  : Vector3( 1.0f, 0.0f, 0.0f );

    const Vector3 tangent( -inward.z, 0.0f, inward.x );
    const float radialMask = 0.18f + 0.82f * SmoothStep01( 1.0f, 0.0f, radial01 );
    const float columnMask = SmoothStep01( 0.0f, 0.12f, height01 ) * SmoothStep01( 1.0f, 0.78f, height01 );
    const float swirlMask = columnMask * ( 0.45f + 0.55f * radialMask );
    return inward * ( field.inwardAccelerationMetersPerSecondSquared * radialMask ) +
           tangent * ( field.tangentialAccelerationMetersPerSecondSquared * swirlMask ) +
           Vector3( 0.0f, field.liftAccelerationMetersPerSecondSquared * columnMask, 0.0f );
}

Vector3 ClampVectorMagnitude( const Vector3& value, float maxMagnitude )
{

    if ( maxMagnitude <= TOLERANCE )
    {
        return ZERO_VECTOR;
    }

    const float magnitudeSq = value * value;
    const float maxSq = maxMagnitude * maxMagnitude;

    if ( magnitudeSq <= maxSq || magnitudeSq <= TOLERANCE * TOLERANCE )
    {
        return value;
    }

    return value * ( maxMagnitude / sqrtf( magnitudeSq ) );
}

} // namespace

ExternalForceStage::ExternalForceStage() = default;

void ExternalForceStage::ReserveBodyCapacity( std::size_t bodyCapacity )
{
    m_fixedTreeReleaseWakeScratch.Reserve( bodyCapacity );
    m_releaseWakeBodies.Reserve( bodyCapacity );
}

void ExternalForceStage::Clear()
{
    m_fixedTreeReleaseWakeScratch.clear();
    m_releaseWakeBodies.clear();
}

std::span<const int> ExternalForceStage::ReleaseFixedBodies( const ExternalForceFrameInput& input,
                                                             PhysicsBodyStore& bodyStore )
{
    m_releaseWakeBodies.clear();
    m_fixedTreeReleaseWakeScratch.clear();

    if ( !input.Active() )
    {
        return std::span<const int>( m_releaseWakeBodies.data(), m_releaseWakeBodies.size() );
    }

    const auto bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( hotFields );

    for ( int index = 0; index < bodyStore.Count(); ++index )
    {
        const std::size_t row = static_cast<std::size_t>( index );
        const PhysicsBodyRecord& record = bodyRecords[row];

        if ( hotFields.fixed[row] == 0u || !record.releasesFromFixedOnContact )
        {
            continue;
        }

        ExternalCylindricalForceField bestField;
        float bestAccelerationSq = 0.0f;
        const Vector3 acceleration = SampleAcceleration( input, PhysicsBodyPosition( hotRead, row ), bestField,
                                                         bestAccelerationSq );

        const float releaseAcceleration = (std::max)( 16.0f, record.contactReleaseImpulseThreshold * 32.0f );

        if ( bestAccelerationSq < releaseAcceleration * releaseAcceleration )
        {
            continue;
        }

        const Vector3 seedLinearVelocity = ClampVectorMagnitude( acceleration * 0.08f,
                                                                 (std::max)( 10.0f,
                                                                             bestField.maxDeltaVelocityMetersPerSecond *
                                                                                 1.5f ) );

        const Vector3 seedAngularVelocity( seedLinearVelocity.z * 0.08f, 0.0f, -seedLinearVelocity.x * 0.08f );

        // Why: release must precede broadphase so every later fixed-body check
        // sees the new dynamic row during this same fixed tick.
        bodyStore.ReleaseFixedBody( index, seedLinearVelocity, seedAngularVelocity );
        m_releaseWakeBodies.push_back( index );
        bodyStore.ReleaseAttachedFixedTreeParts( PhysicsFixedTreeReleaseEvent { index, seedLinearVelocity,
                                                                                seedAngularVelocity },
                                                 m_fixedTreeReleaseWakeScratch );

        for ( int releasedIndex : m_fixedTreeReleaseWakeScratch )
        {
            m_releaseWakeBodies.push_back( releasedIndex );
        }

        m_fixedTreeReleaseWakeScratch.clear();
    }

    return std::span<const int>( m_releaseWakeBodies.data(), m_releaseWakeBodies.size() );
}

void ExternalForceStage::ApplyBodyForces( const ExternalForceFrameInput& input, PhysicsBodyStore& bodyStore,
                                          const ColliderStore& colliderStore, PhysicsNarrowphaseWakeAccess wakeAccess,
                                          const PhysicsExecutionSettings& execution, Threading::WorkerPool& workerPool )
{

    if ( !input.Active() )
    {
        return;
    }

    const auto bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( hotFields );
    const int modelCount = (std::min)( { bodyStore.Count(), static_cast<int>( bodyRecords.size() ), colliderStore.Count(),
                                         static_cast<int>( input.exposureSeconds.size() ),
                                         static_cast<int>( input.repeatCooldownSeconds.size() ),
                                         wakeAccess.SleepRowCount() } );

    const auto applyAt = [&]( int index )
    {
        const std::size_t row = static_cast<std::size_t>( index );

        if ( hotFields.fixed[row] != 0u || wakeAccess.IsUnderwaterSleepLocked( index ) )
        {
            input.exposureSeconds[row] = 0.0f;
            input.repeatCooldownSeconds[row] = 0.0f;
            return;
        }

        const Vector3 position = PhysicsBodyPosition( hotRead, row );
        ExternalCylindricalForceField bestField;
        float bestAccelerationSq = 0.0f;
        Vector3 acceleration = SampleAcceleration( input, position, bestField, bestAccelerationSq );
        const float dx = position.x - bestField.center.x;
        const float dz = position.z - bestField.center.z;
        const float horizontal = sqrtf( dx * dx + dz * dz );
        const float height = (std::max)( bestField.heightMeters, 1.0f );
        const float height01 = ( position.y - bestField.center.y ) / height;

        if ( bestAccelerationSq <= TOLERANCE * TOLERANCE )
        {
            input.exposureSeconds[row] = 0.0f;
            input.repeatCooldownSeconds[row] = (std::max)( 0.0f, input.repeatCooldownSeconds[row] - input.stepSeconds );
            return;
        }

        if ( wakeAccess.IsSleeping( index ) )
        {
            wakeAccess.WakeBody( index );
        }

        Vector3 velocity = PhysicsBodyLinearVelocity( hotRead, row );
        input.exposureSeconds[row] += input.stepSeconds;
        input.repeatCooldownSeconds[row] = (std::max)( 0.0f, input.repeatCooldownSeconds[row] - input.stepSeconds );
        const float ejectBand = std::clamp( bestField.ejectHeightFraction, 0.0f, 1.0f );
        const float minExposure = (std::max)( 0.0f, bestField.minimumExposureSeconds );
        const float cooldown = (std::max)( 0.0f, bestField.repeatCooldownSeconds );
        const float maxDeltaVelocity = (std::max)( 1.0f, bestField.maxDeltaVelocityMetersPerSecond );
        const float minTangentialSpeed = (std::max)( 18.0f, bestField.tangentialAccelerationMetersPerSecondSquared * 0.12f );

        Vector3 outward;

        if ( horizontal > TOLERANCE )
        {
            outward = Vector3( dx / horizontal, 0.0f, dz / horizontal );
        }
        else
        {
            const Vector3 fallbackDirections[4] = { Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 1.0f ),
                                                    Vector3( -1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, -1.0f ) };

            outward = fallbackDirections[index & 3];
        }

        const Vector3 tangent( -outward.z, 0.0f, outward.x );
        const float tangentialSpeed = fabsf( velocity * tangent );
        const int exposureBucket = static_cast<int>( input.exposureSeconds[row] * EJECTION_PHASE_HZ );
        const bool deterministicSlot = ( ( index + exposureBucket ) % 3 ) == 0;

        if ( height01 >= ejectBand && input.exposureSeconds[row] >= minExposure &&
             input.repeatCooldownSeconds[row] <= 0.0f && tangentialSpeed >= minTangentialSpeed && deterministicSlot )
        {
            acceleration += outward * bestField.outwardEjectAccelerationMetersPerSecondSquared +
                            Vector3( 0.0f, bestField.upwardEjectAccelerationMetersPerSecondSquared, 0.0f );
            input.exposureSeconds[row] = 0.0f;
            input.repeatCooldownSeconds[row] = cooldown;
        }

        velocity += ClampVectorMagnitude( acceleration * input.stepSeconds, maxDeltaVelocity );
        hotFields.linearVelocityX[row] = velocity.x;
        hotFields.linearVelocityY[row] = velocity.y;
        hotFields.linearVelocityZ[row] = velocity.z;
    };

    if ( execution.parallel && input.parallelEvaluation )
    {
        workerPool.ParallelForNoAlloc( 0, modelCount, applyAt, PHYSICS_PARALLEL_MIN_BODIES, EXTERNAL_FORCE_WORKER_MARKER,
                                       PHYSICS_EXTERNAL_FORCE_WORKER_HASH );
    }
    else
    {

        for ( int index = 0; index < modelCount; ++index )
        {
            applyAt( index );
        }
    }
}

uint64_t ExternalForceStage::CollectMemoryBytes() const
{
    return m_fixedTreeReleaseWakeScratch.committed_bytes() + m_releaseWakeBodies.committed_bytes();
}

Vector3 ExternalForceStage::SampleAcceleration( const ExternalForceFrameInput& input, const Vector3& position,
                                                ExternalCylindricalForceField& outBestField,
                                                float& outBestAccelerationSq ) const
{
    Vector3 acceleration = ZERO_VECTOR;
    outBestField = input.fields.empty() ? ExternalCylindricalForceField {} : input.fields.front();
    outBestAccelerationSq = 0.0f;

    // Invariant: strict-best selection and left-to-right accumulation preserve
    // the pre-extraction ejection owner and exact floating-point witness.

    for ( const ExternalCylindricalForceField& field : input.fields )
    {
        const Vector3 sample = SampleFieldAcceleration( field, position );
        const float sampleSq = sample * sample;
        acceleration += sample;

        if ( sampleSq > outBestAccelerationSq )
        {
            outBestAccelerationSq = sampleSq;
            outBestField = field;
        }
    }

    return acceleration;
}
} // namespace SkullbonezCore::Physics
