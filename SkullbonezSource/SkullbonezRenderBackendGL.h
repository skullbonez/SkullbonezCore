#pragma once


// --- Includes ---
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


/* -- RenderBackendGL -------------------------------------------------------------------------------------------------------------------------------------------

    OpenGL 3.3 implementation of the render backend interface.
    For Phase 0: wraps existing GL state calls and resource creation.
    The GL context is created by SkullbonezWindow; this class only stores the HDC for SwapBuffers.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RenderBackendGL : public IRenderBackend
{

  private:
    HDC m_hdc;
    int m_width;
    int m_height;
    bool m_depthTestEnabled;
    bool m_blendEnabled;
    bool m_cullFaceEnabled;
    bool m_polygonOffsetEnabled;
    float m_polygonOffsetFactor;
    float m_polygonOffsetUnits;
    std::vector<DynamicVBGL> m_dynamicVBs;
    std::vector<InstancedMesh> m_instancedMeshes;

  public:
    RenderBackendGL();
    ~RenderBackendGL() override = default;

    bool Init( HWND hwnd, HDC hdc, int width, int height ) override;
    void Shutdown() override;
    void Present() override;
    void Finish() override;
    void FlushGPU() override;
    void Resize( int width, int height ) override;

    void SetViewport( int x, int y, int w, int h ) override;
    void Clear( bool color, bool depth ) override;
    void SetClearColor( float r, float g, float b, float a ) override;
    void SetClearDepth( float depth ) override;

    void SetDepthTest( bool enable ) override;
    void SetBlend( bool enable ) override;
    void SetBlendFunc( BlendFactor src, BlendFactor dst ) override;
    void SetCullFace( bool enable ) override;
    void SetPolygonOffset( bool enable, float factor = 0.0f, float units = 0.0f ) override;
    void SetClipPlane( int index, bool enable ) override;

    std::unique_ptr<IShader> CreateShader( const char* vertPath, const char* fragPath ) override;
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

    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) override;
    void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) override;
    void DestroyDynamicVB( uint32_t handle ) override;

    uint32_t CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs ) override;
    void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) override;
    void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) override;
    void DestroyInstancedMesh( uint32_t handle ) override;
};
} // namespace Rendering
} // namespace SkullbonezCore
