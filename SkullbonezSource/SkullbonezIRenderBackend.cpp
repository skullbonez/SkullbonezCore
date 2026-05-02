// --- Includes ---
#include "SkullbonezIRenderBackend.h"
#include <cassert>


namespace SkullbonezCore
{
namespace Rendering
{

static std::unique_ptr<IRenderBackend> s_gfxBackend;


IRenderBackend& Gfx()
{
    assert( s_gfxBackend && "Gfx() called before SetGfxBackend()" );
    return *s_gfxBackend;
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
