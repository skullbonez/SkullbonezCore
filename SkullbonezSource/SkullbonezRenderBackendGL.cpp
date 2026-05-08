// --- Includes ---
#include "SkullbonezRenderBackendGL.h"
#include "SkullbonezShaderGL.h"
#include "SkullbonezMeshGL.h"
#include "SkullbonezFramebufferGL.h"
#include <cstdio>


namespace SkullbonezCore
{
namespace Rendering
{


// GL debug message callback — logs errors and warnings from the driver
static void APIENTRY GLDebugCallback( GLenum source, GLenum type, GLuint /*id*/, GLenum severity, GLsizei /*length*/, const GLchar* message, const void* /*userParam*/ )
{
    // Skip notifications (too noisy)
    if ( severity == GL_DEBUG_SEVERITY_NOTIFICATION )
    {
        return;
    }

    const char* srcStr = "?";
    switch ( source )
    {
    case GL_DEBUG_SOURCE_API:
        srcStr = "API";
        break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        srcStr = "Window";
        break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER:
        srcStr = "Shader";
        break;
    case GL_DEBUG_SOURCE_THIRD_PARTY:
        srcStr = "3rdParty";
        break;
    case GL_DEBUG_SOURCE_APPLICATION:
        srcStr = "App";
        break;
    case GL_DEBUG_SOURCE_OTHER:
        srcStr = "Other";
        break;
    }

    const char* typeStr = "?";
    switch ( type )
    {
    case GL_DEBUG_TYPE_ERROR:
        typeStr = "ERROR";
        break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        typeStr = "DEPRECATED";
        break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        typeStr = "UNDEFINED";
        break;
    case GL_DEBUG_TYPE_PORTABILITY:
        typeStr = "PORTABILITY";
        break;
    case GL_DEBUG_TYPE_PERFORMANCE:
        typeStr = "PERF";
        break;
    case GL_DEBUG_TYPE_OTHER:
        typeStr = "OTHER";
        break;
    case GL_DEBUG_TYPE_MARKER:
        typeStr = "MARKER";
        break;
    }

    const char* sevStr = "?";
    switch ( severity )
    {
    case GL_DEBUG_SEVERITY_HIGH:
        sevStr = "HIGH";
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        sevStr = "MEDIUM";
        break;
    case GL_DEBUG_SEVERITY_LOW:
        sevStr = "LOW";
        break;
    }

    char buf[2048];
    sprintf_s( buf, sizeof( buf ), "[GL %s] [%s] [%s] %s\n", sevStr, srcStr, typeStr, message );
    OutputDebugStringA( buf );
}


RenderBackendGL::RenderBackendGL()
    : m_hdc( nullptr ), m_width( 0 ), m_height( 0 ), m_depthTestEnabled( true ), m_depthWriteEnabled( true ), m_blendEnabled( false ), m_cullFaceEnabled( true ), m_polygonOffsetEnabled( false ), m_polygonOffsetFactor( 0.0f ), m_polygonOffsetUnits( 0.0f )
{
}


bool RenderBackendGL::Init( HWND /*hwnd*/, HDC hdc, int width, int height )
{
    m_hdc = hdc;
    m_width = width;
    m_height = height;

    // Register GL debug callback if KHR_debug is available
    if ( GLAD_GL_KHR_debug )
    {
        // Turn on the GPU driver's built-in error/warning reporting system. When the driver
        // detects a problem (e.g. invalid enum, deprecated usage), it will call our callback.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
        glEnable( GL_DEBUG_OUTPUT );

        // Make debug messages arrive immediately (synchronous) rather than being queued.
        // Slightly slower but makes debugging much easier — the callstack points directly
        // at the offending GL call.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
        glEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS );

        // Register our callback function that the driver will invoke on every debug message.
        // The driver passes the severity, source, and human-readable error string to it.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDebugMessageCallback.xhtml
        glDebugMessageCallback( GLDebugCallback, nullptr );

        // Filter out NOTIFICATION-level messages (too noisy — things like "buffer created
        // successfully"). We only want actual errors, warnings, and performance hints.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDebugMessageControl.xhtml
        glDebugMessageControl( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE );
    }

    // --- Initial GPU State Setup ---
    // These calls configure the GPU's default rendering behaviour for the entire application.
    // Think of it like setting the "factory defaults" before we start drawing anything.

