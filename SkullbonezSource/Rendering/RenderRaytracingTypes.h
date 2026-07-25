/*
File: SkullbonezSource/Rendering/RenderRaytracingTypes.h
Purpose:
  Defines value packets shared by runtime rendering and the concrete DX12
  raytracing owner.

Summary:
  Runtime passes describe cold geometry setup and one reflection dispatch with
  complete values. Native resources, descriptor rows, and command recording
  remain private to the DX12 owner.

Glossary:
  BLAS (Bottom-Level Acceleration Structure): Raytracing index for one mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Scene instance table that references BLAS geometry.
  UAV (Unordered Access View): Descriptor row used when raytracing writes the reflection texture.
  GPU VA (GPU Virtual Address): Device address of vertex data used during cold acceleration-structure setup.

Invariants:
  - Texture handles are engine texture handles, never native descriptor indices.
  - Geometry GPU addresses remain valid through the synchronous cold setup.
  - Dispatch packets own values only and retain no renderer or scene pointers.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp
*/
#pragma once

#include "../Maths/Matrix4.h"
#include "../Maths/Vector3.h"

#include <cstdint>

namespace SkullbonezCore::Rendering
{
struct RaytracingGeometryDesc
{
    // Lifetime: the borrowed GPU address remains valid until cold setup copies
    // its geometry into the acceleration structure.
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
    // The DX12 owner resolves all eight handles as one contiguous shader table.
    Math::Transformation::Matrix4 inverseViewProjection;
    Math::Vector::Vector3 cameraPosition;
    Math::Vector::Vector3 lightPosition;
    Math::Vector::Vector3 skyColorTop { 0.4f, 0.6f, 0.9f };
    Math::Vector::Vector3 skyColorBottom { 0.7f, 0.8f, 0.95f };
    float waterHeight = 0.0f;
    float simulationTimeSeconds = 0.0f;
    ReflectionEnvironmentTextures textures;
};
} // namespace SkullbonezCore::Rendering
