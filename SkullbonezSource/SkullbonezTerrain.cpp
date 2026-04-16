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
#include "SkullbonezTerrain.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;

/* -- CONSTRUCTOR -----------------------------------------------------------------*/
Terrain::Terrain( const char* sFileName,
                  int iMapSize,
                  int iStepSize,
                  int iTextureWrap )
{
    this->mapSize = iMapSize;
    this->stepSize = iStepSize;
    this->textureWrap = iTextureWrap;

    this->terrainSizeWorldCoords = ( ( this->mapSize - this->stepSize ) /
                                     this->stepSize ) *
                                   this->stepSize;

    this->postsPerSide = this->mapSize / this->stepSize;

    this->LoadTerrainData( sFileName );
    this->BuildTerrain();
    this->BuildMesh();

    // Load the shader
    this->terrainShader = std::make_unique<Shader>(
        "SkullbonezData/shaders/lit_textured.vert",
        "SkullbonezData/shaders/lit_textured.frag" );

    // height map no longer needed after build
    this->terrainData.clear();
    this->terrainData.shrink_to_fit();
}

/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
Terrain::~Terrain( void )
{
    // Mesh and Shader cleaned up by unique_ptr
}

/* -- BUILD TERRAIN ---------------------------------------------------------------*/
void Terrain::BuildTerrain( void )
{
    int terrainPostCount = ( this->mapSize / this->stepSize ) *
                           ( this->mapSize / this->stepSize );

    this->postData.resize( terrainPostCount );

    this->TranslatePostings();
    this->GenerateNormals();
}

/* -- GET PIXEL HEIGHT AT ---------------------------------------------------------*/
int Terrain::GetPixelHeightAt( int xCoord, int yCoord )
{
    return this->terrainData[xCoord + yCoord * this->mapSize];
}

/* -- LOAD TERRAIN DATA -----------------------------------------------------------*/
void Terrain::LoadTerrainData( const char* sFileName )
{
    FILE* pRawFile = nullptr;
    fopen_s( &pRawFile, sFileName, "rb" );

    if ( !pRawFile )
    {
        throw std::runtime_error( "Height map file not found.  (Terrain::LoadTerrain)" );
    }

    this->terrainData.resize( this->mapSize * this->mapSize );

    fread( this->terrainData.data(), 1, this->terrainData.size(), pRawFile );

    if ( ferror( pRawFile ) )
    {
        fclose( pRawFile );
        this->terrainData.clear();
        throw std::runtime_error( "Failed to read height map.  (Terrain::LoadTerrain)" );
    }

    fclose( pRawFile );
}

/* -- RENDER ----------------------------------------------------------------------*/
void Terrain::Render( const Matrix4& view, const Matrix4& projection,
                      const float* lightPosition )
{
    this->terrainShader->Use();

    // Model matrix is identity (terrain vertices are in world space)
    Matrix4 model;
    this->terrainShader->SetMat4( "uModel", model );
    this->terrainShader->SetMat4( "uView", view );
    this->terrainShader->SetMat4( "uProjection", projection );

    // Transform light position to view space (matches FFP behavior)
    float lx = view.m[0] * lightPosition[0] + view.m[4] * lightPosition[1] + view.m[8] * lightPosition[2] + view.m[12] * lightPosition[3];
    float ly = view.m[1] * lightPosition[0] + view.m[5] * lightPosition[1] + view.m[9] * lightPosition[2] + view.m[13] * lightPosition[3];
    float lz = view.m[2] * lightPosition[0] + view.m[6] * lightPosition[1] + view.m[10] * lightPosition[2] + view.m[14] * lightPosition[3];
    float lw = lightPosition[3];
    this->terrainShader->SetVec4( "uLightPosition", lx, ly, lz, lw );

    // Light properties (matching FFP StateSetup values)
    this->terrainShader->SetVec4( "uLightAmbient", 1.0f, 0.5f, 0.5f, 1.0f );
    this->terrainShader->SetVec4( "uLightDiffuse", 1.0f, 0.5f, 0.5f, 1.0f );

    // Default GL material properties
    this->terrainShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
    this->terrainShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );

    // Texture sampler
    this->terrainShader->SetInt( "uTexture", 0 );

    this->terrainMesh->Draw();

    // Restore FFP for other subsystems
    glUseProgram( 0 );
}

