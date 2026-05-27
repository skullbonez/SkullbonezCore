#pragma once


// --- Includes ---
#include <glad/gl.h>
#pragma comment( lib, "opengl32.lib" )
#include "SkullbonezIRenderBackend.h"
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

// Internal storage for dynamic vertex buffers (Text, HUD)
struct DynamicVBGL
{
    GLuint vao;
    GLuint vbo;
    int floatsPerVertex;
    int maxVertices;
};


// Internal storage for instanced mesh setups (shadows)
struct InstancedMesh
{
    GLuint vao;
    GLuint staticVBO;
    GLuint instanceVBO;
    int staticFloatsPerVert;
    int instanceFloats;
};

struct GLStateTracking
{
    bool isVsyncEnabled = true;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    bool blendEnabled = false;
    bool cullFaceEnabled = true;
    bool polygonOffsetEnabled = false;
    float polygonOffsetFactor = 0.0f;
    float polygonOffsetUnits = 0.0f;
};


/* -- RenderBackendGL -------------------------------------------------------------------------------------------------------------------------------------------

    OpenGL 3.3 implementation of the render backend interface.
    For Phase 0: wraps existing GL state calls and resource creation.
    The GL context is created by SkullbonezWindow; this class only stores the HDC for SwapBuffers.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RenderBackendGL : public IRenderBackend
{

  private:
    std::vector<DynamicVBGL> m_dynamicVBs;
    std::vector<InstancedMesh> m_instancedMeshes;
    std::unique_ptr<IShader> m_debugLineShader;
    std::unique_ptr<IShader> m_gridLineShader;

    GLStateTracking m_state;

    HDC m_hdc;
    int m_width;
    int m_height;
    GLuint m_debugLineVAO = 0;
    GLuint m_debugLineVBO = 0;
    GLuint m_gridLineVAO = 0;
    GLuint m_gridLineVBO = 0;

  public:
    RenderBackendGL();
    ~RenderBackendGL() override = default;

    bool Init( HWND hwnd, HDC hdc, int width, int height ) override;
    void Shutdown() override;
    void Present() override;
    void SetVsyncEnabled( bool enabled ) override;
    bool IsVsyncEnabled() const override;
    void Finish() override;
    void FlushGPU() override;
    void Resize( int width, int height ) override;

    void SetViewport( int x, int y, int w, int h ) override;
    void Clear( bool color, bool depth ) override;
    void SetClearColor( float r, float g, float b, float a ) override;
    void SetClearDepth( float depth ) override;

    void SetDepthTest( bool enable ) override;
    void SetDepthWrite( bool enable ) override;
    void SetBlend( bool enable ) override;
    void SetBlendFunc( BlendFactor src, BlendFactor dst ) override;
    void SetCullFace( bool enable ) override;
    void SetPolygonOffset( bool enable, float factor = 0.0f, float units = 0.0f ) override;
    void SetClipPlane( int index, bool enable ) override;

    std::unique_ptr<IShader> CreateShader( const char* baseName ) override;
    std::unique_ptr<IMesh> CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords ) override;
    std::unique_ptr<IFramebuffer> CreateFramebuffer( int width, int height ) override;

    uint32_t CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter ) override;
    void BindTexture( uint32_t handle, int slot ) override;
    void DeleteTexture( uint32_t handle ) override;

    std::vector<uint8_t> CaptureBackbuffer( int& outWidth, int& outHeight ) override;

    int GetWidth() const override;
    int GetHeight() const override;

    bool IsDepthTestEnabled() const override;
    bool IsBlendEnabled() const override;
    bool UsesZeroToOneDepth() const override;
    const char* GetRendererName() const override
    {
        return "OpenGL 3.3";
    }

    bool IsDXRSupported() const override
    {
        return false;
    }
    void InitDXR( uint64_t, int, int, uint64_t, int, int, int ) override
    {
    }
    void DispatchReflectionRays( const float*, const float*, float, float, const float*, int, int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t ) override
    {
    }
    void BuildTLAS( const float*, int, uint64_t, uint64_t ) override
    {
    }
    uint32_t GetReflectionUAVTexture() const override
    {
        return 0;
    }
    void ShutdownDXR() override
    {
    }
    uint64_t GetInstancedMeshStaticVBVA( uint32_t ) const override
    {
        return 0;
    }
    int GetInstancedMeshStaticStride( uint32_t ) const override
    {
        return 0;
    }

    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) override;
    void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) override;
    void DestroyDynamicVB( uint32_t handle ) override;

    void DrawLines( const float* verts, int vertCount, float r, float g, float b, const float* viewProjMatrix16 ) override;
    void DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 ) override;

    uint32_t CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs, const int* staticAttribSizes = nullptr, int numStaticAttribs = 0 ) override;
    void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) override;
    void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) override;
    void DestroyInstancedMesh( uint32_t handle ) override;
};
} // namespace Rendering
} // namespace SkullbonezCore
