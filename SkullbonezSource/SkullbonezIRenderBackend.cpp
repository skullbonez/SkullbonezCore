// --- Includes ---
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
