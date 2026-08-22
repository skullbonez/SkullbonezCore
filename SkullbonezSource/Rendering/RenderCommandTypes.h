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
    or retained segment payloads submitted for one frame.
  Instanced mesh: Static mesh plus per-instance data drawn many times in one
    backend call.
  Raster bucket: Pass-local value naming one complete fixed-function PSO recipe.
  Retained geometry chunk: Stable compact record slice whose physical address
    does not change when another feature-owned range grows.

Invariants:
  - Callers borrow concrete command owners only while DX12 is initialized.
  - Texture and buffer handles are opaque backend-owned ids.
  - Every graphics draw derives its PSO from the bucket carried by that draw;
    no command owner exposes ambient raster setter/query authority.
  - Packed spans carry storage bounds. Dynamic handles/styles own their vertex
    layout, and malformed dynamic divisibility rejects the draw before upload.
  - A retained packet's range records address only its borrowed compact span;
    the backend validates each populated range before reading that span.
  - Retained chunk handles name physical storage; draw order is a separate
    value so canonical presentation never requires moving compact records.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "RenderGraph.h"
#include "../Maths/Matrix4.h"

#include <array>
#include <cstddef>
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
// active pass while the graph executor validates resource-state transitions separately.
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
    InstancedRibbon,
    InstancedRibbonDepthHint
};

enum class RetainedGeometryLane : uint8_t
{
    Ordinary,
    Priority
};

struct RetainedGeometryCapacity
{
    uint32_t floatsPerRecord = 0;
    uint32_t ordinaryRecordCapacity = 0;
    uint32_t priorityRecordCapacity = 0;
    uint32_t ordinaryLineFloatCapacity = 0;
    uint32_t priorityLineFloatCapacity = 0;
    uint32_t rangeCapacity = 0;
};

struct RetainedGeometryStreamToken
{
    uint64_t identity = 0;
    uint64_t revision = 0;
};

// Concept: one token describes a stable feature-owned chunk without teaching
// Rendering what the records mean. `drawOrder` preserves caller order while
// `cacheSlot` keeps backend upload history attached to the physical chunk.
struct RetainedGeometryRangeToken
{
    uint64_t identity = 0;
    uint64_t drawOrder = 0;
    uint32_t firstRecord = 0;
    uint32_t recordCapacity = 0;
    uint32_t recordCount = 0;

    // Feature replacement version plus a possible repaired predecessor. The
    // backend refreshes the changed tail without interpreting the feature record.
    uint32_t sourceVersion = 0;
    uint32_t cacheSlot = 0;
    uint32_t continuationRange = UINT32_MAX;
    RetainedGeometryLane lane = RetainedGeometryLane::Ordinary;
};

// Concept: feature-neutral retained overlay values cross into Rendering as
// already-packed ribbons and colored line vertices. Rendering owns submission
// only; the upstream feature owns record meaning, sampling, colors, and the
// monotonically increasing source sequence used by validation.
struct RetainedGeometryPacket
{
    std::span<const float> compactRibbonRecords;
    std::span<const RetainedGeometryRangeToken> ribbonRanges;
    std::span<const float> coloredLineVertices;
    RetainedGeometryStreamToken stream;
    std::uint64_t sourceSequence = 0u;
    std::uint32_t pointMarkerCount = 0u;

    bool HasGeometry() const noexcept
    {
        return ( !compactRibbonRecords.empty() && !ribbonRanges.empty() ) || !coloredLineVertices.empty();
    }
};

struct RetainedGeometryUploadPlan
{
    bool uploadRequired = false;
    std::size_t firstChangedUnit = 0;
};

// Concept: retained buffers are invalidated by value tokens, not by frame
// number. Equal tokens therefore produce no upload plan.
constexpr RetainedGeometryUploadPlan BuildRetainedGeometryUploadPlan( RetainedGeometryStreamToken cached,
                                                                      std::size_t cachedUnitCount,
                                                                      RetainedGeometryStreamToken incoming,
                                                                      std::size_t incomingUnitCount,
                                                                      bool repairPreviousUnit ) noexcept
{
    if ( cached.identity == incoming.identity && cached.revision == incoming.revision )
    {
        return {};
    }

    const bool append = cached.identity == incoming.identity && cachedUnitCount <= incomingUnitCount;
    std::size_t firstChangedUnit = append ? cachedUnitCount : 0u;

    if ( append && repairPreviousUnit && firstChangedUnit > 0u )
    {
        --firstChangedUnit;
    }

    return { true, firstChangedUnit };
}

// Concept: each retained range owns an independent compact slice. A sibling
// append may advance the stream revision without changing this slice, while
// extending the slice repairs only its formerly open adjacency tail.
constexpr RetainedGeometryUploadPlan
BuildRetainedGeometryRangeUploadPlan( const RetainedGeometryRangeToken& cached,
                                      const RetainedGeometryRangeToken& incoming ) noexcept
{
    const bool sameRange = cached.identity == incoming.identity && cached.sourceVersion == incoming.sourceVersion;

    if ( sameRange && cached.recordCount == incoming.recordCount )
    {
        return {};
    }

    if ( sameRange && cached.recordCount < incoming.recordCount )
    {
        return { true, cached.recordCount > 0u ? cached.recordCount - 1u : 0u };
    }

    return { true, 0u };
}

} // namespace Rendering
} // namespace SkullbonezCore
