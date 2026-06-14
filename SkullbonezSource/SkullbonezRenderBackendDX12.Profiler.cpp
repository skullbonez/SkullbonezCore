// --- Includes ---
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderDX12.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezFramebufferDX12.h"
#include "SkullbonezRenderGraph.h"
#include "SkullbonezPlatformProfiler.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <wrl/client.h>


// --- Usings ---
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;


// --- Helpers ---
static void ReportDX12DescriptorHeapExhausted( const char* heapName, UINT nextIndex, UINT capacity )
{
    const char* name = heapName ? heapName : "unknown";
    fprintf( stderr, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fprintf( stdout, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fflush( stderr );
    fflush( stdout );
    Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u", name, nextIndex, capacity );
    Log().FlushAll();
}

static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
}

// --- RenderBackendDX12 Profiler methods ---


void RenderBackendDX12::TryConsumeGpuTimerReadback( bool waitForFence )
{
    if ( !m_gpuTimers.queryHeap || !m_gpuTimers.readPending || !m_gpuTimers.readback.IsReady() || !m_renderDevice.FrameFence().IsReady() )
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
            m_gpuTimers.resultMs[i] = static_cast<float>( static_cast<double>( t1 - t0 ) / static_cast<double>( m_gpuTimers.freq ) * 1000.0 );
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
    m_commandList->EndQuery( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot );
    m_gpuTimers.slotWritten[slot] = true;
}


void RenderBackendDX12::GpuTimerEnd( int markerIdx )
{
    if ( !m_gpuTimers.queryHeap || !m_commandListOpen || markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS )
    {
        return;
    }
    int slot = markerIdx * 2 + 1;
    m_commandList->EndQuery( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot );
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

    // resultMs and resultValid are intentionally PRESERVED (same reasoning as DX11):
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


void RenderBackendDX12::PlatformProfilerGpuBegin( const char* name, uint32_t hash )
{
    if ( !SkullbonezCore::Basics::PlatformProfiler::IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( !m_commandList )
    {
        return;
    }
    EnsureCommandListOpen();
    const char* markerName = name ? name : "(null)";
    PIXBeginEvent( m_commandList, SkullbonezCore::Basics::PlatformProfiler::ColorForMarker( markerName, hash ), "%s", markerName );
    ++m_platformProfilerGpuDepth;
#else
    (void)name;
    (void)hash;
#endif
}


void RenderBackendDX12::PlatformProfilerGpuEnd()
{
    if ( !SkullbonezCore::Basics::PlatformProfiler::IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( m_platformProfilerGpuDepth <= 0 )
    {
        Log().WriteEventf( "dx12_platform_profiler_gpu_end_without_begin" );
        return;
    }
    if ( !m_commandList || !m_commandListOpen )
    {
        Log().WriteEventf( "dx12_platform_profiler_gpu_end_without_open_command_list depth=%d", m_platformProfilerGpuDepth );
        return;
    }
    PIXEndEvent( m_commandList );
    --m_platformProfilerGpuDepth;
#endif
}


void RenderBackendDX12::PlatformProfilerGpuMarker( const char* name, uint32_t hash )
{
    if ( !SkullbonezCore::Basics::PlatformProfiler::IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( !m_commandList )
    {
        return;
    }
    EnsureCommandListOpen();
    const char* markerName = name ? name : "(null)";
    PIXSetMarker( m_commandList, SkullbonezCore::Basics::PlatformProfiler::ColorForMarker( markerName, hash ), "%s", markerName );
#else
    (void)name;
    (void)hash;
#endif
}
