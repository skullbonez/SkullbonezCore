/*
File: SkullbonezSource/Rendering/RenderRasterBindingContract.h
Purpose:
  Defines the named UnifiedRaster root-signature contract shared by every
  shipping raster shader family and the active render backend.

Summary:
  Runtime code binds textures by engine slot, while backend code translates
  those slots into shader-visible heap indices carried by root constants.
  Keeping the numbers here makes the ABI visible without browsing the concrete
  DX12 backend class.

Glossary:
  Raster binding ABI: Stable mapping between engine texture slots, shader
    registers, sampler registers, and graphics root-signature parameters.
  Root parameter: Root-signature entry that binds one constant buffer or a
    small inline constant payload for shaders.
  SRV slot: Engine texture slot whose payload value is a descriptor-heap index.
  Sampler register: Shader-visible sampler binding index used by material and
    shadow shaders.

Invariants:
  - UnifiedRaster is the one raster root signature. Lit, unlit, water, post,
    text, and UI families differ only in which rows they consume.
  - Engine texture slot N maps to bindless payload element N.
  - Reflection must reject raster bytecode outside b0/b1, s0/s1/s3, or register
    space zero before the root signature is published.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{

namespace UnifiedRasterRootSignature
{
inline constexpr const char* NAME = "UnifiedRaster";
inline constexpr std::uint32_t REGISTER_SPACE = 0;
inline constexpr std::uint32_t ROOT_PARAMETER_DRAW_CONSTANTS = 0;  // CBV b0, all raster stages
inline constexpr std::uint32_t ROOT_PARAMETER_TEXTURE_INDICES = 1; // six b1 root constants, pixel stage
inline constexpr std::uint32_t SHADER_REGISTER_DRAW_CONSTANTS = 0;
inline constexpr std::uint32_t SHADER_REGISTER_TEXTURE_INDICES = 1;
inline constexpr int TEXTURE_SLOT_COUNT = 6;
inline constexpr std::uint32_t ROOT_PARAMETER_COUNT = 2;

struct TextureSlot
{
    const char* name;
    std::uint32_t payloadIndex;
    const char* consumers;
};

// One slot map replaces shader-local ownership folklore. A family may leave a
// row unused, but it may not reinterpret the register or register space.
inline constexpr TextureSlot TEXTURE_SLOTS[] = {
    { "PrimaryTexture", 0, "lit/unlit/text/UI/post scene color" },
    { "SecondaryTexture", 1, "post depth or water reflection" },
    { "VolumetricTexture", 2, "post volumetric composite" },
    { "ShadowMap", 3, "lit shadow sampling" },
    { "MaterialTable", 4, "instanced material defaults" },
    { "DetailShadowMap", 5, "terrain tight object-shadow sampling" },
};

struct StaticSampler
{
    const char* name;
    std::uint32_t shaderRegister;
};

inline constexpr StaticSampler STATIC_SAMPLERS[] = {
    { "LinearWrap", 0 },
    { "LinearClamp", 1 },
    { "ShadowPointClamp", 3 },
};

inline constexpr bool AcceptsSamplerRegister( std::uint32_t slot )
{
    return slot == STATIC_SAMPLERS[0].shaderRegister || slot == STATIC_SAMPLERS[1].shaderRegister ||
           slot == STATIC_SAMPLERS[2].shaderRegister;
}

static_assert( sizeof( TEXTURE_SLOTS ) / sizeof( TEXTURE_SLOTS[0] ) == TEXTURE_SLOT_COUNT );
static_assert( TEXTURE_SLOTS[4].payloadIndex == 4, "UnifiedRaster keeps the material table at payload index 4." );
static_assert( TEXTURE_SLOTS[5].payloadIndex == 5, "UnifiedRaster keeps the terrain detail shadow at payload index 5." );
} // namespace UnifiedRasterRootSignature

// Engine-facing texture binding still uses an integer slot count. Native root
// parameter/register details stay inside the named contract above.
inline constexpr int TEXTURE_SLOT_COUNT = UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT;

} // namespace Rendering
} // namespace SkullbonezCore
