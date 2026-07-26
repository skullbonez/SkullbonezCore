/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp
Purpose:
  Applies live scene style and cinematic override state outside Run.

Summary:
  Live style changes are scene-runtime behavior: they retint/reset existing
  renderable objects, apply material overrides, and merge authored cinematic
  fields over engine defaults without rebuilding the current scene.

Glossary:
  Style scene: Authored scene used only as a material/cinematic style source.
  Cinematic override: Bitmask-selected render fields layered over defaults.
  Material override: Authored material/tint applied to matching live models.
  Lane R result: Recoverable style-load failure that returns diagnostics instead
    of crashing the active run.

Invariants:
  - Style application mutates render-facing state only; it does not rebuild
    physics bodies or scene queues.
  - Ragdoll part matching uses suffix names and must stay compatible with
    authored generated ragdolls.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "SceneRuntimeStyle.h"
#include "../../Core/WindowConstants.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "SceneWorld.h"
#include "../../Physics/ColliderStore.h"
#include "../../Scene/AuthoredScene.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
SkullbonezCore::Core::CinematicRenderConfig& ActiveSceneCinematicConfig( SceneSessionState& scene,
                                                                         SkullbonezCore::Core::EngineConfig& config )
{
    return scene.isSceneMode ? scene.cinematicRender : config.cinematicRender;
}


const SkullbonezCore::Core::CinematicRenderConfig&
ActiveSceneCinematicConfig( const SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config )
{
    return scene.isSceneMode ? scene.cinematicRender : config.cinematicRender;
}


bool IsSceneCinematicRenderingEnabled( const SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config,
                                       const RunLaunchOptions& launchOptions, const OverlayDebugState& debug,
                                       bool graphicsReady )
{
    const bool enabled = launchOptions.hasCinematicRenderingOverride ? launchOptions.cinematicRendering
                                                                     : ActiveSceneCinematicConfig( scene, config ).enabled;

    return enabled && graphicsReady && !debug.isTextOnly;
}


namespace
{
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Runtime::SceneController;

const char* FileNameFromPath( const char* path )
{

    if ( !path )
    {
        return "";
    }

    const char* slash = strrchr( path, '/' );
    const char* backslash = strrchr( path, '\\' );
    const char* separator = slash;

    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }

    return separator ? separator + 1 : path;
}

bool IsCineScenePath( const std::string& path )
{
    const char* name = FileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}

void LogStyleSceneLoadFailure( const SkullbonezCore::Core::SbResult& result, const char* path )
{
    const char* owner = result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "Runtime/SceneStyle";
    const char* message = result.error.message[0] != '\0' ? result.error.message
                                                          : "style scene load failed without a message";

    std::fprintf( stderr, "[scene] scene_load_failed owner=%s path=\"%s\" reason=\"%s\"\n", owner, path, message );
}

bool IsBroadMaterialTarget( const char* target )
{
    return strcmp( target, "all" ) == 0 || strcmp( target, "balls" ) == 0 || strcmp( target, "boxes" ) == 0 ||
           strcmp( target, "hulls" ) == 0 || strcmp( target, "convex_hulls" ) == 0;
}

