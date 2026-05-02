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
    Concrete implementations: RenderBackendGL (OpenGL 3.3), RenderBackendDX (DirectX 11).
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
    virtual void Finish() = 0;
    virtual void Resize( int width, int height ) = 0;


    // --- Viewport & Clear ---

    virtual void SetViewport( int x, int y, int w, int h ) = 0;
    virtual void Clear( bool color, bool depth ) = 0;
    virtual void SetClearColor( float r, float g, float b, float a ) = 0;
    virtual void SetClearDepth( float depth ) = 0;


    // --- Depth State ---

    virtual void SetDepthTest( bool enable ) = 0;


    // --- Blend State ---

    virtual void SetBlend( bool enable ) = 0;
    virtual void SetBlendFunc( BlendFactor src, BlendFactor dst ) = 0;


    // --- Rasterizer State ---

    virtual void SetCullFace( bool enable ) = 0;
    virtual void SetPolygonOffset( bool enable, float factor = 0.0f, float units = 0.0f ) = 0;


    // --- Clip Planes ---

    virtual void SetClipPlane( int index, bool enable ) = 0;


    // --- Resource Creation ---

    virtual std::unique_ptr<IShader> CreateShader( const char* vertPath, const char* fragPath ) = 0;
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

    virtual bool UsesZeroToOneDepth() const = 0; // true for DX11 [0,1]; false for GL [-1,1]


    // --- Dynamic Vertex Buffer (per-frame geometry: text quads, HUD overlays) ---
    // attribComponents: component count per attribute (e.g. {2,2} = location0:vec2, location1:vec2)

    virtual uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) = 0;
    virtual void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) = 0;
    virtual void DestroyDynamicVB( uint32_t handle ) = 0;


    // --- Instanced Mesh (hardware instancing: shadow decals) ---
    // staticData: per-vertex geometry  |  instance data uploaded per frame
    // instanceAttribSizes: component counts per instance attribute (e.g. {4,4,4,4,1} = mat4+float)
    // instanceStartAttrib: first attribute location for instance data (e.g. 3)

    virtual uint32_t CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs ) = 0;
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
