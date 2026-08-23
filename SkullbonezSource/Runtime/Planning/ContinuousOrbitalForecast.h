/*
File: ContinuousOrbitalForecast.h
Purpose:
  Declares the Planning-owned continuous orbital forecast lifecycle.

Summary:
  One product owner composes the lower private-Physics producer with the
  Planning stability analyzer, converts complete worker ticks into the authored
  cohort's bounded values, and publishes detached operator status. A separate
  double-buffered presentation owner converts logical ring rows into generic
  Rendering ribbons and coherent authored-color head markers. App resolves the
  authored member colors before Start, so Planning never borrows Scene storage.

Glossary:
  Window age: Simulated seconds between the oldest and newest retained complete
    path rows; it saturates at the fixed rolling-window duration.
  Simulation rate: Simulated seconds produced per real second, derived from the
    producer's measured fixed ticks per millisecond.
  Presentation bank: One fixed-capacity record set packed off to the side and
    made visible only after every configured member reaches the same newest tick.

Invariants:
  - Start seeds both producer and analyzer from one authoritative live snapshot.
  - Stop joins the worker before contract or analyzer state is cleared.
  - Complete-tick observation retains at most the 16 authored members and 120
    distinct configured-member contact pairs on the worker stack.
  - No UI, Input, App, ReplayRuntime, or bounded PREDICT owner crosses this seam.
  - Presentation banks and source-row scratch storage allocate at construction;
    ring sampling and packet publication perform no runtime heap growth.
  - Logical oldest-to-newest sampling never joins the rolling ring's physical
    seam, and all configured-body heads share one newest absolute tick.

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
#include "../../Rendering/RenderCommandTypes.h"

#include <array>
#include <chrono>
#include <memory>
#include <span>

namespace SkullbonezCore::Runtime
{
struct ContinuousOrbitalPresentationMember
{
    std::size_t bodyRow = 0u;
    std::uint64_t identity = 0u;
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
};

struct ContinuousOrbitalPresentationView
{
    std::uint64_t oldestAbsoluteTick = 0u;
    std::uint64_t newestAbsoluteTick = 0u;
    std::uint64_t revision = 0u;
    std::size_t sourceRowCount = 0u;
    std::size_t ribbonSegmentCount = 0u;
    std::size_t headMarkerCount = 0u;
    std::size_t retainedBytes = 0u;
    bool coherent = false;
    bool wrapped = false;
};

// Planning owns the sampling and presentation meaning. Two construction-time
// banks make a failed concurrent ring read harmless: only a completely packed
// chronological bank becomes visible to the render frame.
class ContinuousOrbitalPresentation final
{
  public:
    static constexpr std::size_t FLOATS_PER_RIBBON_RECORD = 19u;
    static constexpr std::size_t RIBBON_RECORD_CAPACITY = 24000u;
    static constexpr std::size_t LINE_FLOAT_CAPACITY = Scene::ORBITAL_STABILITY_MEMBER_CAPACITY * 36u;

    ContinuousOrbitalPresentation();
    ContinuousOrbitalPresentation( const ContinuousOrbitalPresentation& ) = delete;
    ContinuousOrbitalPresentation& operator=( const ContinuousOrbitalPresentation& ) = delete;

    bool Begin( std::span<const ContinuousOrbitalPresentationMember> members, std::size_t sourceBodyCount ) noexcept;
    void Reset() noexcept;
    bool Publish( const ContinuousPredictionSampleRingSnapshot& samples ) noexcept;
    Rendering::RetainedGeometryPacket Packet() const noexcept;
    ContinuousOrbitalPresentationView View() const noexcept;

  private:
    static constexpr std::uint64_t STREAM_IDENTITY = 0x434F4E544F524249ull;

    struct BankState
    {
        std::size_t ribbonFloatCount = 0u;
        std::size_t rangeCount = 0u;
        std::size_t lineFloatCount = 0u;
        std::size_t ribbonSegmentCount = 0u;
        std::size_t headMarkerCount = 0u;
        std::uint64_t oldestAbsoluteTick = 0u;
        std::uint64_t newestAbsoluteTick = 0u;
        std::size_t sourceRowCount = 0u;
        bool coherent = false;
    };

    std::array<ContinuousOrbitalPresentationMember, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> m_members = {};
    std::array<std::unique_ptr<float[]>, 2u> m_records;
    std::unique_ptr<Math::Vector::Vector3[]> m_rowPositions;
    std::array<std::array<Rendering::RetainedGeometryRangeToken, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY>, 2u> m_ranges = {};
    std::array<std::array<float, LINE_FLOAT_CAPACITY>, 2u> m_lines = {};
    std::array<BankState, 2u> m_banks = {};
    std::size_t m_memberCount = 0u;
    std::size_t m_sourceBodyCount = 0u;
    std::size_t m_publishedBank = 0u;
    std::uint64_t m_revision = 0u;
    bool m_storageReady = false;
    bool m_configured = false;
};

struct ContinuousOrbitalForecastView
{
    ContinuousOrbitalStabilityView stability;
    ContinuousOrbitalPresentationView presentation;
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
    explicit ContinuousOrbitalForecast( Core::Profiler* profiler = nullptr ) noexcept;
    ~ContinuousOrbitalForecast();
    ContinuousOrbitalForecast( const ContinuousOrbitalForecast& ) = delete;
    ContinuousOrbitalForecast& operator=( const ContinuousOrbitalForecast& ) = delete;

    // Caller contract: App disables bounded PREDICT before Start or Reset and
    // calls Stop before scene state or this owner can be retired.
    bool Start( const Physics::PhysicsEngine& liveEngine, const Gameplay::TornadoGameplay& liveTornado,
                const Core::EngineConfig& config, const Physics::PhysicsWorldForces& worldForces,
                Threading::WorkerPool& workerPool, const Scene::OrbitalStabilityContract& contract,
                std::span<const ContinuousOrbitalPresentationMember> presentationMembers );
    bool Reset( const Physics::PhysicsEngine& liveEngine, const Gameplay::TornadoGameplay& liveTornado,
                const Core::EngineConfig& config, const Physics::PhysicsWorldForces& worldForces,
                Threading::WorkerPool& workerPool, const Scene::OrbitalStabilityContract& contract,
                std::span<const ContinuousOrbitalPresentationMember> presentationMembers );
    void Stop() noexcept;
    bool AdvanceFrame( const std::chrono::steady_clock::time_point& frameBudgetStart ) noexcept;
    Rendering::RetainedGeometryPacket PreparePresentation() noexcept;
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
    ContinuousOrbitalPresentation m_presentation;
    bool m_available = false;
    bool m_failed = false;
};
} // namespace SkullbonezCore::Runtime
