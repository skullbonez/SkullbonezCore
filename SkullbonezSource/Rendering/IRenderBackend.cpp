/*
File: SkullbonezSource/Rendering/IRenderBackend.cpp
Purpose:
  Stores and exposes the active engine render device.

Mental model:
  Runtime systems call Gfx() when they need the active renderer. SetGfxBackend()
  installs the DX12 device during startup. Optional capability accessors borrow
  narrower interfaces from that same backend, and DestroyGfxBackend() clears
  them before releasing the device during shutdown.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Exactly one backend is active at a time and Gfx() is invalid before
    SetGfxBackend succeeds.
  - Narrow capability pointers are borrowed aliases into the active backend.
  - DestroyGfxBackend releases backend ownership; callers must not retain
    references returned by Gfx() across teardown.

Related:
  - SkullbonezSource/Rendering/IRenderBackend.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "IRenderBackend.h"
#include <cassert>
#include <stdexcept>


namespace SkullbonezCore
{
namespace Rendering
{

static std::unique_ptr<IRenderBackend> s_gfxBackend;
static IRenderRayTracing* s_gfxRayTracingBackend = nullptr;


IRenderBackend& Gfx()
{
    assert( s_gfxBackend && "Gfx() called before SetGfxBackend()" );
    if ( !s_gfxBackend )
    {
        throw std::runtime_error( "Gfx() called before SetGfxBackend()" );
    }
    return *s_gfxBackend;
}


IRenderCaptureBackend& GfxCapture()
{
    // Lifetime: capture is a borrowed capability of the active backend, not a
    // separately owned service. Gfx() keeps the startup/teardown guard central.
    return Gfx();
}


IRenderRayTracing& GfxRayTracing()
{
    assert( s_gfxRayTracingBackend && "GfxRayTracing() called before SetGfxRayTracingBackend()" );
    if ( !s_gfxRayTracingBackend )
    {
        throw std::runtime_error( "GfxRayTracing() called before SetGfxRayTracingBackend()" );
    }
    return *s_gfxRayTracingBackend;
}


bool IsGfxReady()
{
    return s_gfxBackend != nullptr;
}


bool IsGfxRayTracingReady()
{
    return s_gfxRayTracingBackend != nullptr;
}


void SetGfxBackend( std::unique_ptr<IRenderBackend> backend )
{
    s_gfxRayTracingBackend = nullptr;
    s_gfxBackend = std::move( backend );
}


void SetGfxRayTracingBackend( IRenderRayTracing* backend )
{
    s_gfxRayTracingBackend = backend;
}


void DestroyGfxBackend()
{
    s_gfxRayTracingBackend = nullptr;
    s_gfxBackend.reset();
}


} // namespace Rendering
} // namespace SkullbonezCore
