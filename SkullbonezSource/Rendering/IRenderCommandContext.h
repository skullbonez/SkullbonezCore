/*
File: SkullbonezSource/Rendering/IRenderCommandContext.h
Purpose:
  Declares the narrow render capability used to mutate frame draw state and
  submit immediate draw work.

Mental model:
  Command-context callers are inside the frame: they bind textures, set
  viewport/depth/blend/cull state, upload transient geometry, and ask the active
  backend to draw. They should not be able to create long-lived resources,
  resize the device, present the swap chain, or capture the back buffer.

Glossary:
  Command context: Borrowed rendering surface for draw-state changes and draw
    submission during a frame.
  Viewport: Pixel rectangle that maps clip-space output into the current target.
  Blend state: Rule for combining new pixel color with the target's old color.
  Dynamic vertex buffer: Backend-owned transient buffer used for text, overlays,
    and other per-frame geometry.
  Replay ribbon: Camera-facing overlay stroke expanded from a replay debug line
    segment so the shader can smooth and glow it.
  Instanced mesh: Static mesh plus per-instance data drawn many times in one
    backend call.

Invariants:
  - Callers borrow this interface only while the renderer is initialized.
  - Texture and buffer handles are opaque backend-owned ids.
  - State query methods report the backend's tracked engine state so callers can
    restore temporary overlay changes without knowing native API state.

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
    // Concept: graph-owned textures resolve through the command context so
    // runtime passes can bind ordinary engine texture handles without learning
    // native DX12 descriptor or resource ownership.
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
    virtual void DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 )
    {
        (void)data;
        (void)vertCount;
        (void)viewProjMatrix16;
    }
    virtual void DrawTransientColoredTriangles( const float* data, int vertexCount, const float* viewProjMatrix16 )
    {
        (void)data;
        (void)vertexCount;
        (void)viewProjMatrix16;
    }
    // Draws replay-only ribbon vertices packed as position/color/style floats.
    // Generic debug overlays stay on DrawLinesColored; this path is for
    // camera-facing replay strokes that need shader-controlled edge falloff.
    virtual void DrawReplayRibbons( const float* data, int vertexCount, const float* viewProjMatrix16 )
    {
        (void)data;
        (void)vertexCount;
        (void)viewProjMatrix16;
    }

    virtual void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) = 0;
    virtual void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore
