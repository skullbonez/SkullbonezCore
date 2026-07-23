/*
File: SkullbonezSource/Rendering/RenderCommandTypes.h
Purpose:
  Declares value records shared by concrete DX12 render-command owners.

Summary:
  Pass code carries complete raster, clear, instancing, and transient-triangle
  values into the narrow concrete owner that records each operation. This file
  owns no command capability and introduces no facade across those owners.

Glossary:
  Command owner: Concrete DX12 owner responsible for one command category.
  Viewport: Pixel rectangle that maps clip-space output into the current target.
  Blend recipe: Rule for combining new pixel color with the target's old color.
  Dynamic vertex buffer: Backend-owned transient buffer used for text, overlays,
    and other per-frame geometry.
  Transient triangle style: Shader interpretation for packed overlay triangles
    or replay segment payloads submitted for one frame.
  Instanced mesh: Static mesh plus per-instance data drawn many times in one
    backend call.
  Compiled transition: Render-graph state edge assigned to a specific pass and
    resource before callbacks record live commands.
  Raster bucket: Pass-local value naming one complete fixed-function PSO recipe.

Invariants:
  - Callers borrow concrete command owners only while DX12 is initialized.
  - Texture and buffer handles are opaque backend-owned ids.
  - Every graphics draw derives its PSO from the bucket carried by that draw;
    no command owner exposes ambient raster setter/query authority.
  - Packed spans carry storage bounds. Dynamic handles/styles own their vertex
    layout, and malformed dynamic divisibility rejects the draw before upload.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h
*/
#pragma once

#include "RenderGraph.h"
#include "../Maths/Matrix4.h"

#include <cstdint>
#include <span>

namespace SkullbonezCore
{
namespace Rendering
{

// Blend factor enum (matches the subset used by the engine)
enum class BlendFactor
{
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha
};

// Concept: declared raster state is a complete fixed-function recipe selected
// by a draw. It is a value boundary, not a request to mutate ambient backend
// state. Target formats remain graph/pass-owned and are validated against the
// active pass while M1 pilots the state half of the contract.
enum class CullMode
{
    None,
    Back
};

enum class RenderTargetFormatExpectation
{
    ActivePass
};

struct DepthBiasDesc
{
    bool enabled = false;
    float constant = 0.0f;
    float slopeScaled = 0.0f;
};

struct RenderTargetFormatSet
{
    RenderTargetFormatExpectation color = RenderTargetFormatExpectation::ActivePass;
    RenderTargetFormatExpectation depth = RenderTargetFormatExpectation::ActivePass;
    uint8_t sampleCount = 1;
};

struct RasterStateDesc
{
    bool depthTest = true;
    bool depthWrite = true;
    bool blendEnabled = false;
    BlendFactor sourceBlend = BlendFactor::One;
    BlendFactor destinationBlend = BlendFactor::Zero;
    CullMode cullMode = CullMode::Back;
    DepthBiasDesc depthBias;
    RenderTargetFormatSet targets;
};

struct RasterStateBucketId
{
    uint8_t value = 0;
};

struct PassRasterStateBucket
{
    // Invariant: this id is meaningful only inside the declaring pass. It is
    // diagnostic identity, never a durable cross-pass or backend cache key.
    RasterStateBucketId id;
    RasterStateDesc raster;
};

struct InstancedMeshDrawDesc
{
    uint32_t handle = 0;
    int staticVertexCount = 0;
    int instanceCount = 0;
    PassRasterStateBucket rasterState;
};

struct ClearTargetDesc
{
    // Values travel on the clear operation so no later pass can inherit hidden
    // backend clear state from an earlier target.
    bool color = true;
    bool depth = true;
    float colorValue[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float depthValue = 1.0f;
};

// Pass code names the bucket id while spelling out the complete fixed-function
// recipe. Defaults deliberately match an opaque depth-tested mesh draw.
constexpr PassRasterStateBucket MakePassRasterStateBucket( uint8_t id, RasterStateDesc raster = {} )
{
    return { { id }, raster };
}

enum class TransientTriangleStyle
{
    Color,
    SoftAdditiveRibbon,
    TrajectoryRibbon,
    TrajectoryRibbonDepthHint
};

} // namespace Rendering
} // namespace SkullbonezCore
