/*
File: SkullbonezSource/Rendering/IRenderBackend.h
Purpose:
  Declares the temporary aggregate render interface implemented by the DX12 backend.

Mental model:
  The renderer is being split into narrower capability interfaces. Existing
  runtime owners receive IRenderBackend only through startup-provided borrows;
  most code should depend on one of the narrower capability facets instead of
  this aggregate.

Glossary:
  Capability interface: Narrow borrowed surface that exposes one category of
    renderer behavior.
  Render device: Engine-facing object that owns the active GPU backend and its
    resources.
  Aggregate: Temporary compatibility type that groups narrower capabilities
    while call sites migrate.
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
  - SkullbonezSource/Rendering/IRenderCommandContext.h
  - SkullbonezSource/Rendering/IRenderDeviceLifecycle.h
  - SkullbonezSource/Rendering/IRenderDiagnostics.h
  - SkullbonezSource/Rendering/IRenderResourceFactory.h
*/
#pragma once

#include <cstdint>
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
    type is the migration path away from wide renderer access.
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

} // namespace Rendering
} // namespace SkullbonezCore
