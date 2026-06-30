/*
File: SkullbonezSource/Assets/TextureCollection.h
Purpose:
  Loads texture files and hands renderer-neutral texture ids to draw code.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - m_textures is fixed to TOTAL_TEXTURE_COUNT; hash lookup must resolve to one
    resident slot before binding.
  - m_assets is borrowed and may be null for legacy direct texture loads.
  - Texture creation/deletion uses a borrowed render-resource context; texture
    selection uses a borrowed command context.

Related:
  - SkullbonezSource/Assets/TextureCollection.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "AssetSystem.h"
#include "../Core/Common.h"

#include <array>

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCommandContext;
class IRenderResourceFactory;
} // namespace Rendering

namespace Textures
{
class TextureCollection
{

  private:
    struct GpuTextureRecord
    {
        uint32_t legacyHash = 0;
        uint32_t backendHandle = 0;
        Assets::AssetId sourceId = 0;
        int width = 0;
        int height = 0;
        int channels = 0;

        bool IsResident() const
        {
            return backendHandle != 0;
        }
    };

    std::array<GpuTextureRecord, TOTAL_TEXTURE_COUNT> m_textures = {};
    Assets::AssetSystem* m_assets = nullptr;
    Rendering::IRenderResourceFactory* m_renderResources = nullptr;
    Rendering::IRenderCommandContext* m_renderCommands = nullptr;

    int FindIndex( uint32_t hash ) const;
    int FindIndexNoThrow( uint32_t hash ) const;
    int FindFreeSlot() const;
    void ReleaseTexture( GpuTextureRecord& texture );
    void LoadJpegTextureIntoSlot( int slot,
                                  const char* fileName,
                                  uint32_t hash,
                                  Assets::AssetId sourceId,
                                  bool generateMips,
                                  bool linearFilter,
                                  int channelsHint );
    void CreateTextureFromSourceAsset( const Assets::TextureSourceAsset& source );

  public:
    TextureCollection() = default;
    ~TextureCollection() = default;
    TextureCollection( const TextureCollection& ) = delete;
    TextureCollection& operator=( const TextureCollection& ) = delete;

    static TextureCollection* Instance();
    static void Destroy();
    void BindAssetSystem( Assets::AssetSystem* assets );
    void BindRenderContexts( Rendering::IRenderResourceFactory* renderResources,
                             Rendering::IRenderCommandContext* renderCommands );
    bool HasTexture( uint32_t hash ) const;
    void EnsureTexture( uint32_t hash );
    void SelectTexture( uint32_t hash );
    uint32_t GetTextureHandle( uint32_t hash );
    int NumFreeTextureSpaces() const;
    void DeleteTexture( uint32_t hash );
    void DeleteAllTextures();
    void CreateJpegTexture( const char* cFileName, uint32_t hash );
    void EnsureJpegTexture( const char* cFileName, uint32_t hash );
    void RebuildTexturesFromSourceAssets();
    void DumpTextureAssets( FILE* out ) const;
};
} // namespace Textures
} // namespace SkullbonezCore
