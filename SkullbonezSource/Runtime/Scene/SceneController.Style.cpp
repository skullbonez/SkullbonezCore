/*
File: SkullbonezSource/Runtime/Scene/SceneController.Style.cpp
Purpose:
  Implements SceneController-owned live style and cinematic state changes.

Summary:
  SceneController retints existing renderable objects and updates cinematic
  presentation without rebuilding the scene. Partial authored styles merge over
  defaults, while a standalone snapshot replaces the complete authored surface.

Glossary:
  Material override: Authored material/tint applied to matching live models.

Invariants:
  - Style application mutates render-facing state only; it does not rebuild
    physics bodies or scene queues.
  - A standalone style clears curated-browser selection because its candidate
    is not a browser catalog row.
  - Ragdoll part matching uses suffix names and must stay compatible with
    authored generated ragdolls.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneCinematicPolicy.h"
#include "SceneController.h"
#include "../../Core/WindowConstants.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "SceneWorld.h"
#include "../../Physics/ColliderStore.h"
#include "../../Scene/AuthoredScene.h"
#include "../../Scene/StandaloneStyleWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
Math::Vector::Vector3 CinematicSkySunDirection( const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    constexpr float twoPi = 6.28318530718f;
    const float azimuth = std::clamp( cinematic.sunAzimuth, 0.0f, 1.0f ) * twoPi;
    const float elevation = -0.08f + std::clamp( cinematic.sunElevation, 0.0f, 1.0f ) * 1.13f;
    const float cosElevation = cosf( elevation );
    Math::Vector::Vector3 direction( sinf( azimuth ) * cosElevation, sinf( elevation ), cosf( azimuth ) * cosElevation );
    direction.Normalise();
    return direction;
}

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
    const char* owner = result.ErrorOwner() && result.ErrorOwner()[0] != '\0' ? result.ErrorOwner() : "Runtime/SceneStyle";
    const char* message = result.ErrorMessage()[0] != '\0' ? result.ErrorMessage()
                                                           : "style scene load failed without a message";

    std::fprintf( stderr, "[scene] scene_load_failed owner=%s path=\"%s\" reason=\"%s\"\n", owner, path, message );
}

bool IsBroadMaterialTarget( const char* target )
{
    return strcmp( target, "all" ) == 0 || strcmp( target, "balls" ) == 0 || strcmp( target, "boxes" ) == 0 ||
           strcmp( target, "hulls" ) == 0 || strcmp( target, "convex_hulls" ) == 0;
}

bool SceneMaterialTargetMatches( const char* target, const char* displayName, bool simpleRagdollPart,
                                 ColliderShapeKind shapeKind )
{

    // Invariant: Simple ragdoll parts keep their authored body materials; broad
    // style targets apply to ordinary scene bodies only. Exact and prefix
    // targets still opt a named ragdoll into scene-local showcase material.

    if ( simpleRagdollPart && IsBroadMaterialTarget( target ) )
    {
        return false;
    }

    if ( strcmp( target, "all" ) == 0 )
    {
        return true;
    }

    if ( strcmp( target, "balls" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::Sphere;
    }

    if ( strcmp( target, "boxes" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::Box;
    }

    if ( strcmp( target, "hulls" ) == 0 || strcmp( target, "convex_hulls" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::ConvexHull;
    }

    if ( strncmp( target, "prefix:", 7 ) == 0 )
    {
        const char* prefix = target + 7;
        return prefix[0] != '\0' && strncmp( displayName, prefix, strlen( prefix ) ) == 0;
    }

    return strcmp( target, displayName ) == 0;
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

            if ( SceneMaterialTargetMatches( material.target, entities.At( modelIndex ).displayName,
                                             entities.IsSimpleRagdollPart( modelIndex ), shapeKind ) )
            {
                entities.MutableAt( modelIndex ).renderMaterial = material.material;
            }
        }
    }
}

void ApplyObjectMaterials( SceneWorld& world, const SkullbonezCore::Scene::StandaloneStyleSnapshot& style )
{
    SceneEntityStore& entities = world.Entities();
    ResetObjectMaterials( world );
    const auto colliders = world.Colliders().Records();

    for ( const SkullbonezCore::Scene::StandaloneStyleMaterialRule& material : style.materialRules )
    {

        for ( int modelIndex = 0; modelIndex < world.SceneEntityCount(); ++modelIndex )
        {
            const ColliderShapeKind shapeKind = modelIndex < static_cast<int>( colliders.size() )
                                                    ? colliders[static_cast<std::size_t>( modelIndex )].shapeKind
                                                    : ColliderShapeKind::Sphere;

            if ( SceneMaterialTargetMatches( material.target.data(), entities.At( modelIndex ).displayName,
                                             entities.IsSimpleRagdollPart( modelIndex ), shapeKind ) )
            {
                entities.MutableAt( modelIndex ).renderMaterial = material.material;
            }
        }
    }
}
} // namespace


bool SceneController::ApplyCinematicBrowserStyle( RunLaunchOptions& launchOptions,
                                                  SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser,
                                                  const Assets::AssetSystem& assets,
                                                  SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                                  const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic,
                                                  int index )
{
    SceneSessionState& scene = State();
    SceneWorld& world = Scene();
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
    const SkullbonezCore::Core::SbResult loadResult = AuthoredScene::TryLoadFromFile( m_resultDiagnostics,
                                                                                      sceneBrowser.paths[index].c_str(),
                                                                                      assets, lookScene );

    if ( !loadResult.Ok() )
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


void SceneController::ApplyLiveStyle( RunLaunchOptions& launchOptions,
                                      SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser,
                                      SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                      const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic,
                                      const AuthoredScene& styleScene )
{
    SceneSessionState& scene = State();
    SceneWorld& world = Scene();
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


void SceneController::ApplyStandaloneStyle( RunLaunchOptions& launchOptions,
                                            SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser,
                                            SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                            const SkullbonezCore::Scene::StandaloneStyleSnapshot& style )
{
    SceneSessionState& scene = State();
    launchOptions.hasCinematicRenderingOverride = false;
    ApplyObjectMaterials( Scene(), style );
    activeCinematic = style.cinematic;

    if ( scene.isSceneMode )
    {
        scene.hasCinematicRenderingOverride = true;
        scene.isCinematicRenderingEnabled = activeCinematic.enabled;
        scene.hasCinematicExposure = true;
        scene.cinematicExposure = activeCinematic.exposure;
        scene.hasCinematicGamma = true;
        scene.cinematicGamma = activeCinematic.gamma;

        // Invariant: standalone styles carry every authorable cinematic field,
        // including the grouped shadow-participation value at bit 55.
        scene.cinematicOverrideMask = ( 1ull << 63 ) - 1ull;
        scene.uiCinematicOverrideMask = 0;
    }

    // UI coherence: a generated Look Lab candidate is not one of the curated
    // browser rows, so no stale catalog selection may remain highlighted.
    sceneBrowser.selectedCineModeSceneIndex = -1;
}


bool SceneController::ApplyDemoHeroStyle( RunLaunchOptions& launchOptions,
                                          SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser,
                                          const Assets::AssetSystem& assets,
                                          SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                          const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic )
{
    SceneSessionState& scene = State();

    if ( !launchOptions.demoHeroStyle || scene.isSceneMode )
    {
        return false;
    }

    const std::string stylePath = std::string( DATA_ROOT ) + "styles/low_poly_art_style.style.json";
    AuthoredScene styleScene;
    const SkullbonezCore::Core::SbResult loadResult = AuthoredScene::TryLoadStyleFromFile( m_resultDiagnostics,
                                                                                           stylePath.c_str(), assets,
                                                                                           styleScene );

    if ( !loadResult.Ok() )
    {
        LogStyleSceneLoadFailure( loadResult, stylePath.c_str() );
        return false;
    }

    ApplyLiveStyle( launchOptions, sceneBrowser, activeCinematic, defaultCinematic, styleScene );
    printf( "[scene] Applied low-poly hero rendering mode to generated demo scene.\n" );
    return true;
}

} // namespace Runtime
} // namespace SkullbonezCore
