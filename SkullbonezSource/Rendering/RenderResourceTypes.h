/*
File: SkullbonezSource/Rendering/RenderResourceTypes.h
Purpose:
  Defines value types shared by concrete DX12 resources and runtime passes.

Summary:
  Render resources are concrete DX12 objects, while their authored format
  choices remain small backend-independent values. This header carries those
  values without recreating a resource interface.

Glossary:
  Framebuffer: Off-screen color/depth target used by cinematic and shadow passes.
  HDR (High Dynamic Range): Floating-point color that can represent values above white.

Invariants:
  - Values own no native resource, descriptor, or renderer pointer.
  - RGBA16F remains the cinematic HDR target format.

Related:
  - SkullbonezSource/Rendering/DX12/FramebufferDX12.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
*/
#pragma once

#include <cstdint>
#include <span>

namespace SkullbonezCore::Rendering
{
enum class FramebufferColorFormat
{
    RGBA8,
    RGBA16F
};

enum class TextureMipPolicy : uint8_t
{
    SingleLevel,
    Generate
};

enum class TextureFilterPolicy : uint8_t
{
    Nearest,
    Linear
};

struct Texture2DUploadDesc
{
    // Lifetime: pixels are borrowed only while the texture owner records the
    // cold upload. Policy is value state; no device or descriptor owner crosses.
    const uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    TextureMipPolicy mipPolicy = TextureMipPolicy::SingleLevel;
    TextureFilterPolicy filterPolicy = TextureFilterPolicy::Nearest;
};

struct InstancedMeshCreateDesc
{
    // Lifetime: vertex and layout spans are consumed synchronously during cold
    // GPU resource creation. They name one mesh layout, not renderer authority.
    const float* staticVertices = nullptr;
    int staticVertexCount = 0;
    int staticFloatsPerVertex = 0;
    int instanceFloats = 0;
    int instanceStartAttribute = 0;
    std::span<const int> instanceAttributeSizes;
    std::span<const int> staticAttributeSizes;
};
} // namespace SkullbonezCore::Rendering