bool SceneMaterialTargetMatches( const SceneObjectMaterialOverride& material, const char* displayName,
                                 bool simpleRagdollPart, ColliderShapeKind shapeKind )
{

    // Invariant: Simple ragdoll parts keep their authored body materials; broad
    // style targets apply to ordinary scene bodies only. Exact and prefix
    // targets still opt a named ragdoll into scene-local showcase material.

    if ( simpleRagdollPart && IsBroadMaterialTarget( material.target ) )
    {
        return false;
    }

    if ( strcmp( material.target, "all" ) == 0 )
    {
        return true;
    }

    if ( strcmp( material.target, "balls" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::Sphere;
    }

    if ( strcmp( material.target, "boxes" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::Box;
    }

    if ( strcmp( material.target, "hulls" ) == 0 || strcmp( material.target, "convex_hulls" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::ConvexHull;
    }

    if ( strncmp( material.target, "prefix:", 7 ) == 0 )
    {
        const char* prefix = material.target + 7;
        return prefix[0] != '\0' && strncmp( displayName, prefix, strlen( prefix ) ) == 0;
    }

    return strcmp( material.target, displayName ) == 0;
}

void ResetObjectMaterials( SceneWorld& world )
{
    SceneEntityStore& entities = world.Entities();

    for ( int modelIndex = 0; modelIndex < world.SceneEntityCount(); ++modelIndex )
    {

        if ( !entities.IsSimpleRagdollPart( modelIndex ) )
        {
            entities.MutableAt( modelIndex ).renderMaterial = Rendering::MakeRenderMaterialFromLegacyTint( 1.0f, 1.0f, 1.0f,
                                                                                                           0.0f );
        }
    }
}

void ApplyObjectMaterials( SceneWorld& world, const AuthoredScene& styleScene )
{
    SceneEntityStore& entities = world.Entities();
    ResetObjectMaterials( world );
    const auto colliders = world.Colliders().Records();

    for ( int materialIndex = 0; materialIndex < styleScene.GetObjectMaterialOverrideCount(); ++materialIndex )
    {
        const SceneObjectMaterialOverride& material = styleScene.GetObjectMaterialOverride( materialIndex );

        for ( int modelIndex = 0; modelIndex < world.SceneEntityCount(); ++modelIndex )
        {
            const ColliderShapeKind shapeKind = modelIndex < static_cast<int>( colliders.size() )
                                                    ? colliders[static_cast<std::size_t>( modelIndex )].shapeKind
                                                    : ColliderShapeKind::Sphere;

            if ( SceneMaterialTargetMatches( material, entities.At( modelIndex ).displayName,
                                             entities.IsSimpleRagdollPart( modelIndex ), shapeKind ) )
            {
                entities.MutableAt( modelIndex ).renderMaterial = material.material;
            }
        }
    }
}
} // namespace


void ApplyCinematicSceneOverrides( SkullbonezCore::Core::CinematicRenderConfig& target, uint64_t mask,
                                   const SkullbonezCore::Core::CinematicRenderConfig& source )
{

    // Concept: The mask is the compatibility boundary for authored cinematic
    // scenes; unset fields continue to inherit engine/default UI state.
#define APPLY_CINEMATIC_OVERRIDE( bit, field )                                                                              \
    if ( ( mask & ( bit ) ) != 0 )                                                                                          \
    {                                                                                                                       \
        target.field = source.field;                                                                                        \
    }

    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_RENDERING, enabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_ATMOSPHERE, skyAtmosphereEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUDS, cloudsEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_GOD_RAYS, godRaysEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_VOLUMETRIC_LIGHTING, volumetricLightingEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM, bloomEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG, fogEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_RELIEF_ENABLED, terrainReliefEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_EXPOSURE, exposure )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_GAMMA, gamma )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_AZIMUTH, sunAzimuth )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_ELEVATION, sunElevation )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_COLOR_R, sunColorR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_COLOR_G, sunColorG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_COLOR_B, sunColorB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_INTENSITY, sunIntensity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_HORIZON_R, skyHorizonR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_HORIZON_G, skyHorizonG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_HORIZON_B, skyHorizonB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_ZENITH_R, skyZenithR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_ZENITH_G, skyZenithG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_ZENITH_B, skyZenithB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_GLOW_STRENGTH, skyGlowStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUD_COVERAGE, cloudCoverage )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUD_SOFTNESS, cloudSoftness )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUD_SCALE, cloudScale )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUD_INTENSITY, cloudIntensity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_SHAFT_STRENGTH, sunShaftStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_SHAFT_FALLOFF, sunShaftFalloff )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_VOLUMETRIC_STRENGTH, volumetricStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_VOLUMETRIC_DENSITY, volumetricDensity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_VOLUMETRIC_DECAY, volumetricDecay )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM_THRESHOLD, bloomThreshold )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM_KNEE, bloomKnee )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM_STRENGTH, bloomStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM_RADIUS, bloomRadius )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_RELIEF, terrainRelief )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_DEPTH, basinDepth )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_RIM_LIFT, basinRimLift )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOWS, shadow.enabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_MAP_SIZE, shadow.mapSize )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_PCF_RADIUS, shadow.pcfRadius )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_STRENGTH, shadow.strength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_SOFTNESS, shadow.softness )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_DEPTH_BIAS, shadow.depthBias )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_SLOPE_BIAS, shadow.slopeBias )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_MAX_DISTANCE, shadow.maxDistance )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_COLOR_R, fogColorR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_COLOR_G, fogColorG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_COLOR_B, fogColorB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_START, fogStart )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_END, fogEnd )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_DENSITY, fogDensity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_MAX_OPACITY, fogMaxOpacity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_MODES, skyMode )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_MODES, terrainMode )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_MODES, objectStyle )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_MODES, waterMode )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_GRADE, styleSaturation )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_GRADE, styleContrast )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_GRADE, styleVignette )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_TINT, terrainTintR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_TINT, terrainTintG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_TINT, terrainTintB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_ACCENT, terrainAccentR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_ACCENT, terrainAccentG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_ACCENT, terrainAccentB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_GRID, terrainGridScale )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_GRID, terrainGridStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_TINT, waterTintR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_TINT, waterTintG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_TINT, waterTintB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_PROFILE, waterAlpha )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_PROFILE, waterReflectionStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_PROFILE, waterGlintStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinCenterX )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinCenterZ )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinRadiusX )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinRadiusZ )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinFeather )

#undef APPLY_CINEMATIC_OVERRIDE
}


