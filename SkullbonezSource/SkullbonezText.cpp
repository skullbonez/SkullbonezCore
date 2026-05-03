// --- Includes ---
#include "SkullbonezText.h"
#include "SkullbonezIRenderBackend.h"


// --- Usings ---
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;


static const int FONT_SIZE = 32;                         // Font rendering m_height in pixels (CreateFont -nHeight)
static const int FONT_CELL_W = 40;                       // Width of each character cell (wider than any Verdana glyph)
static const int FONT_CELL_H = 48;                       // Height of each character cell (FONT_SIZE + descender/AA padding)
static const int FONT_COLS = 16;                         // Number of columns in the atlas
static const int FONT_ROWS = 6;                          // Number of rows in the atlas (16*6 = 96 chars)
static const int FONT_ATLAS_W = FONT_CELL_W * FONT_COLS; // 640 pixels
static const int FONT_ATLAS_H = FONT_CELL_H * FONT_ROWS; // 288 pixels

void Text2d::BuildFont( const HDC hDC, const char* cFontName )
{
    // Create a top-down 32bpp DIB section to render the font atlas into
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
    bmi.bmiHeader.biWidth = FONT_ATLAS_W;
    bmi.bmiHeader.biHeight = -FONT_ATLAS_H; // negative = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HDC memDC = CreateCompatibleDC( hDC );
    HBITMAP hBitmap = CreateDIBSection( hDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0 );

    if ( !hBitmap || !memDC )
    {
        if ( memDC )
        {
            DeleteDC( memDC );
        }
        if ( hBitmap )
        {
            DeleteObject( hBitmap );
        }
        throw std::runtime_error( "DIB section creation failed (Text2d::BuildFont)" );
    }

    HBITMAP hOldBitmap = reinterpret_cast<HBITMAP>( SelectObject( memDC, hBitmap ) );

    // Fill with black
    RECT fillRect = { 0, 0, FONT_ATLAS_W, FONT_ATLAS_H };
    HBRUSH hBlackBrush = CreateSolidBrush( RGB( 0, 0, 0 ) );
    FillRect( memDC, &fillRect, hBlackBrush );
    DeleteObject( hBlackBrush );

    // Create the requested font at the cell m_height
    HFONT hFont = CreateFont(
        -FONT_SIZE, // negative = character m_height in pixels
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        ANSI_CHARSET,
        OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        FF_DONTCARE | DEFAULT_PITCH,
        cFontName );

    if ( !hFont )
    {
        SelectObject( memDC, hOldBitmap );
        DeleteObject( hBitmap );
        DeleteDC( memDC );
        throw std::runtime_error( "Font creation failed (Text2d::BuildFont)" );
    }

    HFONT hOldFont = reinterpret_cast<HFONT>( SelectObject( memDC, hFont ) );

    // Measure advance widths for all 96 printable ASCII chars (32..127)
    INT advWidths[96] = {};
    GetCharWidth32( memDC, 32, 127, advWidths );
    for ( int i = 0; i < 96; ++i )
    {
        Text2d::charAdvance[i] = static_cast<float>( advWidths[i] ) / static_cast<float>( FONT_SIZE );
    }

    // Render each character into its atlas cell
    SetBkMode( memDC, TRANSPARENT );
    SetTextColor( memDC, RGB( 255, 255, 255 ) );

    char ch[2] = { 0, 0 };
    for ( int i = 0; i < 96; ++i )
    {
        ch[0] = static_cast<char>( i + 32 );
        int col = i % FONT_COLS;
        int row = i / FONT_COLS;
        TextOutA( memDC, col * FONT_CELL_W, row * FONT_CELL_H, ch, 1 );
    }

    // Extract the red channel (white on black, so R=luminance) into a single-channel buffer
    std::unique_ptr<unsigned char[]> atlasData( new unsigned char[FONT_ATLAS_W * FONT_ATLAS_H] );
    const DWORD* pPixels = reinterpret_cast<const DWORD*>( pBits );
    for ( int i = 0; i < FONT_ATLAS_W * FONT_ATLAS_H; ++i )
    {
        atlasData[i] = static_cast<unsigned char>( pPixels[i] & 0xFF );
    }

    // Upload atlas to a backend texture (single red channel)
    Text2d::fontTexture = Gfx().CreateTexture2D( atlasData.get(), FONT_ATLAS_W, FONT_ATLAS_H, 1, false, false );

    // Create dynamic vertex buffer for text quad batches: [x, y, u, v] per vertex
    int textAttribs[] = { 2, 2 };
    Text2d::dynamicVB = Gfx().CreateDynamicVB( textAttribs, 2, 512 * 6 );

    // Compile the text m_shader
    Text2d::pTextShader = Gfx().CreateShader(
        "SkullbonezData/shaders/text.vert",
        "SkullbonezData/shaders/text.frag" );
    Text2d::pTextShader->Use();
    Text2d::pTextShader->SetInt( "uFontTexture", 0 );

    // Compile the solid-colour HUD quad m_shader (used by Render2dQuad)
    Text2d::pSolidShader = Gfx().CreateShader(
        "SkullbonezData/shaders/solid_color.vert",
        "SkullbonezData/shaders/solid_color.frag" );

    // Cleanup GDI resources
    SelectObject( memDC, hOldFont );
    SelectObject( memDC, hOldBitmap );
    DeleteObject( hFont );
    DeleteObject( hBitmap );
    DeleteDC( memDC );
}


