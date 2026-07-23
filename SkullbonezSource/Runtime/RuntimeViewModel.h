/*
File: SkullbonezSource/Runtime/RuntimeViewModel.h
Purpose:
  Defines the lightweight runtime view model consumed by UI and diagnostics.

Summary:
  RuntimeViewModel is a read-only snapshot of common runtime presentation data.
  It is rebuilt from an explicit presentation context rather than letting UI
  code chase storage owners directly.

Glossary:
  View model: Read-only presentation snapshot assembled from runtime owners.
  RuntimeViewModelContext: Narrow borrowed view of the scene, capture, runtime
    settings, and physics owners needed for presentation.
  Snapshot payload: Small copyable values such as counts, flags, indices, and
    bounded frame-local arrays.
  Presentation layer: UI or diagnostics code that reads state without owning it.
  Presentation alpha: Bounded live interpolation fraction copied for UI
    diagnostics; capture pin state explains intentional alpha 1 frames.

Invariants:
  - View models are copies; consumers must not infer ownership from them.
  - Builder reads through RuntimeViewModelContext and leaves source systems
    untouched.

Related:
  - SkullbonezSource/Runtime/UiTextPass.cpp
*/
#pragma once

namespace SkullbonezCore
{
namespace Physics
{
class PhysicsEngine;
}

namespace Runtime
{
class CaptureController;
class SceneWorld;
struct SceneSessionState;

struct RuntimeViewModel
{
    bool sceneMode = false;                // True when an authored scene is active
    bool scenePhysics = false;             // Active scene physics toggle
    bool sceneText = false;                // Active scene text overlay toggle
    bool fixedStep = false;                // Active fixed-step toggle
    bool screenshotPending = false;        // True when scene capture has not completed
    int sceneIndex = -1;                   // Current scene queue index
    int sceneCount = 0;                    // Number of queued scene entries
    int frame = 0;                         // Current per-load frame
    int targetFrameCount = -1;             // Completion frame target (-1 = unlimited)
    int modelCount = 0;                    // Current runtime model count
    float timeScale = 1.0f;                // Active simulation time scale
    bool presentationInterpolation = true; // Configured live render policy.
    bool presentationPinned = false;       // Capture/replay policy forced exact current state this frame.
    float presentationAlpha = 1.0f;        // Effective previous-to-current pose blend.
};

struct RuntimeViewModelContext
{
    // Lifetime: Run builds this from owners that outlive the frame-local view
    // model rebuild. The builder copies values and never stores these borrows.
    const SceneSessionState& scene;
    const SceneWorld& world;
    int sceneCount = 0;
    const CaptureController& capture;
    bool presentationInterpolation = true;
    bool presentationPinned = false;
    float presentationAlpha = 1.0f;
};

class RuntimeViewModelBuilder
{
  public:
    static RuntimeViewModel Build( const RuntimeViewModelContext& context );
};
} // namespace Runtime
} // namespace SkullbonezCore
