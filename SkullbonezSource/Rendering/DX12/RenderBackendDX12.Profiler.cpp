/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp
Purpose:
  Implements DX12 GPU timestamp collection and profiler readback.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  GPU timer: Timestamp query pair written by the command list and read back
  later to estimate GPU time for a profiler marker.
  PIX: Microsoft GPU debugger/profiler that can read engine markers and DX12
  event ranges.
  Platform profiler GPU stack: Bounded mirror of nested GPU marker names that
  lets PIX ranges be suspended around command-list submission and restored
  afterward.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Platform profiler GPU ranges are stack-shaped and bounded. Overflow means
    marker nesting exceeded the backend contract, not a recoverable runtime
    condition.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderBackendDX12.h"
#include "ShaderDX12.h"
#include "MeshDX12.h"
#include "FramebufferDX12.h"
#include "../RenderGraph.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <wrl/client.h>


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;


// --- Helpers ---
// --- RenderBackendDX12 Profiler methods ---


void RenderBackendDX12::TryConsumeGpuTimerReadback( bool waitForFence )
{
    if ( !m_gpuTimers.queryHeap || !m_gpuTimers.readPending || !m_gpuTimers.readback.IsReady() ||
         !m_renderDevice.FrameFence().IsReady() )
    {
        return;
    }

    // Non-blocking mode is used in the normal frame loop so GPU timers keep working even when
    // PipelineSync is disabled. Blocking mode is only used by Finish()/FlushGPU().
    if ( waitForFence )
    {
        m_renderDevice.FrameFence().WaitForValue( m_gpuTimers.readFenceValue );
    }
    else if ( m_renderDevice.FrameFence().CompletedValue() < m_gpuTimers.readFenceValue )
    {
        // The Signal is submitted right after vsync — the GPU needs only nanoseconds to
        // process it, but in optimised builds the CPU can arrive here before it fires.
        // Spin briefly (a few hundred pauses ≈ a few microseconds) to catch it without
        // burning a full WaitForSingleObject kernel call.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-yieldprocessor
        for ( int spin = 0; spin < 512; ++spin )
        {
            YieldProcessor();
            if ( m_renderDevice.FrameFence().CompletedValue() >= m_gpuTimers.readFenceValue )
            {
                break;
            }
        }
        if ( m_renderDevice.FrameFence().CompletedValue() < m_gpuTimers.readFenceValue )
        {
            return; // genuinely not ready — try again next frame
        }
    }

    const UINT64 readbackBytes = static_cast<UINT64>( TIMER_HEAP_SIZE ) * sizeof( uint64_t );
    const uint64_t* pData = static_cast<const uint64_t*>( m_gpuTimers.readback.MapRead( readbackBytes ) );

    std::memset( m_gpuTimers.resultMs, 0, sizeof( m_gpuTimers.resultMs ) );
    std::memset( m_gpuTimers.resultValid, 0, sizeof( m_gpuTimers.resultValid ) );

    for ( int i = 0; i < TIMER_HEAP_MARKERS; ++i )
    {
        const uint64_t t0 = pData[i * 2 + 0];
        const uint64_t t1 = pData[i * 2 + 1];
        if ( t1 > t0 && m_gpuTimers.freq > 0 )
        {
            m_gpuTimers.resultMs[i] =
                static_cast<float>( static_cast<double>( t1 - t0 ) / static_cast<double>( m_gpuTimers.freq ) * 1000.0 );
            m_gpuTimers.resultValid[i] = true;
        }
    }

    m_gpuTimers.readback.UnmapNoWrite();

    m_gpuTimers.readPending = false;
}


void RenderBackendDX12::GpuTimerBegin( int markerIdx )
{
    if ( !m_gpuTimers.queryHeap || markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS )
    {
        return;
    }
    EnsureCommandListOpen();
    int slot = markerIdx * 2 + 0;
    CommandList()->EndQuery( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot );
    m_gpuTimers.slotWritten[slot] = true;
}


void RenderBackendDX12::GpuTimerEnd( int markerIdx )
{
    if ( !m_gpuTimers.queryHeap || !m_commandListOpen || markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS )
    {
        return;
    }
    int slot = markerIdx * 2 + 1;
    CommandList()->EndQuery( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot );
    m_gpuTimers.slotWritten[slot] = true;
}


void RenderBackendDX12::GpuTimerInvalidate()
{
    // If there's a pending readback from a previous frame, consume it now (blocking)
    // before clearing state. This prevents the dangling readPending from causing
    // stale data to be attributed to newly-registered markers after the reset.
    if ( m_gpuTimers.readPending )
    {
        TryConsumeGpuTimerReadback( true );
    }

    // resultMs and resultValid are intentionally preserved:
    // After a reset, the non-blocking TryConsumeGpuTimerReadback in GpuTimerRead may fail
    // its 512-spin if the GPU hasn't completed the first post-reset frame yet. Preserving
    // stale resultValid lets ReadPendingGpuResults immediately see data and keeps the GPU
    // column visible. The next successful consume overwrites all entries with fresh data.
    std::memset( m_gpuTimers.slotWritten, 0, sizeof( m_gpuTimers.slotWritten ) );
    m_gpuTimers.readPending = false;
    m_gpuTimers.readFenceValue = 0;
}


bool RenderBackendDX12::GpuTimerRead( int markerIdx, float& outMs )
{
    TryConsumeGpuTimerReadback( false );

    if ( markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS || !m_gpuTimers.resultValid[markerIdx] )
    {
        return false;
    }
    outMs = m_gpuTimers.resultMs[markerIdx];
    return true;
}