/* -- GET TERRAIN HEIGHT AT -------------------------------------------------------*/
float Terrain::GetTerrainHeightAt( float xPosition,
                                   float zPosition,
                                   bool isFluidMin )
{
    float terrainHeight =
        ( GeometricMath::GetHeightFromPlane( this->LocatePolygon( xPosition,
                                                                  zPosition ),
                                             xPosition,
                                             zPosition ) );

    if ( isFluidMin )
    {
        return ( terrainHeight < FLUID_HEIGHT ) ? FLUID_HEIGHT : terrainHeight;
    }
    else
    {
        return terrainHeight;
    }
}

/* -- GET TERRAIN NORMAL AT -------------------------------------------------------*/
Vector3 Terrain::GetTerrainNormalAt( float xPosition, float zPosition )
{
    Triangle tri = this->LocatePolygon( xPosition, zPosition );
    Vector3 edge1 = tri.v2 - tri.v1;
    Vector3 edge2 = tri.v3 - tri.v1;
    Vector3 normal = Vector::CrossProduct( edge1, edge2 );
    normal.Normalise();

    // ensure the normal points upward
    if ( normal.y < 0.0f )
    {
        normal = normal * -1.0f;
    }

    return normal;
}

/* -- IS IN BOUNDS ----------------------------------------------------------------*/
bool Terrain::IsInBounds( float xPosition, float zPosition )
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

    return ( ( xPosition >= 0.0f ) &&
             ( zPosition >= 0.0f ) &&
             ( xPosition < this->terrainSizeWorldCoords * TERRAIN_SCALING ) &&
             ( zPosition < this->terrainSizeWorldCoords * TERRAIN_SCALING ) );
}

/* -- GET BOUNDS XZ ---------------------------------------------------------------*/
XZBounds Terrain::GetXZBounds( void )
{
    XZBounds bounds;

    bounds.xMin = 0.0f;
    bounds.zMin = 0.0f;
    bounds.xMax = this->terrainSizeWorldCoords * TERRAIN_SCALING;
    bounds.zMax = this->terrainSizeWorldCoords * TERRAIN_SCALING;

    return bounds;
}

