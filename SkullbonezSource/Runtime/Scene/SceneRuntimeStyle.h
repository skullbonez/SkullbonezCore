/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h
Purpose:
  Declares live scene style and cinematic override helpers outside Run.

Mental model:
  Scene style changes mutate render-facing scene state and object materials
  without rebuilding the active simulation. The caller still owns when a user
  action makes a run interactive; this module owns applying the style payload.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#pragma once

#include "SceneRuntime.h"
#include "../RunState.h"

#include <cstdint>

namespace SkullbonezCore
{
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
    CinematicRenderConfig& activeCinematic;
    const CinematicRenderConfig& defaultCinematic;
};

void ApplyCinematicSceneOverrides( CinematicRenderConfig& target, uint64_t mask, const CinematicRenderConfig& source );
bool ApplyCinematicModeFromBrowserIndex( SceneRuntimeStyleContext context, int index );
void ApplyLiveStyleScene( SceneRuntimeStyleContext context, const TestScene& styleScene );
bool ApplyDemoHeroStyleOverride( SceneRuntimeStyleContext context );

} // namespace Basics
} // namespace SkullbonezCore
