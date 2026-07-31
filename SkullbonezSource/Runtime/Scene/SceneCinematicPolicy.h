/*
File: SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h
Purpose:
  Declares cinematic render-state policy shared by scene-facing owners.

Summary:
  Scene style changes mutate render-facing scene state and object materials
  without rebuilding the active simulation. This policy also owns cinematic UI
  clamping/override bits and the pure sun-direction projection shared by render
  passes. Callers retain interaction and transaction ordering.

Invariants:
  - Helpers do not create or destroy scene models.
  - Helpers borrow active cinematic, model state, and asset metadata only for
    the call.
  - UI mutations update both persistent and UI-origin override masks together.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Style.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
  - Agentic/Reference/engine-glossary.md
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
namespace UI
{
enum class UICinematicFeature;
enum class UICinematicParam;
} // namespace UI
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
Math::Vector::Vector3 CinematicSkySunDirection( const SkullbonezCore::Core::CinematicRenderConfig& cinematic );
void ApplyCinematicUIParam( SkullbonezCore::Core::CinematicRenderConfig& cinematic, SceneSessionState& scene,
                            UI::UICinematicParam param, float rawValue );
void SetCinematicShadowsEnabledFromUI( SkullbonezCore::Core::CinematicRenderConfig& cinematic, SceneSessionState& scene,
                                       bool enabled );
void ToggleCinematicUIFeature( SkullbonezCore::Core::CinematicRenderConfig& cinematic, SceneSessionState& scene,
                               UI::UICinematicFeature feature );
} // namespace Runtime
} // namespace SkullbonezCore
