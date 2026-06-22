/*
File: SkullbonezSource/Rendering/IRenderBackend.h
Purpose:
  Declares the engine-facing render device contract implemented by DX12.

Mental model:
  Renderer-facing code asks for engine concepts such as textures, shaders,
  framebuffers, draw calls, captures, and diagnostics. The DX12 backend maps
  those requests to descriptors, resources, command lists, and fences.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BGR (Blue, Green, Red): Capture byte order used by BMP files.
  BMP (Bitmap): Simple image file format used by validation backbuffer captures.
  Render device: Engine-facing object that owns the active GPU backend and its
  resources.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  HWND (Window Handle): Win32 identifier for the native application window.
  HDC (Handle to Device Context): Win32 drawing context paired with an HWND.
  UAV (Unordered Access View): Descriptor row used when shaders write to a GPU
  resource such as the raytraced reflection texture.
  GPU VA (GPU Virtual Address): Device address used by DXR geometry records.
  VB (Vertex Buffer): GPU buffer containing vertex attributes for a mesh.

Related:
  - SkullbonezSource/Rendering/IRenderBackend.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>
#include <memory>
#include <vector>
#include <windows.h>
#include "../Core/Common.h"
#include "DrawCallTrace.h"
#include "IShader.h"
#include "IMesh.h"
#include "IFramebuffer.h"


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

struct RenderCapabilities
{
    bool supportsBackbufferCapture = true;
    bool supportsGpuTimers = false;
    bool supportsDxrReflection = false;
    bool supportsDebugLines = false;
};

class IRenderCaptureBackend
{
  public:
    virtual ~IRenderCaptureBackend() = default;

    virtual RenderCapabilities GetCapabilities() const = 0;

    // Capture data is BGR and bottom-up so validation artifacts can be written
    // straight to BMP without a second image-layout conversion.
    virtual std::vector<uint8_t> CaptureBackbuffer( int& outWidth, int& outHeight ) = 0;
};


/* -- IRenderBackend
---------------------------------------------------------------------------------------------------------------------------------------------

    Engine-facing render device interface. It owns GPU state and resource creation
    for the active DX12 backend. One global instance is set during init and
    accessed via Gfx().

    Keep this surface in engine terms. DX12 concepts such as descriptor handles,
    root parameters, command allocator fences, and D3D12 barrier structs belong
    in RenderBackendDX12 and its helper subsystems, not in callers.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IRenderBackend : public IRenderCaptureBackend
{

  public:
    virtual ~IRenderBackend() = default;


    // --- Lifecycle ---

    virtual bool Init( HWND hwnd, HDC hdc, int width, int height ) = 0;
    virtual void Shutdown() = 0;
    virtual void Present() = 0;
    virtual void SetVsyncEnabled( bool enabled ) = 0;
    virtual bool IsVsyncEnabled() const = 0;
    virtual void Finish() = 0;
    virtual void FlushGPU() = 0;                             // Block until all submitted GPU work completes (required before resource destruction)
    virtual void Resize( int width, int height ) = 0;


    // --- Viewport & Clear ---

    virtual void SetViewport( int x, int y, int w, int h ) = 0;
    virtual void Clear( bool color, bool depth ) = 0;
    virtual void SetClearColor( float r, float g, float b, float a ) = 0;
    virtual void SetClearDepth( float depth ) = 0;


    // --- Depth State ---

    virtual void SetDepthTest( bool enable ) = 0;
    virtual void SetDepthWrite( bool enable ) = 0;


    // --- Blend State ---

    virtual void SetBlend( bool enable ) = 0;
    virtual void SetBlendFunc( BlendFactor src, BlendFactor dst ) = 0;


    // --- Rasterizer State ---

    virtual void SetCullFace( bool enable ) = 0;
    virtual void SetPolygonOffset( bool enable, float factor = 0.0f, float units = 0.0f ) = 0;


    // --- Clip Planes ---

    virtual void SetClipPlane( int index, bool enable ) = 0;


    // --- Resource Creation ---

    // baseName is relative to DATA_ROOT with no extension, e.g. "shaders/lit_textured"
    // Each backend resolves the full path and appends the appropriate extension(s).
    virtual std::unique_ptr<IShader> CreateShader( const char* baseName ) = 0;
    virtual std::unique_ptr<IMesh>
    CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords ) = 0;
    // colorFormat defaults to ordinary RGBA8 for existing reflection buffers.
    // Cinematic rendering asks for RGBA16F so the off-screen scene can hold
    // over-bright sunlight/bloom data before the final tonemap pass.
    virtual std::unique_ptr<IFramebuffer>
    CreateFramebuffer( int width, int height, FramebufferColorFormat colorFormat = FramebufferColorFormat::RGBA8 ) = 0;


    // --- Textures (opaque uint32_t handles) ---

    virtual uint32_t
    CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter ) = 0;
    virtual void BindTexture( uint32_t handle, int slot ) = 0;
    virtual void DeleteTexture( uint32_t handle ) = 0;


    // --- Window Dimensions ---

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;


    // --- State Queries (tracked internally, not queried from API) ---

    virtual bool IsDepthTestEnabled() const = 0;
    virtual bool IsDepthWriteEnabled() const = 0;
    virtual bool IsBlendEnabled() const = 0;
    virtual bool IsCullFaceEnabled() const = 0;
    virtual void GetBlendFunc( BlendFactor& outSrc, BlendFactor& outDst ) const = 0;


    // Runtime identity and optional feature flags. DX12 is the only renderer,
    // but the UI and diagnostics still consume this compact description.
    virtual const char* GetRendererName() const = 0;


    // --- Frame Diagnostics ---

    virtual void ResetFrameDrawCalls()
    {
    }
    virtual void RecordDrawCall( const DrawCallRecord& record )
    {
        (void)record;
    }
    void RecordDrawCall()
    {
        RecordDrawCall( DrawCallRecord() );
    }
    virtual int GetFrameDrawCallCount() const
    {
        return 0;
    }
    virtual DrawCallTraceSnapshot GetFrameDrawCallTrace() const
    {
        return DrawCallTraceSnapshot();
    }
    virtual void PushDrawCallTraceScope( const char* fullPathOrLeaf, uint32_t hash )
    {
        (void)fullPathOrLeaf;
        (void)hash;
    }
    virtual void PopDrawCallTraceScope( uint32_t hash )
    {
        (void)hash;
    }


    // --- DXR Raytracing Support ---

    virtual void InitDXR( uint64_t terrainVBVA,
                          int terrainVertCount,
                          int terrainStride,
                          uint64_t sphereVBVA,
                          int sphereVertCount,
                          int sphereStride,
                          int maxInstances ) = 0;
    virtual void DispatchReflectionRays( const float* invViewProj,
                                         const float* cameraPos,
                                         float waterY,
                                         float time,
                                         const float* lightPos,
                                         int width,
                                         int height,
                                         uint32_t sphereTexHandle,
                                         uint32_t terrainTexHandle,
                                         uint32_t skyUpHandle,
                                         uint32_t skyDownHandle,
                                         uint32_t skyRightHandle,
                                         uint32_t skyLeftHandle,
                                         uint32_t skyFrontHandle,
                                         uint32_t skyBackHandle ) = 0;
    virtual void
    BuildTLAS( const float* instanceTransforms, int instanceCount, uint64_t terrainBLAS, uint64_t sphereBLAS ) = 0;
    virtual uint32_t
    GetReflectionUAVTexture() const = 0;                     // Engine texture handle for sampling the completed water reflection.
    virtual void ShutdownDXR() = 0;
    virtual uint64_t
    GetInstancedMeshStaticVBVA( uint32_t handle ) const = 0; // DXR: GPU virtual address of the static vertex buffer.
    virtual int GetInstancedMeshStaticStride( uint32_t handle ) const = 0;


    // --- GPU Timers (profiler overlay) ---

    virtual void GpuTimerBegin( int markerIdx )
    {
        (void)markerIdx;
    }
    virtual void GpuTimerEnd( int markerIdx )
    {
        (void)markerIdx;
    }
    virtual void GpuTimerInvalidate()
    {
    }
    virtual bool GpuTimerRead( int markerIdx, float& outMs )
    {
        (void)markerIdx;
        (void)outMs;
        return false;
    }


    // --- Platform profiler GPU markers ---

    virtual void PlatformProfilerGpuBegin( const char* name, uint32_t hash )
    {
        (void)name;
        (void)hash;
    }
    virtual void PlatformProfilerGpuEnd()
    {
    }
    virtual void PlatformProfilerGpuMarker( const char* name, uint32_t hash )
    {
        (void)name;
        (void)hash;
    }


    // --- Dynamic Vertex Buffer (per-frame geometry: text quads, HUD overlays) ---
    // attribComponents: component count per attribute (e.g. {2,2} = location0:vec2, location1:vec2)

    virtual uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) = 0;
    virtual void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) = 0;
    virtual void DestroyDynamicVB( uint32_t handle ) = 0;


    // --- Debug Line Rendering ---
    // Debug lines are immediate diagnostic geometry: interleaved [x,y,z,r,g,b]
    // vertices are consumed directly by the backend for overlays and broadphase views.
    // vertCount is the total number of vertices (2 per line segment).
    virtual void DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 )
    {
        (void)data;
        (void)vertCount;
        (void)viewProjMatrix16;
    }


    // --- Instanced Mesh (hardware instancing: sphere, box, and diagnostic batches) ---
    // staticData: per-vertex geometry  |  instance data uploaded per frame
    // staticAttribSizes/numStaticAttribs: component counts per static vertex attribute (e.g. {3,3,2} = pos+normal+uv)
    //   If numStaticAttribs==0, all floats go into a single attribute at location 0.
    // instanceAttribSizes: component counts per instance attribute (e.g. {4,4,4,4,1} = mat4+float)
    // instanceStartAttrib: first attribute location for instance data (e.g. 3)

    virtual uint32_t CreateInstancedMesh( const float* staticData,
                                          int staticVertCount,
                                          int staticFloatsPerVert,
                                          int maxInstances,
                                          int instanceFloats,
                                          int instanceStartAttrib,
                                          const int* instanceAttribSizes,
                                          int numInstanceAttribs,
                                          const int* staticAttribSizes = nullptr,
                                          int numStaticAttribs = 0 ) = 0;
    virtual void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) = 0;
    virtual void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) = 0;
    virtual void DestroyInstancedMesh( uint32_t handle ) = 0;
};


// --- Global Render Backend Accessor ---

IRenderBackend& Gfx();
bool IsGfxReady();
void SetGfxBackend( std::unique_ptr<IRenderBackend> backend );
void DestroyGfxBackend();

class DrawCallTraceScope
{
  public:
    DrawCallTraceScope( const char* fullPathOrLeaf, uint32_t hash ) : m_hash( hash ), m_active( IsGfxReady() )
    {
        if ( m_active )
        {
            Gfx().PushDrawCallTraceScope( fullPathOrLeaf, hash );
        }
    }
    ~DrawCallTraceScope()
    {
        if ( m_active )
        {
            Gfx().PopDrawCallTraceScope( m_hash );
        }
    }
    DrawCallTraceScope( const DrawCallTraceScope& ) = delete;
    DrawCallTraceScope& operator=( const DrawCallTraceScope& ) = delete;

  private:
    uint32_t m_hash = 0;
    bool m_active = false;
};


} // namespace Rendering
} // namespace SkullbonezCore

#define DRAW_CALL_TRACE_PASTE_INNER( a, b ) a##b
#define DRAW_CALL_TRACE_PASTE( a, b ) DRAW_CALL_TRACE_PASTE_INNER( a, b )
#define DRAW_CALL_TRACE_SCOPE( name )                                                                                  \
    constexpr uint32_t DRAW_CALL_TRACE_PASTE( _drawTraceHash_, __LINE__ ) = ::HashStr( name );                         \
    ::SkullbonezCore::Rendering::DrawCallTraceScope DRAW_CALL_TRACE_PASTE( _drawTraceScope_, __LINE__ )(               \
        name,                                                                                                          \
        DRAW_CALL_TRACE_PASTE( _drawTraceHash_, __LINE__ ) )
