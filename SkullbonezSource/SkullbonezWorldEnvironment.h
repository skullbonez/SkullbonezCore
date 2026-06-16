/*
File: SkullbonezSource/SkullbonezWorldEnvironment.h
Purpose:
  Stores world forces, fluid parameters, and water rendering resources.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezWorldEnvironment.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezConfig.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezVector3.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezIMesh.h"
#include "SkullbonezIShader.h"


namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
} // namespace GameObjects

namespace Environment
{
struct WaterReflectionInput
{
    Math::Transformation::Matrix4 sampleViewProjection;
    uint32_t textureHandle = 0;
    bool noReflection = false;
    bool raytraced = false;
};

struct WaterStyleParams
{
    float tintR = 0.05f;
    float tintG = 0.15f;
    float tintB = 0.42f;
    float alpha = 0.65f;
    float reflectionStrength = 0.35f;
    float glintStrength = 0.0f;
    float sunR = 1.0f;
    float sunG = 1.0f;
    float sunB = 1.0f;
    int mode = 2;
    float basinCenterX = 620.0f;
    float basinCenterZ = 615.0f;
    float basinRadiusX = 205.0f;
    float basinRadiusZ = 145.0f;
    float basinFeather = 1.0f;
    bool cinematic = false;
};

/* -- World Environment ------------------------------------------------------------------------------------------------------------------------------------------

    Encapsulates the physical properties of the simulation world and applies
    environmental forces to every dynamic object each frame via AddWorldForces().

    Forces applied:
      1. Gravity:       F_g = m * g  (constant downward force, g < 0 in config)
      2. Buoyancy:      F_b = -ρ_fluid * V_submerged * g  (Archimedes' Principle)
                        — acts upward on any volume below m_fluidSurfaceHeight
      3. Viscous drag:  F_d = -v̂ * 0.5 * ρ_avg * |v|² * C_d * A
                        — opposes velocity, proportional to speed²; ρ_avg is
                          blended between gas and fluid density by submersion %.

    Fluid / gas:
      Objects below m_fluidSurfaceHeight are in fluid (water), above are in gas (air).
      Both phases contribute drag simultaneously for partially submerged objects.

    Water surface rendering:
      BuildFluidMesh() creates two meshes:
        - Calm mesh: flat mirror within terrain bounds (for reflections)
        - Ocean mesh: animated waves outside terrain bounds
      RenderFluid() drives these each frame.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class WorldEnvironment
{

  public:
    WorldEnvironment();                                                                                    // Default constructor
    WorldEnvironment( float fFluidSurfaceHeight, float fFluidDensity, float fGasDensity, float fGravity ); // Overloaded constructor
    ~WorldEnvironment();                                                                                   // Default destructor
    WorldEnvironment( WorldEnvironment&& ) noexcept = default;                                             // Move constructor
    WorldEnvironment& operator=( WorldEnvironment&& ) noexcept = default;                                  // Move assignment

    void SetTerrainBounds( float xMin, float xMax, float zMin, float zMax );                                                                                                                                                                                                                                                              // Must be called before first render; drives calm/ocean mesh split
    void RenderFluid( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const WaterReflectionInput& reflection, float time, bool flatWater = false, bool cinematic = false, const Basics::CinematicRenderConfig* cinematicConfig = nullptr ); // Renders the water in the scene
    void ResetRenderResources();                                                                                                                                                                                                                                                                                                          // Rebuilds GPU resources after renderer reset/switch
    float GetFluidSurfaceHeight();                                                                                                                                                                                                                                                                                                        // Returns the fluid surface height
    void SetFluidSurfaceHeight( float height );                                                                                                                                                                                                                                                                                           // Sets the fluid surface height
    float GetGravity() const;                                                                                                                                                                                                                                                                                                             // Returns the gravity value (m/s^2)
    void SetGravity( float gravity );                                                                                                                                                                                                                                                                                                     // Sets the gravity value (m/s^2)
    float GetFluidDensity() const;                                                                                                                                                                                                                                                                                                        // Returns the fluid density (kg/m^3)
    void SetFluidDensity( float density );                                                                                                                                                                                                                                                                                                // Sets the fluid density (kg/m^3)
    void AddWorldForces( GameObjects::GameModel& target, float changeInTime );                                                                                                                                                                                                                                                            // Adds world forces to the referenced game model

  private:
    float m_fluidSurfaceHeight; // World-space Y of the fluid surface (m).  Objects below this are submerged
    float m_fluidDensity;       // Density of the fluid medium (kg/m³).  Water ≈ 1000, heavy oil ≈ 850
    float m_gasDensity;         // Density of the gas medium above the surface (kg/m³).  Air ≈ 1.225
    float m_gravity;            // Gravitational acceleration (m/s², stored NEGATIVE for downward, e.g. -9.81)
    float m_terrainXMin = 0.0f; // terrain footprint — calm mesh bounds
    float m_terrainXMax = 0.0f;
    float m_terrainZMin = 0.0f;
    float m_terrainZMax = 0.0f;
    std::unique_ptr<Rendering::IMesh> m_calmMesh; // Inner water: flat, reflective
    std::unique_ptr<Rendering::IShader> m_calmShader;
    std::unique_ptr<Rendering::IMesh> m_oceanMesh; // Outer water: waves + perturbation
    std::unique_ptr<Rendering::IShader> m_oceanShader;

    void BuildFluidMesh(); // Builds calm and ocean meshes
    WaterStyleParams BuildCalmWaterStyle( bool cinematic, const Basics::CinematicRenderConfig& cinematicConfig ) const;
    WaterStyleParams BuildOceanWaterStyle( bool cinematic, const Basics::CinematicRenderConfig& cinematicConfig ) const;
    void BindCommonWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style, const WaterReflectionInput& reflection ) const;
    void BindCalmWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style ) const;
    void BindOceanWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style, float time, bool flatWater ) const;

    float CalculateGravity( float objectMass );                                                                                                                          // F_g = m * g  (returns negative Y Newtons = downward)
    float CalculateBuoyancy( float submergedObjectVolume );                                                                                                              // F_b = -g * ρ_fluid * V_sub  (returns positive Y = upward lift, Archimedes)
    Math::Vector::Vector3 CalculateViscousDrag( Math::Vector::Vector3 velocityVector, float submergedVolumePercent, float dragCoefficient, float projectedSurfaceArea ); // F_d = -v̂ * 0.5 * ρ_avg * v² * C_d * A  (opposes motion)
};
} // namespace Environment
} // namespace SkullbonezCore
