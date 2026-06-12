// --- Includes ---
#include "SkullbonezTextureCollection.h"
#include "SkullbonezIRenderBackend.h"
#include "stb_image.h"

#include <string>


// --- Usings ---
using namespace SkullbonezCore::Textures;
using namespace SkullbonezCore::Rendering;


TextureCollection::TextureCollection()
{
    m_nextAvailableTextureIndex = 0;
    m_textureCounter = 0;

    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        m_textureArray[count] = 0;
        m_textureHashes[count] = 0;
        m_textureSourceIds[count] = 0;
        m_textureWidths[count] = 0;
        m_textureHeights[count] = 0;
        m_textureChannels[count] = 0;
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
        TextureCollection::pInstance->m_assets = nullptr;
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


int TextureCollection::FindIndex( uint32_t hash ) const
{
    const int index = FindIndexNoThrow( hash );
    if ( index >= 0 )
    {
        return index;
    }

    throw std::runtime_error( "Texture does not exist.  (TextureCollection::FindIndex)" );
}


int TextureCollection::FindIndexNoThrow( uint32_t hash ) const
{
    if ( hash == 0 )
    {
        return -1;
    }

    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        if ( m_textureHashes[count] == hash )
        {
            return count;
        }
    }

    return -1;
}


void TextureCollection::DeleteAllTextures()
{
    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        if ( m_textureArray[count] )
        {
            Gfx().DeleteTexture( m_textureArray[count] );
        }
        m_textureHashes[count] = 0;
        m_textureArray[count] = 0;
        m_textureSourceIds[count] = 0;
        m_textureWidths[count] = 0;
        m_textureHeights[count] = 0;
        m_textureChannels[count] = 0;
    }

    UpdateCounters();
}


void TextureCollection::DeleteTexture( uint32_t hash )
{
    int index = FindIndex( hash );

    m_textureHashes[index] = 0;

    if ( m_textureArray[index] )
    {
        Gfx().DeleteTexture( m_textureArray[index] );
    }
    m_textureArray[index] = 0;
    m_textureSourceIds[index] = 0;
    m_textureWidths[index] = 0;
    m_textureHeights[index] = 0;
    m_textureChannels[index] = 0;

    UpdateCounters();
}


int TextureCollection::NumFreeTextureSpaces()
{
    return TOTAL_TEXTURE_COUNT - m_textureCounter;
}


void TextureCollection::SelectTexture( uint32_t hash )
{
    EnsureTexture( hash );
    Gfx().BindTexture( m_textureArray[FindIndex( hash )], 0 );
}


uint32_t TextureCollection::GetTextureHandle( uint32_t hash )
{
    EnsureTexture( hash );
    return m_textureArray[FindIndex( hash )];
}


void TextureCollection::BindAssetSystem( Assets::AssetSystem* assets )
{
    m_assets = assets;
}


bool TextureCollection::HasTexture( uint32_t hash ) const
{
    return FindIndexNoThrow( hash ) >= 0;
}


void TextureCollection::EnsureTexture( uint32_t hash )
{
    if ( HasTexture( hash ) )
    {
        return;
    }

    if ( m_assets )
    {
        const Assets::TextureSourceAsset* source = m_assets->FindTextureSourceAssetByLegacyHash( hash );
        if ( source )
        {
            CreateTextureFromSourceAsset( *source );
            return;
        }
    }

    (void)FindIndex( hash );
}


void TextureCollection::LoadJpegTextureIntoSlot( int slot,
                                                 const char* fileName,
                                                 uint32_t hash,
                                                 Assets::AssetId sourceId,
                                                 bool generateMips,
                                                 bool linearFilter,
                                                 int channelsHint )
{
    const int requestedChannels = channelsHint > 0 ? channelsHint : 3;

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* data = stbi_load( fileName, &width, &height, &sourceChannels, requestedChannels );

    if ( !data )
    {
        std::string message = "Image load failed: ";
        message += fileName ? fileName : "";
        message += "  (TextureCollection::CreateJpegTexture)";
        throw std::runtime_error( message.c_str() );
    }

    m_textureArray[slot] = Gfx().CreateTexture2D( data, width, height, requestedChannels, generateMips, linearFilter );
    m_textureHashes[slot] = hash;
    m_textureSourceIds[slot] = sourceId;
    m_textureWidths[slot] = width;
    m_textureHeights[slot] = height;
    m_textureChannels[slot] = requestedChannels;

    stbi_image_free( data );

    UpdateCounters();
}


