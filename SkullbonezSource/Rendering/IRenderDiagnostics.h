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

Invariants:
  - Diagnostics are optional and must have no-op fallbacks for unsupported
    backend features.
  - Capability flags describe the active backend lifetime; callers must not
    cache them across backend teardown and replacement.

Related:
  - SkullbonezSource/Rendering/DrawCallTrace.h
  - SkullbonezSource/Rendering/IRenderBackend.h
*/
#pragma once

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

class IRenderDiagnostics
{
  public:
    virtual ~IRenderDiagnostics() = default;

    virtual const char* GetRendererName() const = 0;
    virtual RenderCapabilities GetCapabilities() const = 0;

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

} // namespace Rendering
} // namespace SkullbonezCore
