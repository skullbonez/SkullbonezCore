/*
File: SkullbonezSource/Rendering/IRenderCommandContext.h
Purpose:
  Declares the narrow render capability used to mutate frame draw state and
  submit immediate draw work.

Summary:
  Command-context callers are inside the frame: they bind textures, set
  viewport/depth/blend/cull state, upload transient geometry, and ask the active
  backend to draw. Modernized passes carry pass-local raster buckets on their
  draw calls while legacy passes retain setters until M3. Callers should not be
  able to create long-lived resources, resize the device, present the swap
  chain, or capture the back buffer.

Glossary:
  Command context: Borrowed rendering surface for draw-state changes and draw
    submission during a frame.
  Viewport: Pixel rectangle that maps clip-space output into the current target.
  Blend state: Rule for combining new pixel color with the target's old color.
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
  - Callers borrow this interface only while the renderer is initialized.
  - Texture and buffer handles are opaque backend-owned ids.
  - State query methods report the backend's tracked engine state so callers can
    restore temporary overlay changes without knowing native API state.
  - A declared-raster draw derives its PSO only from its bucket, never from the
    legacy setter/query state retained for unmigrated passes.

Related:
  - SkullbonezSource/Rendering/IRenderResourceFactory.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h
*/
#pragma once

#include "RenderGraph.h"

#include <cstdint>

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

enum class TransientTriangleStyle
{
    Color,
    SoftAdditiveRibbon,
    TrajectoryRibbon,
    TrajectoryRibbonDepthHint
};

class IRenderCommandContext
{
  public:
    virtual ~IRenderCommandContext() = default;

    virtual void SetViewport( int x, int y, int w, int h ) = 0;
    virtual void Clear( bool color, bool depth ) = 0;
    virtual void SetClearColor( float r, float g, float b, float a ) = 0;
    virtual void SetClearDepth( float depth ) = 0;

    virtual void SetDepthTest( bool enable ) = 0;
    virtual void SetDepthWrite( bool enable ) = 0;
    virtual void SetBlend( bool enable ) = 0;
    virtual void SetBlendFunc( BlendFactor src, BlendFactor dst ) = 0;
    virtual void SetCullFace( bool enable ) = 0;
    virtual void SetPolygonOffset( bool enable, float factor = 0.0f, float units = 0.0f ) = 0;
    virtual void SetClipPlane( int index, bool enable ) = 0;

    virtual void BindTexture( uint32_t handle, int slot ) = 0;
    // Concept: graph-declared transient textures resolve through the command
    // context so runtime passes can bind ordinary engine texture handles
    // without learning native DX12 descriptor or resource ownership.
    virtual RenderGraphTransientMaterializationStats
    MaterializeGraphTransientResources( const RenderGraph& graph, const RenderGraphCompileResult& compiled )
    {
        (void)graph;
        (void)compiled;
        return {};
    }
    virtual RenderGraphTextureBinding ResolveGraphTextureBinding( RenderGraphResourceHandle resource ) const
    {
        (void)resource;
        return {};
    }
    // Resolves an opaque engine texture handle to a non-owning token that the
    // graph transports back to the concrete backend executor.
    virtual RenderGraphNativeResourceToken ResolveGraphResourceToken( uint32_t textureHandle ) const
    {
        (void)textureHandle;
        return {};
    }
    // Returns the current swap-chain image plus its tracked access so the
    // first executable graph pass can compile the normal Present -> RT edge.
    virtual RenderGraphBackbufferBinding ResolveGraphBackbufferBinding() const
    {
        return {};
    }
    // Executes every compiled transition assigned to one callback pass. The
    // returned count proves that declared producer/consumer edges became live.
    virtual size_t
    ExecuteGraphTransitions( const RenderGraph& graph, const RenderGraphCompileResult& compiled, uint32_t passIndex )
    {
        (void)graph;
        (void)compiled;
        (void)passIndex;
        return 0;
    }
    virtual void BeginGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
    {
        (void)binding;
        (void)passName;
    }
    virtual void EndGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
    {
        (void)binding;
        (void)passName;
    }

    virtual bool IsDepthTestEnabled() const = 0;
    virtual bool IsDepthWriteEnabled() const = 0;
    virtual bool IsBlendEnabled() const = 0;
    virtual bool IsCullFaceEnabled() const = 0;
    virtual void GetBlendFunc( BlendFactor& outSrc, BlendFactor& outDst ) const = 0;

    virtual void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) = 0;
    // M1 pilot path: precompile and draw consume the same pass-local value, so
    // the backend never reconstructs this draw's PSO from setter history.
    virtual bool PrecompileDynamicVBRasterState( uint32_t handle, const PassRasterStateBucket& bucket ) = 0;
    virtual void UploadAndDrawDynamicVB( uint32_t handle,
                                         const float* data,
                                         int vertexCount,
                                         const PassRasterStateBucket& bucket ) = 0;
    virtual void DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 )
    {
        (void)data;
        (void)vertCount;
        (void)viewProjMatrix16;
    }
    virtual void DrawTransientColoredTriangles( const float* data,
                                                int vertexCount,
                                                const float* viewProjMatrix16,
                                                TransientTriangleStyle style = TransientTriangleStyle::Color )
    {
        (void)data;
        (void)vertexCount;
        (void)viewProjMatrix16;
        (void)style;
    }

    virtual void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) = 0;
    virtual void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore
