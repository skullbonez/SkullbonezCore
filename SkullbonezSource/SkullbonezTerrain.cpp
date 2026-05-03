// --- Includes ---
#include "SkullbonezTerrain.h"
#include "SkullbonezIRenderBackend.h"


// --- Usings ---
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;


Terrain::Terrain( const char* sFileName,
                  int iMapSize,
                  int iStepSize,
                  int iTextureWrap )
{
    m_mapSize = iMapSize;
    m_stepSize = iStepSize;
    m_textureWrap = iTextureWrap;

    m_terrainSizeWorldCoords = ( ( m_mapSize - m_stepSize ) /
                                 m_stepSize ) *
                               m_stepSize;

    m_postsPerSide = m_mapSize / m_stepSize;

    LoadTerrainData( sFileName );
    BuildTerrain();
    BuildMesh();

    // Load the m_shader
    m_terrainShader = Gfx().CreateShader(
        "SkullbonezData/shaders/lit_textured.vert",
        "SkullbonezData/shaders/lit_textured.frag" );

    m_terrainShader->Use();
    m_terrainShader->SetVec4( "uLightAmbient", 1.0f, 0.5f, 0.5f, 1.0f );
    m_terrainShader->SetVec4( "uLightDiffuse", 1.0f, 0.5f, 0.5f, 1.0f );
    m_terrainShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
    m_terrainShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );
    m_terrainShader->SetInt( "uTexture", 0 );

    // m_height map no longer needed after build
    m_terrainData.clear();
    m_terrainData.shrink_to_fit();
}


Terrain::~Terrain()
{
    // MeshGL and ShaderGL cleaned up by unique_ptr
}


void Terrain::BuildTerrain()
{
    int terrainPostCount = ( m_mapSize / m_stepSize ) *
                           ( m_mapSize / m_stepSize );

    m_postData.resize( terrainPostCount );

    TranslatePostings();
    GenerateNormals();
}


int Terrain::GetPixelHeightAt( int xCoord, int yCoord )
{
    return m_terrainData[xCoord + yCoord * m_mapSize];
}


void Terrain::LoadTerrainData( const char* sFileName )
{
    FILE* pRawFile = nullptr;
    fopen_s( &pRawFile, sFileName, "rb" );

    if ( !pRawFile )
    {
        throw std::runtime_error( "Height map file not found.  (Terrain::LoadTerrain)" );
    }

    m_terrainData.resize( m_mapSize * m_mapSize );

    fread( m_terrainData.data(), 1, m_terrainData.size(), pRawFile );

    if ( ferror( pRawFile ) )
    {
        fclose( pRawFile );
        m_terrainData.clear();
        throw std::runtime_error( "Failed to read m_height map.  (Terrain::LoadTerrain)" );
    }

    fclose( pRawFile );
}


void Terrain::Render( const Matrix4& view, const Matrix4& projection, const float* lightPosition )
{
    m_terrainShader->Use();

    // Model matrix is identity (m_terrain vertices are in world space)
    Matrix4 model;
    m_terrainShader->SetMat4( "uModel", model );
    m_terrainShader->SetMat4( "uView", view );
    m_terrainShader->SetMat4( "uProjection", projection );

    // Transform light position to view space
    float lx = view.m[0] * lightPosition[0] + view.m[4] * lightPosition[1] + view.m[8] * lightPosition[2] + view.m[12] * lightPosition[3];
    float ly = view.m[1] * lightPosition[0] + view.m[5] * lightPosition[1] + view.m[9] * lightPosition[2] + view.m[13] * lightPosition[3];
    float lz = view.m[2] * lightPosition[0] + view.m[6] * lightPosition[1] + view.m[10] * lightPosition[2] + view.m[14] * lightPosition[3];
    float lw = lightPosition[3];
    m_terrainShader->SetVec4( "uLightPosition", lx, ly, lz, lw );

    m_terrainMesh->Draw();
}


float Terrain::GetTerrainHeightAt( float xPosition,
                                   float zPosition,
                                   bool isFluidMin )
{
    float terrainHeight =
        ( GeometricMath::GetHeightFromPlane( LocatePolygon( xPosition,
                                                            zPosition ),
                                             xPosition,
                                             zPosition ) );

    if ( isFluidMin )
    {
        return ( terrainHeight < Cfg().fluidHeight ) ? Cfg().fluidHeight : terrainHeight;
    }
    else
    {
        return terrainHeight;
    }
}


