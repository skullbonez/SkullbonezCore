/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
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
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezTextureCollection.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Textures;



/* -- SINGLETON INSTANCE INITIALISATION -------------------------------------------*/
TextureCollection* TextureCollection::pInstance = 0;



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
TextureCollection::TextureCollection(int iMaxTextureCount)
{
	this->maxTextureCount = iMaxTextureCount;
	this->nextAvailableTextureIndex = 0;
	this->textureCounter = 0;

	this->textureArray = new UINT[iMaxTextureCount];
	this->textureNames = new char*[iMaxTextureCount];

	for(int count=0; count<iMaxTextureCount; ++count)
	{
		this->textureArray[count] = 0;
		this->textureNames[count] = 0;
	}
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
TextureCollection::~TextureCollection(void)
{ 
	this->DeleteAllTextures();

	if(this->textureArray) delete [] this->textureArray;
	if(this->textureNames) delete [] this->textureNames;
}



/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
TextureCollection* TextureCollection::Instance(int iMaxTextureCount)
{
	if(!TextureCollection::pInstance) 
		TextureCollection::pInstance = new TextureCollection(iMaxTextureCount);

	return TextureCollection::pInstance;
}



/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void TextureCollection::Destroy(void)
{
	if(TextureCollection::pInstance) delete TextureCollection::pInstance;

	TextureCollection::pInstance = 0;
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
	unsigned char** rowPtr = new unsigned char*[pImageData->sizeY];
	for (int i=0; i<pImageData->sizeY; i++)
		rowPtr[i] = &(pImageData->data[i*pImageData->rowSpan]);

	// Now comes the juice of our work, here we extract all the pixel data
	int rowsRead = 0;
	while (info->output_scanline < info->output_height) 
	{
		// Read in the current row of pixels and increase the rowsRead count
		rowsRead += jpeg_read_scanlines(info, 
										&rowPtr[rowsRead], 
										info->output_height - rowsRead);
	}
	
	// Delete the temporary row pointers
	delete [] rowPtr;

	// Finish decompressing the data
	jpeg_finish_decompress(info);
}



/* -- LOAD JPEG -------------------------------------------------------------------*/
tImageJPG* TextureCollection::LoadJPEG(const char* cFileName)
{
	struct jpeg_decompress_struct cinfo;
	tImageJPG *pImageData = 0;
	FILE *pFile;
	
	// Open a file pointer to the jpeg file
	pFile = fopen(cFileName, "rb");

	// Check for failure
	if(!(pFile)) throw "JPEG file not found (TextureCollection::LoadJPEG)";
	
	// Create an error handler
	jpeg_error_mgr jerr;

	// Have our compression info object point to the error handler address
	cinfo.err = jpeg_std_error(&jerr);
	
	// Initialize the decompression object
	jpeg_create_decompress(&cinfo);
	
	// Specify the data source (Our file pointer)	
	jpeg_stdio_src(&cinfo, pFile);
	
	// Allocate the structure that will hold our eventual jpeg data (must free it!)
	pImageData = (tImageJPG*)malloc(sizeof(tImageJPG));

	// Decode the jpeg file and fill in the image data structure to pass back
	DecodeJPEG(&cinfo, pImageData);
	
	// This releases all the stored memory for reading and decoding the jpeg
	jpeg_destroy_decompress(&cinfo);
	
	// Close the file pointer that opened the file
	fclose(pFile);

	// Return the image data
	return pImageData;
}



/* -- UPDATE COUNTERS -------------------------------------------------------------*/
void TextureCollection::UpdateCounters(void)
{
	// reset the counter
	this->textureCounter = 0;
	bool isNextAvailIndexSet = false;

	// iterate through all textures
	for(int count=0; count<this->maxTextureCount; ++count)
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
int TextureCollection::FindIndex(const char* cTextureName)
{
	// iterate through texture array
	for(int count=0; count<this->maxTextureCount; ++count)
	{
		// if the texture names are identical, reutrn the index
		if(!strcmp(this->textureNames[count], cTextureName)) 
			return count;
	}

	// throw an exception if the texture name does not exist
	throw "Texture does not exist.  (TextureCollection::FindIndex)";
}



/* -- DELETE ALL TEXTURES ---------------------------------------------------------*/
void TextureCollection::DeleteAllTextures(void)
{
	// delete all OpenGL textures
	glDeleteTextures(this->nextAvailableTextureIndex, this->textureArray);

	// iterate through texture array
	for(int count=0; count<this->maxTextureCount; ++count)
	{
		// if texture at specified index exists
		if(this->textureArray[count])
		{
			// delete the space allocated for the string
			if(this->textureNames[count]) delete [] this->textureNames[count];

			// reset texture name to null
			this->textureNames[count] = 0;

			// reset texture identifier to null
			this->textureArray[count] = 0;
		}
	}

	// Update capacity and progress counters
	this->UpdateCounters();
}



/* -- DELETE TEXTURE --------------------------------------------------------------*/
void TextureCollection::DeleteTexture(const char* cTextureName)
{
	// get the index of the specified texture
	int index = this->FindIndex(cTextureName);

	// delete the space allocated for the string
	if(this->textureNames[index]) delete this->textureNames[index];

	// reset texture name to null
	this->textureNames[index] = NULL;

	// remove OpenGL texture
	glDeleteTextures(1, &this->textureArray[index]);

	// reset texture identifier to null
	this->textureArray[index] = NULL;

	// Update capacity and progress counters
	this->UpdateCounters();
}



/* -- NUM FREE TEXTURE SPACES -----------------------------------------------------*/
int	TextureCollection::NumFreeTextureSpaces(void)
{
	return this->maxTextureCount - this->textureCounter;
}



/* -- SELECT TEXTURE --------------------------------------------------------------*/
void TextureCollection::SelectTexture(const char* cTextureName)
{
	// make specified texture the current opengl target
	glBindTexture(GL_TEXTURE_2D, this->textureArray[this->FindIndex(cTextureName)]);
}



/* -- CREATE JPEG TEXTURE ---------------------------------------------------------*/
void TextureCollection::CreateJpegTexture(const char *cFileName, 
										  const char *cTextureName)
{
	// check to ensure there is room in the texture array
	if(this->textureCounter == this->maxTextureCount)
		throw "Texture array full!  (TextureCollection::CreateJpegTexture)";

	// make space for texture name
	this->textureNames[this->nextAvailableTextureIndex] = 
												new char[strlen(cTextureName) + 1];

	// deep copy texture name
	strcpy(this->textureNames[this->nextAvailableTextureIndex], cTextureName);

	// load the image and store the data
	tImageJPG *pImage = this->LoadJPEG(cFileName);

	// check for data
	if(!pImage) throw "Jpeg load failed!  (TextureCollection::CreateJpegTexture)";
	
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
		free(pImage);
	}

	// Update capacity and progress counters
	this->UpdateCounters();
}