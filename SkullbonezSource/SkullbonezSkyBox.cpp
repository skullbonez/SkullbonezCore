/*-----------------------------------------------------------------------------------
                                  THE SKULLBONEZ CORE
                                        _______
                                     .-"       "-.
                                    /             \
                                   /               \
                                   |   .--. .--.   |
                                   | )/   | |   \( |
                                   |/ \__/   \__/ \|
                                   /      /^\      \
                                   \__    '='    __/
                                     |\         /|
                                     |\'"VUUUV"'/|
                                     \ `"""""""` /
                                      `-._____.-'

                                 www.simoneschbach.com
-----------------------------------------------------------------------------------*/

/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezSkyBox.h"
#include <vector>

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Geometry;

/* -- SINGLETON INSTANCE INITIALISATION -------------------------------------------*/
SkyBox* SkyBox::pInstance = 0;

/* -- CONSTRUCTOR -----------------------------------------------------------------*/
SkyBox::SkyBox( int m_xMin,
                int m_xMax,
                int yMin,
                int yMax,
                int m_zMin,
                int m_zMax )
{
    m_boundaries.m_xMin = m_xMin;
    m_boundaries.m_xMax = m_xMax;
    m_boundaries.yMin = yMin;
    m_boundaries.yMax = yMax;
    m_boundaries.m_zMin = m_zMin;
    m_boundaries.m_zMax = m_zMax;
    m_textures = 0;
}

/* -- LOAD TEXTURES ---------------------------------------------------------------*/
void SkyBox::LoadTextures( void )
{
    m_textures = TextureCollection::Instance();
    const SkullbonezCore::Basics::SkullbonezConfig& cfg = Cfg();
    m_textures->CreateJpegTexture( cfg.skyLeft.c_str(), TEXTURE_SKY_LEFT );
    m_textures->CreateJpegTexture( cfg.skyRight.c_str(), TEXTURE_SKY_RIGHT );
    m_textures->CreateJpegTexture( cfg.skyFront.c_str(), TEXTURE_SKY_FRONT );
    m_textures->CreateJpegTexture( cfg.skyBack.c_str(), TEXTURE_SKY_BACK );
    m_textures->CreateJpegTexture( cfg.skyUp.c_str(), TEXTURE_SKY_UP );
    m_textures->CreateJpegTexture( cfg.skyDown.c_str(), TEXTURE_SKY_DOWN );
}

