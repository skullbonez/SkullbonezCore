/*
File: SkullbonezSource/Rendering/Text.cpp
Purpose:
  Builds and draws bitmap/SDF text for HUD and diagnostics.

Summary:
  Text.cpp builds and draws bitmap/SDF text for HUD and diagnostics. As an
  implementation unit, keep edits anchored on render submission and resource
  lifetime and on the glossary/invariants below.

Glossary:
  SDF (Signed Distance Field): Texture representation used for crisp scalable
  text rendering.
  Lane R result: Recoverable asset/tooling failure reported at startup with an
    owner and bounded message instead of an exception.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Text and quad batches use fixed vertex layouts that must match their
    shaders and backend upload calls.
  - Font atlas resources are backend-owned and must be released before renderer
    teardown or rebuild.

Related:
  - SkullbonezSource/Rendering/Text.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "Text.h"
#include "../Runtime/WindowConstants.h"
#include "../Assets/AssetSystem.h"
#include "IRenderCommandContext.h"
#include "IRenderResourceFactory.h"

#include <memory>


using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;


static const int FONT_SIZE = 32;   // Font rendering m_height in pixels (CreateFont -nHeight)
static const int FONT_CELL_W = 40; // Width of each character cell (wider than any Verdana glyph)
static const int FONT_CELL_H = 48; // Height of each character cell (FONT_SIZE + descender/AA padding)
static const int FONT_COLS = 16;   // Number of columns in the atlas
static const int FONT_ROWS = 6;    // Number of rows in the atlas (16*6 = 96 chars)
static const int FONT_ATLAS_W = FONT_CELL_W * FONT_COLS; // 640 pixels
static const int FONT_ATLAS_H = FONT_CELL_H * FONT_ROWS; // 288 pixels

// SDF generation parameters
static const int SDF_SCALE = 6;      // hi-res render factor: final × SDF_SCALE = hi-res
static const int SDF_SPREAD_HI = 36; // max encoded signed distance (hi-res px) = 6 final-atlas px

// --- Text batch accumulation buffers ---
// Layout per vertex: [x, y, u, v, r, g, b] (7 floats)
// Batching all Render2dText* calls into one UploadAndDrawDynamicVB per frame
// eliminates ~20 individual draw calls, shader binds, and state save/restores.
static constexpr int TEXT_BATCH_MAX_CHARS = 2048;
static constexpr int TEXT_BATCH_FLOATS_PER_VERT = 7;
static constexpr int TEXT_BATCH_VERTS_PER_CHAR = 6;
static float s_batchBuf[TEXT_BATCH_MAX_CHARS * TEXT_BATCH_VERTS_PER_CHAR * TEXT_BATCH_FLOATS_PER_VERT];
static int s_batchVerts = 0;

// --- Quad batch accumulation buffers ---
// Layout per vertex: [x, y, r, g, b, a] (6 floats)
// BatchQuad() accumulates quads here; FlushQuads() uploads and draws them all in
// one draw call — so an entire profiler bar overlay (background + N segments +
// legend swatches) costs exactly one draw call for all quads.
static constexpr int QUAD_BATCH_MAX_QUADS = 8192;       // up to 8192 quads per flush
static constexpr int QUAD_BATCH_FLOATS_PER_VERT = 6;    // x, y, r, g, b, a
static constexpr int QUAD_BATCH_VERTS_PER_QUAD = 6;     // 2 triangles
static constexpr int QUAD_BATCH_VERTS_PER_TRIANGLE = 3; // 1 triangle
static float s_quadBatchBuf[QUAD_BATCH_MAX_QUADS * QUAD_BATCH_VERTS_PER_QUAD * QUAD_BATCH_FLOATS_PER_VERT];
static int s_quadBatchVerts = 0;

// Ortho projection matrix cached once at BuildFont time — screen dimensions never
// change after init, so there is no need to recompute this every frame.
static Matrix4 s_orthoProj;

namespace
{
struct FileCloser
{
    void operator()( FILE* file ) const
    {
        if ( file )
        {
            fclose( file );
        }
    }
};

using FileHandle = std::unique_ptr<FILE, FileCloser>;
} // namespace

// =============================================================================
// SDF ATLAS — binary file format
// =============================================================================
//
// Header (416 bytes) followed directly by (atlasW × atlasH) R8 pixel bytes.
//   0   = far outside the glyph
//   128 = on the glyph edge
//   255 = deep inside the glyph
//
// The SDF is computed at SDF_SCALE × resolution then box-filtered down,
// preserving sub-pixel gradient information through the downsample.
// =============================================================================
struct SdfFileHeader
{
    char magic[8];         // "SBSDF001" — format identifier
    uint32_t version;      // 1
    uint32_t atlasW;       // FONT_ATLAS_W (640)
    uint32_t atlasH;       // FONT_ATLAS_H (288)
    uint32_t fontSize;     // FONT_SIZE (32)
    uint32_t cellW;        // FONT_CELL_W (40)
    uint32_t cellH;        // FONT_CELL_H (48)
    float charAdvance[96]; // per-glyph advance, in FONT_SIZE units
    // Total: 416 bytes. All members naturally aligned — no padding.
};


// =============================================================================
// 1D Euclidean Distance Transform  (Felzenszwalb & Huttenlocher, PAMI 2012)
// =============================================================================
//
// Transforms f[0..n-1] in place using caller-supplied scratch arrays.
//   Before: f[i] = 0 for source pixels, 1e20 for non-source.
//   After:  f[i] = squared Euclidean distance to the nearest source.
//
// scratch_src / scratch_v / scratch_z must have capacity ≥ n, n, n+1 respectively.
// =============================================================================
static void ComputeEDT1D( float* f, int n, float* scratch_src, int* scratch_v, float* scratch_z )
{
    memcpy( scratch_src, f, static_cast<size_t>( n ) * sizeof( float ) );

    // --- Build phase: lower envelope of upward parabolas g_q(x) = (x−q)² + src[q] ---
    //
    // We maintain a stack of non-dominated parabolas, whose pairwise intersection
    // x-coordinates are strictly increasing left to right.  Each new parabola q
    // pops any topmost parabola whose region it completely dominates.
    int k = 0;
    scratch_v[0] = 0;
    scratch_z[0] = -1e20f;
    scratch_z[1] = +1e20f;

    for ( int q = 1; q < n; ++q )
    {
        int r = scratch_v[k];
        // Intersection of parabola at q with the current topmost parabola at r:
        //   s = [(src[q] + q²) − (src[r] + r²)] / (2q − 2r)
        float s = ( ( scratch_src[q] + (float)( q * q ) ) - ( scratch_src[r] + (float)( r * r ) ) ) /
                  (float)( 2 * ( q - r ) );

        while ( k > 0 && s <= scratch_z[k] )
        {
            --k;
            r = scratch_v[k];
            s = ( ( scratch_src[q] + (float)( q * q ) ) - ( scratch_src[r] + (float)( r * r ) ) ) /
                (float)( 2 * ( q - r ) );
        }

        ++k;
        scratch_v[k] = q;
        scratch_z[k] = s;
        scratch_z[k + 1] = +1e20f;
    }

    // --- Query phase: evaluate the lower envelope at each pixel position ---
    k = 0;
    for ( int q = 0; q < n; ++q )
    {
        while ( scratch_z[k + 1] < (float)q )
        {
            ++k;
        }
        const int r = scratch_v[k];
        f[q] = (float)( ( q - r ) * ( q - r ) ) + scratch_src[r];
    }
}


// Reuse one scratch buffer while running the 1D distance transform across rows
// and then columns.
static void ComputeEDT2D( float* grid, int w, int h )
{
    const int maxN = ( w > h ) ? w : h;
    std::vector<float> scratch_src( maxN );
    std::vector<float> scratch_z( maxN + 1 );
    std::vector<float> col_tmp( h );
    std::vector<int> scratch_v( maxN );

    // Horizontal pass
    for ( int y = 0; y < h; ++y )
    {
        ComputeEDT1D( grid + y * w, w, scratch_src.data(), scratch_v.data(), scratch_z.data() );
    }

    // Vertical pass
    for ( int x = 0; x < w; ++x )
    {
        for ( int y = 0; y < h; ++y )
        {
            col_tmp[y] = grid[y * w + x];
        }
        ComputeEDT1D( col_tmp.data(), h, scratch_src.data(), scratch_v.data(), scratch_z.data() );
        for ( int y = 0; y < h; ++y )
        {
            grid[y * w + x] = col_tmp[y];
        }
    }
}


// Load a pre-generated SDF atlas only when the file matches the current font
// contract.
static bool LoadSdfAtlasFromFile( IRenderResourceFactory& renderResources, const char* path )
{
    FILE* rawFile = nullptr;
    if ( fopen_s( &rawFile, path, "rb" ) != 0 || !rawFile )
    {
        return false;
    }
    FileHandle file( rawFile );

    SdfFileHeader hdr = {};
    if ( fread( &hdr, sizeof( hdr ), 1, file.get() ) != 1u )
    {
        return false;
    }

    // Reject stale or corrupt files before touching any engine state.
    if ( memcmp( hdr.magic, "SBSDF001", 8 ) != 0 || hdr.version != 1u ||
         hdr.atlasW != static_cast<uint32_t>( FONT_ATLAS_W ) || hdr.atlasH != static_cast<uint32_t>( FONT_ATLAS_H ) ||
         hdr.fontSize != static_cast<uint32_t>( FONT_SIZE ) || hdr.cellW != static_cast<uint32_t>( FONT_CELL_W ) ||
         hdr.cellH != static_cast<uint32_t>( FONT_CELL_H ) )
    {
        return false;
    }

    memcpy( Text2d::charAdvance, hdr.charAdvance, 96 * sizeof( float ) );

    const size_t dataSize = static_cast<size_t>( FONT_ATLAS_W ) * static_cast<size_t>( FONT_ATLAS_H );
    std::unique_ptr<uint8_t[]> pixels( new uint8_t[dataSize] );
    if ( fread( pixels.get(), 1, dataSize, file.get() ) != dataSize )
    {
        return false;
    }

    // SDF rendering requires linear filtering; nearest-neighbour would staircase
    // the distance gradient and make glyph edges look aliased.
    Text2d::fontTexture = renderResources.CreateTexture2D( pixels.get(), FONT_ATLAS_W, FONT_ATLAS_H, 1, false, true );
    return true;
}


// =============================================================================
// Text2d::GenerateSdfAtlasToFile
// =============================================================================
//
// All 96 printable ASCII glyphs are drawn at SDF_SCALE x resolution using GDI,
// computes a per-cell Signed Distance Field via two 2D Euclidean Distance
// Transforms, box-filters the result down to the final atlas size, then writes
// a binary .sdf file that LoadSdfAtlasFromFile / BuildFont can read directly.
//
// This function requires no window or GPU context — it can run from --gen-atlas
// before any graphics initialisation.  Call it when the atlas is absent or
// when the font or cell dimensions have changed.
// =============================================================================
bool Text2d::GenerateSdfAtlasToFile( const char* cFontName, const char* cOutPath )
{
    // Hi-res dimensions — each axis is SDF_SCALE × the final atlas.
    const int FONT_SIZE_HI = FONT_SIZE * SDF_SCALE;     // 192 px glyph height
    const int FONT_CELL_W_HI = FONT_CELL_W * SDF_SCALE; // 240 px cell width
    const int FONT_CELL_H_HI = FONT_CELL_H * SDF_SCALE; // 288 px cell height
    const int ATLAS_W_HI = FONT_ATLAS_W * SDF_SCALE;    // 3840 px total width
    const int ATLAS_H_HI = FONT_ATLAS_H * SDF_SCALE;    // 1728 px total height
    const float INF = 1e20f;

    // =========================================================================
    // Phase 1: render glyphs into a hi-res GDI memory bitmap
    // =========================================================================
    //
    // CreateCompatibleDC(NULL) creates a DC compatible with the display without
    // requiring an existing window, so this runs before renderer startup.
    // Ref: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatibledc
    HDC memDC = CreateCompatibleDC( NULL );
    if ( !memDC )
    {
        return false;
    }

    // Top-down 32bpp DIB: ~25 MB, allocated once for the whole atlas.
    // Negative biHeight means scan-line 0 is the topmost row, matching the
    // texture upload orientation expected by the text renderer.
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
    bmi.bmiHeader.biWidth = ATLAS_W_HI;
    bmi.bmiHeader.biHeight = -ATLAS_H_HI;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection( memDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0 );
    if ( !hBitmap )
    {
        DeleteDC( memDC );
        return false;
    }
    HBITMAP hOldBitmap = reinterpret_cast<HBITMAP>( SelectObject( memDC, hBitmap ) );

    RECT fillRect = { 0, 0, ATLAS_W_HI, ATLAS_H_HI };
    HBRUSH hBlackBrush = CreateSolidBrush( RGB( 0, 0, 0 ) );
    FillRect( memDC, &fillRect, hBlackBrush );
    DeleteObject( hBlackBrush );

    // TrueType outline font at 6× size.
    // OUT_TT_PRECIS requests a vector outline for clean scaling.
    // ANTIALIASED_QUALITY gives GDI sub-pixel blending for a smooth binary mask.
    // Ref: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createfonta
    HFONT hFont = CreateFont( -FONT_SIZE_HI,
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
        return false;
    }
    HFONT hOldFont = reinterpret_cast<HFONT>( SelectObject( memDC, hFont ) );

    // Per-glyph advance widths measured at hi-res, then normalised to FONT_SIZE
    // units so the runtime advance table is resolution-independent.
    float charAdvBuf[96] = {};
    INT advWidths[96] = {};
    GetCharWidth32( memDC, 32, 127, advWidths );
    for ( int i = 0; i < 96; ++i )
    {
        charAdvBuf[i] = static_cast<float>( advWidths[i] ) / static_cast<float>( FONT_SIZE_HI );
    }

    // Render all 96 printable ASCII characters (0x20–0x7F).
    // Cell layout mirrors the final atlas (FONT_COLS × FONT_ROWS) but scaled up.
    SetBkMode( memDC, TRANSPARENT );
    SetTextColor( memDC, RGB( 255, 255, 255 ) );
    char ch[2] = { 0, 0 };
    for ( int i = 0; i < 96; ++i )
    {
        ch[0] = static_cast<char>( i + 32 );
        const int col = i % FONT_COLS;
        const int row = i / FONT_COLS;
        TextOutA( memDC, col * FONT_CELL_W_HI, row * FONT_CELL_H_HI, ch, 1 );
    }

    // Flush GDI drawing queue before reading pBits.
    // Ref: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush
    GdiFlush();

    // Extract the red channel into a packed byte array.
    // White-on-black rendering means R = G = B = luminance, so any channel works.
    const int hiTotalPx = ATLAS_W_HI * ATLAS_H_HI;
    std::unique_ptr<uint8_t[]> hiAtlas( new uint8_t[hiTotalPx] );
    const DWORD* pPixels = reinterpret_cast<const DWORD*>( pBits );
    for ( int i = 0; i < hiTotalPx; ++i )
    {
        hiAtlas[i] = static_cast<uint8_t>( pPixels[i] & 0xFF );
    }

    // Release all GDI resources — the rest is CPU-only.
    SelectObject( memDC, hOldFont );
    SelectObject( memDC, hOldBitmap );
    DeleteObject( hFont );
    DeleteObject( hBitmap );
    DeleteDC( memDC );

    // =========================================================================
    // Phase 2 — signed distance field via two 2D Euclidean Distance Transforms
    // =========================================================================
    //
    // For each hi-res glyph cell:
    //   edtOut[p] = squared dist from p to the nearest OUTSIDE pixel
    //   edtIn [p] = squared dist from p to the nearest INSIDE  pixel
    //
    // Signed distance: sdf = sqrt(edtOut) − sqrt(edtIn)
    //   > 0 inside the glyph  |  ≈ 0 on the edge  |  < 0 outside
    //
    // Byte encoding: byte = clamp(128 + sdf × 128 / SDF_SPREAD_HI, 0, 255)
    //   255 = deep inside  |  128 = edge  |  0 = deep outside
    //
    // Each SDF_SCALE×SDF_SCALE block is box-averaged to one final-atlas byte,
    // preserving sub-pixel gradients through the 6× downsample.
    // =========================================================================
    const int finalTotalPx = FONT_ATLAS_W * FONT_ATLAS_H;
    std::unique_ptr<uint8_t[]> finalAtlas( new uint8_t[finalTotalPx] );

    const int cellPx = FONT_CELL_W_HI * FONT_CELL_H_HI;
    std::vector<float> edtOut( cellPx );
    std::vector<float> edtIn( cellPx );

    for ( int glyph = 0; glyph < 96; ++glyph )
    {
        const int gcol = glyph % FONT_COLS;
        const int grow = glyph / FONT_COLS;

        // Top-left corner of this glyph's cell in the hi-res atlas
        const int cx = gcol * FONT_CELL_W_HI;
        const int cy = grow * FONT_CELL_H_HI;

        // Initialise EDT source grids from the binary glyph mask.
        // edtOut: source = OUTSIDE pixels  edtIn: source = INSIDE pixels
        for ( int py = 0; py < FONT_CELL_H_HI; ++py )
        {
            for ( int px = 0; px < FONT_CELL_W_HI; ++px )
            {
                const uint8_t val = hiAtlas[( cy + py ) * ATLAS_W_HI + ( cx + px )];
                const bool inside = ( val > 127u );
                const int idx = py * FONT_CELL_W_HI + px;
                edtOut[idx] = inside ? INF : 0.0f;
                edtIn[idx] = inside ? 0.0f : INF;
            }
        }

        ComputeEDT2D( edtOut.data(), FONT_CELL_W_HI, FONT_CELL_H_HI );
        ComputeEDT2D( edtIn.data(), FONT_CELL_W_HI, FONT_CELL_H_HI );

        // Box-filter SDF_SCALE×SDF_SCALE hi-res blocks → one final-atlas byte.
        const int fcy = grow * FONT_CELL_H;
        const int fcx = gcol * FONT_CELL_W;
        for ( int fy = 0; fy < FONT_CELL_H; ++fy )
        {
            for ( int fx = 0; fx < FONT_CELL_W; ++fx )
            {
                float sum = 0.0f;
                for ( int sy = 0; sy < SDF_SCALE; ++sy )
                {
                    for ( int sx = 0; sx < SDF_SCALE; ++sx )
                    {
                        const int hidx = ( fy * SDF_SCALE + sy ) * FONT_CELL_W_HI + ( fx * SDF_SCALE + sx );
                        const float distOut = sqrtf( edtOut[hidx] );
                        const float distIn = sqrtf( edtIn[hidx] );
                        sum += ( distOut - distIn ); // positive inside, negative outside
                    }
                }
                const float avgSdf = sum / static_cast<float>( SDF_SCALE * SDF_SCALE );
                const float encoded = 128.0f + avgSdf * 128.0f / static_cast<float>( SDF_SPREAD_HI );
                int byte = static_cast<int>( encoded + 0.5f );
                if ( byte < 0 )
                {
                    byte = 0;
                }
                if ( byte > 255 )
                {
                    byte = 255;
                }
                finalAtlas[( fcy + fy ) * FONT_ATLAS_W + ( fcx + fx )] = static_cast<uint8_t>( byte );
            }
        }
    }

    // =========================================================================
    // Phase 3 — write binary atlas file
    // =========================================================================
    FILE* rawFile = nullptr;
    if ( fopen_s( &rawFile, cOutPath, "wb" ) != 0 || !rawFile )
    {
        return false;
    }
    FileHandle file( rawFile );

    SdfFileHeader hdr = {};
    memcpy( hdr.magic, "SBSDF001", 8 );
    hdr.version = 1u;
    hdr.atlasW = static_cast<uint32_t>( FONT_ATLAS_W );
    hdr.atlasH = static_cast<uint32_t>( FONT_ATLAS_H );
    hdr.fontSize = static_cast<uint32_t>( FONT_SIZE );
    hdr.cellW = static_cast<uint32_t>( FONT_CELL_W );
    hdr.cellH = static_cast<uint32_t>( FONT_CELL_H );
    memcpy( hdr.charAdvance, charAdvBuf, 96 * sizeof( float ) );

    fwrite( &hdr, sizeof( hdr ), 1, file.get() );
    fwrite( finalAtlas.get(), 1, static_cast<size_t>( finalTotalPx ), file.get() );
    return true;
}


SkullbonezCore::Basics::SbResult Text2d::BuildFont( IRenderResourceFactory& renderResources,
                                                    const SkullbonezCore::Assets::AssetSystem& assets,
                                                    int screenW,
                                                    int screenH,
                                                    const char* cFontName )
{
    // Load the pre-generated SDF atlas if available.  To regenerate, run:
    //   SKULLBONEZ_CORE.exe --gen-atlas
    // If the file is absent or stale the engine generates it on first run so
    // it always works without a manual pre-step.
    const std::string atlasPath = std::string( DATA_ROOT ) + "font_atlas.sdf";
    if ( !LoadSdfAtlasFromFile( renderResources, atlasPath.c_str() ) )
    {
        fprintf( stderr, "[Text2d] SDF atlas missing or stale — generating (one time)...\n" );
        if ( !Text2d::GenerateSdfAtlasToFile( cFontName, atlasPath.c_str() ) )
        {
            return SkullbonezCore::Basics::SbResult::Failure( "Rendering/Text",
                                                              "SDF atlas generation failed: %s",
                                                              atlasPath.c_str() );
        }
        if ( !LoadSdfAtlasFromFile( renderResources, atlasPath.c_str() ) )
        {
            return SkullbonezCore::Basics::SbResult::Failure( "Rendering/Text",
                                                              "SDF atlas load-after-generate failed: %s",
                                                              atlasPath.c_str() );
        }
        fprintf( stderr, "[Text2d] SDF atlas saved to %s\n", atlasPath.c_str() );
    }

    // Create the text batch VB: [x, y, u, v, r, g, b] per vertex, large enough for a full HUD frame.
    // All Render2dText* calls accumulate into this; FlushText() does one upload+draw per frame.
    int batchAttribs[] = { 2, 2, 3 };
    Text2d::textBatchVB =
        renderResources.CreateDynamicVB( batchAttribs, 3, TEXT_BATCH_MAX_CHARS * TEXT_BATCH_VERTS_PER_CHAR );

    // Create the solid-quad VB: [x, y, u, v] per vertex (Render2dQuad only; 6 verts max).
    int quadAttribs[] = { 2, 2 };
    Text2d::dynamicVB = renderResources.CreateDynamicVB( quadAttribs, 2, 6 );

    // Create the quad batch VB: [x, y, r, g, b, a] per vertex, sized for QUAD_BATCH_MAX_QUADS.
    // All BatchQuad() calls accumulate here; FlushQuads() does one upload+draw per flush.
    int quadBatchAttribs[] = { 2, 4 };
    Text2d::quadBatchVB =
        renderResources.CreateDynamicVB( quadBatchAttribs, 2, QUAD_BATCH_MAX_QUADS * QUAD_BATCH_VERTS_PER_QUAD );

    // Compile the text shader and bind the atlas sampler slot once.
    Text2d::pTextShader = assets.CreateShader( renderResources, "shader.text" );
    if ( Text2d::pTextShader )
    {
        Text2d::pTextShader->Use();
        Text2d::pTextShader->SetInt( "uFontTexture", 0 );
    }

    // Compile the solid-colour HUD quad shader (used by Render2dQuad — immediate, one draw per call)
    Text2d::pSolidShader = assets.CreateShader( renderResources, "shader.solid_color" );

    // Compile the batched per-vertex-RGBA quad shader (used by FlushQuads — one draw for all quads)
    Text2d::pSolidBatchShader = assets.CreateShader( renderResources, "shader.solid_color_batch" );

    // RebuildProjection() must be called whenever the window is resized so the
    // ortho extents stay matched to the actual viewport aspect ratio.
    Text2d::RebuildProjection( screenW, screenH );
    return SkullbonezCore::Basics::SbResult::Success();
}


void Text2d::RebuildProjection( int w, int h )
{
    if ( w <= 0 || h <= 0 )
    {
        return;
    }
    // Matches the legacy FFP coordinate space: FOV=45°, aspect=w/h.
    // halfH is derived from the half-FOV angle and is invariant to resolution;
    // halfW scales with the aspect ratio so text is never distorted on resize.
    const float halfH = tanf( 22.5f * _PI / 180.0f );
    const float halfW = halfH * static_cast<float>( w ) / static_cast<float>( h );
    s_orthoProj = Matrix4::Ortho( -halfW, halfW, -halfH, halfH, -1.0f, 1.0f );
    s_halfW = halfW;
    s_halfH = halfH;
}


float Text2d::MeasureText( float fSize, const char* text )
{
    if ( !text )
    {
        return 0.0f;
    }
    float width = 0.0f;
    for ( const char* p = text; *p; ++p )
    {
        unsigned char c = static_cast<unsigned char>( *p );
        if ( c >= 32 && c <= 127 )
        {
            width += charAdvance[c - 32] * fSize;
        }
        else
        {
            width += fSize * 0.5f; // fallback advance for non-printable
        }
    }
    return width;
}


void Text2d::DeleteFont( IRenderResourceFactory* renderResources )
{
    if ( Text2d::fontTexture )
    {
        if ( renderResources )
        {
            renderResources->DeleteTexture( Text2d::fontTexture );
        }
        Text2d::fontTexture = 0;
    }
    if ( Text2d::textBatchVB )
    {
        if ( renderResources )
        {
            renderResources->DestroyDynamicVB( Text2d::textBatchVB );
        }
        Text2d::textBatchVB = 0;
    }
    if ( Text2d::dynamicVB )
    {
        if ( renderResources )
        {
            renderResources->DestroyDynamicVB( Text2d::dynamicVB );
        }
        Text2d::dynamicVB = 0;
    }
    if ( Text2d::quadBatchVB )
    {
        if ( renderResources )
        {
            renderResources->DestroyDynamicVB( Text2d::quadBatchVB );
        }
        Text2d::quadBatchVB = 0;
    }
    Text2d::pTextShader.reset();
    Text2d::pSolidShader.reset();
    Text2d::pSolidBatchShader.reset();
    s_batchVerts = 0;
    s_quadBatchVerts = 0;
}


static void RenderTextInternal( float xPosition,
                                float yPosition,
                                float fSize,
                                float colR,
                                float colG,
                                float colB,
                                const char* formatted )
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
        float u1 = u0 +
                   ( Text2d::charAdvance[idx] * static_cast<float>( FONT_SIZE ) ) / static_cast<float>( FONT_ATLAS_W ) -
                   halfU;
        // Sample the full cell height so descenders (g, j, p, q, y) are not clipped.
        // Previously this was clamped to FONT_SIZE pixels, cutting off the bottom
        // (FONT_CELL_H - FONT_SIZE) rows that hold descender strokes.
        float v1 = static_cast<float>( row * FONT_CELL_H + FONT_CELL_H ) / static_cast<float>( FONT_ATLAS_H ) - halfV;

        float charW = Text2d::charAdvance[idx] * fSize;

        // Extend the quad downward by the descender fraction so the extra atlas rows
        // map onto screen space without distorting the cap-height region.
        const float descH = fSize * static_cast<float>( FONT_CELL_H - FONT_SIZE ) / static_cast<float>( FONT_SIZE );

        float x0 = penX;
        float x1 = penX + charW;
        float y0 = penY - descH; // below yPosition — descender region
        float y1 = penY + fSize; // above yPosition — cap-height region

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


void Text2d::FlushText( IRenderCommandContext& renderCommands )
{
    if ( s_batchVerts == 0 || !Text2d::pTextShader || !Text2d::textBatchVB )
    {
        return;
    }

    bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    bool blendWasEnabled = renderCommands.IsBlendEnabled();

    renderCommands.SetDepthTest( false );
    renderCommands.SetBlend( true );
    renderCommands.SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );

    Text2d::pTextShader->Use();
    Text2d::pTextShader->SetMat4( "uProjection", s_orthoProj );
    renderCommands.BindTexture( Text2d::fontTexture, 0 );

    // One GPU upload + one draw call covers the entire frame's text at all colors.
    renderCommands.UploadAndDrawDynamicVB( Text2d::textBatchVB, s_batchBuf, s_batchVerts );

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );

    s_batchVerts = 0;
}


void Text2d::Render2dText( float xPosition, float yPosition, float fSize, const char* cRawText, ... )
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


void Text2d::Render2dQuad( IRenderCommandContext& renderCommands,
                           float x0,
                           float y0,
                           float x1,
                           float y1,
                           float r,
                           float g,
                           float b,
                           float a )
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

    const Matrix4& proj = s_orthoProj; // Kept current by RebuildProjection() on every resize

    bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    bool blendWasEnabled = renderCommands.IsBlendEnabled();

    renderCommands.SetDepthTest( false );
    renderCommands.SetBlend( true );
    renderCommands.SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );

    Text2d::pSolidShader->Use();
    Text2d::pSolidShader->SetMat4( "uProjection", proj );
    Text2d::pSolidShader->SetVec4( "uColor", r, g, b, a );

    renderCommands.UploadAndDrawDynamicVB( Text2d::dynamicVB, s_quadBuf, 6 );

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
}


void Text2d::BatchQuad( IRenderCommandContext& renderCommands,
                        float x0,
                        float y0,
                        float x1,
                        float y1,
                        float r,
                        float g,
                        float b,
                        float a )
{
    // Accumulate one quad (two triangles, 6 vertices) into s_quadBatchBuf.
    // Vertex layout: [x, y, r, g, b, a] — 6 floats per vertex.
    // FlushQuads() uploads and draws all accumulated quads in one call.

    if ( s_quadBatchVerts + QUAD_BATCH_VERTS_PER_QUAD > QUAD_BATCH_MAX_QUADS * QUAD_BATCH_VERTS_PER_QUAD )
    {
        // Buffer full — flush now and continue accumulating.
        FlushQuads( renderCommands );
    }

    float* v = s_quadBatchBuf + s_quadBatchVerts * QUAD_BATCH_FLOATS_PER_VERT;

    // Triangle 1: bottom-left, bottom-right, top-right
    v[0] = x0;
    v[1] = y0;
    v[2] = r;
    v[3] = g;
    v[4] = b;
    v[5] = a;
    v[6] = x1;
    v[7] = y0;
    v[8] = r;
    v[9] = g;
    v[10] = b;
    v[11] = a;
    v[12] = x1;
    v[13] = y1;
    v[14] = r;
    v[15] = g;
    v[16] = b;
    v[17] = a;
    // Triangle 2: bottom-left, top-right, top-left
    v[18] = x0;
    v[19] = y0;
    v[20] = r;
    v[21] = g;
    v[22] = b;
    v[23] = a;
    v[24] = x1;
    v[25] = y1;
    v[26] = r;
    v[27] = g;
    v[28] = b;
    v[29] = a;
    v[30] = x0;
    v[31] = y1;
    v[32] = r;
    v[33] = g;
    v[34] = b;
    v[35] = a;

    s_quadBatchVerts += QUAD_BATCH_VERTS_PER_QUAD;
}


void Text2d::BatchTriangle( IRenderCommandContext& renderCommands,
                            float x0,
                            float y0,
                            float x1,
                            float y1,
                            float x2,
                            float y2,
                            float r,
                            float g,
                            float b,
                            float a )
{
    if ( s_quadBatchVerts + QUAD_BATCH_VERTS_PER_TRIANGLE > QUAD_BATCH_MAX_QUADS * QUAD_BATCH_VERTS_PER_QUAD )
    {
        FlushQuads( renderCommands );
    }

    float* v = s_quadBatchBuf + s_quadBatchVerts * QUAD_BATCH_FLOATS_PER_VERT;
    v[0] = x0;
    v[1] = y0;
    v[2] = r;
    v[3] = g;
    v[4] = b;
    v[5] = a;
    v[6] = x1;
    v[7] = y1;
    v[8] = r;
    v[9] = g;
    v[10] = b;
    v[11] = a;
    v[12] = x2;
    v[13] = y2;
    v[14] = r;
    v[15] = g;
    v[16] = b;
    v[17] = a;

    s_quadBatchVerts += QUAD_BATCH_VERTS_PER_TRIANGLE;
}


void Text2d::FlushQuads( IRenderCommandContext& renderCommands )
{
    // This is the counterpart to FlushText(); together they give exactly two
    // draw calls for an entire overlay frame (quads first, then text on top).

    if ( s_quadBatchVerts == 0 || !Text2d::pSolidBatchShader || !Text2d::quadBatchVB )
    {
        return;
    }

    bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    bool blendWasEnabled = renderCommands.IsBlendEnabled();

    renderCommands.SetDepthTest( false );
    renderCommands.SetBlend( true );
    renderCommands.SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );

    Text2d::pSolidBatchShader->Use();
    Text2d::pSolidBatchShader->SetMat4( "uProjection", s_orthoProj );

    // One GPU upload + one draw call covers every quad batched this frame.
    renderCommands.UploadAndDrawDynamicVB( Text2d::quadBatchVB, s_quadBatchBuf, s_quadBatchVerts );

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );

    s_quadBatchVerts = 0;
}
