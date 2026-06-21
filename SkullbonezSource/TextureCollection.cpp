/*
File: SkullbonezSource/TextureCollection.cpp
Purpose:
  Loads texture files and hands renderer-neutral texture ids to draw code.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/TextureCollection.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "TextureCollection.h"
#include "IRenderBackend.h"
#include "stb_image.h"

#include <memory>
#include <stdexcept>
#include <string>


using namespace SkullbonezCore::Textures;
using namespace SkullbonezCore::Rendering;


TextureCollection* TextureCollection::Instance()
{
    static TextureCollection instance;
    return &instance;
}


void TextureCollection::Destroy()
{
    TextureCollection* textures = Instance();
    textures->DeleteAllTextures();
    textures->m_assets = nullptr;
}


int TextureCollection::FindIndex( uint32_t hash ) const
{
    const int index = FindIndexNoThrow( hash );
    if ( index >= 0 )
    {
        return index;
    }

    char message[128];
    sprintf_s( message, "Texture 0x%08X does not exist.  (TextureCollection::FindIndex)", hash );
    throw std::runtime_error( message );
}


int TextureCollection::FindIndexNoThrow( uint32_t hash ) const
{
    if ( hash == 0 )
    {
        return -1;
    }

    for ( size_t index = 0; index < m_textures.size(); ++index )
    {
        if ( m_textures[index].legacyHash == hash && m_textures[index].IsResident() )
        {
            return static_cast<int>( index );
        }
    }

    return -1;
}


int TextureCollection::FindFreeSlot() const
{
    for ( size_t index = 0; index < m_textures.size(); ++index )
    {
        if ( !m_textures[index].IsResident() )
        {
            return static_cast<int>( index );
        }
    }

    throw std::runtime_error( "Texture array full!  (TextureCollection::FindFreeSlot)" );
}


void TextureCollection::ReleaseTexture( GpuTextureRecord& texture )
{
    if ( texture.backendHandle )
    {
        Gfx().DeleteTexture( texture.backendHandle );
    }
    texture = {};
}


void TextureCollection::DeleteAllTextures()
{
    for ( GpuTextureRecord& texture : m_textures )
    {
        ReleaseTexture( texture );
    }
}


void TextureCollection::DeleteTexture( uint32_t hash )
{
    ReleaseTexture( m_textures[FindIndex( hash )] );
}


int TextureCollection::NumFreeTextureSpaces() const
{
    int freeCount = 0;
    for ( const GpuTextureRecord& texture : m_textures )
    {
        if ( !texture.IsResident() )
        {
            ++freeCount;
        }
    }
    return freeCount;
}


void TextureCollection::SelectTexture( uint32_t hash )
{
    EnsureTexture( hash );
    Gfx().BindTexture( m_textures[FindIndex( hash )].backendHandle, 0 );
}


uint32_t TextureCollection::GetTextureHandle( uint32_t hash )
{
    EnsureTexture( hash );
    return m_textures[FindIndex( hash )].backendHandle;
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

    char message[160];
    sprintf_s( message,
               "Texture 0x%08X is not resident and has no registered source asset.  (TextureCollection::EnsureTexture)",
               hash );
    throw std::runtime_error( message );
}


void TextureCollection::LoadJpegTextureIntoSlot( int slot,
                                                 const char* fileName,
                                                 uint32_t hash,
                                                 Assets::AssetId sourceId,
                                                 bool generateMips,
                                                 bool linearFilter,
                                                 int channelsHint )
{
    if ( slot < 0 || slot >= static_cast<int>( m_textures.size() ) )
    {
        throw std::runtime_error( "Invalid texture slot.  (TextureCollection::LoadJpegTextureIntoSlot)" );
    }
    if ( !fileName || fileName[0] == '\0' )
    {
        throw std::invalid_argument( "TextureCollection::LoadJpegTextureIntoSlot requires a file path." );
    }

    const int requestedChannels = channelsHint > 0 ? channelsHint : 3;

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    std::unique_ptr<unsigned char, decltype( &stbi_image_free )> data(
        stbi_load( fileName, &width, &height, &sourceChannels, requestedChannels ),
        stbi_image_free );

    if ( !data )
    {
        std::string message = "Image load failed: ";
        message += fileName;
        message += "  (TextureCollection::CreateJpegTexture)";
        throw std::runtime_error( message );
    }

    const uint32_t backendHandle =
        Gfx().CreateTexture2D( data.get(), width, height, requestedChannels, generateMips, linearFilter );
    if ( backendHandle == 0 )
    {
        throw std::runtime_error(
            "Backend returned an invalid texture handle.  (TextureCollection::LoadJpegTextureIntoSlot)" );
    }

    GpuTextureRecord& texture = m_textures[slot];
    texture.legacyHash = hash;
    texture.backendHandle = backendHandle;
    texture.sourceId = sourceId;
    texture.width = width;
    texture.height = height;
    texture.channels = requestedChannels;
}


void TextureCollection::CreateTextureFromSourceAsset( const Assets::TextureSourceAsset& source )
{
    if ( source.legacyHash == 0 )
    {
        throw std::runtime_error(
            "Texture source asset is missing a legacy hash.  (TextureCollection::CreateTextureFromSourceAsset)" );
    }
    const int existingIndex = FindIndexNoThrow( source.legacyHash );
    if ( existingIndex >= 0 )
    {
        ReleaseTexture( m_textures[existingIndex] );
    }

    LoadJpegTextureIntoSlot( FindFreeSlot(),
                             source.resolvedPath.c_str(),
                             source.legacyHash,
                             source.id,
                             source.generateMips,
                             source.linearFilter,
                             source.channelsHint );
}


void TextureCollection::CreateJpegTexture( const char* cFileName, uint32_t hash )
{
    if ( hash == 0 )
    {
        throw std::invalid_argument( "TextureCollection::CreateJpegTexture requires a non-zero legacy hash." );
    }

    const Assets::TextureSourceAsset* source =
        m_assets ? m_assets->FindTextureSourceAssetByLegacyHash( hash ) : nullptr;
    if ( source )
    {
        CreateTextureFromSourceAsset( *source );
        return;
    }

    const int existingIndex = FindIndexNoThrow( hash );
    if ( existingIndex >= 0 )
    {
        ReleaseTexture( m_textures[existingIndex] );
    }

    LoadJpegTextureIntoSlot( FindFreeSlot(), cFileName, hash, 0, true, true, 3 );
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
            const GpuTextureRecord* texture = index >= 0 ? &m_textures[index] : nullptr;
            const uint32_t backendHandle = texture ? texture->backendHandle : 0;
            const int width = texture ? texture->width : 0;
            const int height = texture ? texture->height : 0;
            const int channels = texture ? texture->channels : source.channelsHint;
            fprintf( out,
                     "texture source_id=%u logical=\"%s\" path=\"%s\" hash=0x%08X backend_handle=%u width=%d height=%d "
                     "channels=%d\n",
                     source.id,
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

    for ( const GpuTextureRecord& texture : m_textures )
    {
        if ( texture.IsResident() )
        {
            fprintf( out,
                     "texture source_id=%u logical=\"legacy:0x%08X\" path=\"\" hash=0x%08X backend_handle=%u width=%d "
                     "height=%d channels=%d\n",
                     texture.sourceId,
                     texture.legacyHash,
                     texture.legacyHash,
                     texture.backendHandle,
                     texture.width,
                     texture.height,
                     texture.channels );
        }
    }
}
