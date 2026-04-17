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
#include "SkullbonezWorldEnvironment.h"
#include <vector>


/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;


/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
WorldEnvironment::WorldEnvironment( void )
    : m_fluidSurfaceHeight( 0.0f ),
      m_fluidDensity( 0.0f ),
      m_gasDensity( 0.0f ),
      m_gravity( 0.0f )
{
}


/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
WorldEnvironment::WorldEnvironment( float fFluidSurfaceHeight,
                                    float fFluidDensity,
                                    float fGasDensity,
                                    float fGravity )
    : m_fluidSurfaceHeight( fFluidSurfaceHeight ),
      m_fluidDensity( fFluidDensity ),
      m_gasDensity( fGasDensity ),
      m_gravity( fGravity )
{
}


/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
WorldEnvironment::~WorldEnvironment()
{
}


/* -- RENDER FLUID ----------------------------------------------------------------*/
void WorldEnvironment::RenderFluid( const Matrix4& view, const Matrix4& proj, const Matrix4& reflectVP, float time, GLuint reflectionTex, bool flatWater, bool noReflect, bool noPerturb )
{
    if ( !m_fluidMesh )
    {
        this->BuildFluidMesh();
    }

    glEnable( GL_BLEND );

    m_fluidShader->Use();

    Matrix4 identity;
    m_fluidShader->SetMat4( "uModel", identity );
    m_fluidShader->SetMat4( "uView", view );
    m_fluidShader->SetMat4( "uProjection", proj );
    m_fluidShader->SetMat4( "uReflectVP", reflectVP );
    m_fluidShader->SetVec4( "uColorTint", 0.05f, 0.15f, 0.42f, 0.65f );
    m_fluidShader->SetFloat( "uTime", time );
    m_fluidShader->SetFloat( "uReflectionStrength", 0.35f );
    m_fluidShader->SetInt( "uFlatWater", flatWater ? 1 : 0 );
    m_fluidShader->SetInt( "uNoReflect", noReflect ? 1 : 0 );
    m_fluidShader->SetInt( "uNoPerturb", noPerturb ? 1 : 0 );

    glActiveTexture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_2D, reflectionTex );
    m_fluidShader->SetInt( "uReflectionTex", 1 );
    glActiveTexture( GL_TEXTURE0 );

    m_fluidMesh->Draw();
    glUseProgram( 0 );

    glDisable( GL_BLEND );
}


/* -- BUILD FLUID MESH ------------------------------------------------------------*/
void WorldEnvironment::BuildFluidMesh( void )
{
    float h = m_fluidSurfaceHeight;
    float f = Cfg().frustumFar;

    const int N = 64;
    const float step = 2.0f * f / static_cast<float>( N );

    // N*N quads, 2 triangles each, 6 m_position-only vertices per quad
    std::vector<float> verts;
    verts.reserve( N * N * 6 * 3 );

    for ( int row = 0; row < N; ++row )
    {
        for ( int col = 0; col < N; ++col )
        {
            float x0 = -f + static_cast<float>( col ) * step;
            float x1 = x0 + step;
            float z0 = -f + static_cast<float>( row ) * step;
            float z1 = z0 + step;

            // triangle 1
            verts.push_back( x0 );
            verts.push_back( h );
            verts.push_back( z0 );
            verts.push_back( x0 );
            verts.push_back( h );
            verts.push_back( z1 );
            verts.push_back( x1 );
            verts.push_back( h );
            verts.push_back( z1 );
            // triangle 2
            verts.push_back( x0 );
            verts.push_back( h );
            verts.push_back( z0 );
            verts.push_back( x1 );
            verts.push_back( h );
            verts.push_back( z1 );
            verts.push_back( x1 );
            verts.push_back( h );
            verts.push_back( z0 );
        }
    }

    m_fluidMesh = std::make_unique<Mesh>( verts.data(), N * N * 6, false, false );
    m_fluidShader = std::make_unique<Shader>(
        "SkullbonezData/shaders/water.vert",
        "SkullbonezData/shaders/water.frag" );
}