void Text2d::DeleteFont()
{
    if ( Text2d::fontTexture )
    {
        Gfx().DeleteTexture( Text2d::fontTexture );
        Text2d::fontTexture = 0;
    }
    if ( Text2d::dynamicVB )
    {
        Gfx().DestroyDynamicVB( Text2d::dynamicVB );
        Text2d::dynamicVB = 0;
    }
    Text2d::pTextShader.reset();
    Text2d::pSolidShader.reset();
}


static void RenderTextInternal( float xPosition, float yPosition, float fSize, float colR, float colG, float colB, const char* formatted )
{
    using SkullbonezCore::Math::Transformation::Matrix4;
    using SkullbonezCore::Text::Text2d;

    static float s_vertBuf[512 * 6 * 4]; // max 512 chars * 6 verts * 4 floats

    const int len = static_cast<int>( strlen( formatted ) );
    if ( len == 0 )
    {
        return;
    }

    // Build orthographic projection matching the legacy FFP coordinate space.
    const float halfH = tanf( 22.5f * _PI / 180.0f );
    const float halfW = halfH * static_cast<float>( Cfg().screenX ) / static_cast<float>( Cfg().screenY );
    Matrix4 proj = Matrix4::Ortho( -halfW, halfW, -halfH, halfH, -1.0f, 1.0f );

    // Build vertex data: 6 verts per character (2 triangles), 4 floats per vert
    int vertCount = 0;
    float penX = xPosition;
    float penY = yPosition;

    for ( int i = 0; i < len && ( vertCount + 6 ) <= 512 * 6; ++i )
    {
        unsigned char c = (unsigned char)formatted[i];
        if ( c < 32 || c > 127 )
        {
            penX += fSize * 0.5f;
            continue;
        }

        int idx = c - 32;
        int col = idx % FONT_COLS;
        int row = idx / FONT_COLS;

        const float halfU = 0.5f / static_cast<float>( FONT_ATLAS_W );
        const float halfV = 0.5f / static_cast<float>( FONT_ATLAS_H );

        float u0 = static_cast<float>( col * FONT_CELL_W ) / static_cast<float>( FONT_ATLAS_W ) + halfU;
        float v0 = static_cast<float>( row * FONT_CELL_H ) / static_cast<float>( FONT_ATLAS_H ) + halfV;
        float u1 = u0 + ( Text2d::charAdvance[idx] * static_cast<float>( FONT_SIZE ) ) / static_cast<float>( FONT_ATLAS_W ) - halfU;
        float v1 = static_cast<float>( row * FONT_CELL_H + FONT_SIZE ) / static_cast<float>( FONT_ATLAS_H ) - halfV;

        float charW = Text2d::charAdvance[idx] * fSize;
        float charH = fSize;

        float x0 = penX;
        float x1 = penX + charW;
        float y0 = penY;
        float y1 = penY + charH;

        float* v = &s_vertBuf[vertCount * 4];

        // Triangle 1
        v[0] = x0;
        v[1] = y0;
        v[2] = u0;
        v[3] = v1;
        v[4] = x1;
        v[5] = y0;
        v[6] = u1;
        v[7] = v1;
        v[8] = x1;
        v[9] = y1;
        v[10] = u1;
        v[11] = v0;
        // Triangle 2
        v[12] = x0;
        v[13] = y0;
        v[14] = u0;
        v[15] = v1;
        v[16] = x1;
        v[17] = y1;
        v[18] = u1;
        v[19] = v0;
        v[20] = x0;
        v[21] = y1;
        v[22] = u0;
        v[23] = v0;

        vertCount += 6;
        penX += charW;
    }

    if ( vertCount == 0 )
    {
        return;
    }

    // Save relevant state
    bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    bool blendWasEnabled = Gfx().IsBlendEnabled();

    Gfx().SetDepthTest( false );
    Gfx().SetBlend( true );
    Gfx().SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );

    // Set up ShaderGL and uniforms
    Text2d::pTextShader->Use();
    Text2d::pTextShader->SetMat4( "uProjection", proj );
    Text2d::pTextShader->SetVec3( "uTextColor", colR, colG, colB );

    Gfx().BindTexture( Text2d::fontTexture, 0 );

    // Upload quads and draw via dynamic VB
    Gfx().UploadAndDrawDynamicVB( Text2d::dynamicVB, s_vertBuf, vertCount );

    // Restore saved state
    Gfx().SetDepthTest( depthWasEnabled );
    Gfx().SetBlend( blendWasEnabled );
}


