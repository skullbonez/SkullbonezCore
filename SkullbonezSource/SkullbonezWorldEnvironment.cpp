/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezWorldEnvironment.h"
#include <vector>

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
WorldEnvironment::WorldEnvironment()
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

/* -- SET TERRAIN BOUNDS ----------------------------------------------------------*/
void WorldEnvironment::SetTerrainBounds( float xMin, float xMax, float zMin, float zMax )
{
    m_terrainXMin = xMin;
    m_terrainXMax = xMax;
    m_terrainZMin = zMin;
    m_terrainZMax = zMax;
}

/* -- RENDER FLUID ----------------------------------------------------------------*/
void WorldEnvironment::RenderFluid( const Matrix4& view, const Matrix4& proj, const Matrix4& reflectVP, float time, GLuint reflectionTex, bool flatWater, bool noReflect )
{
    if ( !m_calmMesh )
    {
        BuildFluidMesh();
    }

    Matrix4 identity;

    glEnable( GL_BLEND );
    glActiveTexture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_2D, reflectionTex );
    glActiveTexture( GL_TEXTURE0 );

    // --- calm (inner) pass: flat, always reflective, no waves ---
    m_calmShader->Use();
    m_calmShader->SetMat4( "uModel", identity );
    m_calmShader->SetMat4( "uView", view );
    m_calmShader->SetMat4( "uProjection", proj );
    m_calmShader->SetMat4( "uReflectVP", reflectVP );
    m_calmShader->SetVec4( "uColorTint", 0.05f, 0.15f, 0.42f, 0.65f );
    m_calmShader->SetFloat( "uReflectionStrength", 0.35f );
    m_calmShader->SetInt( "uReflectionTex", 1 );
    m_calmMesh->Draw();

    // --- ocean (outer) pass: vertex displacement + UV perturbation ---
    m_oceanShader->Use();
    m_oceanShader->SetMat4( "uModel", identity );
    m_oceanShader->SetMat4( "uView", view );
    m_oceanShader->SetMat4( "uProjection", proj );
    m_oceanShader->SetMat4( "uReflectVP", reflectVP );
    m_oceanShader->SetVec4( "uColorTint", 0.02f, 0.10f, 0.35f, 0.72f );
    m_oceanShader->SetFloat( "uTime", time );
    m_oceanShader->SetFloat( "uWaveHeight", Cfg().oceanWaveHeight );
    m_oceanShader->SetFloat( "uPerturbStrength", Cfg().oceanPerturbStrength );
    m_oceanShader->SetFloat( "uReflectionStrength", 0.25f );
    m_oceanShader->SetInt( "uReflectionTex", 1 );
    m_oceanShader->SetInt( "uNoReflect", noReflect ? 1 : 0 );
    m_oceanShader->SetInt( "uFlatWater", flatWater ? 1 : 0 );
    m_oceanMesh->Draw();

    glUseProgram( 0 );
    glDisable( GL_BLEND );
}

/* -- BUILD FLUID MESH ------------------------------------------------------------*/
void WorldEnvironment::BuildFluidMesh()
{
    float h = m_fluidSurfaceHeight;
    float f = Cfg().frustumFar;

    const int N = 64;
    const float step = 2.0f * f / static_cast<float>( N );

    // Calm region: half the terrain footprint, centered on the terrain
    float cxMid = ( m_terrainXMin + m_terrainXMax ) * 0.5f;
    float czMid = ( m_terrainZMin + m_terrainZMax ) * 0.5f;
    float cxHalf = ( m_terrainXMax - m_terrainXMin ) * 0.25f;
    float czHalf = ( m_terrainZMax - m_terrainZMin ) * 0.25f;
    float calmXMin = cxMid - cxHalf;
    float calmXMax = cxMid + cxHalf;
    float calmZMin = czMid - czHalf;
    float calmZMax = czMid + czHalf;

    std::vector<float> calmVerts;
    std::vector<float> oceanVerts;
    calmVerts.reserve( N * N * 6 * 3 );
    oceanVerts.reserve( N * N * 6 * 3 );

    for ( int row = 0; row < N; ++row )
    {
        for ( int col = 0; col < N; ++col )
        {
            float x0 = -f + static_cast<float>( col ) * step;
            float x1 = x0 + step;
            float z0 = -f + static_cast<float>( row ) * step;
            float z1 = z0 + step;

            // A quad belongs to the calm mesh only if it lies fully inside the calm region
            bool isCalm = ( x0 >= calmXMin && x1 <= calmXMax &&
                            z0 >= calmZMin && z1 <= calmZMax );

            std::vector<float>& v = isCalm ? calmVerts : oceanVerts;

            v.push_back( x0 );
            v.push_back( h );
            v.push_back( z0 );
            v.push_back( x0 );
            v.push_back( h );
            v.push_back( z1 );
            v.push_back( x1 );
            v.push_back( h );
            v.push_back( z1 );

            v.push_back( x0 );
            v.push_back( h );
            v.push_back( z0 );
            v.push_back( x1 );
            v.push_back( h );
            v.push_back( z1 );
            v.push_back( x1 );
            v.push_back( h );
            v.push_back( z0 );
        }
    }

    int calmCount = static_cast<int>( calmVerts.size() ) / 3;
    int oceanCount = static_cast<int>( oceanVerts.size() ) / 3;

    m_calmMesh = std::make_unique<Mesh>( calmVerts.data(), calmCount, false, false );
    m_oceanMesh = std::make_unique<Mesh>( oceanVerts.data(), oceanCount, false, false );

    m_calmShader = std::make_unique<Shader>( "SkullbonezData/shaders/water_calm.vert", "SkullbonezData/shaders/water_calm.frag" );
    m_oceanShader = std::make_unique<Shader>( "SkullbonezData/shaders/water_ocean.vert", "SkullbonezData/shaders/water_ocean.frag" );
}

/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void WorldEnvironment::ResetGLResources()
{
    m_calmMesh.reset();
    m_calmShader.reset();
    m_oceanMesh.reset();
    m_oceanShader.reset();
    BuildFluidMesh();
}

/* -- GET FLUID SURFACE HEIGHT ----------------------------------------------------*/
float WorldEnvironment::GetFluidSurfaceHeight()
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
    m_worldForce.y += CalculateGravity( target.GetMass() );

    // add the force of buoyancy to the world force
    m_worldForce.y += CalculateBuoyancy( totalVolume * submergedVolumePercent );

    // add the linear viscous drag to the world force
    m_worldForce += CalculateViscousDrag( target.GetVelocity(),
                                                submergedVolumePercent,
                                                m_dragCoefficient,
                                                m_projectedSurfaceArea );

    // add the angular viscous drag to the world force
    m_worldTorque += CalculateViscousDrag( target.GetAngularVelocity(),
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
