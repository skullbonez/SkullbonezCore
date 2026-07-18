/*
File: SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
Purpose:
  Owns DX12 GPU timing, draw visibility, trace, and diagnostic policy state.

Summary:
  One concrete diagnostics owner keeps timestamp resources and their covering
  fence beside per-frame draw/visibility evidence. Backend-facing interface
  methods delegate here, while command recording and fault submission remain
  reachable only through a narrow transient frame capability.

Glossary:
  Timestamp pair: Begin/end query slots used to measure one GPU marker.
  Covering fence: Queue value proving resolved readback bytes are CPU-readable.
  Draw trace: Bounded hierarchy of scoped draw records for diagnostics UI.
  Fault injection: Debug-only policy that blocks a reviewed submission point.

Invariants:
  - Readback mapping occurs only after its covering fence completes.
  - Draw count, visibility rows, and trace scopes reset at one frame boundary.
  - The owner stores no backend, frame-owner, device, or command-list pointer.
  - Fault policy enters frame submission only through Dx12DiagnosticsFrame.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/IRenderDiagnostics.h
*/
#pragma once

#include "RenderDeviceDX12.h"
#include "../IRenderDiagnostics.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12DescriptorHeaps;
class Dx12DiagnosticsFrame;
class Dx12FrameOwner;

class Dx12Diagnostics
{
  public:
    static constexpr int TIMER_HEAP_MARKERS = 128;
    static constexpr int TIMER_HEAP_SIZE = TIMER_HEAP_MARKERS * 2;

    SkullbonezCore::Core::SbResult InitializeGpuTimers( ID3D12Device* device, ID3D12CommandQueue* queue );
    void ShutdownGpuTimers();
    bool SupportsGpuTimers() const
    {
        return m_gpuTimers.queryHeap != nullptr;
    }
    Dx12ReadbackBufferStats TimerReadbackStats() const
    {
        return m_gpuTimers.readback.GetStats();
    }
    void GpuTimerBegin( Dx12DiagnosticsFrame& frame, int markerIndex );
    void GpuTimerEnd( Dx12DiagnosticsFrame& frame, int markerIndex );
    void GpuTimerInvalidate( Dx12DiagnosticsFrame& frame );
    bool GpuTimerRead( Dx12DiagnosticsFrame& frame, int markerIndex, float& outMilliseconds );
    bool ResolveWrittenGpuTimers( Dx12DiagnosticsFrame& frame );
    void PublishResolvedGpuTimerFence( bool resolvedThisFrame, UINT64 fenceValue );
    void ConsumeGpuTimerReadback( Dx12DiagnosticsFrame& frame, bool waitForFence );

    void ResetFrameDrawCalls();
    void RecordDrawCall( const DrawCallRecord& record );
    int FrameDrawCallCount() const
    {
        return m_frameDrawCallCount;
    }
    int DrawCallHighWater() const;
    void RecordVisibility( RenderVisibilityView view, int candidates, int submitted, int culled, int draws );
    RenderVisibilityStats FrameVisibilityStats() const
    {
        return m_frameVisibilityStats;
    }
    DrawCallTraceSnapshot FrameDrawCallTrace() const
    {
        return m_drawCallTrace.Snapshot();
    }
    void PushDrawCallTraceScope( const char* fullPathOrLeaf, uint32_t hash );
    void PopDrawCallTraceScope( uint32_t hash );

    void ConfigureFaultInjection( Dx12DiagnosticsFrame& frame );
    void ReportArchitectureStats( const char* reason,
                                  const Dx12DescriptorHeaps& descriptors,
                                  const Dx12FrameOwner& frame ) const;

  private:
    struct GpuTimerState
    {
        ID3D12QueryHeap* queryHeap = nullptr;
        Dx12ReadbackBuffer readback;
        float resultMilliseconds[TIMER_HEAP_MARKERS] = {};
        bool resultValid[TIMER_HEAP_MARKERS] = {};
        uint64_t frequency = 1;
        bool readPending = false;
        UINT64 readFenceValue = 0;
        bool slotWritten[TIMER_HEAP_SIZE] = {};
    };

    GpuTimerState m_gpuTimers;
    int m_frameDrawCallCount = 0;
    int m_frameDrawCallHighWater = 0;
    RenderVisibilityStats m_frameVisibilityStats;
    DrawCallTrace m_drawCallTrace;
};
} // namespace Rendering
} // namespace SkullbonezCore