/* -- BUILD MESHES ----------------------------------------------------------------*/
void SkyBox::BuildMeshes( void )
{
    // Shorthand for boundary values with overflow
    const int overflow = Cfg().skyboxOverflow;
    float xn = (float)( m_boundaries.m_xMin - overflow );
    float xp = (float)( m_boundaries.m_xMax + overflow );
    float yn = (float)( m_boundaries.yMin - overflow );
    float yp = (float)( m_boundaries.yMax + overflow );
    float zn = (float)( m_boundaries.m_zMin - overflow );
    float zp = (float)( m_boundaries.m_zMax + overflow );
    float yMinF = (float)m_boundaries.yMin;
    float yMaxF = (float)m_boundaries.yMax;
    float xMinF = (float)m_boundaries.m_xMin;
    float xMaxF = (float)m_boundaries.m_xMax;
    float zMinF = (float)m_boundaries.m_zMin;
    float zMaxF = (float)m_boundaries.m_zMax;

    // Each face: 2 triangles = 6 vertices, 5 floats each (pos3 + tex2)
    // Face order: up, down, right(west), left(east), front, back

    // UP face (y = yMax)
    float up[] = {
        xn,
        yMaxF,
        zp,
        0,
        1,
        xn,
        yMaxF,
        zn,
        0,
        0,
        xp,
        yMaxF,
        zn,
        1,
        0,
        xn,
        yMaxF,
        zp,
        0,
        1,
        xp,
        yMaxF,
        zn,
        1,
        0,
        xp,
        yMaxF,
        zp,
        1,
        1,
    };

    // DOWN face (y = yMin)
    float down[] = {
        xp,
        yMinF,
        zp,
        1,
        0,
        xp,
        yMinF,
        zn,
        1,
        1,
        xn,
        yMinF,
        zn,
        0,
        1,
        xp,
        yMinF,
        zp,
        1,
        0,
        xn,
        yMinF,
        zn,
        0,
        1,
        xn,
        yMinF,
        zp,
        0,
        0,
    };

    // RIGHT/WEST face (x = m_xMin)
    float right[] = {
        xMinF,
        yn,
        zp,
        1,
        1,
        xMinF,
        yn,
        zn,
        0,
        1,
        xMinF,
        yp,
        zn,
        0,
        0,
        xMinF,
        yn,
        zp,
        1,
        1,
        xMinF,
        yp,
        zn,
        0,
        0,
        xMinF,
        yp,
        zp,
        1,
        0,
    };

    // LEFT/EAST face (x = m_xMax)
    float left[] = {
        xMaxF,
        yn,
        zn,
        1,
        1,
        xMaxF,
        yn,
        zp,
        0,
        1,
        xMaxF,
        yp,
        zp,
        0,
        0,
        xMaxF,
        yn,
        zn,
        1,
        1,
        xMaxF,
        yp,
        zp,
        0,
        0,
        xMaxF,
        yp,
        zn,
        1,
        0,
    };

    // FRONT face (z = m_zMax)
    float front[] = {
        xp,
        yn,
        zMaxF,
        1,
        1,
        xn,
        yn,
        zMaxF,
        0,
        1,
        xn,
        yp,
        zMaxF,
        0,
        0,
        xp,
        yn,
        zMaxF,
        1,
        1,
        xn,
        yp,
        zMaxF,
        0,
        0,
        xp,
        yp,
        zMaxF,
        1,
        0,
    };

    // BACK face (z = m_zMin)
    float back[] = {
        xn,
        yn,
        zMinF,
        1,
        1,
        xp,
        yn,
        zMinF,
        0,
        1,
        xp,
        yp,
        zMinF,
        0,
        0,
        xn,
        yn,
        zMinF,
        1,
        1,
        xp,
        yp,
        zMinF,
        0,
        0,
        xn,
        yp,
        zMinF,
        1,
        0,
    };

    float* faceData[] = { up, down, right, left, front, back };
    m_faceTextures = { TEXTURE_SKY_UP, TEXTURE_SKY_DOWN, TEXTURE_SKY_RIGHT, TEXTURE_SKY_LEFT, TEXTURE_SKY_FRONT, TEXTURE_SKY_BACK };

    for ( int i = 0; i < 6; ++i )
    {
        m_faceMeshes[i] = std::make_unique<Mesh>( faceData[i], 6, false, true );
    }

    // Load m_shader
    m_shader = std::make_unique<Shader>(
        "SkullbonezData/shaders/unlit_textured.vert",
        "SkullbonezData/shaders/unlit_textured.frag" );
}

/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
SkyBox* SkyBox::Instance( int m_xMin, int m_xMax, int yMin, int yMax, int m_zMin, int m_zMax )
{
    if ( !SkyBox::pInstance )
    {
        static SkyBox instance( m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax );
        SkyBox::pInstance = &instance;
    }
    return SkyBox::pInstance;
}

/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void SkyBox::Destroy( void )
{
    if ( SkyBox::pInstance )
    {
        for ( int i = 0; i < 6; ++i )
        {
            SkyBox::pInstance->m_faceMeshes[i].reset();
        }
        SkyBox::pInstance->m_shader.reset();
        SkyBox::pInstance = 0;
    }
}

/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void SkyBox::ResetGLResources( void )
{
    for ( int i = 0; i < 6; ++i )
    {
        m_faceMeshes[i].reset();
    }
    m_shader.reset();
    this->LoadTextures();
    this->BuildMeshes();
}

/* -- RENDER ----------------------------------------------------------------------*/
void SkyBox::Render( const Matrix4& view, const Matrix4& proj )
{
    // Identity model matrix (transform baked into view via FFP matrix stack)
    Matrix4 identity;

    m_shader->Use();
    m_shader->SetMat4( "uModel", identity );
    m_shader->SetMat4( "uView", view );
    m_shader->SetMat4( "uProjection", proj );
    m_shader->SetVec4( "uColorTint", 1.0f, 1.0f, 1.0f, 1.0f );

    for ( int i = 0; i < 6; ++i )
    {
        m_textures->SelectTexture( m_faceTextures[i] );
        m_faceMeshes[i]->Draw();
    }

    glUseProgram( 0 );
}