    // Set the color that the screen gets wiped to when we call glClear(). RGBA = all zeros = black.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glClearColor.xhtml
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );

    // Set the depth buffer clear value to 1.0 (maximum distance). The depth buffer stores how
    // far each pixel is from the camera — clearing to 1.0 means "nothing drawn yet, everything
    // is infinitely far away."
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glClearDepth.xhtml
    glClearDepth( 1.0f );

    // Enable depth testing — the GPU will compare each new pixel's depth against what's already
    // in the depth buffer. If something is behind an existing pixel, it won't be drawn.
    // Without this, objects would draw on top of each other in the order they were submitted,
    // regardless of distance from camera.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
    glEnable( GL_DEPTH_TEST );

    // Set the depth comparison to "less than or equal" — a pixel passes if its depth is <=
    // the existing depth buffer value. LEQUAL (instead of LESS) allows objects at the exact
    // same depth to still render, which helps with overlapping coplanar geometry.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDepthFunc.xhtml
    glDepthFunc( GL_LEQUAL );

    // Enable backface culling — triangles facing away from the camera are skipped entirely.
    // This halves the triangle count for solid objects since you can never see their backsides.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
    glEnable( GL_CULL_FACE );

    // Define "front face" as counter-clockwise winding. When you look at a triangle head-on,
    // if its vertices go counter-clockwise, it's front-facing. Clockwise = back-facing = culled.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFrontFace.xhtml
    glFrontFace( GL_CCW );

    // Set the alpha blending formula: final = (src × srcAlpha) + (dst × (1 - srcAlpha)).
    // This is standard transparency blending — the more opaque the new pixel (higher alpha),
    // the more it covers up what was already drawn behind it.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBlendFunc.xhtml
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    return true;
}


void RenderBackendGL::Shutdown()
{
    // Destroy all dynamic vertex buffers
    for ( auto& dvb : m_dynamicVBs )
    {
        if ( dvb.vbo )
        {
            // Delete the GPU-side buffer that holds vertex data. Once deleted, the GPU
            // memory is freed and the handle becomes invalid.
            // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteBuffers.xhtml
            glDeleteBuffers( 1, &dvb.vbo );
        }
        if ( dvb.vao )
        {
            // Delete the Vertex Array Object — the "settings bookmark" that remembers how
            // vertex attributes are laid out. Cleaning up prevents GPU resource leaks.
            // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteVertexArrays.xhtml
            glDeleteVertexArrays( 1, &dvb.vao );
        }
    }
    m_dynamicVBs.clear();

    // Destroy all instanced meshes
    for ( auto& im : m_instancedMeshes )
    {
        if ( im.instanceVBO )
        {
            glDeleteBuffers( 1, &im.instanceVBO );
        }
        if ( im.staticVBO )
        {
            glDeleteBuffers( 1, &im.staticVBO );
        }
        if ( im.vao )
        {
            glDeleteVertexArrays( 1, &im.vao );
        }
    }
    m_instancedMeshes.clear();

    if ( m_debugLineVBO != 0 )
    {
        glDeleteBuffers( 1, &m_debugLineVBO );
        m_debugLineVBO = 0;
    }
    if ( m_debugLineVAO != 0 )
    {
        glDeleteVertexArrays( 1, &m_debugLineVAO );
        m_debugLineVAO = 0;
    }
    m_debugLineShader.reset();

    m_hdc = nullptr;
}


void RenderBackendGL::Present()
{
    // Swap the front and back framebuffers. The GPU has been drawing to the "back buffer"
    // (invisible to the user). This call flips it to the screen (front buffer) in one
    // instant, giving a smooth tear-free image. This is "double buffering."
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-swapbuffers
    SwapBuffers( m_hdc );
}


void RenderBackendGL::Finish()
{
    // Block the CPU until the GPU has finished processing ALL previously submitted commands.
    // Normally the CPU and GPU run in parallel — this forces them to sync up. Used when we
    // need to guarantee the GPU is done (e.g. before reading back pixel data).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFinish.xhtml
    glFinish();
}


void RenderBackendGL::FlushGPU()
{
    // Same as Finish() — stall until GPU is idle. Used as a generic "wait for GPU" barrier.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFinish.xhtml
    glFinish();
}


