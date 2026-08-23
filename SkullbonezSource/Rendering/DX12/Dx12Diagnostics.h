/*
File: SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
Purpose:
  Owns DX12 GPU timing, draw visibility, trace, and diagnostic policy state.

Summary:
  One concrete diagnostics owner keeps timestamp resources and their covering
  fence beside per-frame draw/visibility evidence. Runtime diagnostics borrow
  this owner directly, while command recording and fault submission remain
  reachable only through a narrow transient frame capability.

Glossary:
  Timestamp pair: Begin/end query slots used to measure one GPU marker.
  Draw trace: Bounded hierarchy of scoped draw records for diagnostics UI.

Invariants:
  - Readback mapping occurs only after its covering fence completes.
  - Draw count, visibility rows, and trace scopes reset at one frame boundary.
  - The owner stores no backend, frame-owner, device, or command-list pointer.
  - Fault policy enters frame submission only through Dx12DiagnosticsFrame.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/RenderDiagnosticsTypes.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "RenderDeviceDX12.h"
#include "../RenderDiagnosticsTypes.h"
#include "../../Core/StringHash.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12DescriptorHeaps;
class Dx12DiagnosticsFrame;
class Dx12FrameOwner;
class Dx12GeometryOwner;
class Dx12GraphTransientPool;
class Dx12PipelineOwner;
class Dx12RaytracingOwner;
class Dx12TextureOwner;

class Dx12Diagnostics
{
  public:
    explicit Dx12Diagnostics( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics ) noexcept
        : m_resultDiagnostics( resultDiagnostics )
    {
    }
    static constexpr int TIMER_HEAP_MARKERS = 128;
    static constexpr int TIMER_HEAP_SIZE = TIMER_HEAP_MARKERS * 2;

    // Lifetime: all sources belong to the same backend device epoch and outlive
    // diagnostics publication. The explicit list keeps observation authority
    // inside this owner without retaining the RenderBackendDX12 facade.
    void BindSources( Dx12RenderDevice& device, Dx12DescriptorHeaps& descriptors, Dx12FrameOwner& frame,
                      Dx12TextureOwner& textures, Dx12PipelineOwner& pipeline, Dx12GeometryOwner& geometry,
                      Dx12GraphTransientPool& graphTransients, Dx12RaytracingOwner& raytracing );
    const char* GetRendererName() const
    {
        return "DirectX 12";
    }
    RenderCapabilities GetCapabilities() const;
    RenderMemoryStats GetRenderMemoryStats() const;

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
    void GpuTimerBegin( int markerIndex );
    void GpuTimerEnd( int markerIndex );
    void GpuTimerInvalidate();
    bool GpuTimerRead( int markerIndex, float& outMilliseconds );
    bool ResolveWrittenGpuTimers( Dx12DiagnosticsFrame& frame );
    void PublishResolvedGpuTimerFence( bool resolvedThisFrame, UINT64 fenceValue );
    void ConsumeGpuTimerReadback( Dx12DiagnosticsFrame& frame, bool waitForFence );

    void ResetFrameDrawCalls();
    void RecordDrawCall( const DrawCallRecord& record );
    int FrameDrawCallCount() const
    {
        return m_frameDrawCallCount;
    }
    int GetFrameDrawCallCount() const
    {
        return FrameDrawCallCount();
    }
    int DrawCallHighWater() const;
    void RecordVisibility( RenderVisibilityView view, int candidates, int submitted, int culled, int draws );
    RenderVisibilityStats FrameVisibilityStats() const
    {
        return m_frameVisibilityStats;
    }
    RenderVisibilityStats GetFrameVisibilityStats() const
    {
        return FrameVisibilityStats();
    }
    DrawCallTraceSnapshot FrameDrawCallTrace() const
    {
        return m_drawCallTrace.Snapshot();
    }
    DrawCallTraceSnapshot GetFrameDrawCallTrace() const
    {
        return FrameDrawCallTrace();
    }
    void PushDrawCallTraceScope( const char* fullPathOrLeaf, uint32_t hash );
    void PopDrawCallTraceScope( uint32_t hash );
    void PlatformProfilerGpuBegin( const char* name, uint32_t hash );
    void PlatformProfilerGpuEnd();

    void ConfigureFaultInjection( Dx12DiagnosticsFrame& frame );
    void ReportArchitectureStats( const char* reason, const Dx12DescriptorHeaps& descriptors,
                                  const Dx12FrameOwner& frame ) const;

  private:
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
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
    Dx12RenderDevice* m_device = nullptr;
    Dx12DescriptorHeaps* m_descriptors = nullptr;
    Dx12FrameOwner* m_frame = nullptr;
    Dx12TextureOwner* m_textures = nullptr;
    Dx12PipelineOwner* m_pipeline = nullptr;
    Dx12GeometryOwner* m_geometry = nullptr;
    Dx12GraphTransientPool* m_graphTransients = nullptr;
    Dx12RaytracingOwner* m_raytracing = nullptr;
};

class DrawCallTraceScope
{
  public:
    DrawCallTraceScope( Dx12Diagnostics& diagnostics, const char* fullPathOrLeaf )
        : m_diagnostics( &diagnostics ), m_hash( HashStr( fullPathOrLeaf ) )
    {
        m_diagnostics->PushDrawCallTraceScope( fullPathOrLeaf, m_hash );
    }
    DrawCallTraceScope( Dx12Diagnostics& diagnostics, const char* fullPathOrLeaf, uint32_t hash )
        : m_diagnostics( &diagnostics ), m_hash( hash )
    {
        m_diagnostics->PushDrawCallTraceScope( fullPathOrLeaf, m_hash );
    }
    ~DrawCallTraceScope()
    {
        if ( m_diagnostics )
        {
            m_diagnostics->PopDrawCallTraceScope( m_hash );
        }
    }
    DrawCallTraceScope( const DrawCallTraceScope& ) = delete;
    DrawCallTraceScope& operator=( const DrawCallTraceScope& ) = delete;

  private:
    Dx12Diagnostics* m_diagnostics = nullptr;
    uint32_t m_hash = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore

#define DRAW_CALL_TRACE_PASTE_INNER( a, b ) a##b
#define DRAW_CALL_TRACE_PASTE( a, b ) DRAW_CALL_TRACE_PASTE_INNER( a, b )
#define DRAW_CALL_TRACE_SCOPE( diagnostics, name )                                                                          \
    ::SkullbonezCore::Rendering::DrawCallTraceScope DRAW_CALL_TRACE_PASTE( _drawTraceScope_, __LINE__ )( diagnostics, name )
