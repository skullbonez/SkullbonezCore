/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp
Purpose:
  Delegates GPU timing and implements platform-profiler marker integration.

Summary:
  Dx12Diagnostics owns timestamp resources, readback, results, and fence state;
  this retained backend unit forwards the public timer facet and keeps PIX
  marker calls beside frame-owner profiler stack operations.

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
  - Timer wrappers never expose diagnostic storage or frame submission authority.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
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
using SkullbonezCore::Core::SbResult;


// --- Helpers ---
// --- RenderBackendDX12 SkullbonezCore::Core::Profiler methods ---


void RenderBackendDX12::GpuTimerBegin( int markerIdx )
{
    m_diagnostics.GpuTimerBegin( m_frameOwner.DiagnosticsFrame(), markerIdx );
}


void RenderBackendDX12::GpuTimerEnd( int markerIdx )
{
    m_diagnostics.GpuTimerEnd( m_frameOwner.DiagnosticsFrame(), markerIdx );
}


void RenderBackendDX12::GpuTimerInvalidate()
{
    m_diagnostics.GpuTimerInvalidate( m_frameOwner.DiagnosticsFrame() );
}


bool RenderBackendDX12::GpuTimerRead( int markerIdx, float& outMs )
{
    return m_diagnostics.GpuTimerRead( m_frameOwner.DiagnosticsFrame(), markerIdx, outMs );
}


void RenderBackendDX12::PlatformProfilerGpuBegin( const char* name, uint32_t hash )
{
    if ( !SkullbonezCore::Core::PlatformProfiler::IsEnabled() )
    {
        return;
    }

    m_frameOwner.BeginProfilerEvent( name, hash );
}


void RenderBackendDX12::PlatformProfilerGpuEnd()
{
    m_frameOwner.EndProfilerEvent();
}


void RenderBackendDX12::PlatformProfilerGpuMarker( const char* name, uint32_t hash )
{
    if ( !SkullbonezCore::Core::PlatformProfiler::IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( !CommandList() )
    {
        return;
    }
    if ( !m_frameOwner.EnsureOpen().ok )
    {
        return;
    }
    char gpuMarkerName[SkullbonezCore::Core::PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
    const char* markerName = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled()
                                 ? SkullbonezCore::Core::PlatformProfiler::DecorateMarkerName( name,
                                                                                               "_GPU",
                                                                                               gpuMarkerName,
                                                                                               sizeof( gpuMarkerName ) )
                                 : name;
    PIXSetMarker( CommandList(),
                  SkullbonezCore::Core::PlatformProfiler::ColorForMarker( markerName, hash ),
                  "%s",
                  markerName );
#else
    (void)name;
    (void)hash;
#endif
}
