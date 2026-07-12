/*
File: SkullbonezSource/Rendering/RenderRasterBindingContract.h
Purpose:
  Defines the named UnifiedRaster root-signature contract shared by every
  shipping raster shader family and the active render backend.

Summary:
  Runtime code binds textures by engine slot, while backend code translates
  those slots into root-signature parameters, shader registers, and sampler
  registers. Keeping the numbers here makes the ABI visible without browsing
  the concrete DX12 backend class.

Glossary:
  Raster binding ABI: Stable mapping between engine texture slots, shader
    registers, sampler registers, and graphics root-signature parameters.
  Root parameter: Root-signature entry that binds one constant buffer or
    descriptor table for shaders.
  SRV slot: Engine texture slot that maps to one shader resource register.
  Sampler register: Shader-visible sampler binding index used by material and
    shadow shaders.

Invariants:
  - UnifiedRaster is the one raster root signature. Lit, unlit, water, post,
    text, and UI families differ only in which rows they consume.
  - Engine texture slot N maps to shader resource register tN.
  - Reflection must reject raster bytecode outside b0, t0..t5, s0/s1/s3, or
    register space zero before the root signature is published.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - SkullbonezSource/Runtime/RunPasses.cpp
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
inline constexpr std::uint32_t ROOT_PARAMETER_DRAW_CONSTANTS = 0; // CBV b0, all raster stages
inline constexpr std::uint32_t ROOT_PARAMETER_FIRST_TEXTURE = 1;  // one table for each t0..t5 row
inline constexpr std::uint32_t SHADER_REGISTER_DRAW_CONSTANTS = 0;
inline constexpr std::uint32_t SHADER_REGISTER_FIRST_TEXTURE = 0;
inline constexpr int TEXTURE_SLOT_COUNT = 6;
inline constexpr std::uint32_t ROOT_PARAMETER_COUNT =
    ROOT_PARAMETER_FIRST_TEXTURE + static_cast<std::uint32_t>( TEXTURE_SLOT_COUNT );

struct TextureSlot
{
    const char* name;
    std::uint32_t shaderRegister;
    std::uint32_t rootParameter;
    const char* consumers;
};

// One slot map replaces shader-local ownership folklore. A family may leave a
// row unused, but it may not reinterpret the register or register space.
inline constexpr TextureSlot TEXTURE_SLOTS[] = {
    { "PrimaryTexture",
      SHADER_REGISTER_FIRST_TEXTURE + 0,
      ROOT_PARAMETER_FIRST_TEXTURE + 0,
      "lit/unlit/text/UI/post scene color" },
    { "SecondaryTexture",
      SHADER_REGISTER_FIRST_TEXTURE + 1,
      ROOT_PARAMETER_FIRST_TEXTURE + 1,
      "post depth or water reflection" },
    { "VolumetricTexture",
      SHADER_REGISTER_FIRST_TEXTURE + 2,
      ROOT_PARAMETER_FIRST_TEXTURE + 2,
      "post volumetric composite" },
    { "ShadowMap", SHADER_REGISTER_FIRST_TEXTURE + 3, ROOT_PARAMETER_FIRST_TEXTURE + 3, "lit shadow sampling" },
    { "MaterialTable",
      SHADER_REGISTER_FIRST_TEXTURE + 4,
      ROOT_PARAMETER_FIRST_TEXTURE + 4,
      "instanced material defaults" },
    { "DetailShadowMap",
      SHADER_REGISTER_FIRST_TEXTURE + 5,
      ROOT_PARAMETER_FIRST_TEXTURE + 5,
      "terrain tight object-shadow sampling" },
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

inline constexpr bool AcceptsTextureRegister( std::uint32_t slot )
{
    return slot < static_cast<std::uint32_t>( TEXTURE_SLOT_COUNT );
}

inline constexpr bool AcceptsSamplerRegister( std::uint32_t slot )
{
    return slot == STATIC_SAMPLERS[0].shaderRegister || slot == STATIC_SAMPLERS[1].shaderRegister ||
           slot == STATIC_SAMPLERS[2].shaderRegister;
}

static_assert( sizeof( TEXTURE_SLOTS ) / sizeof( TEXTURE_SLOTS[0] ) == TEXTURE_SLOT_COUNT );
static_assert( TEXTURE_SLOTS[4].shaderRegister == 4 && TEXTURE_SLOTS[4].rootParameter == 5,
               "UnifiedRaster keeps the material table at t4/root parameter 5." );
static_assert( TEXTURE_SLOTS[5].shaderRegister == 5 && TEXTURE_SLOTS[5].rootParameter == 6,
               "UnifiedRaster appends the terrain detail shadow without moving t4." );
} // namespace UnifiedRasterRootSignature

// Engine-facing texture binding still uses an integer slot count. Native root
// parameter/register details stay inside the named contract above.
inline constexpr int TEXTURE_SLOT_COUNT = UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT;

} // namespace Rendering
} // namespace SkullbonezCore
