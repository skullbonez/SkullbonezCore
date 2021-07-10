/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
																		  THE SKULLBONEZ CORE
																				_______
																			 .-"       "-.
																			/             \
																		   /               \
																		   ¦   .--. .--.   ¦
																		   ¦ )/   ¦ ¦   \( ¦
																		   ¦/ \__/   \__/ \¦
																		   /      /^\      \
																		   \__    '='    __/
								   											 ¦\         /¦
																			 ¦\'"VUUUV"'/¦
																			 \ `"""""""` /
																			  `-._____.-'

																		 www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/



/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_TEXTURE_COLLECTION_H
#define	SKULLBONEZ_TEXTURE_COLLECTION_H



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

			TextureCollection				(int iMaxTextureCount);						// Constructor
			~TextureCollection				(void);										// Default destructor

			static TextureCollection* 		pInstance;									// Singleton instance pointer
			int								textureCounter;								// To keep track of number of textures created
			int								nextAvailableTextureIndex;					// Tracks the next available index
			int								maxTextureCount;							// Maximum amount of textures allowed
			UINT*							textureArray;								// Keeps track of textures created by OpenGL
			char**							textureNames;								// Gives meaning to texture indexes

			tImageJPG* LoadJPEG				(const char* cFileName);					// Loads a jpeg file
			int		   FindIndex			(const char* cTextureName);					// Returns the index of the specified texture			
			void	   UpdateCounters		(void);										// Updates texture counter members
			void		DecodeJPEG			(jpeg_decompress_struct* info, 
											 tImageJPG*				 pImageData);		// Decodes jpeg files			


		public:


			static TextureCollection*			Instance				(int iMaxTextureCount);			// Call to request a pointer to the singleton instance
			static void							Destroy					(void);							// Call to destroy the singleton instance
			void								SelectTexture			(const char* cTextureName);		// Selects the texture as the OpenGL target
			int									NumFreeTextureSpaces	(void);							// Returns the number of free texture spaces
			void								DeleteTexture			(const char* cTextureName);		// Deletes the texture from OpenGL
			void								DeleteAllTextures		(void);							// Deletes all textures from OpenGL
			void								CreateJpegTexture		(const char* cFileName,
																		 const char* cTextureName);		// Creates a new texture from a jpeg file
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/