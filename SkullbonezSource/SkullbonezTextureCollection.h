#pragma once


// --- Includes ---
#include "SkullbonezAssetSystem.h"
#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Textures
{
/* -- Texture Collection -----------------------------------------------------------------------------------------------------------------------------------------

    A singleton class that manages a collection of Open GL textures and mipmaps.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TextureCollection
{

  private:
    TextureCollection(); // Constructor
    ~TextureCollection() = default;

    inline static TextureCollection* pInstance = nullptr;
    int m_textureCounter;                          // To keep track of number of m_textures created
    int m_nextAvailableTextureIndex;               // Tracks the next available index
    UINT m_textureArray[TOTAL_TEXTURE_COUNT];      // Keeps track of m_textures created by OpenGL
    uint32_t m_textureHashes[TOTAL_TEXTURE_COUNT]; // Stores hashed texture name keys
    Assets::AssetId m_textureSourceIds[TOTAL_TEXTURE_COUNT];
    int m_textureWidths[TOTAL_TEXTURE_COUNT];
    int m_textureHeights[TOTAL_TEXTURE_COUNT];
    int m_textureChannels[TOTAL_TEXTURE_COUNT];
    Assets::AssetSystem* m_assets = nullptr;

    int FindIndex( uint32_t hash ) const;        // Returns the index of the specified texture
    int FindIndexNoThrow( uint32_t hash ) const; // Returns -1 when the texture is not loaded
    void UpdateCounters();                       // Updates texture counter members
    void LoadJpegTextureIntoSlot( int slot,
                                  const char* fileName,
                                  uint32_t hash,
                                  Assets::AssetId sourceId,
                                  bool generateMips,
                                  bool linearFilter,
                                  int channelsHint );
    void CreateTextureFromSourceAsset( const Assets::TextureSourceAsset& source );

  public:
    static TextureCollection* Instance();                           // Call to request a pointer to the singleton instance
    static void Destroy();                                          // Call to destroy the singleton instance
    void BindAssetSystem( Assets::AssetSystem* assets );            // Sets the source asset registry used to rebuild backend textures
    bool HasTexture( uint32_t hash ) const;                         // True when the legacy hash currently has a backend handle
    void EnsureTexture( uint32_t hash );                            // Loads a registered texture if it is not already resident
    void SelectTexture( uint32_t hash );                            // Selects the texture as the OpenGL target
    uint32_t GetTextureHandle( uint32_t hash );                     // Returns the backend handle for the texture with the given hash key
    int NumFreeTextureSpaces();                                     // Returns the number of free texture spaces
    void DeleteTexture( uint32_t hash );                            // Deletes the texture from OpenGL
    void DeleteAllTextures();                                       // Deletes all textures from OpenGL
    void CreateJpegTexture( const char* cFileName, uint32_t hash ); // Creates a new texture from an image file
    void EnsureJpegTexture( const char* cFileName, uint32_t hash ); // Creates a new texture only when the hash is not already loaded
    void RebuildTexturesFromSourceAssets();                         // Recreates all registered GPU textures for the active backend
    void DumpTextureAssets( FILE* out ) const;                      // Diagnostic dump of texture source and backend records
};
} // namespace Textures
} // namespace SkullbonezCore
