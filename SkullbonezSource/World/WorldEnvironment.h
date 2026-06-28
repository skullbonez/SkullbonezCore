/*
File: SkullbonezSource/World/WorldEnvironment.h
Purpose:
  Stores world forces, fluid parameters, and water rendering resources.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Asset system: Runtime-owned registry borrowed by water render passes to
  resolve calm and ocean shader source.
  DXR (DirectX Raytracing): DX12 raytracing path that can provide water
  reflection textures.
  GPU (Graphics Processing Unit): Hardware device that owns renderer resources
  such as meshes, shaders, textures, and reflection targets.
  UV (Texture Coordinates): Two-dimensional texture/sample coordinates used by
  water shaders when perturbing reflection lookup.
  Buoyancy: Upward force from displaced fluid volume; depends on gravity, fluid
  density, and submerged volume.
  Drag coefficient: Shape factor used by viscous drag to scale velocity-based
  resistance.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/World/WorldEnvironment.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../GameObjects/GameModel.h"
#include "../Maths/Vector3.h"
#include "../Maths/Matrix4.h"
#include "../Rendering/IMesh.h"
#include "../Rendering/IShader.h"


namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
} // namespace Assets

namespace GameObjects
{
class GameModel;
} // namespace GameObjects
namespace Rendering
{
class IRenderCommandContext;
class IRenderResourceFactory;
} // namespace Rendering

namespace Environment
{
enum class WaterMode
{
    Off = 0,
    Basin = 1,
    Ocean = 2,
    WetFloor = 3,
    StylizedBasin = 4,
};

struct WaterReflectionInput
{
    Math::Transformation::Matrix4 sampleViewProjection;                   // Reflection sampling matrix for raster or raytraced water.
    uint32_t textureHandle = 0;                                           // Engine reflection texture handle; 0 means none.
    bool noReflection = false;                                            // Scene/style explicitly disables reflection sampling.
    bool raytraced = false;                                               // Reflection texture came from the DXR path instead of raster capture.
};

struct WaterStyleParams
{
    float tintR = 0.05f;                                                  // Linear water tint red channel.
    float tintG = 0.15f;                                                  // Linear water tint green channel.
    float tintB = 0.42f;                                                  // Linear water tint blue channel.
    float alpha = 0.65f;                                                  // Blend alpha used by water material.
    float reflectionStrength = 0.35f;                                     // Reflection mix weight before Fresnel response.
    float fresnelF0 = 0.025f;                                             // Base reflectance for grazing-angle highlight falloff.
    float glintStrength = 0.0f;                                           // Sun glint multiplier for cinematic styles.
    float waveHeight = 4.0f;                                              // Visual wave displacement amplitude, not physics height.
    float perturbStrength = 0.002f;                                       // Normal/UV perturbation scale for reflection distortion.
    float sunR = 1.0f;                                                    // Linear sun color red channel for glints.
    float sunG = 1.0f;                                                    // Linear sun color green channel for glints.
    float sunB = 1.0f;                                                    // Linear sun color blue channel for glints.
    WaterMode mode = WaterMode::Ocean;                                    // Visual water mesh/shader mode chosen by scene/style.
    float basinCenterX = 620.0f;                                          // Basin mask center X in world meters.
    float basinCenterZ = 615.0f;                                          // Basin mask center Z in world meters.
    float basinRadiusX = 205.0f;                                          // Basin mask horizontal radius in world meters.
    float basinRadiusZ = 145.0f;                                          // Basin mask depth radius in world meters.
    float basinFeather = 1.0f;                                            // Soft edge width for basin/ocean transition.
    bool cinematic = false;                                               // Scene/style path enabled higher-art-direction water tuning.
};

/* -- World Environment
------------------------------------------------------------------------------------------------------------------------------------------

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
    WorldEnvironment();                                                   // Initializes default gravity/fluid values from config-era constants.
    WorldEnvironment( const Basics::EngineConfig& config,
                      float fFluidSurfaceHeight,
                      float fFluidDensity,
                      float fGasDensity,
                      float fGravity );                                   // Runtime world bound to live config for water style and drag policy.
    WorldEnvironment( float fFluidSurfaceHeight,
                      float fFluidDensity,
                      float fGasDensity,
                      float fGravity );                                   // Explicit physics constants for tests and scene loading.
    ~WorldEnvironment();                                                  // Releases owned water mesh/shader resources.
    WorldEnvironment( WorldEnvironment&& ) noexcept = default;            // Scene containers move worlds during setup only.
    WorldEnvironment& operator=( WorldEnvironment&& ) noexcept = default; // Scene containers move worlds during setup only.

    void SetTerrainBounds( float xMin,
                           float xMax,
                           float zMin,
                           float zMax );                                  // Must be called before first render; drives calm/ocean mesh split
    void EnsureRenderResources( const Assets::AssetSystem& assets,
                                Rendering::IRenderResourceFactory& renderResources ); // Lazily builds water meshes/shaders.
    void RenderFluid( Rendering::IRenderCommandContext& renderCommands,
                      const Math::Transformation::Matrix4& view,
                      const Math::Transformation::Matrix4& proj,
                      const Math::Vector::Vector3& cameraWorld,
                      const WaterReflectionInput& reflection,
                      float time,
                      bool flatWater = false,
                      bool cinematic = false,
                      const Basics::CinematicRenderConfig* cinematicConfig =
                          nullptr );                                      // Active water mesh render path with current style/reflection inputs.
    void ResetRenderResources();                                          // Rebuilds GPU resources after renderer reset/switch
    float GetFluidSurfaceHeight();                                        // World-space Y plane where water begins.
    void SetFluidSurfaceHeight( float height );                           // Moves the water plane without rebuilding collision geometry.
    float GetGravity() const;                                             // Gravitational acceleration in m/s^2; negative is downward.
    void SetGravity( float gravity );                                     // Updates gravity for future force integration ticks.
    float GetFluidDensity() const;                                        // Fluid density in kg/m^3 for buoyancy and drag.
    void SetFluidDensity( float density );                                // Updates fluid density for future force integration ticks.
    void AddWorldForces( GameObjects::GameModel& target,
                         float changeInTime );                            // Adds world forces to the referenced game model

  private:
    float m_fluidSurfaceHeight;                                           // World-space Y of the fluid surface (m).  Objects below this are submerged
    float m_fluidDensity;                                                 // Density of the fluid medium (kg/m³).  Water ≈ 1000, heavy oil ≈ 850
    float m_gasDensity;                                                   // Density of the gas medium above the surface (kg/m³).  Air ≈ 1.225
    float m_gravity;                                                      // Gravitational acceleration (m/s², stored NEGATIVE for downward, e.g. -9.81)
    const Basics::EngineConfig* m_config;                                  // Borrowed live config for water style and fluid drag tuning.
    float m_terrainXMin = 0.0f;                                           // terrain footprint — calm mesh bounds
    float m_terrainXMax = 0.0f;
    float m_terrainZMin = 0.0f;
    float m_terrainZMax = 0.0f;
    std::unique_ptr<Rendering::IMesh> m_calmMesh;                         // Inner water: flat, reflective
    std::unique_ptr<Rendering::IShader> m_calmShader;
    std::unique_ptr<Rendering::IMesh> m_oceanMesh;                        // Outer water: waves + perturbation
    std::unique_ptr<Rendering::IShader> m_oceanShader;

    void BuildFluidMesh( const Assets::AssetSystem& assets,
                         Rendering::IRenderResourceFactory& renderResources ); // Builds calm and ocean meshes from current terrain bounds.
    const Basics::EngineConfig& Config() const;                            // Runtime config bound by Run/scene world construction.
    WaterStyleParams BuildCalmWaterStyle( bool cinematic, const Basics::CinematicRenderConfig& cinematicConfig ) const;
    WaterStyleParams BuildOceanWaterStyle( bool cinematic, const Basics::CinematicRenderConfig& cinematicConfig ) const;
    void BindCommonWaterStyle( Rendering::IShader& shader,
                               const WaterStyleParams& style,
                               const Math::Vector::Vector3& cameraWorld,
                               const WaterReflectionInput& reflection ) const;
    void BindCalmWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style ) const;
    void
    BindOceanWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style, float time, bool flatWater ) const;

    float CalculateGravity( float objectMass );                           // F_g = m * g  (returns negative Y Newtons = downward)
    float CalculateBuoyancy(
        float submergedObjectVolume );                                    // F_b = -g * ρ_fluid * V_sub  (returns positive Y = upward lift, Archimedes)
    Math::Vector::Vector3
    CalculateViscousDrag( Math::Vector::Vector3 velocityVector,
                          float submergedVolumePercent,
                          float dragCoefficient,
                          float projectedSurfaceArea );                   // F_d = -v̂ * 0.5 * ρ_avg * v² * C_d * A  (opposes motion)
};
} // namespace Environment
} // namespace SkullbonezCore
