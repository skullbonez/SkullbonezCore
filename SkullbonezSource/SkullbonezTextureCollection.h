/*
File: SkullbonezSource/SkullbonezTextureCollection.h
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
  - SkullbonezSource/SkullbonezTextureCollection.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezAssetSystem.h"
#include "SkullbonezCommon.h"

#include <array>

namespace SkullbonezCore
{
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

    TextureCollection() = default;
    ~TextureCollection() = default;

    std::array<GpuTextureRecord, TOTAL_TEXTURE_COUNT> m_textures = {};
    Assets::AssetSystem* m_assets = nullptr;

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
    static TextureCollection* Instance();
    static void Destroy();
    void BindAssetSystem( Assets::AssetSystem* assets );
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
