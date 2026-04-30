// --- Includes ---
#include "SkullbonezTextureCollectionGL.h"
#include "stb_image.h"


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

    // Load image via stb_image (supports JPEG, PNG, BMP, etc.)
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load( cFileName, &width, &height, &channels, 3 );

    if ( !data )
    {
        throw std::runtime_error( "Image load failed!  (TextureCollection::CreateJpegTexture)" );
    }

    // Generate and bind GL texture
    glGenTextures( 1, &m_textureArray[m_nextAvailableTextureIndex] );
    glBindTexture( GL_TEXTURE_2D, m_textureArray[m_nextAvailableTextureIndex] );

    // Upload texture and generate mipmaps
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data );
    glGenerateMipmap( GL_TEXTURE_2D );

    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR );

    // stb_image handles cleanup
    stbi_image_free( data );

    // Update capacity and progress counters
    UpdateCounters();
}
