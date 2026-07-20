/*
File: SkullbonezSource/Rendering/IRenderRayTracing.h
Purpose:
  Declares the narrow render raytracing capability used by water reflection setup and dispatch.

Summary:
  Most runtime rendering needs an ordinary raster device. DXR reflection is a
  separate capability with its own acceleration structures, writable reflection
  texture, and typed setup/dispatch values. Callers that only need reflection
  rays should depend on this interface instead of the concrete backend owner.

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
  - Setup and dispatch are complete operation values; callers never rely on
    positional geometry, matrix, vector, or environment-texture arguments.

Related:
  - SkullbonezSource/Rendering/IRenderDiagnostics.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/SbResult.h"
#include "../Maths/Matrix4.h"
#include "../Maths/Vector3.h"

#include <cstdint>
#include <span>


namespace SkullbonezCore
{
namespace Rendering
{

struct RaytracingGeometryDesc
{
    // Lifetime: the borrowed GPU address must remain valid through cold setup;
    // the backend copies it into an acceleration structure before returning.
    uint64_t vertexBufferAddress = 0;
    int vertexCount = 0;
    int vertexStride = 0;
};

struct RaytracingSetupDesc
{
    RaytracingGeometryDesc terrain;
    RaytracingGeometryDesc sphere;
    int maxInstances = 0;
};

struct ReflectionEnvironmentTextures
{
    uint32_t sphere = 0;
    uint32_t terrain = 0;
    uint32_t skyUp = 0;
    uint32_t skyDown = 0;
    uint32_t skyRight = 0;
    uint32_t skyLeft = 0;
    uint32_t skyFront = 0;
    uint32_t skyBack = 0;
};

struct WaterReflectionRayDesc
{
    // Invariant: every environment handle names a registered engine texture.
    // The backend resolves all eight handles as one contiguous shader table.
    Math::Transformation::Matrix4 inverseViewProjection;
    Math::Vector::Vector3 cameraPosition;
    Math::Vector::Vector3 lightPosition;
    Math::Vector::Vector3 skyColorTop{ 0.4f, 0.6f, 0.9f };
    Math::Vector::Vector3 skyColorBottom{ 0.7f, 0.8f, 0.95f };
    float waterHeight = 0.0f;
    float simulationTimeSeconds = 0.0f;
    ReflectionEnvironmentTextures textures;
};

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

    // Creates device-lifetime raytracing resources from one complete cold-setup
    // value. Repeated calls after successful setup are harmless.
    virtual SkullbonezCore::Core::SbResult InitDXR( const RaytracingSetupDesc& setup ) = 0;
    // Records one water-reflection dispatch using current scene instances and
    // the complete camera/light/environment description for this frame.
    virtual void DispatchReflectionRays( const WaterReflectionRayDesc& reflection ) = 0;
    // Rebuilds the scene instance table from bounded engine matrices. Terrain
    // is backend-owned and inserted separately as instance zero.
    virtual void BuildTLAS( std::span<const Math::Transformation::Matrix4> instanceTransforms ) = 0;
    virtual uint32_t GetReflectionUAVTexture() const = 0;
    virtual void ShutdownDXR() = 0;
    virtual uint64_t GetInstancedMeshStaticVBVA( uint32_t handle ) const = 0;
    virtual int GetInstancedMeshStaticStride( uint32_t handle ) const = 0;
};


} // namespace Rendering
} // namespace SkullbonezCore
