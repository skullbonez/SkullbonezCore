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
  Borrowed sample path: Contact-audio asset path owned by the audio service and
    valid only for the current presentation snapshot.
  Min impact energy: Contact-audio threshold (joules) below which a collision
    stays silent; one of the two Sound-tab knobs alongside master gain.
  Presentation alpha: Bounded live interpolation fraction copied for UI
    diagnostics; capture pin state explains intentional alpha 1 frames.

Invariants:
  - View models are copies; consumers must not infer ownership from them.
  - Builder reads through RuntimeViewModelContext and leaves source systems
    untouched.

Related:
  - SkullbonezSource/Runtime/RunUiTextPass.cpp
*/
#pragma once

#include "Audio/ContactAudioService.h"

namespace SkullbonezCore
{
namespace Physics
{
class PhysicsEngine;
}

namespace Runtime
{
class CaptureController;
class SceneController;

constexpr int RUNTIME_CONTACT_AUDIO_SET_MAX = 16;
constexpr int RUNTIME_CONTACT_AUDIO_SAMPLE_MAX = 64;

struct RuntimeContactAudioSnapshot
{
    // The two live tuning knobs plus read-only sample/set browsing data for
    // the Sound tab. Everything else about contact audio is fixed policy
    // inside ContactAudioService.
    bool enabled = false;
    bool available = false;
    float masterGain = 0.0f;
    float minImpactEnergy = 125.0f; // Joules; the "big enough to hear" threshold.
    Runtime::Audio::ContactAudioStats stats;
    int soundSetCount = 0;
    int soundSampleCount = 0;
    const char* soundSamplePaths[RUNTIME_CONTACT_AUDIO_SAMPLE_MAX] = {};
    Runtime::Audio::ContactAudioSetTuning soundSets[RUNTIME_CONTACT_AUDIO_SET_MAX];
};

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
    RuntimeContactAudioSnapshot contactAudio;
};

struct RuntimeViewModelContext
{
    // Lifetime: Run builds this from owners that outlive the frame-local view
    // model rebuild. The builder copies values and never stores these borrows.
    const SceneController& scene;
    const CaptureController& capture;
    const Physics::PhysicsEngine& physics;
    bool presentationInterpolation = true;
    bool presentationPinned = false;
    float presentationAlpha = 1.0f;
};

class RuntimeViewModelBuilder
{
  public:
    static RuntimeViewModel Build( const RuntimeViewModelContext& context );
    static RuntimeViewModel Build( const RuntimeViewModelContext& context,
                                   const Runtime::Audio::ContactAudioService& contactAudio );
};
} // namespace Runtime
} // namespace SkullbonezCore
