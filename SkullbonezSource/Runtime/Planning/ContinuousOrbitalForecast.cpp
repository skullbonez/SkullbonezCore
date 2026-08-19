/*
File: ContinuousOrbitalForecast.cpp
Purpose:
  Implements the Planning-owned continuous orbital forecast lifecycle.

Summary:
  Start admits one authored scene contract and live seed. Worker callbacks copy
  only configured members plus distinct configured contact pairs into bounded
  values before updating the mutex-protected stability analyzer.

Invariants:
  - Contract mutation happens only after the producer worker has joined.
  - A configured body appears exactly once in every analyzer publication.
  - Contact duplication cannot overflow the authored-pair stack capacity.

Related:
  - ContinuousOrbitalForecast.h
  - SkullbonezSource/Physics/PhysicsDiagnosticsView.h
  - SkullbonezSource/Physics/PhysicsBodyStore.h
*/
#include "ContinuousOrbitalForecast.h"

#include "../../Physics/PhysicsDiagnosticsView.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>

namespace SkullbonezCore::Runtime
{
ContinuousOrbitalForecast::~ContinuousOrbitalForecast()
{
    Stop();
}

bool ContinuousOrbitalForecast::Start( const Physics::PhysicsEngine& liveEngine,
                                       const Gameplay::TornadoGameplay& liveTornado, const Core::EngineConfig& config,
                                       const Physics::PhysicsWorldForces& worldForces, Threading::WorkerPool& workerPool,
                                       const Scene::OrbitalStabilityContract& contract )
{
    Stop();
    m_contract = contract;
    m_failed = false;
    m_available = contract.enabled && worldForces.mutualGravity.enabled && contract.memberCount > 0u &&
                  contract.memberCount <= Scene::ORBITAL_STABILITY_MEMBER_CAPACITY;

    std::array<ContinuousOrbitalBodySample, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> seedBodies = {};
    std::size_t seedBodyCount = 0u;

    if ( !m_available ||
         !CaptureConfiguredBodies( Physics::PhysicsEngine::ReadBodies( liveEngine ), seedBodies, seedBodyCount ) ||
         !m_stability.Begin( contract, static_cast<double>( worldForces.mutualGravity.gravitationalConstant ),
                             static_cast<double>( worldForces.mutualGravity.softeningLength ),
                             std::span<const ContinuousOrbitalBodySample>( seedBodies.data(), seedBodyCount ) ) ||
         !m_producer.Begin( liveEngine, liveTornado, config, worldForces, workerPool,
                            ContinuousPredictionWindowRowCapacity(), this ) )
    {
        m_producer.Stop();
        m_stability.Reset();
        m_failed = true;
        return false;
    }

    return true;
}

bool ContinuousOrbitalForecast::Reset( const Physics::PhysicsEngine& liveEngine,
                                       const Gameplay::TornadoGameplay& liveTornado, const Core::EngineConfig& config,
                                       const Physics::PhysicsWorldForces& worldForces, Threading::WorkerPool& workerPool,
                                       const Scene::OrbitalStabilityContract& contract )
{
    return Start( liveEngine, liveTornado, config, worldForces, workerPool, contract );
}

void ContinuousOrbitalForecast::Stop() noexcept
{
    m_producer.Stop();
    m_stability.Reset();
    m_contract = {};
    m_available = false;
    m_failed = false;
}

bool ContinuousOrbitalForecast::AdvanceFrame( const std::chrono::steady_clock::time_point& frameBudgetStart ) noexcept
{
    return m_producer.AdvanceFrame( frameBudgetStart );
}

ContinuousOrbitalForecastView ContinuousOrbitalForecast::View() const noexcept
{
    const ContinuousPredictionProducerView producer = m_producer.View();
    ContinuousOrbitalForecastView view;
    view.stability = m_stability.View();
    view.newestAbsoluteTick = producer.newestAbsoluteTick;
    view.simulatedSeconds = producer.simulatedSeconds;
    view.simulatedSecondsPerRealSecond = producer.measuredTicksPerMillisecond * 1000.0 *
                                         static_cast<double>( PHYSICS_FIXED_DT );
    view.rollingWindowAgeSeconds = producer.samples.RowCount() > 1u
                                       ? static_cast<double>( producer.samples.NewestAbsoluteTick() -
                                                              producer.samples.OldestAbsoluteTick() ) *
                                             static_cast<double>( PHYSICS_FIXED_DT )
                                       : 0.0;
    view.retainedBytes = producer.retainedBytes;
    view.available = m_available;
    view.active = producer.active;
    view.workerInFlight = producer.workerInFlight;
    view.failed = m_failed || producer.failed;
    return view;
}

void ContinuousOrbitalForecast::ObserveCompleteContinuousPredictionTick( const Physics::PhysicsBodyStore& bodies, std::span<const Physics::PersistentContact> contacts,
                                                                         std::uint64_t absoluteTick ) noexcept
{
    std::array<ContinuousOrbitalBodySample, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> bodySamples = {};
    std::array<ContinuousOrbitalContactSample, CONTACT_CAPACITY> contactSamples = {};
    std::size_t bodySampleCount = 0u;
    std::size_t contactSampleCount = 0u;

    if ( !CaptureConfiguredBodies( bodies, bodySamples, bodySampleCount ) )
    {
        ObserveInvalidContinuousPredictionPublication( absoluteTick );
        return;
    }

    for ( const Physics::PersistentContact& contact : contacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 )
        {
            continue;
        }

        const Physics::PhysicsBodyRecord* bodyA = bodies.RecordForHandle( bodies.HandleForModelIndex( contact.bodyA ) );
        const Physics::PhysicsBodyRecord* bodyB = bodies.RecordForHandle( bodies.HandleForModelIndex( contact.bodyB ) );

        if ( !bodyA || !bodyB || !IsConfiguredMember( bodyA->sceneObjectId ) ||
             !IsConfiguredMember( bodyB->sceneObjectId ) || bodyA->sceneObjectId == bodyB->sceneObjectId )
        {
            continue;
        }

        ContinuousOrbitalContactSample candidate { bodyA->sceneObjectId, bodyB->sceneObjectId };

        if ( candidate.bodyB.value < candidate.bodyA.value )
        {
            std::swap( candidate.bodyA, candidate.bodyB );
        }

        const bool duplicate = std::find_if( contactSamples.begin(), contactSamples.begin() + contactSampleCount,
                                             [&]( const ContinuousOrbitalContactSample& existing )
                                             {
                                                 return existing.bodyA == candidate.bodyA &&
                                                        existing.bodyB == candidate.bodyB;
                                             } ) != contactSamples.begin() + contactSampleCount;

        if ( !duplicate && contactSampleCount < contactSamples.size() )
        {
            contactSamples[contactSampleCount++] = candidate;
        }
    }

