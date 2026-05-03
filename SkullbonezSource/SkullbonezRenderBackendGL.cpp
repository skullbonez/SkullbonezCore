// --- Includes ---
#include "SkullbonezRenderBackendGL.h"
#include "SkullbonezShaderGL.h"
#include "SkullbonezMeshGL.h"
#include "SkullbonezFramebufferGL.h"


namespace SkullbonezCore
{
namespace Rendering
{


RenderBackendGL::RenderBackendGL()
    : m_hdc( nullptr ), m_width( 0 ), m_height( 0 ), m_depthTestEnabled( true ), m_blendEnabled( false )
{
}


bool RenderBackendGL::Init( HWND /*hwnd*/, HDC hdc, int width, int height )
{
    m_hdc = hdc;
    m_width = width;
    m_height = height;

    // Initial GL state (replaces SkullbonezHelper::StateSetup)
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClearDepth( 1.0f );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glEnable( GL_CULL_FACE );
    glFrontFace( GL_CCW );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    return true;
}


void RenderBackendGL::Shutdown()
{
    m_hdc = nullptr;
}


void RenderBackendGL::Present()
{
    SwapBuffers( m_hdc );
}


void RenderBackendGL::Finish()
{
    glFinish();
}


void RenderBackendGL::FlushGPU()
{
    glFinish();
}


void RenderBackendGL::Resize( int width, int height )
{
    m_width = width;
    m_height = height;
    glViewport( 0, 0, width, height );
}


// --- Viewport & Clear ---


void RenderBackendGL::SetViewport( int x, int y, int w, int h )
{
    glViewport( x, y, w, h );
}


void RenderBackendGL::Clear( bool color, bool depth )
{
    GLbitfield mask = 0;
    if ( color )
    {
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if ( depth )
    {
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear( mask );
}


void RenderBackendGL::SetClearColor( float r, float g, float b, float a )
{
    glClearColor( r, g, b, a );
}


void RenderBackendGL::SetClearDepth( float depth )
{
    glClearDepth( depth );
}


// --- Depth State ---


void RenderBackendGL::SetDepthTest( bool enable )
{
    m_depthTestEnabled = enable;
    if ( enable )
    {
        glEnable( GL_DEPTH_TEST );
    }
    else
    {
        glDisable( GL_DEPTH_TEST );
    }
}


// --- Blend State ---


void RenderBackendGL::SetBlend( bool enable )
{
    m_blendEnabled = enable;
    if ( enable )
    {
        glEnable( GL_BLEND );
    }
    else
    {
        glDisable( GL_BLEND );
    }
}


void RenderBackendGL::SetBlendFunc( BlendFactor src, BlendFactor dst )
{
    auto toGL = []( BlendFactor f ) -> GLenum
    {
        switch ( f )
        {
        case BlendFactor::Zero:
            return GL_ZERO;
        case BlendFactor::One:
            return GL_ONE;
        case BlendFactor::SrcAlpha:
            return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return GL_ONE_MINUS_SRC_ALPHA;
        default:
            return GL_ONE;
        }
    };
    glBlendFunc( toGL( src ), toGL( dst ) );
}


// --- Rasterizer State ---


void RenderBackendGL::SetCullFace( bool enable )
{
    if ( enable )
    {
        glEnable( GL_CULL_FACE );
    }
    else
    {
        glDisable( GL_CULL_FACE );
    }
}


void RenderBackendGL::SetPolygonOffset( bool enable, float factor, float units )
{
    if ( enable )
    {
        glEnable( GL_POLYGON_OFFSET_FILL );
        glPolygonOffset( factor, units );
    }
    else
    {
        glDisable( GL_POLYGON_OFFSET_FILL );
    }
}


// --- Clip Planes ---


void RenderBackendGL::SetClipPlane( int index, bool enable )
{
    GLenum plane = GL_CLIP_DISTANCE0 + index;
    if ( enable )
    {
        glEnable( plane );
    }
    else
    {
        glDisable( plane );
    }
}


// --- Resource Creation ---


std::unique_ptr<IShader> RenderBackendGL::CreateShader( const char* vertPath, const char* fragPath )
{
    return std::make_unique<ShaderGL>( vertPath, fragPath );
}


std::unique_ptr<IMesh> RenderBackendGL::CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
    return std::make_unique<MeshGL>( data, vertexCount, hasNormals, hasTexCoords );
}


std::unique_ptr<IFramebuffer> RenderBackendGL::CreateFramebuffer( int width, int height )
{
    return std::make_unique<FramebufferGL>( width, height );
}


// --- Textures ---


uint32_t RenderBackendGL::CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter )
{
    GLuint tex = 0;
    glGenTextures( 1, &tex );
    glBindTexture( GL_TEXTURE_2D, tex );

    GLenum format = ( channels == 4 ) ? GL_RGBA : ( channels == 3 ) ? GL_RGB
                                              : ( channels == 1 )   ? GL_RED
                                                                    : GL_RGB;

    glTexImage2D( GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data );

    if ( generateMips )
    {
        glGenerateMipmap( GL_TEXTURE_2D );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linearFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST );
    }
    else
    {
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linearFilter ? GL_LINEAR : GL_NEAREST );
    }

    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linearFilter ? GL_LINEAR : GL_NEAREST );
    glBindTexture( GL_TEXTURE_2D, 0 );

    return static_cast<uint32_t>( tex );
}