void RenderBackendGL::Resize( int width, int height )
{
    m_width = width;
    m_height = height;
    // Tell the GPU which rectangle of the window to draw into. (0,0) is bottom-left.
    // When the window resizes, we update this so rendering fills the entire new area.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glViewport.xhtml
    glViewport( 0, 0, width, height );
}


// --- Viewport & Clear ---


void RenderBackendGL::SetViewport( int x, int y, int w, int h )
{
    // Set the rendering viewport to a specific sub-rectangle of the window.
    // Used for effects like rendering to only part of the screen (e.g. reflection pass
    // at lower resolution).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glViewport.xhtml
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
    // Wipe the specified buffers to their clear values (set by glClearColor/glClearDepth).
    // Called at the start of each frame to erase the previous frame's image and depth data.
    // Without clearing, you'd see ghosting from the previous frame.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glClear.xhtml
    glClear( mask );
}


void RenderBackendGL::SetClearColor( float r, float g, float b, float a )
{
    // Change the color that glClear() will fill the screen with. RGBA floats from 0-1.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glClearColor.xhtml
    glClearColor( r, g, b, a );
}


void RenderBackendGL::SetClearDepth( float depth )
{
    // Change the depth value that glClear() writes to the depth buffer. 1.0 = far plane.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glClearDepth.xhtml
    glClearDepth( depth );
}


// --- Depth State ---


void RenderBackendGL::SetDepthTest( bool enable )
{
    if ( enable == m_depthTestEnabled )
    {
        return;
    }
    m_depthTestEnabled = enable;
    if ( enable )
    {
        // Enable depth testing — each pixel's distance from camera is compared against the
        // depth buffer. Pixels that are behind already-drawn geometry are discarded.
        // This is what makes closer objects correctly occlude farther ones.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
        glEnable( GL_DEPTH_TEST );
    }
    else
    {
        // Disable depth testing — everything draws on top of everything, in submission order.
        // Used for UI/text overlays that should always be visible regardless of 3D depth.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDisable.xhtml
        glDisable( GL_DEPTH_TEST );
    }
}


void RenderBackendGL::SetDepthWrite( bool enable )
{
    if ( enable == m_depthWriteEnabled )
    {
        return;
    }
    m_depthWriteEnabled = enable;
    // Control whether depth values are written to the depth buffer during rendering.
    // When false, depth testing still occurs (closer objects occlude farther ones) but
    // the depth buffer is not updated. This lets transparent/overlay geometry (like shadow
    // decals) respect depth without blocking subsequent geometry from passing the depth test.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDepthMask.xhtml
    glDepthMask( enable ? GL_TRUE : GL_FALSE );
}


// --- Blend State ---


