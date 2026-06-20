/*
File: SkullbonezSource/SkullbonezWorldEnvironment.cpp
Purpose:
  Stores world forces, fluid parameters, and water rendering resources.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezWorldEnvironment.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezWorldEnvironment.h"
#include "SkullbonezAssetSystem.h"
#include "SkullbonezIRenderBackend.h"
#include <vector>


using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;

namespace
{
WaterMode WaterModeFromConfigValue( int value )
{
    switch ( value )
    {
    case 0:
        return WaterMode::Off;
    case 1:
        return WaterMode::Basin;
    case 2:
        return WaterMode::Ocean;
    case 3:
        return WaterMode::WetFloor;
    case 4:
        return WaterMode::StylizedBasin;
    default:
        return WaterMode::Ocean;
    }
}

int WaterModeUniformValue( WaterMode mode )
{
    return static_cast<int>( mode );
}

bool WaterModeIncludesOuterOcean( WaterMode mode )
{
    return mode == WaterMode::Ocean;
}

float ClampAngularDragTorqueAxis( float torque, float angularVelocity, float inertia, float changeInTime )
{
    if ( fabsf( angularVelocity ) <= TOLERANCE ||
         inertia <= TOLERANCE ||
         changeInTime <= TOLERANCE )
    {
        return torque;
    }

    const float maxDampingTorque = fabsf( angularVelocity ) * inertia / changeInTime;
    return (std::clamp)( torque, -maxDampingTorque, maxDampingTorque );
}
} // namespace


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


WaterStyleParams WorldEnvironment::BuildCalmWaterStyle( bool cinematic, const SkullbonezCore::Basics::CinematicRenderConfig& cinematicStyle ) const
{
    WaterStyleParams style;
    style.cinematic = cinematic;
    style.sunR = cinematicStyle.sunColorR;
    style.sunG = cinematicStyle.sunColorG;
    style.sunB = cinematicStyle.sunColorB;
    style.mode = cinematic ? WaterModeFromConfigValue( cinematicStyle.waterMode ) : WaterMode::Ocean;
    style.waveHeight = Cfg().oceanWaveHeight;
    style.perturbStrength = Cfg().oceanPerturbStrength;
    style.basinCenterX = cinematicStyle.basinCenterX;
    style.basinCenterZ = cinematicStyle.basinCenterZ;
    style.basinRadiusX = cinematicStyle.basinRadiusX;
    style.basinRadiusZ = cinematicStyle.basinRadiusZ;
    style.basinFeather = cinematic ? cinematicStyle.basinFeather : 1.0f;

    if ( cinematic )
    {
        style.tintR = cinematicStyle.waterTintR;
        style.tintG = cinematicStyle.waterTintG;
        style.tintB = cinematicStyle.waterTintB;
        style.alpha = cinematicStyle.waterAlpha;
        style.reflectionStrength = cinematicStyle.waterReflectionStrength;
        style.glintStrength = cinematicStyle.waterGlintStrength;
    }
    else
    {
        const SkullbonezCore::Basics::OrdinaryRenderConfig& ordinary = Cfg().ordinaryRender;
        style.tintR = ordinary.waterTintR;
        style.tintG = ordinary.waterTintG;
        style.tintB = ordinary.waterTintB;
        style.alpha = ordinary.waterAlpha;
        style.reflectionStrength = ordinary.waterReflectionStrength;
        style.fresnelF0 = ordinary.waterFresnelF0;
        style.sunR = ordinary.sunColorR;
        style.sunG = ordinary.sunColorG;
        style.sunB = ordinary.sunColorB;
    }

    return style;
}


WaterStyleParams WorldEnvironment::BuildOceanWaterStyle( bool cinematic, const SkullbonezCore::Basics::CinematicRenderConfig& cinematicStyle ) const
{
    WaterStyleParams style;
    style.cinematic = cinematic;
    style.sunR = cinematicStyle.sunColorR;
    style.sunG = cinematicStyle.sunColorG;
    style.sunB = cinematicStyle.sunColorB;
    style.mode = cinematic ? WaterModeFromConfigValue( cinematicStyle.waterMode ) : WaterMode::Ocean;
    style.waveHeight = Cfg().oceanWaveHeight;
    style.perturbStrength = Cfg().oceanPerturbStrength;

    if ( cinematic )
    {
        style.tintR = cinematicStyle.waterTintR;
        style.tintG = cinematicStyle.waterTintG;
        style.tintB = cinematicStyle.waterTintB;
        style.alpha = cinematicStyle.waterAlpha;
        style.reflectionStrength = cinematicStyle.waterReflectionStrength;
        style.glintStrength = cinematicStyle.waterGlintStrength;
    }
    else
    {
        const SkullbonezCore::Basics::OrdinaryRenderConfig& ordinary = Cfg().ordinaryRender;
        style.tintR = ordinary.waterTintR;
        style.tintG = ordinary.waterTintG;
        style.tintB = ordinary.waterTintB;
        style.alpha = ordinary.waterAlpha;
        style.reflectionStrength = ordinary.waterReflectionStrength;
        style.fresnelF0 = ordinary.waterFresnelF0;
        style.sunR = ordinary.sunColorR;
        style.sunG = ordinary.sunColorG;
        style.sunB = ordinary.sunColorB;
    }

    return style;
}


void WorldEnvironment::BindCommonWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style, const Vector3& cameraWorld, const WaterReflectionInput& reflection ) const
{
    shader.SetMat4( "uModel", Matrix4::Translate( 0.0f, m_fluidSurfaceHeight, 0.0f ) );
    shader.SetMat4( "uReflectVP", reflection.sampleViewProjection );
    shader.SetVec4( "uColorTint", style.tintR, style.tintG, style.tintB, style.alpha );
    shader.SetFloat( "uReflectionStrength", style.reflectionStrength );
    shader.SetFloat( "uWaterFresnelF0", style.fresnelF0 );
    shader.SetVec3( "uCameraWorld", cameraWorld.x, cameraWorld.y, cameraWorld.z );
    shader.SetInt( "uNoReflect", reflection.noReflection ? 1 : 0 );
    shader.SetFloat( "uCinematicMode", style.cinematic ? 1.0f : 0.0f );
    shader.SetVec3( "uSunColor", style.sunR, style.sunG, style.sunB );
    shader.SetFloat( "uSunGlintStrength", style.glintStrength );
}


void WorldEnvironment::BindCalmWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style ) const
{
    shader.SetInt( "uWaterMode", WaterModeUniformValue( style.mode ) );
    shader.SetVec4( "uBasinMask", style.basinCenterX, style.basinCenterZ, style.basinRadiusX, style.basinRadiusZ );
    shader.SetFloat( "uBasinMaskFeather", style.basinFeather );
}


void WorldEnvironment::BindOceanWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style, float time, bool flatWater ) const
{
    shader.SetFloat( "uTime", time );
    shader.SetFloat( "uWaveHeight", style.waveHeight );
    shader.SetFloat( "uPerturbStrength", style.perturbStrength );
    shader.SetInt( "uFlatWater", flatWater ? 1 : 0 );
}


void WorldEnvironment::RenderFluid( const Matrix4& view,
                                    const Matrix4& proj,
                                    const Vector3& cameraWorld,
                                    const WaterReflectionInput& reflection,
                                    float time,
                                    bool flatWater,
                                    bool cinematic,
                                    const SkullbonezCore::Basics::CinematicRenderConfig* cinematicConfig )
{
    if ( !m_calmMesh )
    {
        BuildFluidMesh();
    }
    const SkullbonezCore::Basics::CinematicRenderConfig& cinematicStyle = cinematicConfig ? *cinematicConfig : Cfg().cinematicRender;
    const WaterMode waterMode = cinematic ? WaterModeFromConfigValue( cinematicStyle.waterMode ) : WaterMode::Ocean;
    if ( cinematic && waterMode == WaterMode::Off )
    {
        return;
    }

    Gfx().BindTexture( reflection.textureHandle, 1 );

    const WaterStyleParams calmStyle = BuildCalmWaterStyle( cinematic, cinematicStyle );
    const WaterStyleParams oceanStyle = BuildOceanWaterStyle( cinematic, cinematicStyle );

    // --- calm (inner) pass: flat, reflective unless disabled ---
    m_calmShader->Use();
    m_calmShader->SetMat4( "uView", view );
    m_calmShader->SetMat4( "uProjection", proj );
    BindCommonWaterStyle( *m_calmShader, calmStyle, cameraWorld, reflection );
    BindCalmWaterStyle( *m_calmShader, calmStyle );
    m_calmMesh->Draw();

    if ( cinematic && !WaterModeIncludesOuterOcean( waterMode ) )
    {
        // Cinematic preview stops after the calm basin pool. Skipping the outer
        // ocean avoids a giant water sheet behind the shot and keeps attention on
        // the terrain bowl, balls, sunset, and fog.
        return;
    }

    // --- ocean (outer) pass: vertex displacement + UV perturbation ---
    m_oceanShader->Use();
    m_oceanShader->SetMat4( "uView", view );
    m_oceanShader->SetMat4( "uProjection", proj );
    BindCommonWaterStyle( *m_oceanShader, oceanStyle, cameraWorld, reflection );
    BindOceanWaterStyle( *m_oceanShader, oceanStyle, time, flatWater );
    m_oceanMesh->Draw();
}


void WorldEnvironment::BuildFluidMesh()
{
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
    constexpr int CALM_N = 128;
    calmVerts.reserve( CALM_N * CALM_N * 6 * 3 );
    oceanVerts.reserve( N * N * 6 * 3 );

    const float calmStepX = ( calmXMax - calmXMin ) / static_cast<float>( CALM_N );
    const float calmStepZ = ( calmZMax - calmZMin ) / static_cast<float>( CALM_N );
    for ( int row = 0; row < CALM_N; ++row )
    {
        for ( int col = 0; col < CALM_N; ++col )
        {
            float x0 = calmXMin + static_cast<float>( col ) * calmStepX;
            float x1 = x0 + calmStepX;
            float z0 = calmZMin + static_cast<float>( row ) * calmStepZ;
            float z1 = z0 + calmStepZ;

            calmVerts.push_back( x0 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z0 );
            calmVerts.push_back( x0 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z1 );
            calmVerts.push_back( x1 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z1 );

            calmVerts.push_back( x0 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z0 );
            calmVerts.push_back( x1 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z1 );
            calmVerts.push_back( x1 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z0 );
        }
    }

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
            if ( isCalm )
            {
                continue;
            }
            std::vector<float>& v = oceanVerts;

            v.push_back( x0 );
            v.push_back( 0.0f );
            v.push_back( z0 );
            v.push_back( x0 );
            v.push_back( 0.0f );
            v.push_back( z1 );
            v.push_back( x1 );
            v.push_back( 0.0f );
            v.push_back( z1 );

            v.push_back( x0 );
            v.push_back( 0.0f );
            v.push_back( z0 );
            v.push_back( x1 );
            v.push_back( 0.0f );
            v.push_back( z1 );
            v.push_back( x1 );
            v.push_back( 0.0f );
            v.push_back( z0 );
        }
    }

    int calmCount = static_cast<int>( calmVerts.size() ) / 3;
    int oceanCount = static_cast<int>( oceanVerts.size() ) / 3;

    m_calmMesh = Gfx().CreateMesh( calmVerts.data(), calmCount, false, false );
    m_oceanMesh = Gfx().CreateMesh( oceanVerts.data(), oceanCount, false, false );

    m_calmShader = SkullbonezCore::Assets::CreateShaderFromActiveAssets( "shader.water_calm" );
    m_calmShader->Use();
    m_calmShader->SetMat4( "uModel", Matrix4() );
    m_calmShader->SetVec4( "uColorTint", 0.05f, 0.15f, 0.42f, 0.65f );
    m_calmShader->SetFloat( "uReflectionStrength", 0.35f );
    m_calmShader->SetFloat( "uWaterFresnelF0", Cfg().ordinaryRender.waterFresnelF0 );
    m_calmShader->SetVec3( "uCameraWorld", 0.0f, 0.0f, 0.0f );
    m_calmShader->SetInt( "uReflectionTex", 1 );
    m_calmShader->SetFloat( "uCinematicMode", 0.0f );
    m_calmShader->SetInt( "uWaterMode", WaterModeUniformValue( WaterMode::Ocean ) );
    m_calmShader->SetVec3( "uSunColor", Cfg().cinematicRender.sunColorR, Cfg().cinematicRender.sunColorG, Cfg().cinematicRender.sunColorB );
    m_calmShader->SetFloat( "uSunGlintStrength", 0.0f );
    m_calmShader->SetVec4( "uBasinMask", 620.0f, 615.0f, 205.0f, 145.0f );
    m_calmShader->SetFloat( "uBasinMaskFeather", 1.0f );

    m_oceanShader = SkullbonezCore::Assets::CreateShaderFromActiveAssets( "shader.water_ocean" );
    m_oceanShader->Use();
    m_oceanShader->SetMat4( "uModel", Matrix4() );
    m_oceanShader->SetVec4( "uColorTint", 0.02f, 0.10f, 0.35f, 0.72f );
    m_oceanShader->SetFloat( "uWaveHeight", Cfg().oceanWaveHeight );
    m_oceanShader->SetFloat( "uPerturbStrength", Cfg().oceanPerturbStrength );
    m_oceanShader->SetFloat( "uReflectionStrength", 0.25f );
    m_oceanShader->SetFloat( "uWaterFresnelF0", Cfg().ordinaryRender.waterFresnelF0 );
    m_oceanShader->SetVec3( "uCameraWorld", 0.0f, 0.0f, 0.0f );
    m_oceanShader->SetInt( "uReflectionTex", 1 );
    m_oceanShader->SetFloat( "uCinematicMode", 0.0f );
    m_oceanShader->SetVec3( "uSunColor", Cfg().cinematicRender.sunColorR, Cfg().cinematicRender.sunColorG, Cfg().cinematicRender.sunColorB );
    m_oceanShader->SetFloat( "uSunGlintStrength", 0.0f );
}


void WorldEnvironment::ResetRenderResources()
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


void WorldEnvironment::SetFluidSurfaceHeight( float height )
{
    m_fluidSurfaceHeight = height;
}


float WorldEnvironment::GetGravity() const
{
    return m_gravity;
}


void WorldEnvironment::SetGravity( float gravity )
{
    m_gravity = gravity;
}


float WorldEnvironment::GetFluidDensity() const
{
    return m_fluidDensity;
}


void WorldEnvironment::SetFluidDensity( float density )
{
    m_fluidDensity = density;
}


// World-space forces acting on the target body accumulate here for this frame.
// Forces are scaled by changeInTime before being set on the rigid body, converting
// force (N) to impulse (N·s = kg·m/s) — matching the semi-implicit Euler integrator
// which adds impulse directly to velocity.
//
//  Forces applied (Y-axis positive = up):
//    1. Gravity:      F_g = m * g             (always downward; g is negative)
//    2. Buoyancy:     F_b = -g * ρ_f * V_sub  (upward; Archimedes' Principle)
//    3. Linear drag:  F_d = -v̂ * ½ρv²CdA     (opposes linear velocity)
//    4. Angular drag: τ   = -C_d * ρ_avg * R³ * ω  (opposes spin)
void WorldEnvironment::AddWorldForces( GameModel& target, float changeInTime )
{
    // initialise the world force vector so we can add to it
    Vector3 m_worldForce = Math::Vector::ZERO_VECTOR;
    Vector3 m_worldTorque = Math::Vector::ZERO_VECTOR;

    float totalVolume = target.GetVolume();

    float submergedVolumePercent = target.GetSubmergedVolumePercent();

    float m_dragCoefficient = target.GetDragCoefficient();

    float m_projectedSurfaceArea = target.GetProjectedSurfaceArea();

    m_worldForce.y += CalculateGravity( target.GetMass() );

    const float buoyancyForce = CalculateBuoyancy( totalVolume * submergedVolumePercent );
    m_worldForce.y += buoyancyForce;
    m_worldTorque += target.CalculateBuoyancyRightingTorque( buoyancyForce, submergedVolumePercent );

    m_worldForce += CalculateViscousDrag( target.GetVelocity(),
                                          submergedVolumePercent,
                                          m_dragCoefficient,
                                          m_projectedSurfaceArea );

    // Angular viscous drag torque: τ = -C_d * ρ_avg * R³ * ω
    // This is the correct dimensional form for rotational drag on a sphere.
    // Linear drag (½ρv²CdA) has units of force [N]; rotational drag must have
    // units of torque [N·m]. For a spinning sphere, the Stokes drag torque is
    // proportional to ω and R³, scaled by medium density.
    Vector3 angularVel = target.GetAngularVelocity();
    if ( !angularVel.IsCloseToZero() )
    {
        float radius = target.GetBoundingRadius();
        float avgDensity = ( m_gasDensity * ( 1.0f - submergedVolumePercent ) ) +
                           ( m_fluidDensity * submergedVolumePercent * Cfg().fluidAngularDragMultiplier );
        float angularDragCoeff = m_dragCoefficient * avgDensity * radius * radius * radius;
        Vector3 angularDragTorque = angularVel * ( -angularDragCoeff );

        const Vector3& inertia = target.GetRotationalInertia();
        angularDragTorque.x = ClampAngularDragTorqueAxis( angularDragTorque.x, angularVel.x, inertia.x, changeInTime );
        angularDragTorque.y = ClampAngularDragTorqueAxis( angularDragTorque.y, angularVel.y, inertia.y, changeInTime );
        angularDragTorque.z = ClampAngularDragTorqueAxis( angularDragTorque.z, angularVel.z, inertia.z, changeInTime );
        m_worldTorque += angularDragTorque;
    }

    // scale and then set the world force and m_torque
    target.SetWorldForce( m_worldForce * changeInTime, m_worldTorque * changeInTime );
}


// Newton's second law: F = m * a, rearranged as F = m * g.
// g is stored as a NEGATIVE value (downward) in the config, so the result is
// a negative Y force (pulling the object toward the ground).
float WorldEnvironment::CalculateGravity( float objectMass )
{
    // F_g = m * g  (Newtons, negative = downward)
    return objectMass * m_gravity;
}


// Archimedes' Principle: a submerged object displaces fluid equal to its
// own submerged volume, and the fluid pushes back with force:
//
//   F_b = -ρ_fluid * V_submerged * g
//
// The negative sign is because g is stored as negative (downward), and
// buoyancy acts upward. Multiplying two negatives gives a positive Y force.
// The deeper (or denser the fluid), the stronger the upward push.
float WorldEnvironment::CalculateBuoyancy( float submergedObjectVolume )
{
    // F_b = -g * ρ_fluid * V_submerged  (positive Y = upward lift)
    return m_gravity * m_fluidDensity * submergedObjectVolume * -1.0f;
}


// Viscous drag (fluid resistance) acts OPPOSITE to the direction of motion and
// is proportional to the square of speed. Formula:
//
//   F_d = -v̂ * 0.5 * ρ_avg * v² * C_d * A
//
// Where:
//   v̂       = unit vector in the direction of motion (negated to oppose it)
//   ρ_avg   = average density of medium (blended air/water by submersion %)
//   v²      = speed squared (|velocity|²)
//   C_d     = drag coefficient (shape-dependent; sphere ≈ 0.47, cube ≈ 1.05)
//   A       = projected surface area (cross-sectional area facing the motion)
//
// Physical intuition:
//   - Doubling speed → 4× the drag force (quadratic relationship)
//   - A cube has higher drag than a sphere (C_d of 1.05 vs 0.47)
//   - Partially submerged objects get weighted drag from both mediums
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

    float distanceSquared = Math::Vector::VectorMagSquared( velocityVector );

    // normalise the velocity vector
    velocityVector.Normalise();

    // negate the velocity vector
    velocityVector *= -1;

    // F_d = -v̂ * 0.5 * ρ_avg * v² * C_d * A
    // ρ_avg is linearly interpolated between gas and fluid density by submergedVolumePercent:
    //   ρ_avg = ρ_gas * (1 - submerged%) + ρ_fluid * submerged%
    return velocityVector *
           0.5f *
           ( ( m_gasDensity * ( 1.0f - submergedVolumePercent ) ) +
             ( m_fluidDensity * submergedVolumePercent ) ) *
           distanceSquared *
           m_dragCoefficient *
           m_projectedSurfaceArea;
}
