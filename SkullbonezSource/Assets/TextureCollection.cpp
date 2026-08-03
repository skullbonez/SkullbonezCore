/*
File: SkullbonezSource/Assets/TextureCollection.cpp
Purpose:
  Loads texture files and hands renderer-neutral texture ids to draw code.

Summary:
  TextureCollection resolves legacy hashes through AssetSystem, fills fixed GPU
  texture slots through borrowed DX12 facets, and rebuilds or releases backend
  handles at renderer lifecycle boundaries.

Glossary:
  Legacy hash: 32-bit texture key kept for old render callers while asset ids
  become the durable authoring identity.
  Backend handle: Opaque texture id owned by the active render backend.
  Render resource context: Borrowed renderer facet that creates and deletes
  long-lived texture resources.

Invariants:
  - Texture slots are fixed-size legacy storage keyed by legacy hash.
  - backendHandle values belong to the active renderer and must be deleted or
    rebuilt when the backend is destroyed or reset.
  - Texture creation/deletion requires a bound render resource context; texture
    selection requires a bound render command context.
  - Legacy direct texture creation must receive a non-zero hash before it can
    populate the fixed slot table.

Related:
  - SkullbonezSource/Assets/TextureCollection.h
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "TextureCollection.h"
#include "../Core/FatalError.h"
#include "../Core/SbDiagnosticStore.h"
#include "../Rendering/RenderCommandTypes.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "stb_image.h"

#include <memory>


using namespace SkullbonezCore::Textures;
using namespace SkullbonezCore::Rendering;
namespace Runtime = SkullbonezCore::Runtime;


TextureCollection::TextureCollection( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics ) noexcept
    : m_resultDiagnostics( resultDiagnostics )
{
}


int TextureCollection::FindIndex( uint32_t hash ) const
{
    const int index = FindIndexNoThrow( hash );

    if ( index >= 0 )
    {
        return index;
    }

    // Invariant: public residency/loading APIs return Lane R failures before a
    // caller asks for an index. Reaching this point means an internal caller
    // skipped the result contract.
    SB_FATAL( "TextureCollection", "Texture hash lookup failed after residency check. hash=0x%08X", hash );
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

    // Invariant: texture slots are fixed legacy storage. Runtime asset loading
    // must fit the configured capacity instead of growing during draw/resource
    // rebuild paths.
    SB_FATAL( "TextureCollection", "Texture slot capacity exhausted. capacity=%zu", m_textures.size() );
}


void TextureCollection::ReleaseTexture( GpuTextureRecord& texture )
{

    // Lifetime: the collection stores renderer-neutral ids, but the backend
    // owns the actual GPU texture object behind each handle.

    if ( texture.backendHandle )
    {

        if ( !m_renderResources )
        {

            // Invariant: backend handles can only be destroyed by the resource
            // factory that owns them. Releasing without that facet would leak or
            // orphan the renderer resource.
            SB_FATAL( "TextureCollection",
                      "ReleaseTexture requires a bound render resource context. backendHandle=%u legacyHash=0x%08X",
                      texture.backendHandle, texture.legacyHash );
        }

        m_renderResources->DeleteTexture( texture.backendHandle );
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
    const int index = FindIndexNoThrow( hash );

    if ( index < 0 )
    {
        return;
    }

    ReleaseTexture( m_textures[index] );
}


SkullbonezCore::Core::SbResult TextureCollection::SelectTexture( uint32_t hash )
{
    const SkullbonezCore::Core::SbResult ensureResult = EnsureTexture( hash );

    if ( !ensureResult.Ok() )
    {
        return ensureResult;
    }

    if ( !m_renderBindings )
    {

        // Invariant: selecting a texture mutates frame draw state and therefore
        // requires the command facet for the active backend.
        SB_FATAL( "TextureCollection", "SelectTexture requires a bound render command context. hash=0x%08X", hash );
    }

    m_renderBindings->BindTexture( m_textures[FindIndex( hash )].backendHandle, 0 );
    return SkullbonezCore::Core::SbResult::Success();
}


TextureCollection::TextureHandleResult TextureCollection::GetTextureHandle( uint32_t hash )
{
    TextureHandleResult result;
    result.result = EnsureTexture( hash );

    if ( !result.result.Ok() )
    {
        return result;
    }

    result.handle = m_textures[FindIndex( hash )].backendHandle;
    return result;
}


void TextureCollection::BindAssetSystem( Assets::AssetSystem* assets )
{
    m_assets = assets;
}


void TextureCollection::BindRenderContexts( Dx12TextureOwner* renderResources, Dx12TextureOwner* renderBindings )
{

    // Lifetime: Run owns these backend facets and clears them before backend
    // teardown. TextureCollection keeps only opaque handles created by the same
    // resource factory.
    m_renderResources = renderResources;
    m_renderBindings = renderBindings;
}


bool TextureCollection::HasTexture( uint32_t hash ) const
{
    return FindIndexNoThrow( hash ) >= 0;
}


SkullbonezCore::Core::SbResult TextureCollection::EnsureTexture( uint32_t hash )
{

    if ( HasTexture( hash ) )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( m_assets )
    {
        const Assets::TextureSourceAsset* source = m_assets->FindTextureSourceAssetByLegacyHash( hash );

        if ( source )
        {
            return CreateTextureFromSourceAsset( *source );
        }
    }

    return m_resultDiagnostics.Failure( "TextureCollection",
                                        "Texture 0x%08X is not resident and has no registered source asset.", hash );
}


SkullbonezCore::Core::SbResult TextureCollection::LoadJpegTextureIntoSlot( int slot, const char* fileName, uint32_t hash,
                                                                           Assets::AssetId sourceId, bool generateMips,
                                                                           bool linearFilter, int channelsHint )
{

    if ( slot < 0 || slot >= static_cast<int>( m_textures.size() ) )
    {

        // Invariant: callers reserve slots through FindFreeSlot or reuse an
        // existing resident slot. A direct out-of-range slot would corrupt the
        // fixed legacy texture table.
        SB_FATAL( "TextureCollection", "Invalid texture slot. slot=%d capacity=%zu hash=0x%08X", slot, m_textures.size(),
                  hash );
    }

    if ( !fileName || fileName[0] == '\0' )
    {
        return m_resultDiagnostics.Failure( "TextureCollection", "Texture file path is empty. hash=0x%08X", hash );
    }

    const int requestedChannels = channelsHint > 0 ? channelsHint : 3;

    if ( !m_renderResources )
    {

        // Invariant: texture file bytes become a backend resource immediately
        // after decode, so the render resource facet must be bound first.
        SB_FATAL( "TextureCollection",
                  "LoadJpegTextureIntoSlot requires a bound render resource context. slot=%d hash=0x%08X path=\"%s\"", slot,
                  hash, fileName ? fileName : "" );
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    std::unique_ptr<unsigned char, decltype( &stbi_image_free )> data( stbi_load( fileName, &width, &height, &sourceChannels,
                                                                                  requestedChannels ),
                                                                       stbi_image_free );

    if ( !data )
    {
        return m_resultDiagnostics.Failure( "TextureCollection", "Image load failed. path=\"%s\" hash=0x%08X", fileName,
                                            hash );
    }

    const uint32_t backendHandle = m_renderResources->CreateTexture2D( data.get(), width, height, requestedChannels,
                                                                       generateMips
                                                                           ? Rendering::TextureMipPolicy::Generate
                                                                           : Rendering::TextureMipPolicy::SingleLevel,
                                                                       linearFilter
                                                                           ? Rendering::TextureFilterPolicy::Linear
                                                                           : Rendering::TextureFilterPolicy::Nearest );

    if ( backendHandle == 0 )
    {
        return m_resultDiagnostics.Failure( "TextureCollection",
                                            "Backend returned an invalid texture handle. path=\"%s\" hash=0x%08X", fileName,
                                            hash );
    }

    GpuTextureRecord& texture = m_textures[slot];
    texture.legacyHash = hash;
    texture.backendHandle = backendHandle;
    texture.sourceId = sourceId;
    texture.width = width;
    texture.height = height;
    texture.channels = requestedChannels;
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult TextureCollection::CreateTextureFromSourceAsset( const Assets::TextureSourceAsset& source )
{

    if ( source.legacyHash == 0 )
    {
        return m_resultDiagnostics.Failure( "TextureCollection",
                                            "Texture source asset is missing a legacy hash. source_id=%u logical=\"%s\"",
                                            source.id, source.logicalName.c_str() );
    }

    const int existingIndex = FindIndexNoThrow( source.legacyHash );

    if ( existingIndex >= 0 )
    {
        ReleaseTexture( m_textures[existingIndex] );
    }

    return LoadJpegTextureIntoSlot( FindFreeSlot(), source.resolvedPath.c_str(), source.legacyHash, source.id,
                                    source.generateMips, source.linearFilter, source.channelsHint );
}


SkullbonezCore::Core::SbResult TextureCollection::CreateJpegTexture( const char* cFileName, uint32_t hash )
{

    if ( hash == 0 )
    {

        // Invariant: legacy direct texture creation still indexes the fixed
        // table by hash. Zero is the sentinel for "not addressable".
        SB_FATAL( "TextureCollection", "CreateJpegTexture requires a non-zero legacy hash. path=\"%s\"",
                  cFileName ? cFileName : "" );
    }

    const Assets::TextureSourceAsset* source = m_assets ? m_assets->FindTextureSourceAssetByLegacyHash( hash ) : nullptr;

    if ( source )
    {
        return CreateTextureFromSourceAsset( *source );
    }

    const int existingIndex = FindIndexNoThrow( hash );

    if ( existingIndex >= 0 )
    {
        ReleaseTexture( m_textures[existingIndex] );
    }

    return LoadJpegTextureIntoSlot( FindFreeSlot(), cFileName, hash, 0, true, true, 3 );
}


SkullbonezCore::Core::SbResult TextureCollection::EnsureJpegTexture( const char* cFileName, uint32_t hash )
{

    if ( HasTexture( hash ) )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    return CreateJpegTexture( cFileName, hash );
}


SkullbonezCore::Core::SbResult TextureCollection::RebuildTexturesFromSourceAssets()
{
    DeleteAllTextures();

    if ( !m_assets )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    for ( const Assets::TextureSourceAsset& source : m_assets->GetTextureSourceAssets() )
    {

        if ( source.legacyHash != 0 )
        {
            const SkullbonezCore::Core::SbResult result = CreateTextureFromSourceAsset( source );

            if ( !result.Ok() )
            {
                return result;
            }
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
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
                     source.id, source.logicalName.c_str(), source.resolvedPath.c_str(), source.legacyHash, backendHandle,
                     width, height, channels );
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
                     texture.sourceId, texture.legacyHash, texture.legacyHash, texture.backendHandle, texture.width,
                     texture.height, texture.channels );
        }
    }
}