void RenderBackendGL::SetBlend( bool enable )
{
    if ( enable == m_blendEnabled )
    {
        return;
    }
    m_blendEnabled = enable;
    if ( enable )
    {
        // Enable alpha blending — new pixels are mixed with existing pixels based on their
        // alpha (transparency) value. Without this, semi-transparent objects would be fully
        // opaque. The blend formula was set by glBlendFunc() during Init().
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
        glEnable( GL_BLEND );
    }
    else
    {
        // Disable blending — new pixels completely overwrite existing ones. This is the
        // default for opaque geometry (faster since the GPU skips the blend math).
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDisable.xhtml
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
    // Configure HOW source (new) and destination (existing) pixels are combined when
    // blending is enabled. The formula is: result = src_pixel * src_factor + dst_pixel * dst_factor.
    // For example, SrcAlpha + OneMinusSrcAlpha gives standard transparency blending.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBlendFunc.xhtml
    glBlendFunc( toGL( src ), toGL( dst ) );
}


// --- Rasterizer State ---


void RenderBackendGL::SetCullFace( bool enable )
{
    if ( enable == m_cullFaceEnabled )
    {
        return;
    }
    m_cullFaceEnabled = enable;
    if ( enable )
    {
        // Enable backface culling — triangles whose vertices appear in clockwise order
        // (i.e. facing away from camera) are discarded before the fragment shader runs.
        // This is a major performance optimization for closed/solid objects.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
        glEnable( GL_CULL_FACE );
    }
    else
    {
        // Disable culling — both front and back faces are rendered. Needed for things like
        // water planes or flat objects that should be visible from both sides.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDisable.xhtml
        glDisable( GL_CULL_FACE );
    }
}


void RenderBackendGL::SetPolygonOffset( bool enable, float factor, float units )
{
    if ( enable == m_polygonOffsetEnabled && factor == m_polygonOffsetFactor && units == m_polygonOffsetUnits )
    {
        return;
    }
    m_polygonOffsetEnabled = enable;
    m_polygonOffsetFactor = factor;
    m_polygonOffsetUnits = units;
    if ( enable )
    {
        // Enable polygon offset — nudges the depth value of filled triangles by a small amount.
        // This prevents "z-fighting" (shimmering pixel noise) when two surfaces are at nearly
        // the same depth (e.g. a shadow decal on the ground). The offset is calculated as:
        //   offset = factor × slope + units × minimum_depth_step
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
        glEnable( GL_POLYGON_OFFSET_FILL );
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPolygonOffset.xhtml
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
        // Enable a user-defined clip plane. Clip planes slice the scene — any geometry on
        // the "wrong side" of the plane is discarded. Used for water reflections: we clip
        // everything below the water surface when rendering the reflected view, so underwater
        // objects don't "leak" into the reflection.
        // The clip distance itself is computed in the vertex shader (gl_ClipDistance[index]).
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnable.xhtml
        glEnable( plane );
    }
    else
    {
        glDisable( plane );
    }
}


// --- Resource Creation ---


std::unique_ptr<IShader> RenderBackendGL::CreateShader( const char* baseName )
{
    std::string vertPath = std::string( DATA_ROOT ) + baseName + ".vert";
    std::string fragPath = std::string( DATA_ROOT ) + baseName + ".frag";
    return std::make_unique<ShaderGL>( vertPath.c_str(), fragPath.c_str() );
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

    // Generate a texture "name" (handle/ID). This doesn't allocate GPU memory yet — it just
    // reserves a unique integer we can use to refer to this texture in future calls.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenTextures.xhtml
    glGenTextures( 1, &tex );

    // Make this texture the "active" 2D texture. All subsequent texture configuration calls
    // (glTexImage2D, glTexParameteri, etc.) will apply to this texture until we bind something else.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindTexture.xhtml
    glBindTexture( GL_TEXTURE_2D, tex );

    GLenum format = ( channels == 4 ) ? GL_RGBA : ( channels == 3 ) ? GL_RGB
                                              : ( channels == 1 )   ? GL_RED
                                                                    : GL_RGB;

    // Upload the pixel data from CPU RAM to GPU video memory. This is the main "upload" call
    // that actually creates the texture's storage and fills it with image data.
    // Parameters: target, mip level 0 (full size), internal format, width, height, border (0),
    //             pixel format, data type (unsigned bytes = 0-255 per channel), pointer to pixels.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexImage2D.xhtml
    glTexImage2D( GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data );

    if ( generateMips )
    {
        // Auto-generate mipmaps — smaller pre-filtered versions of the texture (half size,
        // quarter size, etc). When an object is far away, the GPU uses a smaller mipmap
        // instead of the full-res texture, reducing shimmering/aliasing and improving cache perf.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenerateMipmap.xhtml
        glGenerateMipmap( GL_TEXTURE_2D );

        // Set minification filter (used when texture appears smaller than its native resolution).
        // LINEAR_MIPMAP_LINEAR = "trilinear filtering" — smoothly blends between mip levels
        // for the highest quality. NEAREST_MIPMAP_NEAREST = sharp/pixelated (retro look).
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexParameter.xhtml
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linearFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST );
    }
    else
    {
        // No mipmaps — just use LINEAR (smooth) or NEAREST (pixelated) filtering.
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linearFilter ? GL_LINEAR : GL_NEAREST );
    }

    // Set magnification filter (used when texture appears larger than its native resolution —
    // e.g. when the camera is very close). LINEAR = smooth interpolation between texels.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexParameter.xhtml
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linearFilter ? GL_LINEAR : GL_NEAREST );

    // Unbind — good practice to leave no texture accidentally active.
    glBindTexture( GL_TEXTURE_2D, 0 );

    return static_cast<uint32_t>( tex );
}


