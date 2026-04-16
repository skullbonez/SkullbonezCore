/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------------
                                  THE SKULLBONEZ CORE
                                        _______
                                     .-"       "-.
                                    /             \
                                   /               \
                                   ?   .--. .--.   ?
                                   ? )/   ? ?   \( ?
                                   ?/ \__/   \__/ \?
                                   /      /^\      \
                                   \__    '='    __/
                                     ?\         /?
                                     ?\'"VUUUV"'/?
                                     \ `"""""""` /
                                      `-._____.-'

                                 www.simoneschbach.com
-----------------------------------------------------------------------------------*/

/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezText.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;

/* -- INITIALISE STATICS ----------------------------------------------------------*/
GLuint Text2d::fontTexture = 0;
GLuint Text2d::textVAO = 0;
GLuint Text2d::textVBO = 0;
std::unique_ptr<Shader> Text2d::pTextShader;
float Text2d::charAdvance[96] = {};

/* -- Font atlas layout constants -------------------------------------------------*/
static const int FONT_SIZE = 32;                         // Font rendering height in pixels (CreateFont -nHeight)
static const int FONT_CELL_W = 40;                       // Width of each character cell (wider than any Verdana glyph)
static const int FONT_CELL_H = 48;                       // Height of each character cell (FONT_SIZE + descender/AA padding)
static const int FONT_COLS = 16;                         // Number of columns in the atlas
static const int FONT_ROWS = 6;                          // Number of rows in the atlas (16*6 = 96 chars)
static const int FONT_ATLAS_W = FONT_CELL_W * FONT_COLS; // 640 pixels
static const int FONT_ATLAS_H = FONT_CELL_H * FONT_ROWS; // 288 pixels

/* -- BUILD FONT ------------------------------------------------------------------*/
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

    HBITMAP hOldBitmap = (HBITMAP)SelectObject( memDC, hBitmap );

    // Fill with black
    RECT fillRect = { 0, 0, FONT_ATLAS_W, FONT_ATLAS_H };
    HBRUSH hBlackBrush = CreateSolidBrush( RGB( 0, 0, 0 ) );
    FillRect( memDC, &fillRect, hBlackBrush );
    DeleteObject( hBlackBrush );

    // Create the requested font at the cell height
    HFONT hFont = CreateFont(
        -FONT_SIZE, // negative = character height in pixels
        0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
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

    HFONT hOldFont = (HFONT)SelectObject( memDC, hFont );

    // Measure advance widths for all 96 printable ASCII chars (32..127)
    INT advWidths[96] = {};
    GetCharWidth32( memDC, 32, 127, advWidths );
    for ( int i = 0; i < 96; ++i )
    {
        Text2d::charAdvance[i] = (float)advWidths[i] / (float)FONT_SIZE;
    }

    // Render each character into its atlas cell
    SetBkMode( memDC, TRANSPARENT );
    SetTextColor( memDC, RGB( 255, 255, 255 ) );

    char ch[2] = { 0, 0 };
    for ( int i = 0; i < 96; ++i )
    {
        ch[0] = (char)( i + 32 );
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

    // Upload atlas to a GL R8 texture
    glGenTextures( 1, &Text2d::fontTexture );
    glBindTexture( GL_TEXTURE_2D, Text2d::fontTexture );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_R8,
                  FONT_ATLAS_W, FONT_ATLAS_H,
                  0, GL_RED, GL_UNSIGNED_BYTE, atlasData.get() );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glBindTexture( GL_TEXTURE_2D, 0 );

    // Create VAO and dynamic VBO for text quad batches
    glGenVertexArrays( 1, &Text2d::textVAO );
    glGenBuffers( 1, &Text2d::textVBO );
    glBindVertexArray( Text2d::textVAO );
    glBindBuffer( GL_ARRAY_BUFFER, Text2d::textVBO );

    // Layout: [x, y, u, v] per vertex, 4 floats stride
    glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( float ), (void*)0 );
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( float ), (void*)( 2 * sizeof( float ) ) );
    glEnableVertexAttribArray( 1 );

    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    // Compile the text shader
    Text2d::pTextShader = std::make_unique<Shader>(
        "SkullbonezData/shaders/text.vert",
        "SkullbonezData/shaders/text.frag" );

    // Cleanup GDI resources
    SelectObject( memDC, hOldFont );
    SelectObject( memDC, hOldBitmap );
    DeleteObject( hFont );
    DeleteObject( hBitmap );
    DeleteDC( memDC );
}

/* -- DELETE FONT -----------------------------------------------------------------*/
void Text2d::DeleteFont( void )
{
    if ( Text2d::fontTexture )
    {
        glDeleteTextures( 1, &Text2d::fontTexture );
        Text2d::fontTexture = 0;
    }
    if ( Text2d::textVBO )
    {
        glDeleteBuffers( 1, &Text2d::textVBO );
        Text2d::textVBO = 0;
    }
    if ( Text2d::textVAO )
    {
        glDeleteVertexArrays( 1, &Text2d::textVAO );
        Text2d::textVAO = 0;
    }
    Text2d::pTextShader.reset();
}

/* -- RENDER 2D TEXT --------------------------------------------------------------*/
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

    // Format the string into a fixed-size static buffer (no per-frame heap allocation)
    static char s_textBuf[512];
    static float s_vertBuf[512 * 6 * 4]; // max 512 chars * 6 verts * 4 floats

    va_list args;
    va_start( args, cRawText );
    vsprintf_s( s_textBuf, sizeof( s_textBuf ), cRawText, args );
    va_end( args );

    const int len = (int)strlen( s_textBuf );
    if ( len == 0 )
    {
        return;
    }

    // Build orthographic projection matching the legacy FFP coordinate space.
    // With FOV=45 and near=1: halfH = tan(22.5 deg), halfW = halfH * aspect.
    // This exactly reproduces the frustum-unit space used by the old glTranslatef calls.
    const float halfH = tanf( 22.5f * _PI / 180.0f );
    const float halfW = halfH * (float)SCREEN_X / (float)SCREEN_Y;
    Matrix4 proj = Matrix4::Ortho( -halfW, halfW, -halfH, halfH, -1.0f, 1.0f );

    // Build vertex data: 6 verts per character (2 triangles), 4 floats per vert
    int vertCount = 0;
    float penX = xPosition;
    float penY = yPosition;

    for ( int i = 0; i < len && ( vertCount + 6 ) <= 512 * 6; ++i )
    {
        unsigned char c = (unsigned char)s_textBuf[i];
        if ( c < 32 || c > 127 )
        {
            penX += fSize * 0.5f;
            continue;
        }

        int idx = c - 32;
        int col = idx % FONT_COLS;
        int row = idx / FONT_COLS;

        const float halfU = 0.5f / (float)FONT_ATLAS_W;
        const float halfV = 0.5f / (float)FONT_ATLAS_H;

        float u0 = (float)( col * FONT_CELL_W ) / (float)FONT_ATLAS_W + halfU;
        float v0 = (float)( row * FONT_CELL_H ) / (float)FONT_ATLAS_H + halfV;
        // u1 samples only the glyph's actual advance width within the cell
        float u1 = u0 + ( Text2d::charAdvance[idx] * (float)FONT_SIZE ) / (float)FONT_ATLAS_W - halfU;
        // v1 samples only FONT_SIZE texels (the glyph), not the full FONT_CELL_H (which includes padding)
        float v1 = (float)( row * FONT_CELL_H + FONT_SIZE ) / (float)FONT_ATLAS_H - halfV;

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

    // Save relevant GL state
    GLboolean depthEnabled = glIsEnabled( GL_DEPTH_TEST );
    GLboolean blendEnabled = glIsEnabled( GL_BLEND );

    glDisable( GL_DEPTH_TEST );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    // Set up shader and uniforms
    Text2d::pTextShader->Use();
    Text2d::pTextShader->SetMat4( "uProjection", proj );
    Text2d::pTextShader->SetInt( "uFontTexture", 0 );
    Text2d::pTextShader->SetVec3( "uTextColor", 1.0f, 1.0f, 1.0f );

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, Text2d::fontTexture );

    // Upload quads and draw
    glBindVertexArray( Text2d::textVAO );
    glBindBuffer( GL_ARRAY_BUFFER, Text2d::textVBO );
    glBufferData( GL_ARRAY_BUFFER, vertCount * 4 * sizeof( float ), s_vertBuf, GL_STREAM_DRAW );
    glDrawArrays( GL_TRIANGLES, 0, vertCount );

    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindVertexArray( 0 );
    glBindTexture( GL_TEXTURE_2D, 0 );

    // Restore saved state
    if ( depthEnabled )
    {
        glEnable( GL_DEPTH_TEST );
    }
    else
    {
        glDisable( GL_DEPTH_TEST );
    }
    if ( blendEnabled )
    {
        glEnable( GL_BLEND );
    }
    else
    {
        glDisable( GL_BLEND );
    }
}
