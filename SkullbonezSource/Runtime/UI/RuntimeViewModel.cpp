/*
File: SkullbonezSource/Runtime/UI/RuntimeViewModel.cpp
Purpose:
  Builds runtime presentation snapshots from explicit presentation inputs.

Summary:
  The builder reads existing subsystem owners and copies only scalar UI-facing
  state, so no renderer, scene, or physics owner is exposed to presentation.

Glossary:
  View model: Read-only presentation snapshot assembled from runtime owners.
  Scalar state: Small copyable values such as counts, flags, and indices.

Invariants:
  - Building the view model must not mutate subsystems.
  - Callers pass live owners and the builder copies out only presentation values.

Related:
  - SkullbonezSource/Runtime/UI/RuntimeViewModel.h
*/
#include "RuntimeViewModel.h"

#include "../Capture/CaptureController.h"
#include "../Scene/SceneController.h"
#include "../../Physics/PhysicsEngine.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Runtime
{
RuntimeViewModel RuntimeViewModelBuilder::Build( const SceneSessionState& scene, const SceneWorld& world, int sceneCount,
                                                 const CaptureController& capture, bool presentationInterpolation,
                                                 bool presentationPinned, float presentationAlpha )
{
    RuntimeViewModel view;

    const RunScreenshotState& screenshot = capture.Screenshot();
    const bool screenshotConfigured = screenshot.isScreenshotAndExit || screenshot.screenshotFrame >= 0 ||
                                      screenshot.screenshotMs >= 0 || screenshot.screenshotPath[0] != '\0' ||
                                      screenshot.screenshotInterval > 0;

    view.sceneMode = scene.isSceneMode;
    view.scenePhysics = scene.isScenePhysics;
    view.sceneText = scene.isSceneText;
    view.fixedStep = scene.isFixedStep;
    view.screenshotPending = screenshotConfigured && !screenshot.isScreenshotSaved;
    view.sceneIndex = scene.currentSceneIndex;
    view.sceneCount = sceneCount;
    view.frame = scene.currentFrame;
    view.targetFrameCount = scene.targetFrameCount;

    // Why: the UI displays a runtime count, but physics body rows are the
    // simulation snapshot authority. Do not ask SceneController to report a
    // model-order compatibility count for this presentation value.
    view.modelCount = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( world.Physics() ).Count();
    view.timeScale = scene.timeScale;
    view.presentationInterpolation = presentationInterpolation;
    view.presentationPinned = presentationPinned;
    view.presentationAlpha = std::clamp( presentationAlpha, 0.0f, 1.0f );
    return view;
}

} // namespace Runtime
} // namespace SkullbonezCore
