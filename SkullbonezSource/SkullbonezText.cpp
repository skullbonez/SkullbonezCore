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

// --- Text batch accumulation buffers ---
// Layout per vertex: [x, y, u, v, r, g, b] (7 floats)
// Batching all Render2dText* calls into one UploadAndDrawDynamicVB per frame
// eliminates ~20 individual draw calls, shader binds, and state save/restores.
static constexpr int TEXT_BATCH_MAX_CHARS = 2048;
static constexpr int TEXT_BATCH_FLOATS_PER_VERT = 7;
static constexpr int TEXT_BATCH_VERTS_PER_CHAR = 6;
static float s_batchBuf[TEXT_BATCH_MAX_CHARS * TEXT_BATCH_VERTS_PER_CHAR * TEXT_BATCH_FLOATS_PER_VERT];
static int s_batchVerts = 0;

// Ortho projection matrix cached once at BuildFont time — screen dimensions never
// change after init, so there is no need to recompute this every frame.
static Matrix4 s_orthoProj;

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

    // Create the text batch VB: [x, y, u, v, r, g, b] per vertex, large enough for a full HUD frame.
    // All Render2dText* calls accumulate into this; FlushText() does one upload+draw per frame.
    int batchAttribs[] = { 2, 2, 3 };
    Text2d::textBatchVB = Gfx().CreateDynamicVB( batchAttribs, 3, TEXT_BATCH_MAX_CHARS * TEXT_BATCH_VERTS_PER_CHAR );

    // Create the solid-quad VB: [x, y, u, v] per vertex (Render2dQuad only; 6 verts max).
    int quadAttribs[] = { 2, 2 };
    Text2d::dynamicVB = Gfx().CreateDynamicVB( quadAttribs, 2, 6 );

    // Compile the text shader and bind the atlas sampler slot once.
    Text2d::pTextShader = Gfx().CreateShader( "shaders/text" );
    Text2d::pTextShader->Use();
    Text2d::pTextShader->SetInt( "uFontTexture", 0 );

    // Compile the solid-colour HUD quad shader (used by Render2dQuad)
    Text2d::pSolidShader = Gfx().CreateShader( "shaders/solid_color" );

    // Cache the orthographic projection — screen dimensions are fixed after init.
    // Matches the legacy FFP coordinate space: FOV=45°, aspect=screenX/screenY.
    const float halfH = tanf( 22.5f * _PI / 180.0f );
    const float halfW = halfH * static_cast<float>( Cfg().screenX ) / static_cast<float>( Cfg().screenY );
    s_orthoProj = Matrix4::Ortho( -halfW, halfW, -halfH, halfH, -1.0f, 1.0f );

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
    if ( Text2d::textBatchVB )
    {
        Gfx().DestroyDynamicVB( Text2d::textBatchVB );
        Text2d::textBatchVB = 0;
    }
    if ( Text2d::dynamicVB )
    {
        Gfx().DestroyDynamicVB( Text2d::dynamicVB );
        Text2d::dynamicVB = 0;
    }
    Text2d::pTextShader.reset();
    Text2d::pSolidShader.reset();
    s_batchVerts = 0;
}


static void RenderTextInternal( float xPosition, float yPosition, float fSize, float colR, float colG, float colB, const char* formatted )
{
    const int len = static_cast<int>( strlen( formatted ) );
    if ( len == 0 )
    {
        return;
    }

    float penX = xPosition;
    float penY = yPosition;

    for ( int i = 0; i < len; ++i )
    {
        // Guard against overflowing the batch buffer.
        if ( s_batchVerts + TEXT_BATCH_VERTS_PER_CHAR > TEXT_BATCH_MAX_CHARS * TEXT_BATCH_VERTS_PER_CHAR )
        {
            break;
        }

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

        // 7 floats per vertex: [x, y, u, v, r, g, b]
        float* v = &s_batchBuf[s_batchVerts * TEXT_BATCH_FLOATS_PER_VERT];
        // Triangle 1
        v[0] = x0;
        v[1] = y0;
        v[2] = u0;
        v[3] = v1;
        v[4] = colR;
        v[5] = colG;
        v[6] = colB;
        v[7] = x1;
        v[8] = y0;
        v[9] = u1;
        v[10] = v1;
        v[11] = colR;
        v[12] = colG;
        v[13] = colB;
        v[14] = x1;
        v[15] = y1;
        v[16] = u1;
        v[17] = v0;
        v[18] = colR;
        v[19] = colG;
        v[20] = colB;
        // Triangle 2
        v[21] = x0;
        v[22] = y0;
        v[23] = u0;
        v[24] = v1;
        v[25] = colR;
        v[26] = colG;
        v[27] = colB;
        v[28] = x1;
        v[29] = y1;
        v[30] = u1;
        v[31] = v0;
        v[32] = colR;
        v[33] = colG;
        v[34] = colB;
        v[35] = x0;
        v[36] = y1;
        v[37] = u0;
        v[38] = v0;
        v[39] = colR;
        v[40] = colG;
        v[41] = colB;

        s_batchVerts += TEXT_BATCH_VERTS_PER_CHAR;
        penX += charW;
    }
}


void Text2d::FlushText()
{
    if ( s_batchVerts == 0 || !Text2d::pTextShader || !Text2d::textBatchVB )
    {
        return;
    }

    bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    bool blendWasEnabled = Gfx().IsBlendEnabled();

    Gfx().SetDepthTest( false );
    Gfx().SetBlend( true );
    Gfx().SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );

    Text2d::pTextShader->Use();
    Text2d::pTextShader->SetMat4( "uProjection", s_orthoProj );
    Gfx().BindTexture( Text2d::fontTexture, 0 );

    // One GPU upload + one draw call covers the entire frame's text at all colors.
    Gfx().UploadAndDrawDynamicVB( Text2d::textBatchVB, s_batchBuf, s_batchVerts );

    Gfx().SetDepthTest( depthWasEnabled );
    Gfx().SetBlend( blendWasEnabled );

    s_batchVerts = 0;
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

    // Reuse the text VAO/VBO. Layout is (vec2 pos, vec2 uv); the solid shader only reads
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

    const Matrix4& proj = s_orthoProj; // Cached at init — screen dimensions don't change

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
