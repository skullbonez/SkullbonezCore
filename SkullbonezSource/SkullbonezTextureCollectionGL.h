#pragma once

#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Textures
{
class TextureCollection
{
  private:
    TextureCollection();
    ~TextureCollection() = default;

    inline static TextureCollection* pInstance = nullptr;
    int m_textureCounter;
    int m_nextAvailableTextureIndex;
    GLuint m_textureArray[TOTAL_TEXTURE_COUNT];
    uint32_t m_textureHashes[TOTAL_TEXTURE_COUNT];

    int FindIndex( uint32_t hash );
    void UpdateCounters();

  public:
    static TextureCollection* Instance();
    static void Destroy();
    void SelectTexture( uint32_t hash );
    int NumFreeTextureSpaces();
    void DeleteTexture( uint32_t hash );
    void DeleteAllTextures();
    void CreateJpegTexture( const char* cFileName, uint32_t hash );
};
} // namespace Textures
} // namespace SkullbonezCore
