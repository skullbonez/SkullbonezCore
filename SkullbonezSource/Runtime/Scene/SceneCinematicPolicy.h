/*
File: SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h
Purpose:
  Declares cinematic render-state policy shared by scene-facing owners.

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
  - Helpers borrow active cinematic, model state, and asset metadata only for
    the call.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Style.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneSessionState.h"
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

void ApplyCinematicSceneOverrides( SkullbonezCore::Core::CinematicRenderConfig& target, uint64_t mask,
                                   const SkullbonezCore::Core::CinematicRenderConfig& source );
SkullbonezCore::Core::CinematicRenderConfig& ActiveSceneCinematicConfig( SceneSessionState& scene,
                                                                         SkullbonezCore::Core::EngineConfig& config );
const SkullbonezCore::Core::CinematicRenderConfig&
ActiveSceneCinematicConfig( const SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config );
bool IsSceneCinematicRenderingEnabled( const SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config,
                                       const RunLaunchOptions& launchOptions, const OverlayDebugState& debug,
                                       bool graphicsReady );
} // namespace Runtime
} // namespace SkullbonezCore
