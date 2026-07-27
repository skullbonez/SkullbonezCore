/*
File: SkullbonezSource/Assets/TextureCollection.h
Purpose:
  Loads texture files and hands renderer-neutral texture ids to draw code.

Summary:
  TextureCollection.h loads texture files and hands renderer-neutral texture
  ids to draw code. As a public header, keep edits anchored on asset lifetime,
  cache ownership, and load/fallback behavior and on the glossary/invariants
  below.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - m_textures is fixed to SkullbonezCore::Scene::Capacity::TOTAL_TEXTURE_COUNT; hash lookup must resolve to one
    resident slot before binding.
  - m_assets is borrowed and may be null for legacy direct texture loads.
  - Texture creation/deletion uses a borrowed render-resource context; texture
    selection uses a borrowed command context.
  - Texture file and backend creation failures are Lane R results; fixed slot
    capacity and missing renderer facets remain fatal owner invariants.

Related:
  - SkullbonezSource/Assets/TextureCollection.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "AssetSystem.h"
#include "AssetKeys.h"
#include "../Core/SceneCapacity.h"
#include "../Core/Common.h"
#include "../Core/SbResult.h"

#include <array>

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12GeometryOwner;
class Dx12TextureOwner;
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

    std::array<GpuTextureRecord, SkullbonezCore::Scene::Capacity::TOTAL_TEXTURE_COUNT> m_textures = {};
    Assets::AssetSystem* m_assets = nullptr;
    Rendering::Dx12TextureOwner* m_renderResources = nullptr;
    Rendering::Dx12TextureOwner* m_renderBindings = nullptr;

  public:
    struct TextureHandleResult
    {
        SkullbonezCore::Core::SbResult result; // Lane R texture residency/load result before the backend handle can be used.
        uint32_t handle = 0;                   // Opaque renderer texture handle; 0 means no usable texture.
    };

  private:
    int FindIndex( uint32_t hash ) const;
    int FindIndexNoThrow( uint32_t hash ) const;
    int FindFreeSlot() const;
    void ReleaseTexture( GpuTextureRecord& texture );
    SkullbonezCore::Core::SbResult LoadJpegTextureIntoSlot( int slot, const char* fileName, uint32_t hash,
                                                            Assets::AssetId sourceId, bool generateMips, bool linearFilter,
                                                            int channelsHint );
    SkullbonezCore::Core::SbResult CreateTextureFromSourceAsset( const Assets::TextureSourceAsset& source );

  public:
    TextureCollection() = default;
    ~TextureCollection() = default;
    TextureCollection( const TextureCollection& ) = delete;
    TextureCollection& operator=( const TextureCollection& ) = delete;

    void BindAssetSystem( Assets::AssetSystem* assets );
    void BindRenderContexts( Rendering::Dx12TextureOwner* renderResources, Rendering::Dx12TextureOwner* renderBindings );
    bool HasTexture( uint32_t hash ) const;
    SkullbonezCore::Core::SbResult EnsureTexture( uint32_t hash );
    SkullbonezCore::Core::SbResult SelectTexture( uint32_t hash );
    TextureHandleResult GetTextureHandle( uint32_t hash );
    int NumFreeTextureSpaces() const;
    void DeleteTexture( uint32_t hash );
    void DeleteAllTextures();
    SkullbonezCore::Core::SbResult CreateJpegTexture( const char* cFileName, uint32_t hash );
    SkullbonezCore::Core::SbResult EnsureJpegTexture( const char* cFileName, uint32_t hash );
    SkullbonezCore::Core::SbResult RebuildTexturesFromSourceAssets();
    void DumpTextureAssets( FILE* out ) const;
};
} // namespace Textures
} // namespace SkullbonezCore