void RenderBackendGL::BindTexture( uint32_t handle, int slot )
{
    glActiveTexture( GL_TEXTURE0 + slot );
    glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( handle ) );
}


void RenderBackendGL::DeleteTexture( uint32_t handle )
{
    GLuint tex = static_cast<GLuint>( handle );
    glDeleteTextures( 1, &tex );
}


// --- Screenshot ---


std::vector<uint8_t> RenderBackendGL::CaptureBackbuffer( int& outWidth, int& outHeight )
{
    GLint viewport[4];
    glGetIntegerv( GL_VIEWPORT, viewport );
    outWidth = viewport[2];
    outHeight = viewport[3];

    int rowBytes = outWidth * 3;
    int padded = ( rowBytes + 3 ) & ~3;
    std::vector<uint8_t> pixels( padded * outHeight );

    glFinish();
    glPixelStorei( GL_PACK_ALIGNMENT, 4 );
    glReadBuffer( GL_BACK );
    glReadPixels( 0, 0, outWidth, outHeight, GL_BGR, GL_UNSIGNED_BYTE, pixels.data() );

    return pixels;
}


// --- Window Dimensions ---


int RenderBackendGL::GetWidth() const
{
    return m_width;
}


int RenderBackendGL::GetHeight() const
{
    return m_height;
}


// --- State Queries ---


bool RenderBackendGL::IsDepthTestEnabled() const
{
    return m_depthTestEnabled;
}


bool RenderBackendGL::IsBlendEnabled() const
{
    return m_blendEnabled;
}


bool RenderBackendGL::UsesZeroToOneDepth() const
{
    return false;
}


// --- Dynamic Vertex Buffer ---


uint32_t RenderBackendGL::CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices )
{
    DynamicVBGL dvb = {};
    dvb.maxVertices = maxVertices;

    int floatsPerVert = 0;
    for ( int i = 0; i < numAttribs; ++i )
    {
        floatsPerVert += attribComponents[i];
    }
    dvb.floatsPerVertex = floatsPerVert;

    glGenVertexArrays( 1, &dvb.vao );
    glBindVertexArray( dvb.vao );

    glGenBuffers( 1, &dvb.vbo );
    glBindBuffer( GL_ARRAY_BUFFER, dvb.vbo );
    glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( maxVertices ) * floatsPerVert * static_cast<GLsizeiptr>( sizeof( float ) ), nullptr, GL_STREAM_DRAW );

    int stride = floatsPerVert * static_cast<int>( sizeof( float ) );
    int offset = 0;
    for ( int i = 0; i < numAttribs; ++i )
    {
        glEnableVertexAttribArray( static_cast<GLuint>( i ) );
        glVertexAttribPointer( static_cast<GLuint>( i ), attribComponents[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
        offset += attribComponents[i] * static_cast<int>( sizeof( float ) );
    }

    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    m_dynamicVBs.push_back( dvb );
    return static_cast<uint32_t>( m_dynamicVBs.size() ); // 1-based handle
}


void RenderBackendGL::UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount )
{
    if ( handle == 0 || handle > static_cast<uint32_t>( m_dynamicVBs.size() ) )
    {
        return;
    }
    DynamicVBGL& dvb = m_dynamicVBs[handle - 1];

    glBindVertexArray( dvb.vao );
    glBindBuffer( GL_ARRAY_BUFFER, dvb.vbo );
    glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( vertexCount ) * dvb.floatsPerVertex * static_cast<GLsizeiptr>( sizeof( float ) ), data, GL_STREAM_DRAW );
    glDrawArrays( GL_TRIANGLES, 0, vertexCount );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindVertexArray( 0 );
}


