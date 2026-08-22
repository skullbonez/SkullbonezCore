/*
File: SkullbonezSource/Runtime/UI/RuntimeViewModel.h
Purpose:
  Defines the lightweight runtime view model consumed by UI and diagnostics.

Summary:
  RuntimeViewModel is a read-only snapshot of common runtime presentation data.
  It is rebuilt from explicit presentation operands rather than letting UI code
  chase storage owners directly.

Glossary:
  Snapshot payload: Small copyable values such as counts, flags, indices, and
    bounded frame-local arrays.
  Presentation layer: UI or diagnostics code that reads state without owning it.

Invariants:
  - View models are copies; consumers must not infer ownership from them.
  - Builder reads concrete source owners and leaves them untouched.

Related:
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
  - Agentic/Reference/engine-glossary.md
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
    bool fixedStep = false;                // Scene/capture render-frame-lockstep request; not live pacing authority
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

class RuntimeViewModelBuilder
{
  public:
    static RuntimeViewModel Build( const SceneSessionState& scene, const SceneWorld& world, int sceneCount,
                                   const CaptureController& capture, bool presentationInterpolation, bool presentationPinned,
                                   float presentationAlpha );
};
} // namespace Runtime
} // namespace SkullbonezCore
