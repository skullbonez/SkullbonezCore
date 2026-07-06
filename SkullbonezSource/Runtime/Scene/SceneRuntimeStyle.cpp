/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp
Purpose:
  Applies live scene style and cinematic override state outside Run.

Mental model:
  Live style changes are scene-runtime behavior: they retint/reset existing
  renderable objects, apply material overrides, and merge authored cinematic
  fields over engine defaults without rebuilding the current scene.

Glossary:
  Style scene: Authored scene used only as a material/cinematic style source.
  Cinematic override: Bitmask-selected render fields layered over defaults.
  Material override: Authored material/tint applied to matching live models.

Invariants:
  - Style application mutates render-facing state only; it does not rebuild
    physics bodies or scene queues.
  - Ragdoll part matching uses suffix names and must stay compatible with
    authored generated ragdolls.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#include "SceneRuntimeStyle.h"
#include "../../GameObjects/GameModel.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Physics/ColliderStore.h"
#include "../../Scene/TestScene.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::GameObjects::GameModelCollection;
using SkullbonezCore::Physics::ColliderShapeKind;

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

bool IsBroadMaterialTarget( const char* target )
{
    return strcmp( target, "all" ) == 0 || strcmp( target, "balls" ) == 0 || strcmp( target, "boxes" ) == 0 ||
           strcmp( target, "hulls" ) == 0 || strcmp( target, "convex_hulls" ) == 0;
}

bool SceneMaterialTargetMatches( const SceneObjectMaterialOverride& material,
                                 const char* displayName,
                                 bool simpleRagdollPart,
                                 ColliderShapeKind shapeKind )
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

void ResetObjectMaterials( GameModelCollection& models )
{
    for ( int modelIndex = 0; modelIndex < models.GetModelCount(); ++modelIndex )
    {
        GameModel& model = models.GetModelAtIndex( modelIndex );
        if ( !models.IsSimpleRagdollPart( modelIndex ) )
        {
            model.SetRenderTint( 1.0f, 1.0f, 1.0f, 0.0f );
        }
    }
}

void ApplyObjectMaterials( GameModelCollection& models, const TestScene& styleScene )
{
    ResetObjectMaterials( models );
    const auto& colliders = models.GetPhysicsEngine().Colliders().Records();
    for ( int materialIndex = 0; materialIndex < styleScene.GetObjectMaterialOverrideCount(); ++materialIndex )
    {
        const SceneObjectMaterialOverride& material = styleScene.GetObjectMaterialOverride( materialIndex );
        for ( int modelIndex = 0; modelIndex < models.GetModelCount(); ++modelIndex )
        {
            const ColliderShapeKind shapeKind = modelIndex < static_cast<int>( colliders.size() )
                                                    ? colliders[static_cast<std::size_t>( modelIndex )].shapeKind
                                                    : ColliderShapeKind::Sphere;
            GameModel& model = models.GetModelAtIndex( modelIndex );
            if ( SceneMaterialTargetMatches( material,
                                             models.DisplayNameAt( modelIndex ),
                                             models.IsSimpleRagdollPart( modelIndex ),
                                             shapeKind ) )
            {
                model.SetRenderMaterial( material.material );
            }
        }
    }
}
} // namespace


void ApplyCinematicSceneOverrides( CinematicRenderConfig& target, uint64_t mask, const CinematicRenderConfig& source )
{
    // Concept: The mask is the compatibility boundary for authored cinematic
    // scenes; unset fields continue to inherit engine/default UI state.
#define APPLY_CINEMATIC_OVERRIDE( bit, field )                                                                         \
    if ( ( mask & ( bit ) ) != 0 )                                                                                     \
    {                                                                                                                  \
        target.field = source.field;                                                                                   \
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
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_SCREEN_X, sunScreenX )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_SCREEN_Y, sunScreenY )
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
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOWS, shadowsEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_MAP_SIZE, shadowMapSize )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_PCF_RADIUS, shadowPcfRadius )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_STRENGTH, shadowStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_SOFTNESS, shadowSoftness )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_DEPTH_BIAS, shadowDepthBias )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_SLOPE_BIAS, shadowSlopeBias )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_MAX_DISTANCE, shadowMaxDistance )
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


