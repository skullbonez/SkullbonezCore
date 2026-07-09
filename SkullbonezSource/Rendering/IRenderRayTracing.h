/*
File: SkullbonezSource/Rendering/IRenderRayTracing.h
Purpose:
  Declares the narrow render raytracing capability used by water reflection setup and dispatch.

Mental model:
  Most runtime rendering needs an ordinary raster device. DXR reflection is a
  separate capability with its own acceleration structures, writeable reflection
  texture, and mesh geometry addresses. Callers that only need reflection rays
  should depend on this interface instead of the concrete render backend owner.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene instances that point at BLAS geometry.
  UAV (Unordered Access View): Descriptor row used when raytracing writes the reflection texture.
  GPU VA (GPU Virtual Address): Device address used by raytracing geometry records.
  Lane R result: Recoverable DXR device/shader/resource failure returned to the
    scene-load boundary instead of throwing through renderer setup.

Invariants:
  - The capability is borrowed from the active renderer; callers must not cache it across backend teardown.
  - Feature support is still queried through RenderCapabilities before dispatching reflection rays.
  - Texture handles returned here are engine texture handles, not native DX12 descriptors.

Related:
  - SkullbonezSource/Rendering/IRenderDiagnostics.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/SbResult.h"

#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{

/* -- IRenderRayTracing
---------------------------------------------------------------------------------------------------------------------------------------------

    Narrow renderer capability for the DXR-backed water reflection path.
    The method names intentionally retain the current DXR vocabulary while this
    subsystem is still DX12-only; the important boundary is that non-raytracing
    callers receive these functions only through the optional raytracing facet.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IRenderRayTracing
{
  public:
    virtual ~IRenderRayTracing() = default;

    virtual Basics::SbResult InitDXR( uint64_t terrainVBVA,
                                      int terrainVertCount,
                                      int terrainStride,
                                      uint64_t sphereVBVA,
                                      int sphereVertCount,
                                      int sphereStride,
                                      int maxInstances ) = 0;
    virtual void DispatchReflectionRays( const float* invViewProj,
                                         const float* cameraPos,
                                         float waterY,
                                         float time,
                                         const float* lightPos,
                                         const float* skyColorTop,
                                         const float* skyColorBottom,
                                         int width,
                                         int height,
                                         uint32_t sphereTexHandle,
                                         uint32_t terrainTexHandle,
                                         uint32_t skyUpHandle,
                                         uint32_t skyDownHandle,
                                         uint32_t skyRightHandle,
                                         uint32_t skyLeftHandle,
                                         uint32_t skyFrontHandle,
                                         uint32_t skyBackHandle ) = 0;
    virtual void
    BuildTLAS( const float* instanceTransforms, int instanceCount, uint64_t terrainBLAS, uint64_t sphereBLAS ) = 0;
    virtual uint32_t GetReflectionUAVTexture() const = 0;
    virtual void ShutdownDXR() = 0;
    virtual uint64_t GetInstancedMeshStaticVBVA( uint32_t handle ) const = 0;
    virtual int GetInstancedMeshStaticStride( uint32_t handle ) const = 0;
};


} // namespace Rendering
} // namespace SkullbonezCore
