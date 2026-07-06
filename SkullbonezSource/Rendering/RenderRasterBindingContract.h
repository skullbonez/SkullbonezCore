/*
File: SkullbonezSource/Rendering/RenderRasterBindingContract.h
Purpose:
  Names the ordinary raster shader binding contract shared by runtime passes and
  the active render backend.

Mental model:
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
  - Engine texture slot N maps to shader resource register tN.
  - The ordinary raster root signature exposes slots t0 through t4.
  - Material-table work must update this contract, matching HLSL registers, and
    shader contract docs together.

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

inline constexpr std::uint32_t ROOT_PARAMETER_FRAME_CONSTANTS = 0;      // CBV b0
inline constexpr std::uint32_t ROOT_PARAMETER_FIRST_TEXTURE = 1;        // t0 descriptor table
inline constexpr std::uint32_t SHADER_REGISTER_FRAME_CONSTANTS = 0;     // b0
inline constexpr std::uint32_t SHADER_REGISTER_FIRST_TEXTURE = 0;       // t0
inline constexpr std::uint32_t SAMPLER_REGISTER_LINEAR_WRAP = 0;        // s0
inline constexpr std::uint32_t SAMPLER_REGISTER_LINEAR_CLAMP = 1;       // s1
inline constexpr std::uint32_t SAMPLER_REGISTER_SHADOW_POINT_CLAMP = 3; // s3
inline constexpr int TEXTURE_SLOT_COUNT = 5;                            // SRV slots t0..t4
inline constexpr std::uint32_t ORDINARY_RASTER_ROOT_PARAMETER_COUNT =
    ROOT_PARAMETER_FIRST_TEXTURE + static_cast<std::uint32_t>( TEXTURE_SLOT_COUNT );

static_assert( TEXTURE_SLOT_COUNT == 5,
               "Ordinary raster ABI exposes SRV slots t0..t4, including t4 for the object material table." );

} // namespace Rendering
} // namespace SkullbonezCore
