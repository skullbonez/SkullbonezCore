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

Invariants:
  - Values own no native resource, descriptor, or renderer pointer.
  - RGBA16F remains the cinematic HDR target format.

Related:
  - SkullbonezSource/Rendering/DX12/FramebufferDX12.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
  - Agentic/Reference/engine-glossary.md
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

} // namespace SkullbonezCore::Rendering
