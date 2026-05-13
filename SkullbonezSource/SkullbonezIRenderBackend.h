#pragma once


// --- Includes ---
#include <cstdint>
#include <memory>
#include <vector>
#include <windows.h>
#include "SkullbonezIShader.h"
#include "SkullbonezIMesh.h"
#include "SkullbonezIFramebuffer.h"


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


/* -- IRenderBackend ---------------------------------------------------------------------------------------------------------------------------------------------

    Abstract render backend interface. Owns GPU state and resource creation.
    Concrete implementations: RenderBackendGL (OpenGL 3.3), RenderBackendDX11 (DirectX 11).
    One global instance is set during init and accessed via Gfx().
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IRenderBackend
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
    virtual void FlushGPU() = 0; // Block until all submitted GPU work completes (required before resource destruction)
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

    // baseName is relative to DATA_ROOT with no extension, e.g. "shaders/shadow"
    // Each backend resolves the full path and appends the appropriate extension(s).
    virtual std::unique_ptr<IShader> CreateShader( const char* baseName ) = 0;
    virtual std::unique_ptr<IMesh> CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords ) = 0;
    virtual std::unique_ptr<IFramebuffer> CreateFramebuffer( int width, int height ) = 0;


    // --- Textures (opaque uint32_t handles) ---

    virtual uint32_t CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter ) = 0;
    virtual void BindTexture( uint32_t handle, int slot ) = 0;
    virtual void DeleteTexture( uint32_t handle ) = 0;


    // --- Screenshot (returns BGR pixel data, bottom-up for BMP compatibility) ---

    virtual std::vector<uint8_t> CaptureBackbuffer( int& outWidth, int& outHeight ) = 0;


    // --- Window Dimensions ---

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;


    // --- State Queries (tracked internally, not queried from API) ---

    virtual bool IsDepthTestEnabled() const = 0;
    virtual bool IsBlendEnabled() const = 0;


    // --- Depth Range Convention ---

    virtual bool UsesZeroToOneDepth() const = 0; // true for DX11/DX12 [0,1]; false for GL [-1,1]
    virtual const char* GetRendererName() const = 0;


    // --- DXR Raytracing Support ---

    virtual bool IsDXRSupported() const = 0;
    virtual void InitDXR( uint64_t terrainVBVA, int terrainVertCount, int terrainStride, uint64_t sphereVBVA, int sphereVertCount, int sphereStride, int maxInstances ) = 0;
    virtual void DispatchReflectionRays( const float* invViewProj, const float* cameraPos, float waterY, float time, const float* lightPos, int width, int height, uint32_t sphereTexHandle, uint32_t terrainTexHandle, uint32_t skyUpHandle, uint32_t skyDownHandle, uint32_t skyRightHandle, uint32_t skyLeftHandle, uint32_t skyFrontHandle, uint32_t skyBackHandle ) = 0;
    virtual void BuildTLAS( const float* instanceTransforms, int instanceCount, uint64_t terrainBLAS, uint64_t sphereBLAS ) = 0;
    virtual uint32_t GetReflectionUAVTexture() const = 0; // Returns texture handle for water shader binding
    virtual void ShutdownDXR() = 0;
    virtual uint64_t GetInstancedMeshStaticVBVA( uint32_t handle ) const = 0; // DXR: GPU VA of instanced mesh's static VB
    virtual int GetInstancedMeshStaticStride( uint32_t handle ) const = 0;


    // --- GPU Timers (profiler overlay — DX12 only for now) ---

    virtual bool SupportsGpuTimers() const
    {
        return false;
    }
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


    // --- Dynamic Vertex Buffer (per-frame geometry: text quads, HUD overlays) ---
    // attribComponents: component count per attribute (e.g. {2,2} = location0:vec2, location1:vec2)

    virtual uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) = 0;
    virtual void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) = 0;
    virtual void DestroyDynamicVB( uint32_t handle ) = 0;


    // --- Debug Line Rendering ---
    // Draws world-space line segments. verts is a flat array of vec3 pairs (2 × vec3 per line).
    // vertCount is the total number of vertices (2 × number of lines).
    // No-op on backends that do not support it.
    virtual void DrawLines( const float* verts, int vertCount, float r, float g, float b, const float* viewProjMatrix16 )
    {
        (void)verts;
        (void)vertCount;
        (void)r;
        (void)g;
        (void)b;
        (void)viewProjMatrix16;
    }


    // --- Instanced Mesh (hardware instancing: shadow decals, sphere batches) ---
    // staticData: per-vertex geometry  |  instance data uploaded per frame
    // staticAttribSizes/numStaticAttribs: component counts per static vertex attribute (e.g. {3,3,2} = pos+normal+uv)
    //   If numStaticAttribs==0, all floats go into a single attribute at location 0.
    // instanceAttribSizes: component counts per instance attribute (e.g. {4,4,4,4,1} = mat4+float)
    // instanceStartAttrib: first attribute location for instance data (e.g. 3)

    virtual uint32_t CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs, const int* staticAttribSizes = nullptr, int numStaticAttribs = 0 ) = 0;
    virtual void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) = 0;
    virtual void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) = 0;
    virtual void DestroyInstancedMesh( uint32_t handle ) = 0;
};


// --- Global Render Backend Accessor ---

IRenderBackend& Gfx();
bool IsGfxReady();
void SetGfxBackend( std::unique_ptr<IRenderBackend> backend );
void DestroyGfxBackend();


} // namespace Rendering
} // namespace SkullbonezCore
