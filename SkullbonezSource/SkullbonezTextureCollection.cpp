// --- Includes ---
#include "SkullbonezTextureCollection.h"


// --- Usings ---
using namespace SkullbonezCore::Textures;


TextureCollection::TextureCollection()
{
    m_nextAvailableTextureIndex = 0;
    m_textureCounter = 0;

    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        m_textureArray[count] = 0;
        m_textureHashes[count] = 0;
    }
}


TextureCollection* TextureCollection::Instance()
{
    if ( !TextureCollection::pInstance )
    {
        static TextureCollection instance;
        TextureCollection::pInstance = &instance;
    }
    return TextureCollection::pInstance;
}


void TextureCollection::Destroy()
{
    if ( TextureCollection::pInstance )
    {
        TextureCollection::pInstance->DeleteAllTextures();
        TextureCollection::pInstance = nullptr;
    }
}


void TextureCollection::DecodeJPEG( jpeg_decompress_struct* info,
                                    tImageJPG* pImageData )
{
    // Read in the header of the jpeg file
    jpeg_read_header( info, TRUE );

    // Start to decompress the jpeg file with our compression info
    jpeg_start_decompress( info );

    // Get the image dimensions and row span to read in the pixel data
    pImageData->rowSpan = info->image_width * info->num_components;
    pImageData->sizeX = info->image_width;
    pImageData->sizeY = info->image_height;

    // Allocate memory for the pixel buffer
    pImageData->data = new unsigned char[pImageData->rowSpan * pImageData->sizeY];

    // Here we use the library's state variable info.output_scanline as the
    // loop counter, so that we don't have to keep track ourselves.

    // Create an array of row pointers
    std::vector<unsigned char*> rowPtr( pImageData->sizeY );
    for ( int i = 0; i < pImageData->sizeY; i++ )
    {
        rowPtr[i] = &( pImageData->data[i * pImageData->rowSpan] );
    }

    // Now comes the juice of our work, here we extract all the pixel data
    int rowsRead = 0;
    while ( info->output_scanline < info->output_height )
    {
        rowsRead += jpeg_read_scanlines( info,
                                         &rowPtr[rowsRead],
                                         info->output_height - rowsRead );
    }

    // Finish decompressing the data
    jpeg_finish_decompress( info );
}


tImageJPG* TextureCollection::LoadJPEG( const char* cFileName )
{
    struct jpeg_decompress_struct cinfo;
    tImageJPG* pImageData = 0;

    FILE* pFile = nullptr;
    fopen_s( &pFile, cFileName, "rb" );
    if ( !pFile )
    {
        throw std::runtime_error( "JPEG file not found (TextureCollection::LoadJPEG)" );
    }

    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error( &jerr );
    jpeg_create_decompress( &cinfo );
    jpeg_stdio_src( &cinfo, pFile );

    pImageData = new tImageJPG();

    try
    {
        DecodeJPEG( &cinfo, pImageData );
    }
    catch ( ... )
    {
        delete pImageData;
        jpeg_destroy_decompress( &cinfo );
        fclose( pFile );
        throw;
    }

    jpeg_destroy_decompress( &cinfo );
    fclose( pFile );
    return pImageData;
}


void TextureCollection::UpdateCounters()
{
    // reset the counter
    m_textureCounter = 0;
    bool isNextAvailIndexSet = false;

    // iterate through all m_textures
    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        // find the first empty spot
        if ( !m_textureArray[count] )
        {
            if ( !isNextAvailIndexSet )
            {
                // set the next available index counter
                m_nextAvailableTextureIndex = count;

                // do not set this again
                isNextAvailIndexSet = true;
            }
        }
        else
        {
            // for every texture, increment
            ++m_textureCounter;
        }
    }
}


int TextureCollection::FindIndex( uint32_t hash )
{
    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        if ( m_textureHashes[count] == hash )
        {
            return count;
        }
    }

    throw std::runtime_error( "Texture does not exist.  (TextureCollection::FindIndex)" );
}


void TextureCollection::DeleteAllTextures()
{
    // delete all OpenGL m_textures
    glDeleteTextures( m_nextAvailableTextureIndex, m_textureArray );

    // iterate through texture array
    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        if ( m_textureArray[count] )
        {
            m_textureHashes[count] = 0;
            m_textureArray[count] = 0;
        }
    }

    // Update capacity and progress counters
    UpdateCounters();
}


void TextureCollection::DeleteTexture( uint32_t hash )
{
    int index = FindIndex( hash );

    m_textureHashes[index] = 0;

    glDeleteTextures( 1, &m_textureArray[index] );
    m_textureArray[index] = 0;

    UpdateCounters();
}


int TextureCollection::NumFreeTextureSpaces()
{
    return TOTAL_TEXTURE_COUNT - m_textureCounter;
}


void TextureCollection::SelectTexture( uint32_t hash )
{
    glBindTexture( GL_TEXTURE_2D, m_textureArray[FindIndex( hash )] );
}


void TextureCollection::CreateJpegTexture( const char* cFileName,
                                           uint32_t hash )
{
    if ( m_textureCounter == TOTAL_TEXTURE_COUNT )
    {
        throw std::runtime_error( "Texture array full!  (TextureCollection::CreateJpegTexture)" );
    }

    m_textureHashes[m_nextAvailableTextureIndex] = hash;

    // load the image and store the data
    tImageJPG* pImage = LoadJPEG( cFileName );

    // check for data
    if ( !pImage )
    {
        throw std::runtime_error( "Jpeg load failed!  (TextureCollection::CreateJpegTexture)" );
    }

    // specify m_textureArray @ index to Text2dureId @ index
    glGenTextures( 1,                                              // num texture objects to create
                   &m_textureArray[m_nextAvailableTextureIndex] ); // point @ index

    // bind and init, 1st param: 2d m_textures (not 1d), 2nd param: point to location
    glBindTexture( GL_TEXTURE_2D,
                   m_textureArray[m_nextAvailableTextureIndex] );

    // upload texture and generate mipmaps
    glTexImage2D( GL_TEXTURE_2D,    // 2d m_textures (not 1d)
                  0,                // base mipmap level
                  GL_RGB,           // internal format
                  pImage->sizeX,    // x m_width
                  pImage->sizeY,    // y m_width
                  0,                // border (must be 0)
                  GL_RGB,           // pixel format
                  GL_UNSIGNED_BYTE, // index type
                  pImage->data );   // image data

    glGenerateMipmap( GL_TEXTURE_2D );

    // mipmap quality small range
    glTexParameteri( GL_TEXTURE_2D,             // 2d m_textures (not 1d)
                     GL_TEXTURE_MIN_FILTER,     // small range filer
                     GL_LINEAR_MIPMAP_LINEAR ); // linear linear (best quality)

    // mipmap quality large range
    glTexParameteri( GL_TEXTURE_2D,             // 2d m_textures (not 1d)
                     GL_TEXTURE_MAG_FILTER,     // large range filer
                     GL_LINEAR_MIPMAP_LINEAR ); // linear linear (best quality)

    // Cleanup image data (opengl has made a copy)
    if ( pImage )
    {
        // hmmm
        if ( pImage->data )
        {
            delete[] pImage->data;
        }
        delete pImage;
    }

    // Update capacity and progress counters
    UpdateCounters();
}
