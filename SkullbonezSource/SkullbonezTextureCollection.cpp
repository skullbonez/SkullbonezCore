/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   �   .--. .--.   �
								   � )/   � �   \( �
								   �/ \__/   \__/ \�
								   /      /^\      \
								   \__    '='    __/
								   	 �\         /�
									 �\'"VUUUV"'/�
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezTextureCollection.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Textures;



/* -- SINGLETON INSTANCE INITIALISATION -------------------------------------------*/
TextureCollection* TextureCollection::pInstance = 0;



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
TextureCollection::TextureCollection(void)
{
	this->nextAvailableTextureIndex = 0;
	this->textureCounter = 0;

	for(int count=0; count<TOTAL_TEXTURE_COUNT; ++count)
	{
		this->textureArray[count] = 0;
		this->textureHashes[count] = 0;
	}
}



/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
TextureCollection* TextureCollection::Instance(void)
{
	if(!TextureCollection::pInstance)
	{
		static TextureCollection instance;
		TextureCollection::pInstance = &instance;
	}
	return TextureCollection::pInstance;
}



/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void TextureCollection::Destroy(void)
{
	if(TextureCollection::pInstance)
	{
		TextureCollection::pInstance->DeleteAllTextures();
		TextureCollection::pInstance = 0;
	}
}



/* -- DECODE JPEG -----------------------------------------------------------------*/
void TextureCollection::DecodeJPEG(jpeg_decompress_struct* info, 
								   tImageJPG *pImageData)
{
	// Read in the header of the jpeg file
	jpeg_read_header(info, TRUE);
	
	// Start to decompress the jpeg file with our compression info
	jpeg_start_decompress(info);

	// Get the image dimensions and row span to read in the pixel data
	pImageData->rowSpan = info->image_width * info->num_components;
	pImageData->sizeX   = info->image_width;
	pImageData->sizeY   = info->image_height;
	
	// Allocate memory for the pixel buffer
	pImageData->data = new unsigned char[pImageData->rowSpan * pImageData->sizeY];
		
	// Here we use the library's state variable info.output_scanline as the
	// loop counter, so that we don't have to keep track ourselves.
	
	// Create an array of row pointers
	std::vector<unsigned char*> rowPtr(pImageData->sizeY);
	for (int i=0; i<pImageData->sizeY; i++)
		rowPtr[i] = &(pImageData->data[i*pImageData->rowSpan]);

	// Now comes the juice of our work, here we extract all the pixel data
	int rowsRead = 0;
	while (info->output_scanline < info->output_height) 
	{
		rowsRead += jpeg_read_scanlines(info, 
										&rowPtr[rowsRead], 
										info->output_height - rowsRead);
	}

	// Finish decompressing the data
	jpeg_finish_decompress(info);
}



/* -- LOAD JPEG -------------------------------------------------------------------*/
tImageJPG* TextureCollection::LoadJPEG(const char* cFileName)
{
	struct jpeg_decompress_struct cinfo;
	tImageJPG *pImageData = 0;

	FILE *pFile = nullptr;
	fopen_s(&pFile, cFileName, "rb");
	if(!pFile) throw std::runtime_error("JPEG file not found (TextureCollection::LoadJPEG)");

	jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, pFile);

	pImageData = new tImageJPG();

	try
	{
		DecodeJPEG(&cinfo, pImageData);
	}
	catch (...)
	{
		delete pImageData;
		jpeg_destroy_decompress(&cinfo);
		fclose(pFile);
		throw;
	}

	jpeg_destroy_decompress(&cinfo);
	fclose(pFile);
	return pImageData;
}



/* -- UPDATE COUNTERS -------------------------------------------------------------*/
void TextureCollection::UpdateCounters(void)
{
	// reset the counter
	this->textureCounter = 0;
	bool isNextAvailIndexSet = false;

	// iterate through all textures
	for(int count=0; count<TOTAL_TEXTURE_COUNT; ++count)
	{
		// find the first empty spot
		if(!this->textureArray[count])
		{
			if(!isNextAvailIndexSet)
			{
				// set the next available index counter
				this->nextAvailableTextureIndex = count;
				
				// do not set this again
				isNextAvailIndexSet = true;	
			}
		}
		else
			// for every texture, increment
			++this->textureCounter;
	}
}