void RenderBackendGL::DestroyDynamicVB( uint32_t handle )
{
    if ( handle == 0 || handle > static_cast<uint32_t>( m_dynamicVBs.size() ) )
    {
        return;
    }
    DynamicVBGL& dvb = m_dynamicVBs[handle - 1];
    if ( dvb.vbo )
    {
        glDeleteBuffers( 1, &dvb.vbo );
        dvb.vbo = 0;
    }
    if ( dvb.vao )
    {
        glDeleteVertexArrays( 1, &dvb.vao );
        dvb.vao = 0;
    }
}


// --- Instanced MeshGL ---


uint32_t RenderBackendGL::CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs )
{
    InstancedMesh im = {};
    im.staticFloatsPerVert = staticFloatsPerVert;
    im.instanceFloats = instanceFloats;

    glGenVertexArrays( 1, &im.vao );
    glBindVertexArray( im.vao );

    // Static geometry VBO
    glGenBuffers( 1, &im.staticVBO );
    glBindBuffer( GL_ARRAY_BUFFER, im.staticVBO );
    glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( staticVertCount ) * staticFloatsPerVert * static_cast<GLsizeiptr>( sizeof( float ) ), staticData, GL_STATIC_DRAW );

    // Static attributes: location 0 = vec(staticFloatsPerVert), per-vertex
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 0, staticFloatsPerVert, GL_FLOAT, GL_FALSE, staticFloatsPerVert * static_cast<int>( sizeof( float ) ), nullptr );

    // Instance data VBO (dynamic, updated each frame)
    glGenBuffers( 1, &im.instanceVBO );
    glBindBuffer( GL_ARRAY_BUFFER, im.instanceVBO );
    glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( maxInstances ) * instanceFloats * static_cast<GLsizeiptr>( sizeof( float ) ), nullptr, GL_DYNAMIC_DRAW );

    int stride = instanceFloats * static_cast<int>( sizeof( float ) );
    int offset = 0;
    for ( int i = 0; i < numInstanceAttribs; ++i )
    {
        GLuint loc = static_cast<GLuint>( instanceStartAttrib + i );
        glEnableVertexAttribArray( loc );
        glVertexAttribPointer( loc, instanceAttribSizes[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
        glVertexAttribDivisor( loc, 1 );
        offset += instanceAttribSizes[i] * static_cast<int>( sizeof( float ) );
    }

    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    m_instancedMeshes.push_back( im );
    return static_cast<uint32_t>( m_instancedMeshes.size() ); // 1-based handle
}


void RenderBackendGL::UploadInstanceData( uint32_t handle, const float* data, int floatCount )
{
    if ( handle == 0 || handle > static_cast<uint32_t>( m_instancedMeshes.size() ) )
    {
        return;
    }
    InstancedMesh& im = m_instancedMeshes[handle - 1];

    glBindBuffer( GL_ARRAY_BUFFER, im.instanceVBO );
    glBufferSubData( GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>( floatCount ) * static_cast<GLsizeiptr>( sizeof( float ) ), data );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
}


void RenderBackendGL::DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount )
{
    if ( handle == 0 || handle > static_cast<uint32_t>( m_instancedMeshes.size() ) )
    {
        return;
    }
    InstancedMesh& im = m_instancedMeshes[handle - 1];

    glBindVertexArray( im.vao );
    glDrawArraysInstanced( GL_TRIANGLES, 0, staticVertCount, instanceCount );
    glBindVertexArray( 0 );
}


void RenderBackendGL::DestroyInstancedMesh( uint32_t handle )
{
    if ( handle == 0 || handle > static_cast<uint32_t>( m_instancedMeshes.size() ) )
    {
        return;
    }
    InstancedMesh& im = m_instancedMeshes[handle - 1];
    if ( im.instanceVBO )
    {
        glDeleteBuffers( 1, &im.instanceVBO );
        im.instanceVBO = 0;
    }
    if ( im.staticVBO )
    {
        glDeleteBuffers( 1, &im.staticVBO );
        im.staticVBO = 0;
    }
    if ( im.vao )
    {
        glDeleteVertexArrays( 1, &im.vao );
        im.vao = 0;
    }
}


} // namespace Rendering
} // namespace SkullbonezCore
