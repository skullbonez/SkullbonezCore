#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezVector3.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezIMesh.h"
#include "SkullbonezIShader.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;


namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
} // namespace GameObjects

namespace Environment
{
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

    void SetTerrainBounds( float xMin, float xMax, float zMin, float zMax );                                                                                                    // Must be called before first render; drives calm/ocean mesh split
    void RenderFluid( const Matrix4& view, const Matrix4& proj, const Matrix4& reflectVP, float time, uint32_t reflectionTex, bool flatWater = false, bool noReflect = false ); // Renders the water in the scene
    void ResetGLResources();                                                                                                                                                    // Rebuilds GPU resources after GL context recreation
    float GetFluidSurfaceHeight();                                                                                                                                              // Returns the fluid surface height
    float GetGravity() const;                                                                                                                                                   // Returns the gravity value (m/s^2)
    float GetFluidDensity() const;                                                                                                                                              // Returns the fluid density (kg/m^3)
    void AddWorldForces( GameObjects::GameModel& target, float changeInTime );                                                                                                  // Adds world forces to the referenced game model

  private:
    float m_fluidSurfaceHeight; // World-space Y of the fluid surface (m).  Objects below this are submerged
    float m_fluidDensity;       // Density of the fluid medium (kg/m³).  Water ≈ 1000, heavy oil ≈ 850
    float m_gasDensity;         // Density of the gas medium above the surface (kg/m³).  Air ≈ 1.225
    float m_gravity;            // Gravitational acceleration (m/s², stored NEGATIVE for downward, e.g. -9.81)
    float m_terrainXMin = 0.0f; // terrain footprint — calm mesh bounds
    float m_terrainXMax = 0.0f;
    float m_terrainZMin = 0.0f;
    float m_terrainZMax = 0.0f;
    std::unique_ptr<IMesh> m_calmMesh; // Inner water: flat, reflective
    std::unique_ptr<IShader> m_calmShader;
    std::unique_ptr<IMesh> m_oceanMesh; // Outer water: waves + perturbation
    std::unique_ptr<IShader> m_oceanShader;

    void BuildFluidMesh(); // Builds calm and ocean meshes

    float CalculateGravity( float objectMass );                                                                                              // F_g = m * g  (returns negative Y Newtons = downward)
    float CalculateBuoyancy( float submergedObjectVolume );                                                                                  // F_b = -g * ρ_fluid * V_sub  (returns positive Y = upward lift, Archimedes)
    Vector3 CalculateViscousDrag( Vector3 velocityVector, float submergedVolumePercent, float dragCoefficient, float projectedSurfaceArea ); // F_d = -v̂ * 0.5 * ρ_avg * v² * C_d * A  (opposes motion)
};
} // namespace Environment
} // namespace SkullbonezCore
