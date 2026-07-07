/*
File: SkullbonezSource/Rendering/IRenderBackend.h
Purpose:
  Declares the temporary aggregate render facade implemented by the DX12 backend.

Mental model:
  The renderer is being split into narrower capability interfaces. Existing
  callers still ask for IRenderBackend through Gfx(), but the facade now
  aggregates lifecycle, resource factory, command context, diagnostics, and
  capture capabilities instead of declaring one flat method pile.

Glossary:
  Capability interface: Narrow borrowed surface that exposes one category of
    renderer behavior.
  Render device: Engine-facing object that owns the active GPU backend and its
    resources.
  Facade: Temporary compatibility type that groups narrower capabilities while
    call sites migrate.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
    reflection dispatch.

Invariants:
  - This contract stays in engine terms; native DX12 descriptors, barriers, and
    command-list details remain backend-private.
  - IRenderBackend is compatibility glue, not the desired dependency for new
    code; new callers should request the narrow capability they need.
  - Texture, mesh, shader, framebuffer, and capture handles are valid only for
    the active backend lifetime.
  - Raytracing remains a separate IRenderRayTracing capability and must not move
    back onto this facade.

Related:
  - SkullbonezSource/Rendering/IRenderBackend.cpp
  - SkullbonezSource/Rendering/IRenderCommandContext.h
  - SkullbonezSource/Rendering/IRenderDeviceLifecycle.h
  - SkullbonezSource/Rendering/IRenderDiagnostics.h
  - SkullbonezSource/Rendering/IRenderResourceFactory.h
*/
#pragma once

#include <cstdint>
#include <memory>

#include "../Core/Common.h"
#include "IRenderCaptureBackend.h"
#include "IRenderCommandContext.h"
#include "IRenderDeviceLifecycle.h"
#include "IRenderDiagnostics.h"
#include "IRenderResourceFactory.h"


namespace SkullbonezCore
{
namespace Rendering
{

/* -- IRenderBackend
---------------------------------------------------------------------------------------------------------------------------------------------

    Compatibility aggregate for the active DX12 backend. It deliberately has no
    methods of its own beyond the inherited capability contracts; shrinking this
    type is the migration path away from wide Gfx() access.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IRenderBackend : public IRenderDeviceLifecycle,
                       public IRenderResourceFactory,
                       public IRenderCommandContext,
                       public IRenderDiagnostics,
                       public IRenderCaptureBackend
{
  public:
    ~IRenderBackend() override = default;
};


// --- Global Render Backend Accessor ---

IRenderBackend& Gfx();
bool IsGfxReady();
void SetGfxBackend( std::unique_ptr<IRenderBackend> backend );
void DestroyGfxBackend();


} // namespace Rendering
} // namespace SkullbonezCore
