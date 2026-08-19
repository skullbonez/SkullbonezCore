/*
File: ContinuousOrbitalForecast.h
Purpose:
  Declares the Planning-owned continuous orbital forecast lifecycle.

Summary:
  One product owner composes the lower private-Physics producer with the
  Planning stability analyzer, converts complete worker ticks into the authored
  cohort's bounded values, and publishes detached operator status.

Glossary:
  Window age: Simulated seconds between the oldest and newest retained complete
    path rows; it saturates at the fixed rolling-window duration.
  Simulation rate: Simulated seconds produced per real second, derived from the
    producer's measured fixed ticks per millisecond.

Invariants:
  - Start seeds both producer and analyzer from one authoritative live snapshot.
  - Stop joins the worker before contract or analyzer state is cleared.
  - Complete-tick observation retains at most the 16 authored members and 120
    distinct configured-member contact pairs on the worker stack.
  - No UI, Input, App, ReplayRuntime, or bounded PREDICT owner crosses this seam.

Related:
  - SkullbonezSource/Runtime/Prediction/ContinuousPredictionProducer.h
  - SkullbonezSource/Runtime/Planning/ContinuousOrbitalStability.h
  - SkullbonezSource/Scene/OrbitalStabilityContract.h
  - SkullbonezTests/TestContinuousPredictionProducer.cpp
  - SkullbonezTests/TestContinuousOrbitalStability.cpp
*/
#pragma once

#include "ContinuousOrbitalStability.h"
#include "../Prediction/ContinuousPredictionProducer.h"

#include <array>
#include <chrono>

namespace SkullbonezCore::Runtime
{
struct ContinuousOrbitalForecastView
{
    ContinuousOrbitalStabilityView stability;
    std::uint64_t newestAbsoluteTick = 0u;
    double simulatedSeconds = 0.0;
    double simulatedSecondsPerRealSecond = 0.0;
    double rollingWindowAgeSeconds = 0.0;
    std::size_t retainedBytes = 0u;
    bool available = false;
    bool active = false;
    bool workerInFlight = false;
    bool failed = false;
};

class ContinuousOrbitalForecast final : private ContinuousPredictionTickObserver
{
  public:
    explicit ContinuousOrbitalForecast( Core::Profiler* profiler = nullptr ) noexcept : m_producer( profiler )
    {
    }
    ~ContinuousOrbitalForecast();
    ContinuousOrbitalForecast( const ContinuousOrbitalForecast& ) = delete;
    ContinuousOrbitalForecast& operator=( const ContinuousOrbitalForecast& ) = delete;

    // Caller contract: App disables bounded PREDICT before Start or Reset and
    // calls Stop before scene state or this owner can be retired.
    bool Start( const Physics::PhysicsEngine& liveEngine, const Gameplay::TornadoGameplay& liveTornado,
                const Core::EngineConfig& config, const Physics::PhysicsWorldForces& worldForces,
                Threading::WorkerPool& workerPool, const Scene::OrbitalStabilityContract& contract );
    bool Reset( const Physics::PhysicsEngine& liveEngine, const Gameplay::TornadoGameplay& liveTornado,
                const Core::EngineConfig& config, const Physics::PhysicsWorldForces& worldForces,
                Threading::WorkerPool& workerPool, const Scene::OrbitalStabilityContract& contract );
    void Stop() noexcept;
    bool AdvanceFrame( const std::chrono::steady_clock::time_point& frameBudgetStart ) noexcept;
    ContinuousOrbitalForecastView View() const noexcept;

  private:
    static constexpr std::size_t CONTACT_CAPACITY = Scene::ORBITAL_STABILITY_MEMBER_CAPACITY *
                                                    ( Scene::ORBITAL_STABILITY_MEMBER_CAPACITY - 1u ) / 2u;

    void ObserveCompleteContinuousPredictionTick( const Physics::PhysicsBodyStore& bodies,
                                                  std::span<const Physics::PersistentContact> contacts,
                                                  std::uint64_t absoluteTick ) noexcept override;
    void ObserveInvalidContinuousPredictionPublication( std::uint64_t absoluteTick ) noexcept override;
    bool CaptureConfiguredBodies( const Physics::PhysicsBodyStore& bodies,
                                  std::array<ContinuousOrbitalBodySample, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY>& out,
                                  std::size_t& outCount ) const noexcept;
    bool IsConfiguredMember( Physics::PhysicsSceneObjectId id ) const noexcept;

    Scene::OrbitalStabilityContract m_contract;
    ContinuousOrbitalStabilityAnalyzer m_stability;
    ContinuousPredictionProducer m_producer;
    bool m_available = false;
    bool m_failed = false;
};
} // namespace SkullbonezCore::Runtime
