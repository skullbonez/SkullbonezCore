/*
File: SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp
Purpose:
  Owns DX12 timing-query brackets and publishes completed GPU samples.

Summary:
  Rendering translates Core profiler marker identities into backend query
  slots, reads completed results, and publishes renderer counters to Tracy.
  Operator-facing layout and labels live in UIProfilerOverlayPresenter.

Glossary:
  Render GPU timing owner: Renderer lifecycle object that owns query brackets.
  Completed sample: Hash plus milliseconds returned from a finished GPU query.
  Marker epoch: Core generation that invalidates stale backend query slots.
  Lane F: Fatal invariant path for an unbalanced renderer timing stack.

Invariants:
  - GPU begin/end scopes balance before frame or device boundaries.
  - Marker epochs invalidate stale backend query slots before reuse.
  - This owner publishes measurements only; it has no UI layout policy.

Related:
  - SkullbonezSource/Core/Profiler.cpp
  - SkullbonezSource/Rendering/RenderGpuTimingOwner.h
  - SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
*/
#include "RenderGpuTimingOwner.h"

#include "../Core/FatalError.h"
#include "../Core/PlatformProfiler.h"
#include "../Core/Profiler.h"
#include "../Core/TracyClientOwner.h"
#include "../Core/WorkerPool.h"
#include "DX12/Dx12Diagnostics.h"

using namespace SkullbonezCore::Core;
using namespace SkullbonezCore::Rendering;

#if defined( SKULLBONEZ_PROFILE_ENABLED )

RenderGpuTimingOwner::RenderGpuTimingOwner( Core::Profiler* profiler, Dx12Diagnostics* diagnostics )
    : m_profiler( profiler ), m_diagnostics( diagnostics ), m_markerEpoch( profiler ? profiler->MarkerEpoch() : 0 )
{
}


void RenderGpuTimingOwner::BeginFrame()
{
    if ( !m_profiler || !m_diagnostics )
    {
        return;
    }

    if ( m_openDepth != 0 )
    {
        SB_FATAL( "RenderGpuTimingOwner", "frame boundary reached with %d open GPU range(s)", m_openDepth );
    }

    // Invariant: Core clears marker identities only at FrameBegin. Mirror that
    // epoch before reading query slots so an old index can never populate a new
    // marker row after a profiler reset.
    if ( m_markerEpoch != m_profiler->MarkerEpoch() )
    {
        m_diagnostics->GpuTimerInvalidate();
        m_markerEpoch = m_profiler->MarkerEpoch();
    }

    int completedCount = 0;
    const Core::Profiler::ProfilerFrameView frame = m_profiler->FrameView();
    if ( m_diagnostics->GetCapabilities().supportsGpuTimers )
    {
        for ( std::size_t markerIndex = 0; markerIndex < frame.markers.size(); ++markerIndex )
        {
            const Core::Profiler::Marker& marker = frame.markers[markerIndex];
            if ( !marker.hasGpu )
            {
                continue;
            }

            float milliseconds = 0.0f;
            if ( m_diagnostics->GpuTimerRead( static_cast<int>( markerIndex ), milliseconds ) )
            {
                m_completedSamples[completedCount++] = { marker.hash, milliseconds };
            }
        }
    }

    m_profiler->ApplyGpuTimingSamples(
        std::span<const Core::Profiler::GpuTimingSample>( m_completedSamples,
                                                          static_cast<std::size_t>( completedCount ) ) );

#if defined( TRACY_ENABLE )
    if ( SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus().viewerConnected )
    {
        const RenderMemoryStats memory = m_diagnostics->GetRenderMemoryStats();
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/DrawCalls", m_diagnostics->GetFrameDrawCallCount() );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/UploadUsedBytes", memory.uploadUsedBytes );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/UploadPeakBytes", memory.uploadPeakBytes );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/RTVDescriptorsUsed", memory.rtvDescriptorsUsed );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/DSVDescriptorsUsed", memory.dsvDescriptorsUsed );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/StaticSRVDescriptorsUsed", memory.srvStaticDescriptorsUsed );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/StaticSRVDescriptorsHighWater", memory.srvStaticDescriptorsHighWater );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/TransientSRVDescriptorsUsed",
                                memory.srvTransientDescriptorsUsedThisFrame );

        SKORE_TRACY_PLOT_VALUE( "Counter/Render/TransientSRVDescriptorsPeak",
                                memory.srvTransientDescriptorsPeakThisRun );

        SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::PublishDevelopmentAllocationPlots();
    }
