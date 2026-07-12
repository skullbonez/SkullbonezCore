/*
File: SkullbonezSource/Rendering/IRenderDiagnostics.h
Purpose:
  Declares the narrow render capability for draw tracing, GPU timers, platform
  profiler markers, and backend feature metadata.

Mental model:
  Diagnostics code observes and annotates rendering work. It can reset and read
  draw-call traces, bracket GPU timer regions, write platform profiler markers,
  and ask which optional backend capabilities are available. It should not
  create resources or submit ordinary draw calls.

Glossary:
  Draw-call trace: Per-frame list of named draw events used by overlays and
    validation diagnostics.
  GPU timer: Backend measurement of elapsed GPU time for a marked render region.
  Platform profiler marker: Named event emitted for external profiling tools.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
    reflection dispatch.
  Render memory snapshot: Coarse counters that separate engine renderer caches
    from platform-reported adapter memory during stress runs.
  Visibility counters: Per-view candidate, cull, submission, and draw totals
    accumulated between frame-diagnostics resets.
  DXGI adapter memory: Windows graphics-kernel budget/usage counters for the
    adapter that owns the active DX12 device.

Invariants:
  - Diagnostics are optional and must have no-op fallbacks for unsupported
    backend features.
  - Capability flags describe the active backend lifetime; callers must not
    cache them across backend teardown and replacement.
  - Visibility snapshots describe only the current frame and never own a
    visible-index list or influence render decisions.

Related:
  - SkullbonezSource/Rendering/DrawCallTrace.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
*/
#pragma once

#include "../Core/Common.h"
#include "../Assets/AssetKeys.h"

#include <cstddef>
#include <cstdint>

#include "DrawCallTrace.h"

namespace SkullbonezCore
{
namespace Rendering
{

struct RenderCapabilities
{
    bool supportsBackbufferCapture = true;
    bool supportsGpuTimers = false;
    bool supportsDxrReflection = false;
    bool supportsDebugLines = false;
};

struct RenderMemoryStats
{
    bool available = false;                       // False when the backend is not initialized enough to answer.
    char backendName[32] = "unknown";             // Short renderer name for CSV/JSON diagnostics.
    uint64_t recreationGeneration = 0;            // Advances after a complete backend resize publication.
    bool adapterMemoryAvailable = false;          // True when DXGI adapter memory counters were sampled.
    uint64_t localBudgetBytes = 0;                // Adapter-local budget reported by DXGI.
    uint64_t localCurrentUsageBytes = 0;          // Adapter-local bytes currently charged to this process.
    uint64_t localCurrentReservationBytes = 0;    // Adapter-local reservation bytes currently held by this process.
    uint64_t localAvailableForReservationBytes = 0;
    uint64_t nonLocalBudgetBytes = 0;             // Shared/system-memory budget reported by DXGI.
    uint64_t nonLocalCurrentUsageBytes = 0;       // Non-local bytes currently charged to this process.
    uint64_t nonLocalCurrentReservationBytes = 0; // Non-local reservation bytes currently held by this process.
    uint64_t nonLocalAvailableForReservationBytes = 0;
    uint64_t uploadCapacityBytes = 0;             // Sum of persistent per-frame upload-buffer resources.
    uint64_t uploadUsedBytes = 0;                 // Bytes used in the currently sampled upload arenas.
    uint64_t uploadPeakBytes = 0;                 // Sum of per-arena high-water marks for this run.
    uint64_t timerReadbackBytes = 0;              // CPU-readable timer readback resource, when allocated.
    std::size_t textureRegistryCount = 0;
    std::size_t textureRegistryCapacity = 0;
    std::size_t dynamicVertexBufferCount = 0;
    std::size_t dynamicVertexBufferCapacity = 0;
    std::size_t instancedMeshCount = 0;
    std::size_t instancedMeshCapacity = 0;
    std::size_t psoCacheCount = 0;
    std::size_t graphTransientCount = 0;
    std::size_t graphTransientCapacity = 0;
    uint32_t rtvDescriptorsUsed = 0;
    uint32_t rtvDescriptorsCapacity = 0;
    uint32_t dsvDescriptorsUsed = 0;
    uint32_t dsvDescriptorsCapacity = 0;
    uint32_t srvStaticDescriptorsUsed = 0;
    uint32_t srvStaticDescriptorsCapacity = 0;
    uint32_t srvStaticDescriptorsHighWater = 0;
    uint32_t srvTransientDescriptorsUsedThisFrame = 0;
    uint32_t srvTransientDescriptorsCapacityPerFrame = 0;
    uint32_t srvTransientDescriptorsPeakThisRun = 0;
};

enum class RenderVisibilityView : uint8_t
{
    Main,
    Reflection,
    TerrainShadow,
    ObjectShadow,
    Count
};

struct RenderVisibilityViewStats
{
    int candidates = 0;
    int submitted = 0;
    int culled = 0;
    int draws = 0;
};

struct RenderVisibilityStats
{
    RenderVisibilityViewStats views[static_cast<int>( RenderVisibilityView::Count )] = {};
};

class IRenderDiagnostics
{
  public:
    virtual ~IRenderDiagnostics() = default;

