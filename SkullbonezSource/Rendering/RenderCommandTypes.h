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
  Retained trajectory chunk: Stable compact segment slice whose physical
    address does not change when another prediction path grows.

Invariants:
  - Callers borrow concrete command owners only while DX12 is initialized.
  - Texture and buffer handles are opaque backend-owned ids.
  - Every graphics draw derives its PSO from the bucket carried by that draw;
    no command owner exposes ambient raster setter/query authority.
  - Packed spans carry storage bounds. Dynamic handles/styles own their vertex
    layout, and malformed dynamic divisibility rejects the draw before upload.
  - Retained chunk handles name physical storage; draw order is a separate
    value so canonical presentation never requires moving compact records.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h
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

inline constexpr std::size_t RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT = 19u;
inline constexpr std::size_t RETAINED_TRAJECTORY_ORDINARY_SEGMENT_CAPACITY = 24000u;
inline constexpr std::size_t RETAINED_TRAJECTORY_PRIORITY_SEGMENT_CAPACITY = 3000u;
inline constexpr std::size_t RETAINED_TRAJECTORY_MAX_DRAW_RANGES = 4096u;

// Concept: one range is a stable chunk of the retained trajectory arena.
// `drawOrder` preserves record-major presentation even though physical chunks
// append in publication order, while `cacheSlot` keeps DX12 upload history
// attached to the physical chunk when the command list is sorted.
struct RetainedTrajectoryDrawRange
{
    uint64_t identity = 0;
    uint64_t drawOrder = 0;
    uint32_t firstSegment = 0;
    uint32_t segmentCapacity = 0;
    uint32_t segmentCount = 0;
    // Record replacement version plus rare continuation-tail repairs. DX12 uses
    // this token to refresh a full eight-segment chunk when its closed cap joins
    // the first segment of a newly allocated continuation.
    uint32_t sourceVersion = 0;
    uint32_t cacheSlot = 0;
    uint32_t continuationRange = RETAINED_TRAJECTORY_MAX_DRAW_RANGES;
    bool priority = false;
};

// Appends one already-styled compact record into its stable trajectory slice.
// Invariant: only the formerly open tail may change; every earlier record and
// every sibling range remain byte-for-byte stable.
inline bool AppendRetainedTrajectoryRecord( std::span<float> arena,
                                            RetainedTrajectoryDrawRange& range,
                                            const std::array<float, RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT>& incoming,
                                            float continuityToleranceSquared ) noexcept
{
    if ( range.segmentCount >= range.segmentCapacity )
    {
        return false;
    }
    const std::size_t segmentIndex = static_cast<std::size_t>( range.firstSegment ) + range.segmentCount;
    const std::size_t firstFloat = segmentIndex * RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT;
    if ( firstFloat + RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT > arena.size() )
    {
        return false;
    }

    std::array<float, RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT> record = incoming;
    if ( range.segmentCount > 0u )
    {
        float* previous = arena.data() + firstFloat - RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT;
        const float dx = previous[3] - record[0];
        const float dy = previous[4] - record[1];
        const float dz = previous[5] - record[2];
        const bool samePresentation = previous[6] == record[6] && previous[10] == record[10] &&
                                      previous[11] == record[11] && previous[12] == record[12];
        if ( samePresentation && dx * dx + dy * dy + dz * dz <= continuityToleranceSquared )
        {
            record[13] = previous[0];
            record[14] = previous[1];
            record[15] = previous[2];
            previous[16] = record[3];
            previous[17] = record[4];
            previous[18] = record[5];
        }
    }

    float* destination = arena.data() + firstFloat;
    for ( std::size_t component = 0; component < RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT; ++component )
    {
        destination[component] = record[component];
    }
    ++range.segmentCount;
    return true;
}

// Appends the first record of a non-contiguous continuation chunk and repairs
// adjacency across the physical gap. Only the previous chunk's final compact
// record changes; its source token is advanced so GPU caches refresh that tail.
inline bool
AppendRetainedTrajectoryContinuationRecord( std::span<float> arena,
                                            RetainedTrajectoryDrawRange& previousRange,
                                            RetainedTrajectoryDrawRange& range,
                                            const std::array<float, RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT>& incoming,
                                            float continuityToleranceSquared ) noexcept
{
    if ( range.segmentCount != 0u )
    {
        return false;
    }
    if ( !AppendRetainedTrajectoryRecord( arena, range, incoming, continuityToleranceSquared ) )
    {
        return false;
    }
    if ( previousRange.segmentCount == 0u )
    {
        return true;
    }

    const std::size_t previousSegment = static_cast<std::size_t>( previousRange.firstSegment ) +
                                        previousRange.segmentCount - 1u;
    const std::size_t currentSegment = static_cast<std::size_t>( range.firstSegment );
    const std::size_t previousFloat = previousSegment * RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT;
    const std::size_t currentFloat = currentSegment * RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT;
    if ( previousFloat + RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT > arena.size() ||
         currentFloat + RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT > arena.size() )
    {
        return true;
    }

    float* previous = arena.data() + previousFloat;
    float* current = arena.data() + currentFloat;
    const float dx = previous[3] - current[0];
    const float dy = previous[4] - current[1];
    const float dz = previous[5] - current[2];
    const bool samePresentation = previous[6] == current[6] && previous[10] == current[10] &&
                                  previous[11] == current[11] && previous[12] == current[12];
    if ( samePresentation && dx * dx + dy * dy + dz * dz <= continuityToleranceSquared )
    {
        current[13] = previous[0];
        current[14] = previous[1];
        current[15] = previous[2];
        previous[16] = current[3];
        previous[17] = current[4];
        previous[18] = current[5];
        ++previousRange.sourceVersion;
    }
    return true;
}

} // namespace Rendering
} // namespace SkullbonezCore
