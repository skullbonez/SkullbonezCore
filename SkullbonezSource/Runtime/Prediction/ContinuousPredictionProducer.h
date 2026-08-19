/*
File: ContinuousPredictionProducer.h
Purpose:
  Declares the isolated fixed-tick producer used by continuous forecasts.

Summary:
  The producer snapshots one authoritative Physics engine into a private engine,
  advances that private engine in worker-local five-millisecond slices, and
  publishes complete all-body position rows through a fixed sample ring. One
  optional typed observer may inspect each complete private tick synchronously;
  no full-state history crosses the lower publication boundary.

Glossary:
  Continuous slice: One worker submission that advances whole fixed ticks until
    the worker-local wall-clock budget expires.
  Detached view: Value/status publication plus a read-only ring snapshot; it
    exposes no private Physics, tornado, schedule, or bounded PREDICT state.

Invariants:
  - Begin is the only seed boundary and completes every reserve before Start.
  - Worker stepping reaches only producer-owned Physics and gameplay values.
  - The worker has no finite target tick and no deterministic-capture tick cap.
  - Stop requests cancellation and joins the worker before retiring publication.
  - Bounded ReplayPrediction state is neither accepted nor reachable here.
  - The optional tick observer is borrowed only until Stop joins the worker.

Related:
  - SkullbonezSource/Runtime/Prediction/ContinuousPredictionSampleRing.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h
  - SkullbonezTests/TestContinuousPredictionProducer.cpp
  - Agentic/Reference/runtime-reference.md
*/
#pragma once

#include "ContinuousPredictionSampleRing.h"

#include "../../Gameplay/TornadoGameplay.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsSolverSnapshot.h"
#include "../../Physics/PhysicsWorldForces.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
} // namespace Core
namespace Physics
{
class PhysicsEngine;
struct PersistentContact;
} // namespace Physics
namespace Threading
{
class WorkerPool;
}
namespace Runtime
{
constexpr double CONTINUOUS_PREDICTION_WORKER_BUDGET_MILLISECONDS = 5.0;
constexpr double CONTINUOUS_PREDICTION_WINDOW_SECONDS = 120.0;

std::size_t ContinuousPredictionWindowRowCapacity() noexcept;

struct ContinuousPredictionProducerView
{
    ContinuousPredictionSampleRingSnapshot samples;
    std::uint64_t newestAbsoluteTick = 0u;
    double simulatedSeconds = 0.0;
    double measuredTicksPerMillisecond = 0.0;
    std::size_t retainedBytes = 0u;
    bool active = false;
    bool workerInFlight = false;
    bool failed = false;
};

struct ContinuousPredictionBodySeed
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 inverseRotationalInertia = Math::Vector::ZERO_VECTOR;
    bool fixed = false;
};

// Lifetime: Planning may implement this narrow synchronous observation seam.
// The producer retains the borrow only while active and clears it after join;
// observer calls never expose the private engine or survive the worker tick.
class ContinuousPredictionTickObserver
{
  public:
    virtual ~ContinuousPredictionTickObserver() = default;
    virtual void ObserveCompleteContinuousPredictionTick( const Physics::PhysicsBodyStore& bodies,
                                                          std::span<const Physics::PersistentContact> contacts,
                                                          std::uint64_t absoluteTick ) noexcept = 0;
    virtual void ObserveInvalidContinuousPredictionPublication( std::uint64_t absoluteTick ) noexcept = 0;
};

class ContinuousPredictionProducer;

// Lifetime: this fixed task is embedded in its producer. Stop and destruction
// join it before any borrowed owner or WorkerPool can be retired.
class ContinuousPredictionWorkerTask
{
  public:
    void Configure( ContinuousPredictionProducer& producer, Threading::WorkerPool& workerPool ) noexcept;
    bool Submit() noexcept;
    void WaitForIdle() const noexcept;
    bool InFlight() const noexcept
    {
        return m_inFlight.load( std::memory_order_acquire );
    }
    void ExecuteWorkerTask() noexcept;

  private:
    ContinuousPredictionProducer* m_producer = nullptr;
    Threading::WorkerPool* m_workerPool = nullptr;
    std::atomic<bool> m_inFlight { false };
};

class ContinuousPredictionProducer
{
  public:
    explicit ContinuousPredictionProducer( Core::Profiler* profiler = nullptr ) noexcept : m_profiler( profiler )
    {
    }
    ~ContinuousPredictionProducer();
    ContinuousPredictionProducer( const ContinuousPredictionProducer& ) = delete;
    ContinuousPredictionProducer& operator=( const ContinuousPredictionProducer& ) = delete;

    bool Begin( const Physics::PhysicsEngine& liveEngine, const Gameplay::TornadoGameplay& liveTornado,
                const Core::EngineConfig& config, const Physics::PhysicsWorldForces& worldForces,
                Threading::WorkerPool& workerPool, std::size_t rowCapacity = ContinuousPredictionWindowRowCapacity(),
                ContinuousPredictionTickObserver* tickObserver = nullptr );

    // The frame-side budget is independent from the worker-local slice clock,
    // matching bounded PREDICT's ratified dual-check semantics.
    bool AdvanceFrame( const std::chrono::steady_clock::time_point& frameBudgetStart,
                       double frameBudgetMilliseconds = CONTINUOUS_PREDICTION_WORKER_BUDGET_MILLISECONDS ) noexcept;

    void Stop() noexcept;
    ContinuousPredictionProducerView View() const noexcept;

  private:
    friend class ContinuousPredictionWorkerTask;

    bool CaptureSeed( const Physics::PhysicsEngine& liveEngine );
    bool SeedPrivateEngine( const Physics::PhysicsEngine& liveEngine, const Core::EngineConfig& config,
                            const Physics::PhysicsWorldForces& worldForces );
    bool CapturePositionRow( std::uint64_t absoluteTick ) noexcept;
    void RunWorkerSlice( Threading::WorkerPool& workerPool ) noexcept;
    void MarkFailed() noexcept;

    Core::Profiler* m_profiler = nullptr;
    std::unique_ptr<Physics::PhysicsEngine> m_engine;
    int m_engineReserveBytes = 0;
    int m_modelCount = 0;
    Gameplay::TornadoGameplay m_tornadoGameplay;
    Physics::PhysicsWorldForces m_worldForces;
    Physics::PhysicsSolverSnapshot m_solverSnapshot;
    std::vector<ContinuousPredictionBodySeed> m_bodySeeds;
    ContinuousPredictionSampleRing m_samples;
    ContinuousPredictionWorkerTask m_workerTask;
    ContinuousPredictionTickObserver* m_tickObserver = nullptr;
    std::atomic<std::uint64_t> m_newestAbsoluteTick { 0u };
    std::atomic<double> m_measuredTicksPerMillisecond { 0.0 };
    std::atomic<bool> m_active { false };
    std::atomic<bool> m_cancelRequested { false };
    std::atomic<bool> m_failed { false };
};
} // namespace Runtime
} // namespace SkullbonezCore
