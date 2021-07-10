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
#include "SkullbonezTerrain.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
Terrain::Terrain(const char* sFileName,		// path to .raw file
				 int		 iMapSize,		// size of map (pixels length)
				 int		 iStepSize,		// steps (pixel steps AND vertex steps)
				 int		 iTextureWrap)	// number of times to wrap texture
{
	// initialise postData pointer
	this->postData		= 0;

	// set members
	this->mapSize		= iMapSize;
	this->stepSize		= iStepSize;
	this->textureWrap	= iTextureWrap;

	// calculate length of terrain map
	this->terrainSizeWorldCoords = ((this->mapSize - this->stepSize) / 
									 this->stepSize) * this->stepSize;

	// get the number of posts per row or column (per side)
	this->postsPerSide = this->mapSize / this->stepSize;

	// get reference to terrain data
	this->pTerrainData = this->LoadTerrainData(sFileName);

	// build the terrain
	this->BuildTerrain();

	// build the renderable component of the terrain
	this->BuildDisplayList();

	// delete terrain data
	if(this->pTerrainData) delete [] this->pTerrainData;
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
Terrain::~Terrain(void)
{
	glDeleteLists(this->displayListReference, 1);
	if(this->postData) delete [] this->postData;
}



/* -- BUILD TERRAIN ---------------------------------------------------------------*/
void Terrain::BuildTerrain(void)
{
	// calculate the amount of terrain posts required
	int terrainPostCount = (this->mapSize / this->stepSize) *
						   (this->mapSize / this->stepSize);

	// allocate space for the terrain
	this->postData = new TerrainPost[terrainPostCount];

	// translate terrain postings
	this->TranslatePostings();

	// generate normals for the postings
	this->GenerateNormals();
}



/* -- GET PIXEL HEIGHT AT ---------------------------------------------------------*/
int Terrain::GetPixelHeightAt(int xCoord, int yCoord)
{
	return this->pTerrainData[xCoord + yCoord * this->mapSize];
}



/* -- LOAD TERRAIN DATA -----------------------------------------------------------*/
BYTE* Terrain::LoadTerrainData(const char* sFileName)
{
	// initialise pointer to the file
	FILE* pRawFile = 0;

	// open a file pointer to the .raw file
	pRawFile = fopen(sFileName, "rb");

	// check for failure
	if(!pRawFile) throw "Height map file not found.  (Terrain::LoadTerrain)";

	// initialise pointer for our byte array
	BYTE* pHeightMap = new BYTE[this->mapSize*this->mapSize];

	// read the raw bytes to our height map
	fread(pHeightMap, 1, this->mapSize * this->mapSize, pRawFile);

	// check for errors that occured while reading the data
	if(ferror(pRawFile)) 
		throw "Failed to read height map.  (Terrain::LoadTerrain)";

	// close the file
	fclose(pRawFile);

	// return a pointer to the data
	return pHeightMap;
}



/* -- RENDER ----------------------------------------------------------------------*/
void Terrain::Render(void)
{
	glCallList(this->displayListReference);
}



/* -- GET TERRAIN HEIGHT AT -------------------------------------------------------*/
float Terrain::GetTerrainHeightAt(float xPosition,
								  float zPosition,
								  bool  isFluidMin)
{
	float terrainHeight = 
		(GeometricMath::GetHeightFromPlane(this->LocatePolygon(xPosition,
															   zPosition),
										   xPosition,
										   zPosition));

	if(isFluidMin)
	{
		return (terrainHeight < FLUID_HEIGHT) ? FLUID_HEIGHT : terrainHeight;
	}
	else
	{
		return terrainHeight;
	}
}



/* -- IS IN BOUNDS ----------------------------------------------------------------*/
bool Terrain::IsInBounds(float xPosition, float zPosition)
{
	/*
		Justification for not allowing coordinates to the absolute outer bound:
		-----------------------------------------------------------------------
		It is arguable that a point would be in bounds of the terrain if it was
		equal to (terrainSizeWorldCoords * TERRAIN_SCALING).  This may be true on
		a physical level, however, this can cause major problems for the
		Terrain::LocatePolygon method as it uses:
		floor(xPosition/(this->stepSize * TERRAIN_SCALING)) and
		floor(zPosition/(this->stepSize * TERRAIN_SCALING))
		to determine which terrain quadric the point is in - you can only imagine
		what happens when the xPosition or the zPosition are equal to the upper 
		bound of the terrain - the quadric is set to something that does not exist 
		and all hell breaks loose (i.e. hours of debugging).
		
		So, who cares if you cant move to the absolute outer bound of the terrain, 
		just move to the abolsute outer bound minus the smallest possible fraction 
		of a float possible instead.
	*/

	return ((xPosition >= 0.0f) &&
			(zPosition >= 0.0f) &&
			(xPosition < this->terrainSizeWorldCoords * TERRAIN_SCALING) &&
			(zPosition < this->terrainSizeWorldCoords * TERRAIN_SCALING));
}



/* -- GET BOUNDS XZ ---------------------------------------------------------------*/
XZBounds Terrain::GetXZBounds(void)
{
	XZBounds bounds;

	bounds.xMin = 0.0f;
	bounds.zMin = 0.0f;
	bounds.xMax = this->terrainSizeWorldCoords * TERRAIN_SCALING;
	bounds.zMax = this->terrainSizeWorldCoords * TERRAIN_SCALING;

	return bounds;
}



/* -- LOCATE POLYGON ---------------------------------------------------------------------------------------------------------------------------------------------------*/
Triangle Terrain::LocatePolygon(float xPosition, float zPosition)
{
	// check to ensure specified co-ordinates are inside the terrain map bounds
	if(!this->IsInBounds(xPosition, zPosition))
	{
		throw "Specified co-ordinates are out of terrain bounds.  "
			  "(Terrain::GetTerrainHeightAt)";
	}

	// NOTE:  X and Z params are switched in this method to account for world 
	// co-ordinate space find which quadric we are in (treat terrain as orthagonal 
	// XZ projection to locate the quadric)
	int xPosting = (int)floorf(zPosition/(this->stepSize * TERRAIN_SCALING));
	int zPosting = (int)floorf(xPosition/(this->stepSize * TERRAIN_SCALING));

	// calculate the BOTTOM RIGHT post of the quadric hit - we will call this the 
	// 'target quadric'
	int targetQuadric = zPosting * this->postsPerSide + 
						xPosting + 
						this->postsPerSide;

	// scale the step size (terrain may be rendered after a glScale)
	float scaledStepSize = this->stepSize * TERRAIN_SCALING;

	// NOTE:  X and Z params are switched in this method to account for world 
	// co-ordinate space make our X and Z positions relative to the target quadric
	// (as we are essentially generating a 2d vector relative to the bottom right 
	// post of the target quadric, the xRelativePosition needs to be negated)
	float xRelativePosition = -(scaledStepSize - (fmodf(zPosition, scaledStepSize)));
	float zRelativePosition = fmodf(xPosition, scaledStepSize);

	// vars to help safely determine the gradient of the vector expressed by
	// xRelativePosition and zRelativePosition
	float gradient			 = 0.0f;
	bool  isGradientInfinite = false;

	// test to see if rise is infinitely greater than run
	if(!zRelativePosition) isGradientInfinite = true;

	// avoid a division by zero
	if(!isGradientInfinite)
	{
		// calculate the gradient of the vector relative from the target quadric
		gradient = xRelativePosition / zRelativePosition;
	}

	// triangle structure for the target polygon
	Triangle targetPolygon;
	ZeroMemory(&targetPolygon, sizeof(targetPolygon));

	/*
		The following test checks to see if triangle A or B has been hit

		|\---|						 \										  |
		| \ A|						  \  <- grad = -1   ------- <- grad = 0   | <- grad = infinite
		|B \ |						   \									  |
		|---\|<- object space origin    \									  |

		(NOTE: The gradient of the cross section is equal to -1)
	*/
	if(isGradientInfinite || gradient < -1.0f)
	{
		// TRIANGLE A
		targetPolygon.v1 = this->postData[targetQuadric].vPosition;
		targetPolygon.v2 = this->postData[targetQuadric - this->postsPerSide].vPosition;
		targetPolygon.v3 = this->postData[targetQuadric - this->postsPerSide + 1].vPosition;
	}
	else
	{
		// TRIANGLE B
		targetPolygon.v1 = this->postData[targetQuadric].vPosition;
		targetPolygon.v2 = this->postData[targetQuadric - this->postsPerSide + 1].vPosition;
		targetPolygon.v3 = this->postData[targetQuadric + 1].vPosition;
	}

	// return the target poly
	return targetPolygon;
}



/* -- TRANSLATE POSTINGS -----------------------------------------------------------------------------------------------------------------------------------------------*/
void Terrain::TranslatePostings(void)
{
	int indexCounter = 0;

	for(int X=0; X<this->mapSize; X+=this->stepSize)
		for(int Z=0; Z<this->mapSize; Z+=this->stepSize)
		{
			this->postData[indexCounter].vPosition.SetAll((float)X * TERRAIN_SCALING, 
														  (float)this->GetPixelHeightAt(X, Z) * TERRAIN_HEIGHT_SCALE * TERRAIN_SCALING,
														  (float)Z * TERRAIN_SCALING);

			++indexCounter;
		}
}



/* -- GENERATE NORMALS -------------------------------------------------------------------------------------------------------------------------------------------------*/
void Terrain::GenerateNormals(void)
{
	// flags to indicate special cases
	bool isFirstRow = true;
	bool isFinalRow = false;
	bool isFirstCol = true;
	bool isFinalCol = false;

	for(int row=0; row<this->postsPerSide; ++row)
	{
		// set flag to indicate we are past the first row
		if(row > 0)	isFirstRow = false;

		// set flag to indicate we are on the final row
		if(row == this->postsPerSide - 1) isFinalRow = true;

		for(int col=0; col<this->postsPerSide; ++col)
		{
			// calculate the index we are talking about
			int postingIndex = row * this->postsPerSide + col;

			// initialise the target normal
			this->postData[postingIndex].vNormal.Zero();

			// set flag to indicate if we are on the first col
			if(col == 0)
				isFirstCol = true;
			else
				isFirstCol = false;

			// set flag to indicate if we are on the last col
			if(col == this->postsPerSide - 1)
				isFinalCol = true;
			else
				isFinalCol = false;
			

			if(isFirstCol)
			{
				// x 0 0 0  - we are an 'x'
				// x 0 0 0
				// x 0 0 0
				// x 0 0 0
				
				if(isFirstRow)
				{
					// x 0 0 0  - we are 'x'
					// 0 0 0 0
					// 0 0 0 0
					// 0 0 0 0

					// get neighbouring posts
					Vector3 rightPost		= this->postData[postingIndex+1].vPosition;
					Vector3 downPost		= this->postData[postingIndex+this->postsPerSide].vPosition;

					// make neighbours relative to target post (conversion to polar coordinates)
					rightPost		-= this->postData[postingIndex].vPosition;
					downPost		-= this->postData[postingIndex].vPosition;

					// right-down normal (1/4 weight)
					this->postData[postingIndex].vNormal += 0.25f * CrossProduct(rightPost, downPost);
				}
				else if(isFinalRow)
				{
					// 0 0 0 0  - we are 'x'
					// 0 0 0 0
					// 0 0 0 0
					// x 0 0 0

					// get neighbouring posts
					Vector3 topPost			= this->postData[postingIndex-this->postsPerSide].vPosition;
					Vector3 topRightPost	= this->postData[postingIndex-this->postsPerSide+1].vPosition;
					Vector3 rightPost		= this->postData[postingIndex+1].vPosition;

					// make neighbours relative to target post (conversion to polar coordinates)
					topPost			-= this->postData[postingIndex].vPosition;
					topRightPost	-= this->postData[postingIndex].vPosition;
					rightPost		-= this->postData[postingIndex].vPosition;

					// top-top-right normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(topPost, topRightPost);

					// top-right-right normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(topRightPost, rightPost);
				}
				else
				{
					// 0 0 0 0  - we are an 'x'
					// x 0 0 0
					// x 0 0 0
					// 0 0 0 0

					// get neighbouring posts
					Vector3 topPost			= this->postData[postingIndex-this->postsPerSide].vPosition;
					Vector3 topRightPost	= this->postData[postingIndex-this->postsPerSide+1].vPosition;
					Vector3 rightPost		= this->postData[postingIndex+1].vPosition;
					Vector3 downPost		= this->postData[postingIndex+this->postsPerSide].vPosition;

					// make neighbours relative to target post (conversion to polar coordinates)
					topPost			-= this->postData[postingIndex].vPosition;
					topRightPost	-= this->postData[postingIndex].vPosition;
					rightPost		-= this->postData[postingIndex].vPosition;
					downPost		-= this->postData[postingIndex].vPosition;

					// top-top-right normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(topPost, topRightPost);

					// top-right-right normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(topRightPost, rightPost);

					// right-down normal (1/4 weight)
					this->postData[postingIndex].vNormal += 0.25f * CrossProduct(rightPost, downPost);
				}
			}
			else if(isFinalCol)
			{
				// 0 0 0 x  - we are an 'x'
				// 0 0 0 x
				// 0 0 0 x
				// 0 0 0 x

				if(isFirstRow)
				{
					// 0 0 0 x  - we are 'x'
					// 0 0 0 0
					// 0 0 0 0
					// 0 0 0 0

					// get neighbouring posts
					Vector3 leftPost		= this->postData[postingIndex-1].vPosition;
					Vector3 downPost		= this->postData[postingIndex+this->postsPerSide].vPosition;
					Vector3 downLeftPost	= this->postData[postingIndex+this->postsPerSide-1].vPosition;

					// make neighbours relative to target post (conversion to polar coordinates)
					leftPost		-= this->postData[postingIndex].vPosition;
					downPost		-= this->postData[postingIndex].vPosition;
					downLeftPost	-= this->postData[postingIndex].vPosition;

					// down-down-left normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(downPost, downLeftPost);

					// down-left-left normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(downLeftPost, leftPost);
				}
				else if(isFinalRow)
				{
					// 0 0 0 0  - we are 'x'
					// 0 0 0 0
					// 0 0 0 0
					// 0 0 0 x

					// get neighbouring posts
					Vector3 leftPost		= this->postData[postingIndex-1].vPosition;
					Vector3 topPost			= this->postData[postingIndex-this->postsPerSide].vPosition;

					// make neighbours relative to target post (conversion to polar coordinates)
					leftPost		-= this->postData[postingIndex].vPosition;
					topPost			-= this->postData[postingIndex].vPosition;

					// top-left normal (1/4 weight)
					this->postData[postingIndex].vNormal += 0.25f * CrossProduct(leftPost, topPost);
				}
				else
				{
					// 0 0 0 0  - we are an 'x'
					// 0 0 0 x
					// 0 0 0 x
					// 0 0 0 0

					// get neighbouring posts
					Vector3 leftPost		= this->postData[postingIndex-1].vPosition;
					Vector3 topPost			= this->postData[postingIndex-this->postsPerSide].vPosition;
					Vector3 downPost		= this->postData[postingIndex+this->postsPerSide].vPosition;
					Vector3 downLeftPost	= this->postData[postingIndex+this->postsPerSide-1].vPosition;

					// make neighbours relative to target post (conversion to polar coordinates)
					leftPost		-= this->postData[postingIndex].vPosition;
					topPost			-= this->postData[postingIndex].vPosition;
					downPost		-= this->postData[postingIndex].vPosition;
					downLeftPost	-= this->postData[postingIndex].vPosition;

					// top-left normal (1/4 weight)
					this->postData[postingIndex].vNormal += 0.25f * CrossProduct(leftPost, topPost);

					// down-down-left normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(downPost, downLeftPost);

					// down-left-left normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(downLeftPost, leftPost);
				}
			}
			else
			{
				// 0 x x 0  - we are an 'x'
				// 0 x x 0
				// 0 x x 0
				// 0 x x 0

				if(isFirstRow)
				{
					// 0 x x 0  - we are an 'x'
					// 0 0 0 0
					// 0 0 0 0
					// 0 0 0 0

					// get neighbouring posts
					Vector3 leftPost		= this->postData[postingIndex-1].vPosition;
					Vector3 rightPost		= this->postData[postingIndex+1].vPosition;
					Vector3 downPost		= this->postData[postingIndex+this->postsPerSide].vPosition;
					Vector3 downLeftPost	= this->postData[postingIndex+this->postsPerSide-1].vPosition;

					// make neighbours relative to target post (conversion to polar coordinates)
					leftPost		-= this->postData[postingIndex].vPosition;
					rightPost		-= this->postData[postingIndex].vPosition;
					downPost		-= this->postData[postingIndex].vPosition;
					downLeftPost	-= this->postData[postingIndex].vPosition;

					// right-down normal (1/4 weight)
					this->postData[postingIndex].vNormal += 0.25f * CrossProduct(rightPost, downPost);

					// down-down-left normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(downPost, downLeftPost);

					// down-left-left normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(downLeftPost, leftPost);
				}
				else if(isFinalRow)
				{
					// 0 0 0 0  - we are an 'x'
					// 0 0 0 0
					// 0 0 0 0
					// 0 x x 0

					// get neighbouring posts
					Vector3 leftPost		= this->postData[postingIndex-1].vPosition;
					Vector3 topPost			= this->postData[postingIndex-this->postsPerSide].vPosition;
					Vector3 topRightPost	= this->postData[postingIndex-this->postsPerSide+1].vPosition;
					Vector3 rightPost		= this->postData[postingIndex+1].vPosition;
					
					// make neighbours relative to target post (conversion to polar coordinates)
					leftPost		-= this->postData[postingIndex].vPosition;
					topPost			-= this->postData[postingIndex].vPosition;
					topRightPost	-= this->postData[postingIndex].vPosition;
					rightPost		-= this->postData[postingIndex].vPosition;

					// top-left normal (1/4 weight)
					this->postData[postingIndex].vNormal += 0.25f * CrossProduct(leftPost, topPost);

					// top-top-right normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(topPost, topRightPost);

					// top-right-right normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(topRightPost, rightPost);
				}
				else
				{
					// 0 0 0 0  - we are an 'x'
					// 0 x x 0
					// 0 x x 0
					// 0 0 0 0

					// get neighbouring posts
					Vector3 leftPost		= this->postData[postingIndex-1].vPosition;
					Vector3 topPost			= this->postData[postingIndex-this->postsPerSide].vPosition;
					Vector3 topRightPost	= this->postData[postingIndex-this->postsPerSide+1].vPosition;
					Vector3 rightPost		= this->postData[postingIndex+1].vPosition;
					Vector3 downPost		= this->postData[postingIndex+this->postsPerSide].vPosition;
					Vector3 downLeftPost	= this->postData[postingIndex+this->postsPerSide-1].vPosition;

					// make neighbours relative to target post (conversion to polar coordinates)
					leftPost		-= this->postData[postingIndex].vPosition;
					topPost			-= this->postData[postingIndex].vPosition;
					topRightPost	-= this->postData[postingIndex].vPosition;
					rightPost		-= this->postData[postingIndex].vPosition;
					downPost		-= this->postData[postingIndex].vPosition;
					downLeftPost	-= this->postData[postingIndex].vPosition;

					// top-left normal (1/4 weight)
					this->postData[postingIndex].vNormal += 0.25f * CrossProduct(leftPost, topPost);

					// top-top-right normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(topPost, topRightPost);

					// top-right-right normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(topRightPost, rightPost);

					// right-down normal (1/4 weight)
					this->postData[postingIndex].vNormal += 0.25f * CrossProduct(rightPost, downPost);

					// down-down-left normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(downPost, downLeftPost);

					// down-left-left normal (1/8 weight)
					this->postData[postingIndex].vNormal += 0.125f * CrossProduct(downLeftPost, leftPost);
				}
			}

			// finally, normalise the normal
			this->postData[postingIndex].vNormal.Normalise();
		}
	}
}



/* -- BUILD DISPLAY LIST -----------------------------------------------------------------------------------------------------------------------------------------------*/
void Terrain::BuildDisplayList(void)
{
	// create the display list for the terrain
	this->displayListReference = glGenLists(1);

	// begin the display list compilation
	glNewList(this->displayListReference, GL_COMPILE);

	for(int row=0; row<this->postsPerSide-1; ++row)
	{
		for(int col=0; col<this->postsPerSide-1; ++col)
		{
			float texCoordS   = ((float)col / (float)this->postsPerSide) * this->textureWrap;
			float texCoordT   = ((float)row / (float)this->postsPerSide) * this->textureWrap;
			float texCoordSP1 = ((float)(col+1) / (float)this->postsPerSide) * this->textureWrap;
			float texCoordTP1 = ((float)(row+1) / (float)this->postsPerSide) * this->textureWrap;

			// calculate the index we are talking about
			int postingIndex = row * this->postsPerSide + col;

			/*
			  NOTE: order of vertex plotting to maintain winding order
			  using GL_TRIANGLE_STRIP primitive:

			   1----------- 2,4
						  /|
					     / |
				polyA   /  |
					   /   |
				      /    |
				     /     |
				    /      |
				   /       |
			      /        |
			     /  polyB  |
			    /          |
			   3		   5
			*/
			glBegin(GL_TRIANGLE_STRIP);

				// Vertex 1
				glNormal3f(this->postData[postingIndex].vNormal.x,
						   this->postData[postingIndex].vNormal.y,
						   this->postData[postingIndex].vNormal.z);

				glTexCoord2f(texCoordS, texCoordT);

				glVertex3i((int)this->postData[postingIndex].vPosition.x,
						   (int)this->postData[postingIndex].vPosition.y,
						   (int)this->postData[postingIndex].vPosition.z);

				// Vertex 2
				glNormal3f(this->postData[postingIndex+1].vNormal.x,
						   this->postData[postingIndex+1].vNormal.y,
						   this->postData[postingIndex+1].vNormal.z);

				glTexCoord2f(texCoordSP1, texCoordT);

				glVertex3i((int)this->postData[postingIndex+1].vPosition.x,
						   (int)this->postData[postingIndex+1].vPosition.y,
						   (int)this->postData[postingIndex+1].vPosition.z);

				// Vertex 3
				glNormal3f(this->postData[postingIndex+this->postsPerSide].vNormal.x,
						   this->postData[postingIndex+this->postsPerSide].vNormal.y,
						   this->postData[postingIndex+this->postsPerSide].vNormal.z);

				glTexCoord2f(texCoordS, texCoordTP1);

				glVertex3i((int)this->postData[postingIndex+this->postsPerSide].vPosition.x,
						   (int)this->postData[postingIndex+this->postsPerSide].vPosition.y,
						   (int)this->postData[postingIndex+this->postsPerSide].vPosition.z);

				// Vertex 4
				glNormal3f(this->postData[postingIndex+1].vNormal.x,
						   this->postData[postingIndex+1].vNormal.y,
						   this->postData[postingIndex+1].vNormal.z);

				glTexCoord2f(texCoordSP1, texCoordT);

				glVertex3i((int)this->postData[postingIndex+1].vPosition.x,
						   (int)this->postData[postingIndex+1].vPosition.y,
						   (int)this->postData[postingIndex+1].vPosition.z);

				// Vertex 5
				glNormal3f(this->postData[postingIndex+this->postsPerSide+1].vNormal.x,
						   this->postData[postingIndex+this->postsPerSide+1].vNormal.y,
						   this->postData[postingIndex+this->postsPerSide+1].vNormal.z);

				glTexCoord2f(texCoordSP1, texCoordTP1);

				glVertex3i((int)this->postData[postingIndex+this->postsPerSide+1].vPosition.x,
						   (int)this->postData[postingIndex+this->postsPerSide+1].vPosition.y,
						   (int)this->postData[postingIndex+this->postsPerSide+1].vPosition.z);

			glEnd();
		}
	}

	// end the display list compilation
	glEndList();
}