    virtual const char* GetRendererName() const = 0;
    virtual RenderCapabilities GetCapabilities() const = 0;
    // Returns a renderer-owned memory snapshot for diagnostics. Unsupported
    // backends leave available=false so callers can log one schema across
    // renderer implementations without inventing backend-specific casts.
    virtual RenderMemoryStats GetRenderMemoryStats() const
    {
        return RenderMemoryStats();
    }

    virtual void ResetFrameDrawCalls()
    {
    }
    virtual void RecordDrawCall( const DrawCallRecord& record )
    {
        (void)record;
    }
    void RecordDrawCall()
    {
        RecordDrawCall( DrawCallRecord() );
    }
    virtual int GetFrameDrawCallCount() const
    {
        return 0;
    }
    // Adds one submission region to the named view. Candidates are the rows
    // tested, submitted are rows surviving culling/masks, and draws are the
    // backend calls emitted by that region.
    virtual void RecordVisibility( RenderVisibilityView view, int candidates, int submitted, int culled, int draws )
    {
        (void)view;
        (void)candidates;
        (void)submitted;
        (void)culled;
        (void)draws;
    }
    virtual RenderVisibilityStats GetFrameVisibilityStats() const
    {
        return RenderVisibilityStats();
    }
    virtual DrawCallTraceSnapshot GetFrameDrawCallTrace() const
    {
        return DrawCallTraceSnapshot();
    }
    virtual void PushDrawCallTraceScope( const char* fullPathOrLeaf, uint32_t hash )
    {
        (void)fullPathOrLeaf;
        (void)hash;
    }
    virtual void PopDrawCallTraceScope( uint32_t hash )
    {
        (void)hash;
    }

    virtual void GpuTimerBegin( int markerIdx )
    {
        (void)markerIdx;
    }
    virtual void GpuTimerEnd( int markerIdx )
    {
        (void)markerIdx;
    }
    virtual void GpuTimerInvalidate()
    {
    }
    virtual bool GpuTimerRead( int markerIdx, float& outMs )
    {
        (void)markerIdx;
        (void)outMs;
        return false;
    }

    virtual void PlatformProfilerGpuBegin( const char* name, uint32_t hash )
    {
        (void)name;
        (void)hash;
    }
    virtual void PlatformProfilerGpuEnd()
    {
    }
    virtual void PlatformProfilerGpuMarker( const char* name, uint32_t hash )
    {
        (void)name;
        (void)hash;
    }
};

class DrawCallTraceScope
{
  public:
    DrawCallTraceScope( IRenderDiagnostics& renderDiagnostics, const char* fullPathOrLeaf )
        : m_renderDiagnostics( &renderDiagnostics ), m_hash( HashStr( fullPathOrLeaf ) )
    {
        // Lifetime: trace scopes are frame-local diagnostics annotations. They
        // borrow the diagnostics facet already owned by the caller instead of
        // reopening the global renderer facade.
        m_renderDiagnostics->PushDrawCallTraceScope( fullPathOrLeaf, m_hash );
    }

    DrawCallTraceScope( IRenderDiagnostics& renderDiagnostics, const char* fullPathOrLeaf, uint32_t hash )
        : m_renderDiagnostics( &renderDiagnostics ), m_hash( hash )
    {
        // Why: graph/object passes may already have the profiler hash in hand.
        // Reusing it keeps CPU and GPU diagnostics grouped by the same key.
        m_renderDiagnostics->PushDrawCallTraceScope( fullPathOrLeaf, m_hash );
    }

    ~DrawCallTraceScope()
    {
        if ( m_renderDiagnostics )
        {
            m_renderDiagnostics->PopDrawCallTraceScope( m_hash );
        }
    }

    DrawCallTraceScope( const DrawCallTraceScope& ) = delete;
    DrawCallTraceScope& operator=( const DrawCallTraceScope& ) = delete;

  private:
    IRenderDiagnostics* m_renderDiagnostics = nullptr;
    uint32_t m_hash = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore

#define DRAW_CALL_TRACE_PASTE_INNER( a, b ) a##b
#define DRAW_CALL_TRACE_PASTE( a, b ) DRAW_CALL_TRACE_PASTE_INNER( a, b )
#define DRAW_CALL_TRACE_SCOPE( diagnostics, name )                                                                     \
    ::SkullbonezCore::Rendering::DrawCallTraceScope DRAW_CALL_TRACE_PASTE( _drawTraceScope_, __LINE__ )( diagnostics,  \
                                                                                                         name )
