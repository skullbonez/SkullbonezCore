/*
File: RuntimeFrameMetricsOwner.h
Purpose:
  Owns process and scene timing plus fixed-cadence frame metric aggregation.

Summary:
  App invokes explicit startup, frame, scene, and publication operations at
  stable frame boundaries. Consumers receive a detached snapshot and cannot
  make metric cadence depend on whether GameUI or ImGui happened to draw.

Invariants:
  - Frame samples are admitted exactly once by App before optional UI work.
  - Half-second aggregation uses submitted frame duration, not presentation visibility.
  - Scene clear resets published measurements; scene activation restarts clocks.

Related:
  - Runtime/App/RunFrame.cpp
  - Runtime/RuntimeFrameViews.h
  - Runtime/App/SceneLoadApplication.h
*/
#pragma once

#include "../../Core/Timer.h"
#include "../RuntimeFrameViews.h"

#include <algorithm>

namespace SkullbonezCore::Runtime
{
struct RuntimeFrameMetricSample
{
    double secondsPerFrame = 0.0;
    double sceneEnergy = 0.0;
};

class RuntimeFrameMetricsOwner
{
  public:
    Core::SbResult Initialise( Core::SbDiagnosticStore& diagnostics )
    {
        Core::SbResult result = m_frameTimer.Initialise( diagnostics );

        if ( !result.Ok() )
        {
            return result;
        }

        result = m_workTimer.Initialise( diagnostics );

        if ( !result.Ok() )
        {
            return result;
        }

        return m_simulationTimer.Initialise( diagnostics );
    }

    double BeginFrame()
    {
        const double elapsed = m_frameTimer.GetElapsedTime();
        m_frameTimer.StartTimer();
        m_workTimer.StartTimer();
        return elapsed;
    }

    void FinishFrameWork()
    {
        m_workTimer.StopTimer();
        m_snapshot.cpuFrameWorkMs = static_cast<float>( std::clamp( m_workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );
    }

    void FinishPresentedFrame()
    {
        m_frameTimer.StopTimer();
    }

    void RecordProfilerSample( float physicsSeconds, float renderSeconds, float gpuFrameWorkMs )
    {
        m_snapshot.physicsSeconds = physicsSeconds;
        m_snapshot.renderSeconds = renderSeconds;
        m_snapshot.gpuFrameWorkMs = gpuFrameWorkMs;
    }

    void SampleFrame( const RuntimeFrameMetricSample& sample )
    {
        m_snapshot.secondsPerFrame = sample.secondsPerFrame;
        m_elapsedAggregationSeconds += static_cast<float>( sample.secondsPerFrame );
        m_sceneEnergyAccumulator += sample.sceneEnergy;
        ++m_sceneEnergySampleCount;

        // Invariant: preserve the existing strictly-greater-than half-second
        // publication boundary while making it independent of UI visibility.
        if ( m_elapsedAggregationSeconds > 0.5f )
        {
            m_snapshot.rollingFrameSeconds = static_cast<float>( sample.secondsPerFrame );
            m_snapshot.rollingPhysicsSeconds = m_snapshot.physicsSeconds;
            m_snapshot.rollingRenderSeconds = m_snapshot.renderSeconds;
            m_snapshot.sceneEnergy = m_sceneEnergySampleCount > 0
                                         ? static_cast<float>( m_sceneEnergyAccumulator /
                                                               static_cast<double>( m_sceneEnergySampleCount ) )
                                         : 0.0f;
            m_hasPublishedAggregate = true;
            m_elapsedAggregationSeconds = 0.0f;
            m_sceneEnergyAccumulator = 0.0;
            m_sceneEnergySampleCount = 0;
        }
        else if ( !m_hasPublishedAggregate )
        {
            // First-frame diagnostics remain useful before the first aggregate publishes.
            m_snapshot.sceneEnergy = static_cast<float>( m_sceneEnergyAccumulator /
                                                         static_cast<double>( m_sceneEnergySampleCount ) );
        }
    }

    void RecordUiDrawCalls( int drawCalls )
    {
        m_snapshot.uiDrawCalls = drawCalls;
    }

    void ResetMeasurements()
    {
        m_snapshot = {};
        m_elapsedAggregationSeconds = 0.0f;
        m_sceneEnergyAccumulator = 0.0;
        m_sceneEnergySampleCount = 0;
        m_hasPublishedAggregate = false;
    }

    void RestartClocks()
    {
        m_frameTimer.StartTimer();
        m_workTimer.StartTimer();
        m_simulationTimer.StartTimer();
    }

    RuntimeFrameMetricsSnapshot Publish()
    {
        RuntimeFrameMetricsSnapshot published = m_snapshot;
        published.simulationTotalSeconds = m_simulationTimer.GetTotalTime();
        published.sceneElapsedSeconds = m_simulationTimer.GetTimeSinceLastStart();
        return published;
    }

    RuntimeFrameMetricsSnapshot MetricsSnapshot() const
    {
        return m_snapshot;
    }

    double SimulationTotalSeconds()
    {
        return m_simulationTimer.GetTotalTime();
    }
    double SceneElapsedSeconds()
    {
        return m_simulationTimer.GetTimeSinceLastStart();
    }
    void RestartSceneClock()
    {
        m_simulationTimer.StartTimer();
    }

  private:
    Environment::Timer m_frameTimer;
    Environment::Timer m_workTimer;
    Environment::Timer m_simulationTimer;
    RuntimeFrameMetricsSnapshot m_snapshot;
    float m_elapsedAggregationSeconds = 0.0f;
    double m_sceneEnergyAccumulator = 0.0;
    int m_sceneEnergySampleCount = 0;
    bool m_hasPublishedAggregate = false;
};
} // namespace SkullbonezCore::Runtime