Vector3 Terrain::GetTerrainNormalAt( float xPosition, float zPosition )
{
    Triangle tri = LocatePolygon( xPosition, zPosition );
    Vector3 edge1 = tri.v2 - tri.v1;
    Vector3 edge2 = tri.v3 - tri.v1;
    Vector3 m_normal = Vector::CrossProduct( edge1, edge2 );
    m_normal.Normalise();

    // ensure the m_normal points upward
    if ( m_normal.y < 0.0f )
    {
        m_normal = m_normal * -1.0f;
    }

    return m_normal;
}


bool Terrain::IsInBounds( float xPosition, float zPosition )
{
    /*
        Justification for not allowing coordinates to the absolute outer bound:
        -----------------------------------------------------------------------
        It is arguable that a point would be in bounds of the m_terrain if it was
        equal to (m_terrainSizeWorldCoords * Cfg().terrainScale).  This may be true on
        a physical level, however, this can cause major problems for the
        Terrain::LocatePolygon method as it uses:
        floor(xPosition/(m_stepSize * Cfg().terrainScale)) and
        floor(zPosition/(m_stepSize * Cfg().terrainScale))
        to determine which m_terrain quadric the point is in - you can only imagine
        what happens when the xPosition or the zPosition are equal to the upper
        bound of the m_terrain - the quadric is set to something that does not exist
        and all hell breaks loose (i.e. hours of debugging).

        So, who cares if you cant move to the absolute outer bound of the m_terrain,
        just move to the abolsute outer bound minus the smallest possible fraction
        of a float possible instead.
    */

    return ( ( xPosition >= 0.0f ) &&
             ( zPosition >= 0.0f ) &&
             ( xPosition < m_terrainSizeWorldCoords * Cfg().terrainScale ) &&
             ( zPosition < m_terrainSizeWorldCoords * Cfg().terrainScale ) );
}


XZBounds Terrain::GetXZBounds()
{
    XZBounds bounds;

    bounds.m_xMin = 0.0f;
    bounds.m_zMin = 0.0f;
    bounds.m_xMax = m_terrainSizeWorldCoords * Cfg().terrainScale;
    bounds.m_zMax = m_terrainSizeWorldCoords * Cfg().terrainScale;

    return bounds;
}


Triangle Terrain::LocatePolygon( float xPosition, float zPosition )
{
    // check to ensure specified co-ordinates are inside the m_terrain map bounds
    if ( !IsInBounds( xPosition, zPosition ) )
    {
        throw std::runtime_error( "Specified co-ordinates are out of m_terrain bounds.  (Terrain::GetTerrainHeightAt)" );
    }

    // NOTE:  X and Z params are switched in this method to account for world
    // co-ordinate space find which quadric we are in (treat m_terrain as orthagonal
    // XZ projection to locate the quadric)
    int xPosting = static_cast<int>( floorf( zPosition / ( m_stepSize * Cfg().terrainScale ) ) );
    int zPosting = static_cast<int>( floorf( xPosition / ( m_stepSize * Cfg().terrainScale ) ) );

    // calculate the BOTTOM RIGHT post of the quadric hit - we will call this the
    // 'target quadric'
    int targetQuadric = zPosting * m_postsPerSide +
                        xPosting +
                        m_postsPerSide;

    float scaledStepSize = m_stepSize * Cfg().terrainScale;

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
        targetPolygon.v1 = m_postData[targetQuadric].vPosition;
        targetPolygon.v2 = m_postData[targetQuadric - m_postsPerSide].vPosition;
        targetPolygon.v3 = m_postData[targetQuadric - m_postsPerSide + 1].vPosition;
    }
    else
    {
        // TRIANGLE B
        targetPolygon.v1 = m_postData[targetQuadric].vPosition;
        targetPolygon.v2 = m_postData[targetQuadric - m_postsPerSide + 1].vPosition;
        targetPolygon.v3 = m_postData[targetQuadric + 1].vPosition;
    }

    // return the target poly
    return targetPolygon;
}


void Terrain::TranslatePostings()
{
    int indexCounter = 0;

    for ( int X = 0; X < m_mapSize; X += m_stepSize )
    {
        for ( int Z = 0; Z < m_mapSize; Z += m_stepSize )
        {
            m_postData[indexCounter].vPosition.SetAll( static_cast<float>( X ) * Cfg().terrainScale,
                                                       static_cast<float>( GetPixelHeightAt( X, Z ) ) * Cfg().terrainHeightScale * Cfg().terrainScale,
                                                       static_cast<float>( Z ) * Cfg().terrainScale );

            ++indexCounter;
        }
    }
}