void TextureCollection::CreateTextureFromSourceAsset( const Assets::TextureSourceAsset& source )
{
    if ( source.legacyHash == 0 )
    {
        throw std::runtime_error( "Texture source asset is missing a legacy hash.  (TextureCollection::CreateTextureFromSourceAsset)" );
    }
    CreateJpegTexture( source.resolvedPath.c_str(), source.legacyHash );
}


void TextureCollection::CreateJpegTexture( const char* cFileName,
                                           uint32_t hash )
{
    const int existingIndex = FindIndexNoThrow( hash );
    if ( existingIndex >= 0 )
    {
        DeleteTexture( hash );
    }

    if ( m_textureCounter == TOTAL_TEXTURE_COUNT )
    {
        throw std::runtime_error( "Texture array full!  (TextureCollection::CreateJpegTexture)" );
    }

    const Assets::TextureSourceAsset* source = m_assets ? m_assets->FindTextureSourceAssetByLegacyHash( hash ) : nullptr;
    if ( source )
    {
        LoadJpegTextureIntoSlot( m_nextAvailableTextureIndex,
                                 source->resolvedPath.c_str(),
                                 hash,
                                 source->id,
                                 source->generateMips,
                                 source->linearFilter,
                                 source->channelsHint );
        return;
    }

    LoadJpegTextureIntoSlot( m_nextAvailableTextureIndex, cFileName, hash, 0, true, true, 3 );
}


void TextureCollection::EnsureJpegTexture( const char* cFileName, uint32_t hash )
{
    if ( HasTexture( hash ) )
    {
        return;
    }
    CreateJpegTexture( cFileName, hash );
}


void TextureCollection::RebuildTexturesFromSourceAssets()
{
    DeleteAllTextures();
    if ( !m_assets )
    {
        return;
    }

    for ( const Assets::TextureSourceAsset& source : m_assets->GetTextureSourceAssets() )
    {
        if ( source.legacyHash != 0 )
        {
            CreateTextureFromSourceAsset( source );
        }
    }
}


void TextureCollection::DumpTextureAssets( FILE* out ) const
{
    if ( !out )
    {
        return;
    }

    fprintf( out, "[texture-assets]\n" );
    if ( m_assets )
    {
        for ( const Assets::TextureSourceAsset& source : m_assets->GetTextureSourceAssets() )
        {
            const int index = FindIndexNoThrow( source.legacyHash );
            const uint32_t backendHandle = index >= 0 ? static_cast<uint32_t>( m_textureArray[index] ) : 0;
            const int width = index >= 0 ? m_textureWidths[index] : 0;
            const int height = index >= 0 ? m_textureHeights[index] : 0;
            const int channels = index >= 0 ? m_textureChannels[index] : source.channelsHint;
            fprintf( out,
                     "texture logical=\"%s\" path=\"%s\" hash=0x%08X backend_handle=%u width=%d height=%d channels=%d\n",
                     source.logicalName.c_str(),
                     source.resolvedPath.c_str(),
                     source.legacyHash,
                     backendHandle,
                     width,
                     height,
                     channels );
        }
        return;
    }

    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        if ( m_textureHashes[count] != 0 )
        {
            fprintf( out,
                     "texture logical=\"legacy:0x%08X\" path=\"\" hash=0x%08X backend_handle=%u width=%d height=%d channels=%d\n",
                     m_textureHashes[count],
                     m_textureHashes[count],
                     static_cast<uint32_t>( m_textureArray[count] ),
                     m_textureWidths[count],
                     m_textureHeights[count],
                     m_textureChannels[count] );
        }
    }
}