#endif
}


void RenderGpuTimingOwner::Begin( const char* fullPath, uint32_t hash )
{
    if ( !m_profiler )
    {
        return;
    }

    if ( m_openDepth >= Core::Profiler::MAX_DEPTH )
    {
        SB_FATAL( "RenderGpuTimingOwner", "GPU range stack overflow at %s", fullPath ? fullPath : "<null>" );
    }

    const int markerIndex = m_profiler->BeginRenderRecord( fullPath, hash );
    if ( markerIndex < 0 )
    {
        return;
    }

    OpenScope& scope = m_openScopes[m_openDepth++];
    scope = { fullPath, hash, markerIndex, false, false };

    if ( m_diagnostics && PlatformProfiler::IsEnabled() )
    {
        m_diagnostics->PlatformProfilerGpuBegin( fullPath, hash );
        scope.platformEventOpen = true;
    }

    if ( m_diagnostics && m_diagnostics->GetCapabilities().supportsGpuTimers )
    {
        m_profiler->MarkGpuMarkerWritten( markerIndex );
        m_diagnostics->GpuTimerBegin( markerIndex );
        scope.timerOpen = true;
    }
}


void RenderGpuTimingOwner::End( const char* fullPath, uint32_t hash )
{
    if ( !m_profiler )
    {
        return;
    }

    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return;
    }

    if ( m_openDepth <= 0 )
    {
        SB_FATAL( "RenderGpuTimingOwner", "GPU range end without begin for %s", fullPath ? fullPath : "<null>" );
    }

    OpenScope& scope = m_openScopes[m_openDepth - 1];
    if ( scope.hash != hash )
    {
        SB_FATAL( "RenderGpuTimingOwner",
                  "GPU range mismatch: expected %s, received %s",
                  scope.fullPath ? scope.fullPath : "<null>",
                  fullPath ? fullPath : "<null>" );
    }

    if ( scope.timerOpen && m_diagnostics )
    {
        m_diagnostics->GpuTimerEnd( scope.markerIndex );
    }

    if ( scope.platformEventOpen && m_diagnostics )
    {
        m_diagnostics->PlatformProfilerGpuEnd();
    }

    scope = OpenScope();
    --m_openDepth;
    m_profiler->EndRenderRecord( fullPath, hash );
}


void RenderGpuTimingOwner::InvalidateDevice()
{
    if ( m_openDepth != 0 )
    {
        SB_FATAL( "RenderGpuTimingOwner", "device invalidation reached with %d open GPU range(s)", m_openDepth );
    }

    if ( m_diagnostics )
    {
        m_diagnostics->GpuTimerInvalidate();
    }

    if ( m_profiler )
    {
        m_profiler->InvalidateGpuSamples();
        m_markerEpoch = m_profiler->MarkerEpoch();
    }
}

#else

// Why: unprofiled tools/tests retain the same renderer-facing no-op contract.
RenderGpuTimingOwner::RenderGpuTimingOwner( Core::Profiler* profiler, Dx12Diagnostics* diagnostics )
    : m_profiler( profiler ), m_diagnostics( diagnostics )
{
}


void RenderGpuTimingOwner::BeginFrame()
{
}


void RenderGpuTimingOwner::InvalidateDevice()
{
}


void RenderGpuTimingOwner::Begin( const char*, uint32_t )
{
}


void RenderGpuTimingOwner::End( const char*, uint32_t )
{
}

#endif
