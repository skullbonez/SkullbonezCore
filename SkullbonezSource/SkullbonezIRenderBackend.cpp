/*
File: SkullbonezSource/SkullbonezIRenderBackend.cpp
Purpose:
  Implements the renderer abstraction shared by GL, DX11, and DX12 backends.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/SkullbonezIRenderBackend.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezIRenderBackend.h"
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
