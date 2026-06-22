/*
File: SkullbonezSource/Rendering/IRenderBackend.cpp
Purpose:
  Stores and exposes the active engine render device.

Mental model:
  Runtime systems call Gfx() when they need the active renderer. SetGfxBackend()
  installs the DX12 device during startup; DestroyGfxBackend() releases it
  during shutdown.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

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


IRenderBackend& Gfx()
{
    assert( s_gfxBackend && "Gfx() called before SetGfxBackend()" );
    if ( !s_gfxBackend )
    {
        throw std::runtime_error( "Gfx() called before SetGfxBackend()" );
    }
    return *s_gfxBackend;
}


bool IsGfxReady()
{
    return s_gfxBackend != nullptr;
}


void SetGfxBackend( std::unique_ptr<IRenderBackend> backend )
{
    s_gfxBackend = std::move( backend );
}


void DestroyGfxBackend()
{
    s_gfxBackend.reset();
}


} // namespace Rendering
} // namespace SkullbonezCore