void RenderBackendGL::BindTexture( uint32_t handle, int slot )
{
    // Activate a texture unit (slot). The GPU has multiple texture slots (0, 1, 2, etc.)
    // so shaders can sample from multiple textures simultaneously (e.g. slot 0 = diffuse
    // color, slot 1 = reflection map). This selects which slot the next bind affects.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glActiveTexture.xhtml
    glActiveTexture( GL_TEXTURE0 + slot );

    // Bind the texture to the active slot. The shader can now sample this texture by
    // referencing the corresponding sampler uniform (set to this slot number).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindTexture.xhtml
    glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( handle ) );
}


void RenderBackendGL::DeleteTexture( uint32_t handle )
{
    GLuint tex = static_cast<GLuint>( handle );
    // Free the GPU memory associated with this texture and release the handle.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteTextures.xhtml
    glDeleteTextures( 1, &tex );
}


// --- Screenshot ---


std::vector<uint8_t> RenderBackendGL::CaptureBackbuffer( int& outWidth, int& outHeight )
{
    GLint viewport[4];
    // Query the current viewport dimensions (x, y, width, height) so we know how many
    // pixels to read back.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGet.xhtml
    glGetIntegerv( GL_VIEWPORT, viewport );
    outWidth = viewport[2];
    outHeight = viewport[3];

    int rowBytes = outWidth * 3;
    int padded = ( rowBytes + 3 ) & ~3;
    std::vector<uint8_t> pixels( padded * outHeight );

    // Wait for ALL GPU commands to finish — we need the final rendered image in the
    // framebuffer before reading it.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFinish.xhtml
    glFinish();

    // Set pixel packing alignment to 4 bytes. When transferring pixel data FROM the GPU
    // back to CPU memory, each row is padded to a multiple of 4 bytes. This matches the
    // BMP file format's row alignment requirement.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPixelStore.xhtml
    glPixelStorei( GL_PACK_ALIGNMENT, 4 );

    // Select the back buffer as the source to read from (the one we just rendered into,
    // before SwapBuffers flips it to the screen).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glReadBuffer.xhtml
    glReadBuffer( GL_BACK );

    // Read the pixel data from the GPU framebuffer into our CPU-side vector.
    // We request BGR format (blue-green-red) because BMP files store pixels in that order.
    // This is a slow operation — data travels from GPU VRAM → CPU RAM over the PCIe bus.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glReadPixels.xhtml
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

    // Create a VAO — this stores the entire vertex layout configuration (which attributes
    // exist, their sizes, offsets, etc). Once set up, binding the VAO restores all settings.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenVertexArrays.xhtml
    glGenVertexArrays( 1, &dvb.vao );
    glBindVertexArray( dvb.vao );

    // Create a VBO (Vertex Buffer Object) — a block of GPU memory that will hold vertex data.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenBuffers.xhtml
    glGenBuffers( 1, &dvb.vbo );

    // Bind the VBO to the GL_ARRAY_BUFFER target — this means "the next buffer operations
    // apply to this buffer."
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindBuffer.xhtml
    glBindBuffer( GL_ARRAY_BUFFER, dvb.vbo );

    // Allocate GPU memory for the buffer but don't fill it yet (nullptr data). GL_STREAM_DRAW
    // tells the driver this buffer will be updated very frequently (every frame) and used for
    // drawing — the driver can optimize memory placement accordingly.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferData.xhtml
    glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( maxVertices ) * floatsPerVert * static_cast<GLsizeiptr>( sizeof( float ) ), nullptr, GL_STREAM_DRAW );

    int stride = floatsPerVert * static_cast<int>( sizeof( float ) );
    int offset = 0;
    for ( int i = 0; i < numAttribs; ++i )
    {
        // Enable vertex attribute at location i. Attributes are the "inputs" to the vertex
        // shader (position, normal, UV coords, etc.). Each one must be explicitly enabled.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnableVertexAttribArray.xhtml
        glEnableVertexAttribArray( static_cast<GLuint>( i ) );

        // Describe the layout of attribute i: how many components (2/3/4), data type (float),
        // stride (bytes between consecutive vertices), and offset (where this attribute starts
        // within each vertex). The GPU uses this to correctly extract data from the VBO.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glVertexAttribPointer.xhtml
        glVertexAttribPointer( static_cast<GLuint>( i ), attribComponents[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
        offset += attribComponents[i] * static_cast<int>( sizeof( float ) );
    }

    // Unbind VAO and VBO to prevent accidental modification.
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

    // Bind the VAO (restores all the vertex layout settings) and the VBO.
    glBindVertexArray( dvb.vao );
    glBindBuffer( GL_ARRAY_BUFFER, dvb.vbo );

    // "Buffer orphaning" technique: call glBufferData with nullptr to tell the driver we're
    // done with the old data. The driver can now give us a fresh memory block without waiting
    // for the GPU to finish reading the old one. This avoids an implicit CPU/GPU sync stall.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferData.xhtml
    GLsizeiptr bufSize = static_cast<GLsizeiptr>( dvb.maxVertices ) * dvb.floatsPerVertex * static_cast<GLsizeiptr>( sizeof( float ) );
    glBufferData( GL_ARRAY_BUFFER, bufSize, nullptr, GL_STREAM_DRAW );

    // Upload the actual vertex data for this frame into the (now-fresh) buffer.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferSubData.xhtml
    glBufferSubData( GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>( vertexCount ) * dvb.floatsPerVertex * static_cast<GLsizeiptr>( sizeof( float ) ), data );

    // Issue a draw call — tells the GPU to run the vertex+fragment shaders on this data.
    // GL_TRIANGLES means every 3 consecutive vertices form one triangle.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDrawArrays.xhtml
    glDrawArrays( GL_TRIANGLES, 0, vertexCount );
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


uint32_t RenderBackendGL::CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs, const int* staticAttribSizes, int numStaticAttribs )
{
    // --- Instanced Rendering Concept ---
    // Instead of drawing the same mesh 300 times with 300 separate draw calls (slow!),
    // instanced rendering draws it once and says "repeat this 300 times, with different
    // per-instance data (position/rotation) each time." One draw call, 300 objects.
    //
    // We need two VBOs:
    //   1. Static VBO — the mesh geometry (shared by all instances, never changes)
    //   2. Instance VBO — per-instance transforms (updated every frame)

    InstancedMesh im = {};
    im.staticFloatsPerVert = staticFloatsPerVert;
    im.instanceFloats = instanceFloats;

    // Create the VAO that will store both the static mesh layout AND the instance data layout.
    glGenVertexArrays( 1, &im.vao );
    glBindVertexArray( im.vao );

    // --- Static geometry VBO (the shared mesh) ---
    glGenBuffers( 1, &im.staticVBO );
    glBindBuffer( GL_ARRAY_BUFFER, im.staticVBO );

    // Upload the mesh vertex data. GL_STATIC_DRAW = this data is uploaded once and never
    // changes (the driver can put it in fast GPU-only memory).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferData.xhtml
    glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( staticVertCount ) * staticFloatsPerVert * static_cast<GLsizeiptr>( sizeof( float ) ), staticData, GL_STATIC_DRAW );

    // Static attributes — describe the per-vertex data layout
    if ( numStaticAttribs > 0 && staticAttribSizes )
    {
        // Multi-attribute layout (e.g. pos3+normal3+uv2)
        int stride = staticFloatsPerVert * static_cast<int>( sizeof( float ) );
        int offset = 0;
        for ( int i = 0; i < numStaticAttribs; ++i )
        {
            glEnableVertexAttribArray( static_cast<GLuint>( i ) );
            glVertexAttribPointer( static_cast<GLuint>( i ), staticAttribSizes[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
            offset += staticAttribSizes[i] * static_cast<int>( sizeof( float ) );
        }
    }
    else
    {
        // Legacy: single attribute at location 0 (shadow disc compatibility)
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, staticFloatsPerVert, GL_FLOAT, GL_FALSE, staticFloatsPerVert * static_cast<int>( sizeof( float ) ), nullptr );
    }

    // --- Instance data VBO (per-object transforms, updated every frame) ---
    glGenBuffers( 1, &im.instanceVBO );
    glBindBuffer( GL_ARRAY_BUFFER, im.instanceVBO );

    // Allocate space for per-instance data. GL_DYNAMIC_DRAW = updated frequently (every frame)
    // but not as aggressively as STREAM_DRAW. The driver balances between GPU-fast and CPU-writable.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferData.xhtml
    glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( maxInstances ) * instanceFloats * static_cast<GLsizeiptr>( sizeof( float ) ), nullptr, GL_DYNAMIC_DRAW );

    int stride = instanceFloats * static_cast<int>( sizeof( float ) );
    int offset = 0;
    for ( int i = 0; i < numInstanceAttribs; ++i )
    {
        GLuint loc = static_cast<GLuint>( instanceStartAttrib + i );
        glEnableVertexAttribArray( loc );
        glVertexAttribPointer( loc, instanceAttribSizes[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );

        // THIS IS THE KEY CALL that makes instancing work: glVertexAttribDivisor(loc, 1)
        // tells the GPU "advance this attribute once per INSTANCE, not once per vertex."
        // Divisor=0 means per-vertex (default), divisor=1 means per-instance.
        // So the model matrix (4×vec4 at locations 3-6) changes for each sphere, but the
        // mesh vertices (locations 0-2) repeat for every instance.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glVertexAttribDivisor.xhtml
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

    // Bind the instance VBO and upload the per-instance data (e.g. 300 model matrices,
    // one 4×4 matrix per sphere). This happens every frame since objects move.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindBuffer.xhtml
    glBindBuffer( GL_ARRAY_BUFFER, im.instanceVBO );

    // Write the new instance data into the buffer. Offset 0 = overwrite from the start.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferSubData.xhtml
    glBufferSubData( GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>( floatCount ) * static_cast<GLsizeiptr>( sizeof( float ) ), data );
}


void RenderBackendGL::DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount )
{
    if ( handle == 0 || handle > static_cast<uint32_t>( m_instancedMeshes.size() ) )
    {
        return;
    }
    InstancedMesh& im = m_instancedMeshes[handle - 1];

    // Bind the VAO (restores the complete vertex/instance attribute layout).
    glBindVertexArray( im.vao );

    // Draw all instances in a single GPU command. This draws the mesh (staticVertCount
    // vertices) repeated instanceCount times. The GPU reads per-vertex data from the static
    // VBO and per-instance data (model matrix) from the instance VBO, thanks to the
    // divisor=1 setting. One draw call renders hundreds of objects.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDrawArraysInstanced.xhtml
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


// --- Debug Line Rendering ---


void RenderBackendGL::DrawLines( const float* verts, int vertCount, float r, float g, float b, const float* viewProjMatrix16 )
{
    if ( vertCount <= 0 )
    {
        return;
    }

    // Lazy-init VAO/VBO for debug line rendering
    if ( m_debugLineVAO == 0 )
    {
        glGenVertexArrays( 1, &m_debugLineVAO );
        glGenBuffers( 1, &m_debugLineVBO );
        glBindVertexArray( m_debugLineVAO );
        glBindBuffer( GL_ARRAY_BUFFER, m_debugLineVBO );
        glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( 2048 * 3 * sizeof( float ) ), nullptr, GL_DYNAMIC_DRAW );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), reinterpret_cast<void*>( 0 ) );
        glBindVertexArray( 0 );
    }

    // Lazy-init shader
    if ( !m_debugLineShader )
    {
        m_debugLineShader = CreateShader( "shaders/debug_line" );
    }

    // Upload line vertex data to the VBO
    glBindBuffer( GL_ARRAY_BUFFER, m_debugLineVBO );
    GLsizeiptr needed = static_cast<GLsizeiptr>( vertCount * 3 * sizeof( float ) );
    GLsizeiptr capacity = static_cast<GLsizeiptr>( 2048 * 3 * sizeof( float ) );
    if ( needed > capacity )
    {
        glBufferData( GL_ARRAY_BUFFER, needed, nullptr, GL_DYNAMIC_DRAW );
    }
    glBufferSubData( GL_ARRAY_BUFFER, 0, needed, verts );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    SetDepthTest( false );
    m_debugLineShader->Use();

    using SkullbonezCore::Math::Transformation::Matrix4;
    Matrix4 vpMat( viewProjMatrix16 );
    m_debugLineShader->SetMat4( "uViewProj", vpMat );
    m_debugLineShader->SetVec4( "uColor", r, g, b, 1.0f );

    glBindVertexArray( m_debugLineVAO );
    glLineWidth( 2.0f );
    glDrawArrays( GL_LINES, 0, vertCount );
    glLineWidth( 1.0f );
    glBindVertexArray( 0 );
    SetDepthTest( true );
}


} // namespace Rendering
} // namespace SkullbonezCore