bool ApplyCinematicModeFromBrowserIndex( RunLaunchOptions& launchOptions, SceneSessionState& scene,
                                         SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser, SceneWorld& world,
                                         const Assets::AssetSystem& assets,
                                         SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                         const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic, int index )
{
    launchOptions.hasCinematicRenderingOverride = false;

    if ( index < 0 )
    {
        activeCinematic = defaultCinematic;

        if ( scene.isSceneMode )
        {
            scene.hasCinematicRenderingOverride = false;
            scene.isCinematicRenderingEnabled = activeCinematic.enabled;
            scene.hasCinematicExposure = false;
            scene.cinematicExposure = activeCinematic.exposure;
            scene.hasCinematicGamma = false;
            scene.cinematicGamma = activeCinematic.gamma;
            scene.cinematicOverrideMask = 0;
            scene.uiCinematicOverrideMask = 0;
        }

        ResetObjectMaterials( world );
        sceneBrowser.selectedCineModeSceneIndex = -1;
        return true;
    }

    if ( index >= static_cast<int>( sceneBrowser.paths.size() ) || !IsCineScenePath( sceneBrowser.paths[index] ) )
    {
        return false;
    }

    AuthoredScene lookScene;
    const SkullbonezCore::Core::SbResult loadResult = AuthoredScene::TryLoadFromFile( sceneBrowser.paths[index].c_str(),
                                                                                      assets, lookScene );

    if ( !loadResult.ok )
    {
        LogStyleSceneLoadFailure( loadResult, sceneBrowser.paths[index].c_str() );
        return false;
    }

    activeCinematic = defaultCinematic;
    ApplyCinematicSceneOverrides( activeCinematic, lookScene.GetCinematicOverrideMask(),
                                  lookScene.GetCinematicRenderConfig() );

    if ( scene.isSceneMode )
    {
        scene.hasCinematicRenderingOverride = lookScene.HasCinematicRenderingOverride();
        scene.isCinematicRenderingEnabled = lookScene.IsCinematicRenderingEnabled();
        scene.hasCinematicExposure = lookScene.HasCinematicExposure();
        scene.cinematicExposure = lookScene.GetCinematicExposure();
        scene.hasCinematicGamma = lookScene.HasCinematicGamma();
        scene.cinematicGamma = lookScene.GetCinematicGamma();
        scene.cinematicOverrideMask = lookScene.GetCinematicOverrideMask();
        scene.uiCinematicOverrideMask = 0;
    }

    ApplyObjectMaterials( world, lookScene );
    sceneBrowser.selectedCineModeSceneIndex = index;
    return true;
}


void ApplyLiveStyleScene( RunLaunchOptions& launchOptions, SceneSessionState& scene,
                          SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser, SceneWorld& world,
                          SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                          const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic,
                          const AuthoredScene& styleScene )
{
    launchOptions.hasCinematicRenderingOverride = false;
    ApplyObjectMaterials( world, styleScene );

    activeCinematic = defaultCinematic;
    ApplyCinematicSceneOverrides( activeCinematic, styleScene.GetCinematicOverrideMask(),
                                  styleScene.GetCinematicRenderConfig() );

    if ( scene.isSceneMode )
    {
        scene.hasCinematicRenderingOverride = styleScene.HasCinematicRenderingOverride();
        scene.isCinematicRenderingEnabled = styleScene.IsCinematicRenderingEnabled();
        scene.hasCinematicExposure = styleScene.HasCinematicExposure();
        scene.cinematicExposure = styleScene.GetCinematicExposure();
        scene.hasCinematicGamma = styleScene.HasCinematicGamma();
        scene.cinematicGamma = styleScene.GetCinematicGamma();
        scene.cinematicOverrideMask = styleScene.GetCinematicOverrideMask();
        scene.uiCinematicOverrideMask = 0;
    }

    sceneBrowser.selectedCineModeSceneIndex = -1;
}


bool ApplyDemoHeroStyleOverride( RunLaunchOptions& launchOptions, SceneSessionState& scene,
                                 SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser, SceneWorld& world,
                                 const Assets::AssetSystem& assets,
                                 SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                 const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic )
{

    if ( !launchOptions.demoHeroStyle || scene.isSceneMode )
    {
        return false;
    }

    const std::string stylePath = std::string( DATA_ROOT ) + "styles/low_poly_art_style.style.json";
    AuthoredScene styleScene;
    const SkullbonezCore::Core::SbResult loadResult = AuthoredScene::TryLoadStyleFromFile( stylePath.c_str(), assets,
                                                                                           styleScene );

    if ( !loadResult.ok )
    {
        LogStyleSceneLoadFailure( loadResult, stylePath.c_str() );
        return false;
    }

    ApplyLiveStyleScene( launchOptions, scene, sceneBrowser, world, activeCinematic, defaultCinematic, styleScene );
    printf( "[scene] Applied low-poly hero rendering mode to generated demo scene.\n" );
    return true;
}

} // namespace Runtime
} // namespace SkullbonezCore
