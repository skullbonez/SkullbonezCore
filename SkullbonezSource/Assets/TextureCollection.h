/*
File: SkullbonezSource/Assets/TextureCollection.h
Purpose:
  Loads texture files and hands renderer-neutral texture ids to draw code.

Mental model:
  Texture loading is resource-phase work: callers pass a render resource
  factory to upload/delete backend textures. Draw-time code may only ask for an
  already-resident handle or bind it through a command context.

Glossary:
  Legacy hash: Stable integer id used by old draw code to refer to a texture.
  Render resource factory: Borrowed renderer capability used to create and
  delete backend texture objects.
  Command context: Borrowed frame capability used to bind a resident texture
  handle to a shader slot.
  Backend handle: Opaque integer token owned by the active renderer backend.

Invariants:
  - m_textures is fixed to TOTAL_TEXTURE_COUNT; hash lookup must resolve to one
    resident slot before binding.
  - m_assets is borrowed and may be null for legacy direct texture loads.
  - Draw-time handle reads must not lazily upload textures; resource/rebuild
    phases are responsible for residency.

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

    int FindIndex( uint32_t hash ) const;
    int FindIndexNoThrow( uint32_t hash ) const;
    int FindFreeSlot() const;
    void ReleaseTexture( Rendering::IRenderResourceFactory* renderResources, GpuTextureRecord& texture );
    void LoadJpegTextureIntoSlot( Rendering::IRenderResourceFactory& renderResources,
                                  int slot,
                                  const char* fileName,
                                  uint32_t hash,
                                  Assets::AssetId sourceId,
                                  bool generateMips,
                                  bool linearFilter,
                                  int channelsHint );
    void CreateTextureFromSourceAsset( Rendering::IRenderResourceFactory& renderResources,
                                       const Assets::TextureSourceAsset& source );

  public:
    TextureCollection() = default;
    ~TextureCollection() = default;
    void BindAssetSystem( Assets::AssetSystem* assets );
    bool HasTexture( uint32_t hash ) const;
    void EnsureTexture( Rendering::IRenderResourceFactory& renderResources, uint32_t hash );
    void SelectTexture( Rendering::IRenderCommandContext& renderCommands, uint32_t hash ) const;
    uint32_t GetTextureHandle( uint32_t hash ) const;
    int NumFreeTextureSpaces() const;
    void DeleteTexture( Rendering::IRenderResourceFactory* renderResources, uint32_t hash );
    void DeleteAllTextures( Rendering::IRenderResourceFactory* renderResources );
    void CreateJpegTexture( Rendering::IRenderResourceFactory& renderResources, const char* cFileName, uint32_t hash );
    void EnsureJpegTexture( Rendering::IRenderResourceFactory& renderResources, const char* cFileName, uint32_t hash );
    void RebuildTexturesFromSourceAssets( Rendering::IRenderResourceFactory& renderResources );
    void DumpTextureAssets( FILE* out ) const;
};
} // namespace Textures
} // namespace SkullbonezCore
