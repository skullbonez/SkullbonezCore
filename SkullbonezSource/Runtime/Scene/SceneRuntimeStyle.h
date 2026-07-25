/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h
Purpose:
  Declares live scene style and cinematic override helpers outside Run.

Summary:
  Scene style changes mutate render-facing scene state and object materials
  without rebuilding the active simulation. The caller still owns when a user
  action makes a run interactive; this module owns applying the style payload.

Glossary:
  Asset system: Runtime-owned registry used to resolve logical scene/style asset
    references without falling back to process-global lookup.
  Cinematic override: Bitmask-selected render fields layered over defaults.
  Style scene: Authored scene used as material/cinematic source data.
  Live style: Runtime object/material changes applied without reloading.

Invariants:
  - Helpers do not create or destroy scene models.
  - Context borrows active cinematic, model state, and asset metadata only for
    the call.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneRuntime.h"
#include "../App/RunLaunchOptions.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Runtime
{
class AuthoredScene;
class SceneWorld;
struct OverlayDebugState;

struct SceneRuntimeStyleContext
{
    RunLaunchOptions& launchOptions;
    SceneSessionState& scene;
    SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser;
    // Lifetime: live-style code borrows the one scene-lifetime owner and then
    // resolves its entity/collider rows locally. Do not republish sibling
    // subowners or reach back through SceneController.
    SceneWorld& world;
    const Assets::AssetSystem& assets;
    SkullbonezCore::Core::CinematicRenderConfig& activeCinematic;
    const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic;
};

void ApplyCinematicSceneOverrides(
    SkullbonezCore::Core::CinematicRenderConfig& target,
    uint64_t mask,
    const SkullbonezCore::Core::CinematicRenderConfig& source
);
SkullbonezCore::Core::CinematicRenderConfig&
ActiveSceneCinematicConfig( SceneSessionState& scene, SkullbonezCore::Core::EngineConfig& config );
const SkullbonezCore::Core::CinematicRenderConfig&
ActiveSceneCinematicConfig( const SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config );
bool IsSceneCinematicRenderingEnabled(
    const SceneSessionState& scene,
    const SkullbonezCore::Core::EngineConfig& config,
    const RunLaunchOptions& launchOptions,
    const OverlayDebugState& debug,
    bool graphicsReady
);
bool ApplyCinematicModeFromBrowserIndex( SceneRuntimeStyleContext context, int index );
void ApplyLiveStyleScene( SceneRuntimeStyleContext context, const AuthoredScene& styleScene );
bool ApplyDemoHeroStyleOverride( SceneRuntimeStyleContext context );

} // namespace Runtime
} // namespace SkullbonezCore