int RenderBackendDX12::SuspendPlatformProfilerGpuStackForSubmit( const char* reason )
{
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( m_platformProfilerGpuDepth <= 0 )
    {
        return 0;
    }
    if ( !CommandList() || !m_commandListOpen )
    {
        Log().WriteEventf( "dx12_platform_profiler_gpu_suspend_without_open_command_list reason=%s depth=%d",
                           reason ? reason : "unknown",
                           m_platformProfilerGpuDepth );
        return 0;
    }

    const int suspendedDepth = m_platformProfilerGpuDepth;
    Log().WriteEventf( "dx12_platform_profiler_gpu_stack_suspended_for_submit reason=%s depth=%d",
                       reason ? reason : "unknown",
                       suspendedDepth );
    for ( int i = suspendedDepth - 1; i >= 0; --i )
    {
        PIXEndEvent( CommandList() );
    }
    m_platformProfilerGpuDepth = 0;
    return suspendedDepth;
#else
    (void)reason;
    return 0;
#endif
}


void RenderBackendDX12::RestorePlatformProfilerGpuStackAfterSubmit( int suspendedDepth )
{
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( suspendedDepth <= 0 )
    {
        return;
    }
    if ( !CommandList() || !m_commandListOpen )
    {
        Log().WriteEventf( "dx12_platform_profiler_gpu_restore_without_open_command_list depth=%d", suspendedDepth );
        return;
    }

    for ( int i = 0; i < suspendedDepth; ++i )
    {
        const PlatformProfilerGpuScopeDX12& scope = m_platformProfilerGpuStack[static_cast<std::size_t>( i )];
        const char* markerName = scope.name[0] != '\0' ? scope.name : "(null)";
        PIXBeginEvent( CommandList(),
                       SkullbonezCore::Basics::PlatformProfiler::ColorForMarker( markerName, scope.hash ),
                       "%s",
                       markerName );
    }
    m_platformProfilerGpuDepth = suspendedDepth;
#else
    (void)suspendedDepth;
#endif
}


void RenderBackendDX12::PlatformProfilerGpuBegin( const char* name, uint32_t hash )
{
    if ( !SkullbonezCore::Basics::PlatformProfiler::IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( !CommandList() )
    {
        return;
    }
    EnsureCommandListOpen();
    if ( m_platformProfilerGpuDepth >= PLATFORM_PROFILER_GPU_SCOPE_STACK_MAX )
    {
        // Invariant: the stack mirrors nested PIX GPU ranges. Overflow means
        // marker begin/end ownership has escaped the backend's fixed budget.
        SB_FATAL( "RenderBackendDX12",
                  "DX12 platform profiler GPU stack overflow. depth=%d capacity=%d",
                  m_platformProfilerGpuDepth,
                  PLATFORM_PROFILER_GPU_SCOPE_STACK_MAX );
    }

    char gpuMarkerName[SkullbonezCore::Basics::PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
    const char* markerName =
        SkullbonezCore::Basics::PlatformProfiler::AreDetailedRangesEnabled()
            ? SkullbonezCore::Basics::PlatformProfiler::DecorateMarkerName( name,
                                                                            "_GPU",
                                                                            gpuMarkerName,
                                                                            sizeof( gpuMarkerName ) )
            : name;
    PlatformProfilerGpuScopeDX12& scope =
        m_platformProfilerGpuStack[static_cast<std::size_t>( m_platformProfilerGpuDepth )];
    _snprintf_s( scope.name, sizeof( scope.name ), _TRUNCATE, "%s", markerName ? markerName : "(null)" );
    scope.hash = hash;
    PIXBeginEvent( CommandList(),
                   SkullbonezCore::Basics::PlatformProfiler::ColorForMarker( markerName, hash ),
                   "%s",
                   markerName );
    ++m_platformProfilerGpuDepth;
#else
    (void)name;
    (void)hash;
#endif
}


void RenderBackendDX12::PlatformProfilerGpuEnd()
{
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( m_platformProfilerGpuDepth <= 0 )
    {
        if ( SkullbonezCore::Basics::PlatformProfiler::IsEnabled() )
        {
            Log().WriteEventf( "dx12_platform_profiler_gpu_end_without_begin" );
        }
        return;
    }
    if ( !CommandList() || !m_commandListOpen )
    {
        Log().WriteEventf( "dx12_platform_profiler_gpu_end_without_open_command_list depth=%d",
                           m_platformProfilerGpuDepth );
        return;
    }
    PIXEndEvent( CommandList() );
    --m_platformProfilerGpuDepth;
    m_platformProfilerGpuStack[static_cast<std::size_t>( m_platformProfilerGpuDepth )] = PlatformProfilerGpuScopeDX12();
#endif
}


void RenderBackendDX12::PlatformProfilerGpuMarker( const char* name, uint32_t hash )
{
    if ( !SkullbonezCore::Basics::PlatformProfiler::IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( !CommandList() )
    {
        return;
    }
    EnsureCommandListOpen();
    char gpuMarkerName[SkullbonezCore::Basics::PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
    const char* markerName =
        SkullbonezCore::Basics::PlatformProfiler::AreDetailedRangesEnabled()
            ? SkullbonezCore::Basics::PlatformProfiler::DecorateMarkerName( name,
                                                                            "_GPU",
                                                                            gpuMarkerName,
                                                                            sizeof( gpuMarkerName ) )
            : name;
    PIXSetMarker( CommandList(),
                  SkullbonezCore::Basics::PlatformProfiler::ColorForMarker( markerName, hash ),
                  "%s",
                  markerName );
#else
    (void)name;
    (void)hash;
#endif
}