void Terrain::GenerateNormals()
{
    // flags to indicate special cases
    bool isFirstRow = true;
    bool isFinalRow = false;
    bool isFirstCol = true;
    bool isFinalCol = false;

    for ( int row = 0; row < m_postsPerSide; ++row )
    {
        // set flag to indicate we are past the first row
        if ( row > 0 )
        {
            isFirstRow = false;
        }

        // set flag to indicate we are on the final row
        if ( row == m_postsPerSide - 1 )
        {
            isFinalRow = true;
        }

        for ( int col = 0; col < m_postsPerSide; ++col )
        {
            // calculate the index we are talking about
            int postingIndex = row * m_postsPerSide + col;

            // initialise the target m_normal
            m_postData[postingIndex].vNormal.Zero();

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
            if ( col == m_postsPerSide - 1 )
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
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    rightPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;

                    // right-down m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // x 0 0 0

                    // get neighbouring posts
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 topRightPost = m_postData[postingIndex - m_postsPerSide + 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    topPost -= m_postData[postingIndex].vPosition;
                    topRightPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;

                    // top-top-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // x 0 0 0
                    // x 0 0 0
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 topRightPost = m_postData[postingIndex - m_postsPerSide + 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    topPost -= m_postData[postingIndex].vPosition;
                    topRightPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;

                    // top-top-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );

                    // right-down m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );
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
                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;
                    Vector3 downLeftPost = m_postData[postingIndex + m_postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;
                    downLeftPost -= m_postData[postingIndex].vPosition;

                    // down-down-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 x

                    // get neighbouring posts
                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    topPost -= m_postData[postingIndex].vPosition;

                    // top-left m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 0 0 x
                    // 0 0 0 x
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;
                    Vector3 downLeftPost = m_postData[postingIndex + m_postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    topPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;
                    downLeftPost -= m_postData[postingIndex].vPosition;

                    // top-left m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // down-down-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
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
                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;
                    Vector3 downLeftPost = m_postData[postingIndex + m_postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;
                    downLeftPost -= m_postData[postingIndex].vPosition;

                    // right-down m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );

                    // down-down-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 x x 0

                    // get neighbouring posts
                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 topRightPost = m_postData[postingIndex - m_postsPerSide + 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    topPost -= m_postData[postingIndex].vPosition;
                    topRightPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;

                    // top-left m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // top-top-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 x x 0
                    // 0 x x 0
                    // 0 0 0 0

                    // get neighbouring posts
                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 topRightPost = m_postData[postingIndex - m_postsPerSide + 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;
                    Vector3 downLeftPost = m_postData[postingIndex + m_postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    topPost -= m_postData[postingIndex].vPosition;
                    topRightPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;
                    downLeftPost -= m_postData[postingIndex].vPosition;

                    // top-left m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // top-top-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );

                    // right-down m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );

                    // down-down-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
            }

            // finally, normalise the m_normal
            m_postData[postingIndex].vNormal.Normalise();
        }
    }
}


void Terrain::BuildMesh()
{
    // 2 triangles per quad, 6 vertices each, 8 floats per vertex (pos3 + normal3 + texcoord2)
    int quadsPerSide = m_postsPerSide - 1;
    int totalQuads = quadsPerSide * quadsPerSide;
    int totalVerts = totalQuads * 6;

    std::vector<float> vertexData;
    vertexData.reserve( static_cast<size_t>( totalVerts ) * 8 );

    for ( int row = 0; row < quadsPerSide; ++row )
    {
        for ( int col = 0; col < quadsPerSide; ++col )
        {
            float texCoordS = ( static_cast<float>( col ) / static_cast<float>( m_postsPerSide ) ) * m_textureWrap;
            float texCoordT = ( static_cast<float>( row ) / static_cast<float>( m_postsPerSide ) ) * m_textureWrap;
            float texCoordSP1 = ( static_cast<float>( col + 1 ) / static_cast<float>( m_postsPerSide ) ) * m_textureWrap;
            float texCoordTP1 = ( static_cast<float>( row + 1 ) / static_cast<float>( m_postsPerSide ) ) * m_textureWrap;

            int idx = row * m_postsPerSide + col;

            // Post references (matching glVertex3i truncation from original display list)
            const TerrainPost& p00 = m_postData[idx];
            const TerrainPost& p10 = m_postData[idx + 1];
            const TerrainPost& p01 = m_postData[idx + m_postsPerSide];
            const TerrainPost& p11 = m_postData[idx + m_postsPerSide + 1];

            // Helper lambda: push m_position (int-truncated) + m_normal + texcoord
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

    m_terrainMesh = Gfx().CreateMesh(
        vertexData.data(),
        totalVerts,
        true, // hasNormals
        true  // hasTexCoords
    );
}
