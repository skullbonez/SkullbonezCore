// --- Includes ---
#include "SkullbonezWorldEnvironment.h"
#include "SkullbonezIRenderBackend.h"
#include <vector>


// --- Usings ---
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;


WorldEnvironment::WorldEnvironment()
    : m_fluidSurfaceHeight( 0.0f ),
      m_fluidDensity( 0.0f ),
      m_gasDensity( 0.0f ),
      m_gravity( 0.0f )
{
}


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


WorldEnvironment::~WorldEnvironment()
{
}


void WorldEnvironment::SetTerrainBounds( float xMin, float xMax, float zMin, float zMax )
{
    m_terrainXMin = xMin;
    m_terrainXMax = xMax;
    m_terrainZMin = zMin;
    m_terrainZMax = zMax;
}


void WorldEnvironment::RenderFluid( const Matrix4& view, const Matrix4& proj, const Matrix4& reflectVP, float time, uint32_t reflectionTex, bool flatWater, bool noReflect )
{
    if ( !m_calmMesh )
    {
        BuildFluidMesh();
    }

    Gfx().SetBlend( true );
    Gfx().BindTexture( reflectionTex, 1 );

    // --- calm (inner) pass: flat, always reflective, no waves ---
    m_calmShader->Use();
    m_calmShader->SetMat4( "uView", view );
    m_calmShader->SetMat4( "uProjection", proj );
    m_calmShader->SetMat4( "uReflectVP", reflectVP );
    m_calmMesh->Draw();

    // --- ocean (outer) pass: vertex displacement + UV perturbation ---
    m_oceanShader->Use();
    m_oceanShader->SetMat4( "uView", view );
    m_oceanShader->SetMat4( "uProjection", proj );
    m_oceanShader->SetMat4( "uReflectVP", reflectVP );
    m_oceanShader->SetFloat( "uTime", time );
    m_oceanShader->SetInt( "uNoReflect", noReflect ? 1 : 0 );
    m_oceanShader->SetInt( "uFlatWater", flatWater ? 1 : 0 );
    m_oceanMesh->Draw();

    Gfx().SetBlend( false );
}


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

    m_calmMesh = Gfx().CreateMesh( calmVerts.data(), calmCount, false, false );
    m_oceanMesh = Gfx().CreateMesh( oceanVerts.data(), oceanCount, false, false );

    m_calmShader = Gfx().CreateShader( "SkullbonezData/shaders/water_calm.vert", "SkullbonezData/shaders/water_calm.frag" );
    m_calmShader->Use();
    m_calmShader->SetMat4( "uModel", Matrix4() );
    m_calmShader->SetVec4( "uColorTint", 0.05f, 0.15f, 0.42f, 0.65f );
    m_calmShader->SetFloat( "uReflectionStrength", 0.35f );
    m_calmShader->SetInt( "uReflectionTex", 1 );

    m_oceanShader = Gfx().CreateShader( "SkullbonezData/shaders/water_ocean.vert", "SkullbonezData/shaders/water_ocean.frag" );
    m_oceanShader->Use();
    m_oceanShader->SetMat4( "uModel", Matrix4() );
    m_oceanShader->SetVec4( "uColorTint", 0.02f, 0.10f, 0.35f, 0.72f );
    m_oceanShader->SetFloat( "uWaveHeight", Cfg().oceanWaveHeight );
    m_oceanShader->SetFloat( "uPerturbStrength", Cfg().oceanPerturbStrength );
    m_oceanShader->SetFloat( "uReflectionStrength", 0.25f );
    m_oceanShader->SetInt( "uReflectionTex", 1 );
}


void WorldEnvironment::ResetGLResources()
{
    m_calmMesh.reset();
    m_calmShader.reset();
    m_oceanMesh.reset();
    m_oceanShader.reset();
    BuildFluidMesh();
}


float WorldEnvironment::GetFluidSurfaceHeight()
{
    return m_fluidSurfaceHeight;
}


void WorldEnvironment::AddWorldForces( GameModel& target, float changeInTime )
{
    const float V = target.GetVolume();
    const float subPct = target.GetSubmergedVolumePercent();
    const float Cd = target.GetDragCoefficient();
    const float A = target.GetProjectedSurfaceArea();

    // F_world = (0, F_g + F_b, 0) + F_drag(v)
    // T_world = F_drag(omega)             ← engine treats angular drag with the same form
    Vector3 F = Math::Vector::ZERO_VECTOR;
    Vector3 T = Math::Vector::ZERO_VECTOR;

    F.y += CalculateGravity( target.GetMass() );
    F.y += CalculateBuoyancy( V * subPct );
    F += CalculateViscousDrag( target.GetVelocity(),        subPct, Cd, A );
    T += CalculateViscousDrag( target.GetAngularVelocity(), subPct, Cd, A );

    // Pre-multiply by dt so RigidBody::ApplyWorldForce computes dv = (F*dt)/m directly
    target.SetWorldForce( F * changeInTime, T * changeInTime );
}


float WorldEnvironment::CalculateGravity( float objectMass )
{
    // F_g = m·g          (g is signed; negative = downward)
    return objectMass * m_gravity;
}


float WorldEnvironment::CalculateBuoyancy( float submergedObjectVolume )
{
    // F_b = -ρ_fluid · V_sub · g    (Archimedes — opposes gravity)
    return -m_gravity * m_fluidDensity * submergedObjectVolume;
}


Vector3 WorldEnvironment::CalculateViscousDrag( Vector3 velocityVector,
                                                float submergedVolumePercent,
                                                float Cd,
                                                float A )
{
    if ( velocityVector.IsCloseToZero() )
    {
        return Math::Vector::ZERO_VECTOR;
    }

    // Quadratic drag:
    //   F_drag = -0.5 * rho * |v|^2 * Cd * A * v_hat
    // rho blended by submersion fraction:  rho = (1-s)*rho_gas + s*rho_fluid
    const float speedSq = Math::Vector::VectorMagSquared( velocityVector );
    velocityVector.Normalise();
    velocityVector *= -1.0f; // -v_hat

    const float density = m_gasDensity * ( 1.0f - submergedVolumePercent ) +
                          m_fluidDensity * submergedVolumePercent;

    return velocityVector * ( 0.5f * density * speedSq * Cd * A );
}