/* -- LOCATE POLYGON ---------------------------------------------------------------------------------------------------------------------------------------------------*/
Triangle Terrain::LocatePolygon( float xPosition, float zPosition )
{
    // check to ensure specified co-ordinates are inside the terrain map bounds
    if ( !this->IsInBounds( xPosition, zPosition ) )
    {
        throw std::runtime_error( "Specified co-ordinates are out of terrain bounds.  (Terrain::GetTerrainHeightAt)" );
    }

    // NOTE:  X and Z params are switched in this method to account for world
    // co-ordinate space find which quadric we are in (treat terrain as orthagonal
    // XZ projection to locate the quadric)
    int xPosting = (int)floorf( zPosition / ( this->stepSize * TERRAIN_SCALING ) );
    int zPosting = (int)floorf( xPosition / ( this->stepSize * TERRAIN_SCALING ) );

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
    float xRelativePosition = -( scaledStepSize - ( fmodf( zPosition, scaledStepSize ) ) );
    float zRelativePosition = fmodf( xPosition, scaledStepSize );

    // vars to help safely determine the gradient of the vector expressed by
    // xRelativePosition and zRelativePosition
    float gradient = 0.0f;
    bool isGradientInfinite = false;

    // test to see if rise is infinitely greater than run
    if ( !zRelativePosition )
    {
        isGradientInfinite = true;
    }

    // avoid a division by zero
    if ( !isGradientInfinite )
    {
        // calculate the gradient of the vector relative from the target quadric
        gradient = xRelativePosition / zRelativePosition;
    }

    // triangle structure for the target polygon
    Triangle targetPolygon;
    ZeroMemory( &targetPolygon, sizeof( targetPolygon ) );

    /*
        The following test checks to see if triangle A or B has been hit

        |\---|						 \										  |
        | \ A|						  \  <- grad = -1   ------- <- grad = 0   | <- grad = infinite
        |B \ |						   \									  |
        |---\|<- object space origin    \									  |

        (NOTE: The gradient of the cross section is equal to -1)
    */
    if ( isGradientInfinite || gradient < -1.0f )
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
void Terrain::TranslatePostings( void )
{
    int indexCounter = 0;

    for ( int X = 0; X < this->mapSize; X += this->stepSize )
    {
        for ( int Z = 0; Z < this->mapSize; Z += this->stepSize )
        {
            this->postData[indexCounter].vPosition.SetAll( (float)X * TERRAIN_SCALING,
                                                           (float)this->GetPixelHeightAt( X, Z ) * TERRAIN_HEIGHT_SCALE * TERRAIN_SCALING,
                                                           (float)Z * TERRAIN_SCALING );

            ++indexCounter;
        }
    }
}

/* -- GENERATE NORMALS -------------------------------------------------------------------------------------------------------------------------------------------------*/
void Terrain::GenerateNormals( void )
{
    // flags to indicate special cases
    bool isFirstRow = true;
    bool isFinalRow = false;
    bool isFirstCol = true;
    bool isFinalCol = false;

    for ( int row = 0; row < this->postsPerSide; ++row )
    {
        // set flag to indicate we are past the first row
        if ( row > 0 )
        {
            isFirstRow = false;
        }

        // set flag to indicate we are on the final row
        if ( row == this->postsPerSide - 1 )
        {
            isFinalRow = true;
        }

        for ( int col = 0; col < this->postsPerSide; ++col )
        {
            // calculate the index we are talking about
            int postingIndex = row * this->postsPerSide + col;

            // initialise the target normal
            this->postData[postingIndex].vNormal.Zero();

            // set flag to indicate if we are on the first col
            if ( col == 0 )
            {
                isFirstCol = true;
            }
            else
            {
                isFirstCol = false;
            }

            // set flag to indicate if we are on the last col
            if ( col == this->postsPerSide - 1 )
            {
                isFinalCol = true;
            }
            else
            {
                isFinalCol = false;
            }

            if ( isFirstCol )
            {
                // x 0 0 0  - we are an 'x'
                // x 0 0 0
                // x 0 0 0
                // x 0 0 0

                if ( isFirstRow )
                {
                    // x 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 rightPost = this->postData[postingIndex + 1].vPosition;
                    Vector3 downPost = this->postData[postingIndex + this->postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    rightPost -= this->postData[postingIndex].vPosition;
                    downPost -= this->postData[postingIndex].vPosition;

                    // right-down normal (1/4 weight)
                    this->postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // x 0 0 0

                    // get neighbouring posts
                    Vector3 topPost = this->postData[postingIndex - this->postsPerSide].vPosition;
                    Vector3 topRightPost = this->postData[postingIndex - this->postsPerSide + 1].vPosition;
                    Vector3 rightPost = this->postData[postingIndex + 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    topPost -= this->postData[postingIndex].vPosition;
                    topRightPost -= this->postData[postingIndex].vPosition;
                    rightPost -= this->postData[postingIndex].vPosition;

                    // top-top-right normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // x 0 0 0
                    // x 0 0 0
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 topPost = this->postData[postingIndex - this->postsPerSide].vPosition;
                    Vector3 topRightPost = this->postData[postingIndex - this->postsPerSide + 1].vPosition;
                    Vector3 rightPost = this->postData[postingIndex + 1].vPosition;
                    Vector3 downPost = this->postData[postingIndex + this->postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    topPost -= this->postData[postingIndex].vPosition;
                    topRightPost -= this->postData[postingIndex].vPosition;
                    rightPost -= this->postData[postingIndex].vPosition;
                    downPost -= this->postData[postingIndex].vPosition;

                    // top-top-right normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );

                    // right-down normal (1/4 weight)
                    this->postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );
                }
            }
            else if ( isFinalCol )
            {
                // 0 0 0 x  - we are an 'x'
                // 0 0 0 x
                // 0 0 0 x
                // 0 0 0 x

                if ( isFirstRow )
                {
                    // 0 0 0 x  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 leftPost = this->postData[postingIndex - 1].vPosition;
                    Vector3 downPost = this->postData[postingIndex + this->postsPerSide].vPosition;
                    Vector3 downLeftPost = this->postData[postingIndex + this->postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= this->postData[postingIndex].vPosition;
                    downPost -= this->postData[postingIndex].vPosition;
                    downLeftPost -= this->postData[postingIndex].vPosition;

                    // down-down-left normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 x

                    // get neighbouring posts
                    Vector3 leftPost = this->postData[postingIndex - 1].vPosition;
                    Vector3 topPost = this->postData[postingIndex - this->postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= this->postData[postingIndex].vPosition;
                    topPost -= this->postData[postingIndex].vPosition;

                    // top-left normal (1/4 weight)
                    this->postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 0 0 x
                    // 0 0 0 x
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 leftPost = this->postData[postingIndex - 1].vPosition;
                    Vector3 topPost = this->postData[postingIndex - this->postsPerSide].vPosition;
                    Vector3 downPost = this->postData[postingIndex + this->postsPerSide].vPosition;
                    Vector3 downLeftPost = this->postData[postingIndex + this->postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= this->postData[postingIndex].vPosition;
                    topPost -= this->postData[postingIndex].vPosition;
                    downPost -= this->postData[postingIndex].vPosition;
                    downLeftPost -= this->postData[postingIndex].vPosition;

                    // top-left normal (1/4 weight)
                    this->postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // down-down-left normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
            }
            else
            {
                // 0 x x 0  - we are an 'x'
                // 0 x x 0
                // 0 x x 0
                // 0 x x 0

                if ( isFirstRow )
                {
                    // 0 x x 0  - we are an 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 leftPost = this->postData[postingIndex - 1].vPosition;
                    Vector3 rightPost = this->postData[postingIndex + 1].vPosition;
                    Vector3 downPost = this->postData[postingIndex + this->postsPerSide].vPosition;
                    Vector3 downLeftPost = this->postData[postingIndex + this->postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= this->postData[postingIndex].vPosition;
                    rightPost -= this->postData[postingIndex].vPosition;
                    downPost -= this->postData[postingIndex].vPosition;
                    downLeftPost -= this->postData[postingIndex].vPosition;

                    // right-down normal (1/4 weight)
                    this->postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );

                    // down-down-left normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 x x 0

                    // get neighbouring posts
                    Vector3 leftPost = this->postData[postingIndex - 1].vPosition;
                    Vector3 topPost = this->postData[postingIndex - this->postsPerSide].vPosition;
                    Vector3 topRightPost = this->postData[postingIndex - this->postsPerSide + 1].vPosition;
                    Vector3 rightPost = this->postData[postingIndex + 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= this->postData[postingIndex].vPosition;
                    topPost -= this->postData[postingIndex].vPosition;
                    topRightPost -= this->postData[postingIndex].vPosition;
                    rightPost -= this->postData[postingIndex].vPosition;

                    // top-left normal (1/4 weight)
                    this->postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // top-top-right normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 x x 0
                    // 0 x x 0
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 leftPost = this->postData[postingIndex - 1].vPosition;
                    Vector3 topPost = this->postData[postingIndex - this->postsPerSide].vPosition;
                    Vector3 topRightPost = this->postData[postingIndex - this->postsPerSide + 1].vPosition;
                    Vector3 rightPost = this->postData[postingIndex + 1].vPosition;
                    Vector3 downPost = this->postData[postingIndex + this->postsPerSide].vPosition;
                    Vector3 downLeftPost = this->postData[postingIndex + this->postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= this->postData[postingIndex].vPosition;
                    topPost -= this->postData[postingIndex].vPosition;
                    topRightPost -= this->postData[postingIndex].vPosition;
                    rightPost -= this->postData[postingIndex].vPosition;
                    downPost -= this->postData[postingIndex].vPosition;
                    downLeftPost -= this->postData[postingIndex].vPosition;

                    // top-left normal (1/4 weight)
                    this->postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // top-top-right normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );

                    // right-down normal (1/4 weight)
                    this->postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );

                    // down-down-left normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left normal (1/8 weight)
                    this->postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
            }

            // finally, normalise the normal
            this->postData[postingIndex].vNormal.Normalise();
        }
    }
}

/* -- BUILD MESH ---------------------------------------------------------------------------------------------------------------------------------------------------*/
void Terrain::BuildMesh( void )
{
    // 2 triangles per quad, 6 vertices each, 8 floats per vertex (pos3 + normal3 + texcoord2)
    int quadsPerSide = this->postsPerSide - 1;
    int totalQuads = quadsPerSide * quadsPerSide;
    int totalVerts = totalQuads * 6;

    std::vector<float> vertexData;
    vertexData.reserve( static_cast<size_t>( totalVerts ) * 8 );

    for ( int row = 0; row < quadsPerSide; ++row )
    {
        for ( int col = 0; col < quadsPerSide; ++col )
        {
            float texCoordS = ( static_cast<float>( col ) / static_cast<float>( this->postsPerSide ) ) * this->textureWrap;
            float texCoordT = ( static_cast<float>( row ) / static_cast<float>( this->postsPerSide ) ) * this->textureWrap;
            float texCoordSP1 = ( static_cast<float>( col + 1 ) / static_cast<float>( this->postsPerSide ) ) * this->textureWrap;
            float texCoordTP1 = ( static_cast<float>( row + 1 ) / static_cast<float>( this->postsPerSide ) ) * this->textureWrap;

            int idx = row * this->postsPerSide + col;

            // Post references (matching glVertex3i truncation from original display list)
            const TerrainPost& p00 = this->postData[idx];
            const TerrainPost& p10 = this->postData[idx + 1];
            const TerrainPost& p01 = this->postData[idx + this->postsPerSide];
            const TerrainPost& p11 = this->postData[idx + this->postsPerSide + 1];

            // Helper lambda: push position (int-truncated) + normal + texcoord
            auto pushVertex = [&]( const TerrainPost& p, float s, float t )
            {
                vertexData.push_back( static_cast<float>( static_cast<int>( p.vPosition.x ) ) );
                vertexData.push_back( static_cast<float>( static_cast<int>( p.vPosition.y ) ) );
                vertexData.push_back( static_cast<float>( static_cast<int>( p.vPosition.z ) ) );
                vertexData.push_back( p.vNormal.x );
                vertexData.push_back( p.vNormal.y );
                vertexData.push_back( p.vNormal.z );
                vertexData.push_back( s );
                vertexData.push_back( t );
            };

            // Triangle 1 (upper-left): v1, v2, v3
            pushVertex( p00, texCoordS, texCoordT );
            pushVertex( p10, texCoordSP1, texCoordT );
            pushVertex( p01, texCoordS, texCoordTP1 );

            // Triangle 2 (lower-right): v3, v2, v5
            pushVertex( p01, texCoordS, texCoordTP1 );
            pushVertex( p10, texCoordSP1, texCoordT );
            pushVertex( p11, texCoordSP1, texCoordTP1 );
        }
    }

    this->terrainMesh = std::make_unique<Mesh>(
        vertexData.data(), totalVerts,
        true, // hasNormals
        true, // hasTexCoords
        GL_TRIANGLES );
}
