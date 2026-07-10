/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h
Purpose:
  Declares live scene style and cinematic override helpers outside Run.

Mental model:
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
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneRuntime.h"
#include "../RunLaunchOptions.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace GameObjects
{
class GameModelCollection;
}
namespace Basics
{
class TestScene;

struct SceneRuntimeStyleContext
{
    RunLaunchOptions& launchOptions;
    RunSceneState& scene;
    RunSceneBrowserState& sceneBrowser;
    GameObjects::GameModelCollection& models;
    const Assets::AssetSystem& assets;
    CinematicRenderConfig& activeCinematic;
    const CinematicRenderConfig& defaultCinematic;
};

void ApplyCinematicSceneOverrides( CinematicRenderConfig& target, uint64_t mask, const CinematicRenderConfig& source );
bool ApplyCinematicModeFromBrowserIndex( SceneRuntimeStyleContext context, int index );
void ApplyLiveStyleScene( SceneRuntimeStyleContext context, const TestScene& styleScene );
bool ApplyDemoHeroStyleOverride( SceneRuntimeStyleContext context );

} // namespace Basics
} // namespace SkullbonezCore