void Text2d::Render2dText( float xPosition,
                           float yPosition,
                           float fSize,
                           const char* cRawText,
                           ... )
{
    if ( !cRawText || !Text2d::pTextShader )
    {
        return;
    }

    static char s_textBuf[512];
    va_list args;
    va_start( args, cRawText );
    vsprintf_s( s_textBuf, sizeof( s_textBuf ), cRawText, args );
    va_end( args );

    RenderTextInternal( xPosition, yPosition, fSize, 1.0f, 1.0f, 1.0f, s_textBuf );
}


void Text2d::Render2dTextColor( float xPosition,
                                float yPosition,
                                float fSize,
                                float r,
                                float g,
                                float b,
                                const char* cRawText,
                                ... )
{
    if ( !cRawText || !Text2d::pTextShader )
    {
        return;
    }

    static char s_textBuf[512];
    va_list args;
    va_start( args, cRawText );
    vsprintf_s( s_textBuf, sizeof( s_textBuf ), cRawText, args );
    va_end( args );

    RenderTextInternal( xPosition, yPosition, fSize, r, g, b, s_textBuf );
}


void Text2d::Render2dQuad( float x0, float y0, float x1, float y1, float r, float g, float b, float a )
{
    if ( !Text2d::pSolidShader || !Text2d::dynamicVB )
    {
        return;
    }

    // Reuse the text VAO/VBO. Layout is (vec2 pos, vec2 uv); the solid ShaderGL only reads
    // location 0, so the uv slots are dummy zeros.
    static float s_quadBuf[6 * 4];
    s_quadBuf[0] = x0;
    s_quadBuf[1] = y0;
    s_quadBuf[2] = 0.0f;
    s_quadBuf[3] = 0.0f;
    s_quadBuf[4] = x1;
    s_quadBuf[5] = y0;
    s_quadBuf[6] = 0.0f;
    s_quadBuf[7] = 0.0f;
    s_quadBuf[8] = x1;
    s_quadBuf[9] = y1;
    s_quadBuf[10] = 0.0f;
    s_quadBuf[11] = 0.0f;
    s_quadBuf[12] = x0;
    s_quadBuf[13] = y0;
    s_quadBuf[14] = 0.0f;
    s_quadBuf[15] = 0.0f;
    s_quadBuf[16] = x1;
    s_quadBuf[17] = y1;
    s_quadBuf[18] = 0.0f;
    s_quadBuf[19] = 0.0f;
    s_quadBuf[20] = x0;
    s_quadBuf[21] = y1;
    s_quadBuf[22] = 0.0f;
    s_quadBuf[23] = 0.0f;

    const float halfH = tanf( 22.5f * _PI / 180.0f );
    const float halfW = halfH * static_cast<float>( Cfg().screenX ) / static_cast<float>( Cfg().screenY );
    Matrix4 proj = Matrix4::Ortho( -halfW, halfW, -halfH, halfH, -1.0f, 1.0f );

    bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    bool blendWasEnabled = Gfx().IsBlendEnabled();

    Gfx().SetDepthTest( false );
    Gfx().SetBlend( true );
    Gfx().SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );

    Text2d::pSolidShader->Use();
    Text2d::pSolidShader->SetMat4( "uProjection", proj );
    Text2d::pSolidShader->SetVec4( "uColor", r, g, b, a );

    Gfx().UploadAndDrawDynamicVB( Text2d::dynamicVB, s_quadBuf, 6 );

    Gfx().SetDepthTest( depthWasEnabled );
    Gfx().SetBlend( blendWasEnabled );
}
