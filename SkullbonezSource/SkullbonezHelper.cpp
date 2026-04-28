/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezHelper.h"
#include <vector>
#include <cmath>

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;

/* -- STATIC MEMBER INITIALISATION ------------------------------------------------*/
std::unique_ptr<Mesh> SkullbonezHelper::sphereMesh;
std::unique_ptr<Shader> SkullbonezHelper::sphereShader;
float SkullbonezHelper::sClipPlane[4] = {
    0.0f,
    1.0f,
    0.0f,
    1.0e9f // default: always pass (GL_CLIP_DISTANCE0 disabled)
};

/* -- SET CLIP PLANE --------------------------------------------------------------*/
void SkullbonezHelper::SetClipPlane( float x, float y, float z, float w )
{
    sClipPlane[0] = x;
    sClipPlane[1] = y;
    sClipPlane[2] = z;
    sClipPlane[3] = w;
}

/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void SkullbonezHelper::ResetGLResources( void )
{
    sphereMesh.reset();
    sphereShader.reset();
}

/* -- BUILD SPHERE MESH -----------------------------------------------------------*/
void SkullbonezHelper::BuildSphereMesh( int slices, int stacks )
{
    // Generate a unit sphere with normals and texcoords (8 floats per vertex)
    std::vector<float> verts;
    verts.reserve( slices * stacks * 6 * 8 );

    for ( int i = 0; i < stacks; ++i )
    {
        float phi0 = _PI * (float)i / (float)stacks;
        float phi1 = _PI * (float)( i + 1 ) / (float)stacks;

        for ( int j = 0; j < slices; ++j )
        {
            float theta0 = _2PI * (float)j / (float)slices;
            float theta1 = _2PI * (float)( j + 1 ) / (float)slices;

            // 4 corners of the quad
            float x00 = sinf( phi0 ) * cosf( theta0 ), y00 = cosf( phi0 ), z00 = sinf( phi0 ) * sinf( theta0 );
            float x01 = sinf( phi0 ) * cosf( theta1 ), y01 = cosf( phi0 ), z01 = sinf( phi0 ) * sinf( theta1 );
            float x10 = sinf( phi1 ) * cosf( theta0 ), y10 = cosf( phi1 ), z10 = sinf( phi1 ) * sinf( theta0 );
            float x11 = sinf( phi1 ) * cosf( theta1 ), y11 = cosf( phi1 ), z11 = sinf( phi1 ) * sinf( theta1 );

            float u0 = (float)j / (float)slices, v0 = (float)i / (float)stacks;
            float u1 = (float)( j + 1 ) / (float)slices, v1 = (float)( i + 1 ) / (float)stacks;

            // Triangle 1: (0,0) → (1,1) → (1,0)  (CCW viewed from outside)
            verts.insert( verts.end(), { x00, y00, z00, x00, y00, z00, u0, v0 } );
            verts.insert( verts.end(), { x11, y11, z11, x11, y11, z11, u1, v1 } );
            verts.insert( verts.end(), { x10, y10, z10, x10, y10, z10, u0, v1 } );

            // Triangle 2: (0,0) → (0,1) → (1,1)  (CCW viewed from outside)
            verts.insert( verts.end(), { x00, y00, z00, x00, y00, z00, u0, v0 } );
            verts.insert( verts.end(), { x01, y01, z01, x01, y01, z01, u1, v0 } );
            verts.insert( verts.end(), { x11, y11, z11, x11, y11, z11, u1, v1 } );
        }
    }

    sphereMesh = std::make_unique<Mesh>( verts.data(), (int)verts.size() / 8, true, true );
}

/* -- DRAW SPHERE BATCH BEGIN -----------------------------------------------------*/
void SkullbonezHelper::DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent )
{
    if ( !sphereMesh )
    {
        BuildSphereMesh( 25, 25 );
        sphereShader = std::make_unique<Shader>(
            "SkullbonezData/shaders/lit_textured.vert",
            "SkullbonezData/shaders/lit_textured.frag" );
    }

    if ( isTransparent )
    {
        glEnable( GL_BLEND );
    }

    float viewLightPos[4];
    for ( int i = 0; i < 3; ++i )
    {
        viewLightPos[i] = view.m[i] * lightPos[0] + view.m[i + 4] * lightPos[1] + view.m[i + 8] * lightPos[2] + view.m[i + 12] * lightPos[3];
    }
    viewLightPos[3] = lightPos[3];

    sphereShader->Use();
    sphereShader->SetMat4( "uView", view );
    sphereShader->SetMat4( "uProjection", proj );
    sphereShader->SetVec4( "uClipPlane", sClipPlane[0], sClipPlane[1], sClipPlane[2], sClipPlane[3] );
    sphereShader->SetVec4( "uLightPosition", viewLightPos[0], viewLightPos[1], viewLightPos[2], viewLightPos[3] );
    sphereShader->SetVec4( "uLightAmbient", 1.0f, 0.5f, 0.5f, 1.0f );
    sphereShader->SetVec4( "uLightDiffuse", 1.0f, 0.5f, 0.5f, 1.0f );
    sphereShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
    sphereShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );
}

/* -- DRAW SPHERE BATCH MODEL -----------------------------------------------------*/
void SkullbonezHelper::DrawSphereBatchModel( const Matrix4& model )
{
    sphereShader->SetMat4( "uModel", model );
    sphereMesh->Draw();
}

/* -- DRAW SPHERE BATCH END -------------------------------------------------------*/
void SkullbonezHelper::DrawSphereBatchEnd( void )
{
    glUseProgram( 0 );
    glDisable( GL_BLEND );
}

/* -- STATE SETUP -----------------------------------------------------------------*/
void SkullbonezHelper::StateSetup( void )
{
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );

    glClearDepth( 1.0f ); // clear depth buffer every frame

    glEnable( GL_DEPTH_TEST ); // enable depth testing
    glDepthFunc( GL_LEQUAL );  // less than or equal depth testing
    glEnable( GL_CULL_FACE );  // enable back face culling
    glFrontFace( GL_CCW );     // counter-clockwise front faces

    // set up alpha blending (used by water and transparent spheres)
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
}
