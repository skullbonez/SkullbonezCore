/*
File: SceneLifecycle.h
Purpose:
  Defines the allocation-free value protocol for scene lifecycle publication.

Summary:
  SceneRuntime advances one monotonic generation for each accepted load attempt
  after preflight succeeds. Reactive owners compare that generation at fixed
  frame boundaries and apply each relevant phase at most once.

Glossary:
  Lifecycle generation: Monotonic identity for one accepted scene load attempt,
    independent of scene index, entity count, or successful activation.
  Lifecycle phase: Ordered commit edge describing how far the load transaction
    progressed before it completed or failed.
  Consumer receipt: Review-time bit proving synchronous legacy consumers ran at
    a phase; migrated reactive owners instead use the generation packet.

Invariants:
  - Generation zero means no accepted load has crossed the preflight boundary.
  - A generation advances before `BeforeSceneUnload` and never changes again for
    that attempt, including recoverable failures.
  - Packets contain values only: no callbacks, owner pointers, subscriber lists,
    or dynamically growing storage.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntime.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Reports/2026-07-22/owner-fanout-reduction-of0-census.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
enum class SceneRuntimeLifecycleEvent
{
    // Invariant: declaration order is transaction order because observers use
    // ordinal comparison to answer whether a required phase was reached.
    None,
    BeforeSceneUnload,
    AfterSceneCleared,
    BeforeScenePopulate,
    AfterScenePopulate,
    AfterSceneActivated,
};

struct SceneLifecycleBeginPolicy
{
    bool preserveUiState = false;
    bool preserveRuntimeState = false;
    bool suppressExitOnComplete = false;
    bool enterInteractiveRun = false;
    bool manualReset = false;
};

struct SceneLifecyclePacket
{
    uint64_t generation = 0;
    SceneRuntimeLifecycleEvent event = SceneRuntimeLifecycleEvent::None;
    SceneLifecycleBeginPolicy policy;
    int sceneIndex = -1;
    bool sceneMode = false;
};

constexpr bool SceneLifecycleReached( SceneRuntimeLifecycleEvent current, SceneRuntimeLifecycleEvent required )
{
    return static_cast<int>( current ) >= static_cast<int>( required );
}

// Concept: an owner keeps one observer per phase-specific action. The packet
// may be sampled repeatedly in one frame or across later frames; only the first
// sample of a new generation that reached the phase returns true.
class SceneLifecycleGenerationObserver
{
  public:
    bool ShouldApply( const SceneLifecyclePacket& packet, SceneRuntimeLifecycleEvent requiredEvent )
    {
        if ( packet.generation == 0 || packet.generation == m_lastAppliedGeneration ||
             !SceneLifecycleReached( packet.event, requiredEvent ) )
        {
            return false;
        }
        m_lastAppliedGeneration = packet.generation;
        return true;
    }

    uint64_t LastAppliedGeneration() const
    {
        return m_lastAppliedGeneration;
    }

  private:
    uint64_t m_lastAppliedGeneration = 0;
};

// Concept: lifecycle receipts make remaining synchronous cross-owner work
// auditable while OF2 migrates those owners to generation observation.
enum class SceneLifecycleConsumer : uint32_t
{
    Diagnostics = 1u << 0,
    RenderDevice = 1u << 1,
    Simulation = 1u << 2,
    Tools = 1u << 4,
    Interaction = 1u << 5,
    Replay = 1u << 6,
};
using SceneLifecycleConsumerMask = uint32_t;

constexpr SceneLifecycleConsumerMask SceneLifecycleConsumerBit( SceneLifecycleConsumer consumer )
{
    return static_cast<SceneLifecycleConsumerMask>( consumer );
}

constexpr SceneLifecycleConsumerMask SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent event )
{
    switch ( event )
    {
    case SceneRuntimeLifecycleEvent::BeforeSceneUnload:
        return SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics ) |
               SceneLifecycleConsumerBit( SceneLifecycleConsumer::RenderDevice );
    case SceneRuntimeLifecycleEvent::AfterSceneCleared:
        return SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics ) |
               SceneLifecycleConsumerBit( SceneLifecycleConsumer::Simulation ) |
               SceneLifecycleConsumerBit( SceneLifecycleConsumer::Tools ) |
               SceneLifecycleConsumerBit( SceneLifecycleConsumer::Interaction ) |
               SceneLifecycleConsumerBit( SceneLifecycleConsumer::Replay );
    case SceneRuntimeLifecycleEvent::AfterSceneActivated:
        return SceneLifecycleConsumerBit( SceneLifecycleConsumer::Replay );
    case SceneRuntimeLifecycleEvent::BeforeScenePopulate:
    case SceneRuntimeLifecycleEvent::AfterScenePopulate:
    case SceneRuntimeLifecycleEvent::None:
        return 0;
    }
    return 0;
}

// A new generation resets the previous event to None. Within that generation,
// phases are strictly ordered and may never restart or skip a commit edge.
constexpr bool SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent previous,
                                                     SceneRuntimeLifecycleEvent next )
{
    return ( previous == SceneRuntimeLifecycleEvent::None && next == SceneRuntimeLifecycleEvent::BeforeSceneUnload ) ||
           ( previous == SceneRuntimeLifecycleEvent::BeforeSceneUnload &&
             next == SceneRuntimeLifecycleEvent::AfterSceneCleared ) ||
           ( previous == SceneRuntimeLifecycleEvent::AfterSceneCleared &&
             next == SceneRuntimeLifecycleEvent::BeforeScenePopulate ) ||
           ( previous == SceneRuntimeLifecycleEvent::BeforeScenePopulate &&
             next == SceneRuntimeLifecycleEvent::AfterScenePopulate ) ||
           ( previous == SceneRuntimeLifecycleEvent::AfterScenePopulate &&
             next == SceneRuntimeLifecycleEvent::AfterSceneActivated );
}

const char* SceneRuntimeLifecycleEventName( SceneRuntimeLifecycleEvent event );
} // namespace Runtime
} // namespace SkullbonezCore