    (void)m_stability.ObserveTick( { std::span<const ContinuousOrbitalBodySample>( bodySamples.data(), bodySampleCount ),
                                     std::span<const ContinuousOrbitalContactSample>( contactSamples.data(), contactSampleCount ), absoluteTick, true,
                                     true } );
}

void ContinuousOrbitalForecast::ObserveInvalidContinuousPredictionPublication( std::uint64_t absoluteTick ) noexcept
{
    (void)m_stability.ObserveTick( { {}, {}, absoluteTick, true, false } );
}

bool ContinuousOrbitalForecast::CaptureConfiguredBodies( const Physics::PhysicsBodyStore& bodies,
                                                         std::array<ContinuousOrbitalBodySample, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY>& out,
                                                         std::size_t& outCount ) const noexcept
{
    outCount = 0u;

    if ( m_contract.memberCount == 0u || m_contract.memberCount > out.size() )
    {
        return false;
    }

    const Physics::PhysicsBodyHotFieldsConstView hot = bodies.HotFields();

    for ( std::size_t memberIndex = 0u; memberIndex < m_contract.memberCount; ++memberIndex )
    {
        const Physics::PhysicsSceneObjectId id = m_contract.members[memberIndex].sceneObjectId;
        const Physics::PhysicsBodyHandle handle = bodies.HandleForSceneObjectId( id );
        const Physics::PhysicsBodyRecord* record = bodies.RecordForHandle( handle );
        const int modelIndex = bodies.ModelIndexForHandle( handle );

        if ( !record || modelIndex < 0 || static_cast<std::size_t>( modelIndex ) >= hot.positionX.size() ||
             outCount >= out.size() )
        {
            return false;
        }

        const std::size_t row = static_cast<std::size_t>( modelIndex );
        out[outCount++] = { id,
                            Physics::PhysicsBodyPosition( hot, row ),
                            Physics::PhysicsBodyOrientation( hot, row ),
                            Physics::PhysicsBodyLinearVelocity( hot, row ),
                            Physics::PhysicsBodyAngularVelocity( hot, row ),
                            static_cast<double>( record->mass ) };
    }

    return outCount == m_contract.memberCount;
}

bool ContinuousOrbitalForecast::IsConfiguredMember( Physics::PhysicsSceneObjectId id ) const noexcept
{
    if ( m_contract.memberCount > m_contract.members.size() )
    {
        return false;
    }

    for ( std::size_t index = 0u; index < m_contract.memberCount; ++index )
    {
        if ( m_contract.members[index].sceneObjectId == id )
        {
            return true;
        }
    }

    return false;
}
} // namespace SkullbonezCore::Runtime