/* -- FIND INDEX ------------------------------------------------------------------*/
int TextureCollection::FindIndex(uint32_t hash)
{
	for(int count=0; count<TOTAL_TEXTURE_COUNT; ++count)
		if(this->textureHashes[count] == hash) return count;

	throw std::runtime_error("Texture does not exist.  (TextureCollection::FindIndex)");
}



/* -- DELETE ALL TEXTURES ---------------------------------------------------------*/
void TextureCollection::DeleteAllTextures(void)
{
	// delete all OpenGL textures
	glDeleteTextures(this->nextAvailableTextureIndex, this->textureArray);

	// iterate through texture array
	for(int count=0; count<TOTAL_TEXTURE_COUNT; ++count)
	{
		if(this->textureArray[count])
		{
			this->textureHashes[count] = 0;
			this->textureArray[count]  = 0;
		}
	}

	// Update capacity and progress counters
	this->UpdateCounters();
}



/* -- DELETE TEXTURE --------------------------------------------------------------*/
void TextureCollection::DeleteTexture(uint32_t hash)
{
	int index = this->FindIndex(hash);

	this->textureHashes[index] = 0;

	glDeleteTextures(1, &this->textureArray[index]);
	this->textureArray[index] = NULL;

	this->UpdateCounters();
}



/* -- NUM FREE TEXTURE SPACES -----------------------------------------------------*/
int	TextureCollection::NumFreeTextureSpaces(void)
{
	return TOTAL_TEXTURE_COUNT - this->textureCounter;
}



/* -- SELECT TEXTURE --------------------------------------------------------------*/
void TextureCollection::SelectTexture(uint32_t hash)
{
	glBindTexture(GL_TEXTURE_2D, this->textureArray[this->FindIndex(hash)]);
}



/* -- CREATE JPEG TEXTURE ---------------------------------------------------------*/
void TextureCollection::CreateJpegTexture(const char* cFileName, 
										  uint32_t    hash)
{
	if(this->textureCounter == TOTAL_TEXTURE_COUNT)
		throw std::runtime_error("Texture array full!  (TextureCollection::CreateJpegTexture)");

	this->textureHashes[this->nextAvailableTextureIndex] = hash;

	// load the image and store the data
	tImageJPG *pImage = this->LoadJPEG(cFileName);

	// check for data
	if(!pImage) throw std::runtime_error("Jpeg load failed!  (TextureCollection::CreateJpegTexture)");
	
	// specify textureArray @ index to Text2dureId @ index
	glGenTextures(1,								// num texture objects to create
				  &textureArray[this->nextAvailableTextureIndex]);	// point @ index

	// bind and init, 1st param: 2d textures (not 1d), 2nd param: point to location
	glBindTexture(GL_TEXTURE_2D,
				  this->textureArray[this->nextAvailableTextureIndex]);

	// build mipmaps
	gluBuild2DMipmaps(GL_TEXTURE_2D,			// 2d textures (not 1d)
					  MIP_MAP_COMPONENTS,		// defined in SkullbonezCommon.h
					  pImage->sizeX,			// x width
					  pImage->sizeY,			// y width
					  GL_RGB,					// pixel format
					  GL_UNSIGNED_BYTE,			// index type
					  pImage->data);			// image data

	// mipmap quality small range
	glTexParameteri(GL_TEXTURE_2D,				// 2d textures (not 1d)
					GL_TEXTURE_MIN_FILTER,		// small range filer
					GL_LINEAR_MIPMAP_LINEAR);	// linear linear (best quality)
	
	// mipmap quality large range
	glTexParameteri(GL_TEXTURE_2D,				// 2d textures (not 1d)
					GL_TEXTURE_MAG_FILTER,		// large range filer
					GL_LINEAR_MIPMAP_LINEAR);	// linear linear (best quality)
					

	// Cleanup image data (opengl has made a copy)
	if (pImage)
	{
		// hmmm
		if(pImage->data) delete [] pImage->data;
		delete pImage;
	}

	// Update capacity and progress counters
	this->UpdateCounters();
}
