#pragma once


// --- Includes ---
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

    int FindIndex( uint32_t hash ); // Returns the index of the specified texture
    void UpdateCounters();          // Updates texture counter members

  public:
    static TextureCollection* Instance();                           // Call to request a pointer to the singleton instance
    static void Destroy();                                          // Call to destroy the singleton instance
    void SelectTexture( uint32_t hash );                            // Selects the texture as the OpenGL target
    int NumFreeTextureSpaces();                                     // Returns the number of free texture spaces
    void DeleteTexture( uint32_t hash );                            // Deletes the texture from OpenGL
    void DeleteAllTextures();                                       // Deletes all textures from OpenGL
    void CreateJpegTexture( const char* cFileName, uint32_t hash ); // Creates a new texture from an image file
};
} // namespace Textures
} // namespace SkullbonezCore
