/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_TEXTURE_COLLECTION_H
#define SKULLBONEZ_TEXTURE_COLLECTION_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
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
    TextureCollection( void ); // Constructor
    ~TextureCollection( void ) = default;

    static TextureCollection* pInstance;           // Singleton instance pointer
    int m_textureCounter;                          // To keep track of number of m_textures created
    int m_nextAvailableTextureIndex;               // Tracks the next available index
    UINT m_textureArray[TOTAL_TEXTURE_COUNT];      // Keeps track of m_textures created by OpenGL
    uint32_t m_textureHashes[TOTAL_TEXTURE_COUNT]; // Stores hashed texture name keys

    tImageJPG* LoadJPEG( const char* cFileName );                           // Loads a jpeg file
    int FindIndex( uint32_t hash );                                         // Returns the index of the specified texture
    void UpdateCounters( void );                                            // Updates texture counter members
    void DecodeJPEG( jpeg_decompress_struct* info, tImageJPG* pImageData ); // Decodes jpeg files

  public:
    static TextureCollection* Instance( void );                     // Call to request a pointer to the singleton instance
    static void Destroy( void );                                    // Call to destroy the singleton instance
    void SelectTexture( uint32_t hash );                            // Selects the texture as the OpenGL target
    int NumFreeTextureSpaces( void );                               // Returns the number of free texture spaces
    void DeleteTexture( uint32_t hash );                            // Deletes the texture from OpenGL
    void DeleteAllTextures( void );                                 // Deletes all textures from OpenGL
    void CreateJpegTexture( const char* cFileName, uint32_t hash ); // Creates a new texture from a jpeg file
};
} // namespace Textures
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