bool ApplyCinematicModeFromBrowserIndex( SceneRuntimeStyleContext context, int index )
{
    context.launchOptions.hasCinematicRenderingOverride = false;

    if ( index < 0 )
    {
        context.activeCinematic = context.defaultCinematic;
        if ( context.scene.isSceneMode )
        {
            context.scene.hasCinematicRenderingOverride = false;
            context.scene.isCinematicRenderingEnabled = context.activeCinematic.enabled;
            context.scene.hasCinematicExposure = false;
            context.scene.cinematicExposure = context.activeCinematic.exposure;
            context.scene.hasCinematicGamma = false;
            context.scene.cinematicGamma = context.activeCinematic.gamma;
            context.scene.cinematicOverrideMask = 0;
            context.scene.uiCinematicOverrideMask = 0;
        }
        ResetObjectMaterials( context.models );
        context.sceneBrowser.selectedCineModeSceneIndex = -1;
        return true;
    }

    if ( index >= static_cast<int>( context.sceneBrowser.paths.size() ) ||
         !IsCineScenePath( context.sceneBrowser.paths[index] ) )
    {
        return false;
    }

    TestScene lookScene = TestScene::LoadFromFile( context.sceneBrowser.paths[index].c_str(), context.assets );
    context.activeCinematic = context.defaultCinematic;
    ApplyCinematicSceneOverrides( context.activeCinematic,
                                  lookScene.GetCinematicOverrideMask(),
                                  lookScene.GetCinematicRenderConfig() );
    if ( context.scene.isSceneMode )
    {
        context.scene.hasCinematicRenderingOverride = lookScene.HasCinematicRenderingOverride();
        context.scene.isCinematicRenderingEnabled = lookScene.IsCinematicRenderingEnabled();
        context.scene.hasCinematicExposure = lookScene.HasCinematicExposure();
        context.scene.cinematicExposure = lookScene.GetCinematicExposure();
        context.scene.hasCinematicGamma = lookScene.HasCinematicGamma();
        context.scene.cinematicGamma = lookScene.GetCinematicGamma();
        context.scene.cinematicOverrideMask = lookScene.GetCinematicOverrideMask();
        context.scene.uiCinematicOverrideMask = 0;
    }
    ApplyObjectMaterials( context.models, lookScene );
    context.sceneBrowser.selectedCineModeSceneIndex = index;
    return true;
}


void ApplyLiveStyleScene( SceneRuntimeStyleContext context, const TestScene& styleScene )
{
    context.launchOptions.hasCinematicRenderingOverride = false;
    ApplyObjectMaterials( context.models, styleScene );

    context.activeCinematic = context.defaultCinematic;
    ApplyCinematicSceneOverrides( context.activeCinematic,
                                  styleScene.GetCinematicOverrideMask(),
                                  styleScene.GetCinematicRenderConfig() );
    if ( context.scene.isSceneMode )
    {
        context.scene.hasCinematicRenderingOverride = styleScene.HasCinematicRenderingOverride();
        context.scene.isCinematicRenderingEnabled = styleScene.IsCinematicRenderingEnabled();
        context.scene.hasCinematicExposure = styleScene.HasCinematicExposure();
        context.scene.cinematicExposure = styleScene.GetCinematicExposure();
        context.scene.hasCinematicGamma = styleScene.HasCinematicGamma();
        context.scene.cinematicGamma = styleScene.GetCinematicGamma();
        context.scene.cinematicOverrideMask = styleScene.GetCinematicOverrideMask();
        context.scene.uiCinematicOverrideMask = 0;
    }
    context.sceneBrowser.selectedCineModeSceneIndex = -1;
}


bool ApplyDemoHeroStyleOverride( SceneRuntimeStyleContext context )
{
    if ( !context.launchOptions.demoHeroStyle || context.scene.isSceneMode )
    {
        return false;
    }

    const std::string stylePath = std::string( DATA_ROOT ) + "styles/low_poly_art_style.style.json";
    const TestScene styleScene = TestScene::LoadStyleFromFile( stylePath.c_str(), context.assets );
    ApplyLiveStyleScene( context, styleScene );
    printf( "[scene] Applied low-poly hero rendering mode to generated demo scene.\n" );
    return true;
}

} // namespace Basics
} // namespace SkullbonezCore