/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void WorldEnvironment::ResetGLResources( void )
{
    m_fluidMesh.reset();
    m_fluidShader.reset();
    this->BuildFluidMesh();
}


/* -- GET FLUID SURFACE HEIGHT ----------------------------------------------------*/
float WorldEnvironment::GetFluidSurfaceHeight( void )
{
    return m_fluidSurfaceHeight;
}


/* -- ADD WORLD FORCES ------------------------------------------------------------*/
void WorldEnvironment::AddWorldForces( GameModel& target, float changeInTime )
{
    // initialise the world force vector so we can add to it
    Vector3 m_worldForce = Math::Vector::ZERO_VECTOR;
    Vector3 m_worldTorque = Math::Vector::ZERO_VECTOR;

    // get the total m_volume of the target
    float totalVolume = target.GetVolume();

    // get the submerged percentage of the m_volume of the target
    float submergedVolumePercent = target.GetSubmergedVolumePercent();

    // get the drag coefficient of the target
    float m_dragCoefficient = target.GetDragCoefficient();

    // get the projected surface area of the target
    float m_projectedSurfaceArea = target.GetProjectedSurfaceArea();

    // add the force of m_gravity to the world force
    m_worldForce.y += this->CalculateGravity( target.GetMass() );

    // add the force of buoyancy to the world force
    m_worldForce.y += this->CalculateBuoyancy( totalVolume * submergedVolumePercent );

    // add the linear viscous drag to the world force
    m_worldForce += this->CalculateViscousDrag( target.GetVelocity(),
                                                submergedVolumePercent,
                                                m_dragCoefficient,
                                                m_projectedSurfaceArea );

    // add the angular viscous drag to the world force
    m_worldTorque += this->CalculateViscousDrag( target.GetAngularVelocity(),
                                                 submergedVolumePercent,
                                                 m_dragCoefficient,
                                                 m_projectedSurfaceArea );

    // scale and then set the world force and m_torque
    target.SetWorldForce( m_worldForce * changeInTime, m_worldTorque * changeInTime );
}


/* -- CALCULATE GRAVITY -----------------------------------------------------------*/
float WorldEnvironment::CalculateGravity( float objectMass )
{
    // Fg = ma
    return objectMass * m_gravity;
}


/* -- CALCULATE BUOYANCY ----------------------------------------------------------*/
float WorldEnvironment::CalculateBuoyancy( float submergedObjectVolume )
{
    // Fb = -m_gravity * m_mass of displaced fluid
    return m_gravity * m_fluidDensity * submergedObjectVolume * -1.0f;
}


/* -- CALCULATE VISCOUS DRAG ------------------------------------------------------*/
Vector3 WorldEnvironment::CalculateViscousDrag( Vector3 velocityVector,
                                                float submergedVolumePercent,
                                                float m_dragCoefficient,
                                                float m_projectedSurfaceArea )
{
    // if there is no velocity, there will be no viscous drag
    if ( velocityVector.IsCloseToZero() )
    {
        return Math::Vector::ZERO_VECTOR;
    }

    // calculate the squared magnitude of the velocity vector
    float distanceSquared = Math::Vector::VectorMagSquared( velocityVector );

    // normalise the velocity vector
    velocityVector.Normalise();

    // negate the velocity vector
    velocityVector *= -1;

    /*
        Formula for viscous drag:

        Viscous Drag (N) = vDir *
                           0.5 *
                           density *
                           velocity * velocity *
                           m_dragCoefficient *
                           surfaceArea

        NOTE: Density is averaged based on the amount
        the body is submerged...
    */
    return velocityVector *
           0.5f *
           ( ( m_gasDensity * ( 1.0f - submergedVolumePercent ) ) +
             ( m_fluidDensity * submergedVolumePercent ) ) *
           distanceSquared *
           m_dragCoefficient *
           m_projectedSurfaceArea;
}