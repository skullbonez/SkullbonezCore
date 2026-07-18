/*
File: SkullbonezSource/World/WorldEnvironment.h
Purpose:
  Stores world forces, fluid parameters, and water rendering resources.

Summary:
  WorldEnvironment.h stores world forces, fluid parameters, and water
  rendering resources. As a public header, keep edits anchored on world-state
  ownership, terrain/environment data, and physics/render handoff and on the
  glossary/invariants below.

Glossary:
  DXR (DirectX Raytracing): DX12 raytracing path that can provide water
  reflection textures.
  GPU (Graphics Processing Unit): Hardware device that owns renderer resources
  such as meshes, shaders, textures, and reflection targets.
  UV (Texture Coordinates): Two-dimensional texture/sample coordinates used by
  water shaders when perturbing reflection lookup.
  Water render style: Values that feed water shader uniforms, including ordinary
  and cinematic fallback style.
  Water mesh build settings: Values used only when regenerating calm/ocean mesh
  geometry.
  Fluid force settings: Values that affect buoyancy and drag force integration.
  Fluid surface adjustment: Typed signed velocity issued by input in world units.
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
  - Device/key vocabulary cannot cross into world-setting mutation methods.

Related:
  - SkullbonezSource/World/WorldEnvironment.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Maths/Vector3.h"
#include "../Maths/Matrix4.h"
#include "../Physics/PhysicsWorldForces.h"
#include "../Rendering/IMesh.h"
#include "../Rendering/IShader.h"
#include "FluidSurfaceAdjustment.h"


namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}

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
    Math::Transformation::Matrix4 sampleViewProjection;                              // Reflection sampling matrix for raster or raytraced water.
    uint32_t textureHandle = 0;                                                      // Engine reflection texture handle; 0 means none.
    bool noReflection = false;                                                       // Scene/style explicitly disables reflection sampling.
    bool raytraced = false;                                                          // Reflection texture came from the DXR path instead of raster capture.
};

struct WaterStyleParams
{
    float tintR = 0.05f;                                                             // Linear water tint red channel.
    float tintG = 0.15f;                                                             // Linear water tint green channel.
    float tintB = 0.42f;                                                             // Linear water tint blue channel.
    float alpha = 0.65f;                                                             // Blend alpha used by water material.
    float reflectionStrength = 0.35f;                                                // Reflection mix weight before Fresnel response.
    float fresnelF0 = 0.025f;                                                        // Base reflectance for grazing-angle highlight falloff.
    float glintStrength = 0.0f;                                                      // Sun glint multiplier for cinematic styles.
    float waveHeight = 4.0f;                                                         // Visual wave displacement amplitude, not physics height.
    float perturbStrength = 0.002f;                                                  // Normal/UV perturbation scale for reflection distortion.
    float sunR = 1.0f;                                                               // Linear sun color red channel for glints.
    float sunG = 1.0f;                                                               // Linear sun color green channel for glints.
    float sunB = 1.0f;                                                               // Linear sun color blue channel for glints.
    WaterMode mode = WaterMode::Ocean;                                               // Visual water mesh/shader mode chosen by scene/style.
    float basinCenterX = 620.0f;                                                     // Basin mask center X in world meters.
    float basinCenterZ = 615.0f;                                                     // Basin mask center Z in world meters.
    float basinRadiusX = 205.0f;                                                     // Basin mask horizontal radius in world meters.
    float basinRadiusZ = 145.0f;                                                     // Basin mask depth radius in world meters.
    float basinFeather = 1.0f;                                                       // Soft edge width for basin/ocean transition.
    bool cinematic = false;                                                          // Scene/style path enabled higher-art-direction water tuning.
};

/* -- World Environment
------------------------------------------------------------------------------------------------------------------------------------------

    Encapsulates world fluid/gravity settings and exposes scalar force inputs
    consumed by the physics body store each fixed step.

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
    WorldEnvironment();                                                              // Initializes default gravity/fluid values from config-era constants.
    WorldEnvironment( float fFluidSurfaceHeight,
                      float fFluidDensity,
                      float fGasDensity,
                      float fGravity );                                              // Explicit physics constants for tests and scene loading.
    ~WorldEnvironment();                                                             // Releases owned water mesh/shader resources.
    WorldEnvironment( WorldEnvironment&& ) noexcept = default;                       // Scene containers move worlds during setup only.
    WorldEnvironment& operator=( WorldEnvironment&& ) noexcept = default;            // Scene containers move worlds during setup only.

    void SetTerrainBounds( float xMin,
                           float xMax,
                           float zMin,
                           float zMax );                                             // Must be called before first render; drives calm/ocean mesh split
    void RenderFluid( const Math::Transformation::Matrix4& view,
                      const Math::Transformation::Matrix4& proj,
                      const Math::Vector::Vector3& cameraWorld,
                      Rendering::IRenderCommandContext& commands,
                      const WaterReflectionInput& reflection,
                      float time,
                      bool flatWater = false,
                      bool cinematic = false,
                      const SkullbonezCore::Core::CinematicRenderConfig* cinematicConfig =
                          nullptr );                                                 // Active water mesh render path with current style/reflection inputs.
    void BindRuntimeConfig( const SkullbonezCore::Core::EngineConfig&
                                config );                                            // Borrow runtime settings for water physics and style constants.
    void BindRenderContexts(
        const SkullbonezCore::Core::EngineConfig& config,
        Assets::AssetSystem& assets,
        Rendering::IRenderResourceFactory& resources );                              // Borrow rebuild-only services for water resources.
    void
    EnsureRenderResources( const SkullbonezCore::Core::EngineConfig& config,
                           Assets::AssetSystem& assets,
                           Rendering::IRenderResourceFactory& resources );           // Lazily rebuilds missing backend resources.
    void ResetRenderResources();                                                     // Rebuilds GPU resources after renderer reset/switch
    void ReleaseRenderResources();                                                   // Releases GPU resources without rebuilding.
    float GetFluidSurfaceHeight() const;                                             // World-space Y plane where water begins.
    void SetFluidSurfaceHeight( float height );                                      // Moves the water plane without rebuilding collision geometry.
    void ApplyFluidSurfaceAdjustment( const FluidSurfaceAdjustment& adjustment,
                                      float deltaSeconds );                          // Applies typed input intent in world units.
    float GetGravity() const;                                                        // Gravitational acceleration in m/s^2; negative is downward.
    void SetGravity( float gravity );                                                // Updates gravity for future force integration ticks.
    float GetFluidDensity() const;                                                   // Fluid density in kg/m^3 for buoyancy and drag.
    void SetFluidDensity( float density );                                           // Updates fluid density for future force integration ticks.
    const Physics::MutualGravitySettings&
    GetMutualGravitySettings() const;                                                // Pairwise attraction settings for authored space scenes.
    void SetMutualGravitySettings( const Physics::MutualGravitySettings& settings ); // Updates future mutual-gravity ticks.
    Physics::PhysicsWorldForces GetPhysicsWorldForces() const;                       // Tick-local force inputs for physics-owned integration.

  private:
    // Snapshot of both render profiles plus the config-owned ocean controls.
    // The public config value is SkullbonezCore::Core::WaterRenderStyleSettings; this bound
    // copy also retains profile state needed across render calls.
    struct BoundWaterRenderStyleSettings
    {
        SkullbonezCore::Core::OrdinaryRenderConfig ordinary;                         // Ordinary water shader style from current runtime config.
        SkullbonezCore::Core::CinematicRenderConfig cinematicFallback;               // Used when cinematic render has no per-frame override.
        float oceanWaveHeight = 4.0f;                                                // Visual wave amplitude, not physics height.
        float oceanPerturbStrength = 0.002f;                                         // Reflection perturbation scale for water shaders.
    };

    struct WaterMeshBuildSettings
    {
        float frustumFar = 5500.0f;                                                  // Far plane used to size generated ocean water mesh.
    };

    struct FluidForceSettings
    {
        float angularDragMultiplier = 2.0f;                                          // Physics damping multiplier for submerged spin/drag.
    };

    float m_fluidSurfaceHeight;                                                      // World-space Y of the fluid surface (m).  Objects below this are submerged
    float m_fluidDensity;                                                            // Density of the fluid medium (kg/m³).  Water ≈ 1000, heavy oil ≈ 850
    float m_gasDensity;                                                              // Density of the gas medium above the surface (kg/m³).  Air ≈ 1.225
    float m_gravity;                                                                 // Gravitational acceleration (m/s², stored NEGATIVE for downward, e.g. -9.81)
    Physics::MutualGravitySettings m_mutualGravity;                                  // Optional body/body attraction copied into physics ticks.
    float m_terrainXMin = 0.0f;                                                      // terrain footprint — calm mesh bounds
    float m_terrainXMax = 0.0f;
    float m_terrainZMin = 0.0f;
    float m_terrainZMax = 0.0f;
    std::unique_ptr<Rendering::IMesh> m_calmMesh;                                    // Inner water: flat, reflective
    std::unique_ptr<Rendering::IShader> m_calmShader;
    std::unique_ptr<Rendering::IMesh> m_oceanMesh;                                   // Outer water: waves + perturbation
    std::unique_ptr<Rendering::IShader> m_oceanShader;
    BoundWaterRenderStyleSettings m_waterStyle;                                      // Owned water shader style subset; defaults support standalone worlds.
    WaterMeshBuildSettings m_waterMeshBuild;                                         // Owned water mesh rebuild subset.
    FluidForceSettings m_fluidForces;                                                // Owned fluid-force subset used by deterministic physics.
    Assets::AssetSystem* m_assets = nullptr;                                         // Borrowed asset registry for water shaders.
    Rendering::IRenderResourceFactory* m_resources = nullptr;                        // Borrowed active backend resource factory for water meshes.

    void BuildFluidMesh();                                                           // Builds calm and ocean meshes from current terrain bounds.
    void ApplyWaterAndFluidSettings( const SkullbonezCore::Core::EngineConfig&
                                         config );                                   // Copies only the water and fluid fields this type consumes.
    WaterStyleParams BuildCalmWaterStyle( bool cinematic,
                                          const SkullbonezCore::Core::CinematicRenderConfig& cinematicConfig ) const;
    WaterStyleParams BuildOceanWaterStyle( bool cinematic,
                                           const SkullbonezCore::Core::CinematicRenderConfig& cinematicConfig ) const;
    void BindCommonWaterStyle( Rendering::IShader& shader,
                               const WaterStyleParams& style,
                               const Math::Vector::Vector3& cameraWorld,
                               const WaterReflectionInput& reflection ) const;
    void BindCalmWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style ) const;
    void
    BindOceanWaterStyle( Rendering::IShader& shader, const WaterStyleParams& style, float time, bool flatWater ) const;
};
} // namespace Environment
} // namespace SkullbonezCore
