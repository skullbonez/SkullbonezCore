/*
File: SkullbonezSource/Runtime/Scene/SceneControllerState.h
Purpose:
  Defines the Runtime-owned snapshot and policy for one scene load transaction.

Summary:
  UI owns the live navigation model. Runtime captures the load-relevant subset
  into this detached value and applies queue/load policy without retaining the
  UI owner.

Glossary:
  Load navigation snapshot: Detached browser paths, selection, and overrides
    consumed by one cold scene-load transaction.
  Navigation policy: Runtime decision that combines UI values with the concrete
    scene queue owner and returns a typed load request.

Invariants:
  - The snapshot owns its path strings and contains no UI pointer or callback.
  - Runtime navigation functions borrow the UI model only for the synchronous
    decision and retain no presentation authority.

Related:
  - SkullbonezSource/UI/UISceneNavigationModel.h
  - SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h
*/
#pragma once

#include "../../UI/UISceneNavigationModel.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
class SceneSession;
struct SceneLoadRequest;

// Value snapshot passed into one cold load transaction. Browser display names
// and c-string views stay with UI because load policy needs only normalized
// paths, the selected cinematic row, and generated-scene overrides.
struct SceneLoadNavigationState
{
    std::vector<std::string> browserPaths;
    SkullbonezCore::UI::RunSceneUIOverrideState overrides;
    int selectedCineModeSceneIndex = -1;

    SceneLoadRequest LoadSceneFromBrowserIndex( int index, SceneSession& scene ) const;
    SceneLoadRequest LoadDemoScene( SceneSession& scene ) const;
};

// Runtime owns navigation policy because these decisions borrow the concrete
// scene queue. The UI model remains a passive presentation value.
SceneLoadRequest LoadSceneFromBrowserIndex( const SkullbonezCore::UI::SceneNavigationModel& navigation, int index,
                                            SceneSession& scene );
int AdjacentCinematicModeBrowserIndex( const SkullbonezCore::UI::SceneNavigationModel& navigation, int direction,
                                       int currentSceneBrowserIndex, bool isCinematicTabActive );
SceneLoadRequest LoadAdjacentScene( const SkullbonezCore::UI::SceneNavigationModel& navigation, int direction,
                                    int currentSceneBrowserIndex, SceneSession& scene );
SceneLoadNavigationState CaptureSceneLoadNavigationState( const SkullbonezCore::UI::SceneNavigationModel& navigation );
void ApplySceneLoadNavigationState( SkullbonezCore::UI::SceneNavigationModel& navigation,
                                    const SceneLoadNavigationState& state );
} // namespace Runtime
} // namespace SkullbonezCore
