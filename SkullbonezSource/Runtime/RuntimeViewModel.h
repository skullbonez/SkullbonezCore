/*
File: SkullbonezSource/Runtime/RuntimeViewModel.h
Purpose:
  Defines the lightweight runtime view model consumed by UI and diagnostics.

Mental model:
  RuntimeViewModel is a read-only snapshot of common runtime presentation data.
  It is rebuilt from EngineContext and Run-owned presentation services rather
  than letting UI code chase storage owners directly.

Glossary:
  View model: Read-only presentation snapshot assembled from runtime owners.
  EngineContext: Bound view over subsystems owned by Run.
  Snapshot payload: Small copyable values such as counts, flags, indices, and
    bounded frame-local arrays.
  Presentation layer: UI or diagnostics code that reads state without owning it.
  Borrowed sample path: Contact-audio asset path owned by the audio service and
    valid only for the current presentation snapshot.
  Contact-audio flash mode: Render-only diagnostic selector for emitted,
    candidate, rejected, or hidden contact-audio decisions.

Invariants:
  - View models are copies; consumers must not infer ownership from them.
  - Builder reads through EngineContext and leaves source systems untouched.

Related:
  - SkullbonezSource/Runtime/EngineContext.h
  - SkullbonezSource/Runtime/RunUiTextPass.cpp
*/
#pragma once

#include "Audio/ContactAudioService.h"

namespace SkullbonezCore
{
namespace Basics
{
class EngineContext;

constexpr int RUNTIME_CONTACT_AUDIO_SET_MAX = 16;
constexpr int RUNTIME_CONTACT_AUDIO_SAMPLE_MAX = 64;

struct RuntimeContactAudioSnapshot
{
    bool enabled = false;
    bool available = false;
    bool debugCounters = false;
    int flashMode = 1;
    const char* flashModeLabel = "Flash: Emitted";
    float masterGain = 0.0f;
    float maxDistanceScale = 1.0f;
    float minClosingSpeed = 0.0f;
    float minImpactScore = 0.0f;
    float impactScoreRangeSeconds = 1.0f;
    uint32_t burstVoicesPerWindow = 0; // Max submitted contact sounds per 100 ms burst.
    Runtime::Audio::ContactAudioStats stats;
    int soundSetCount = 0;
    int soundSampleCount = 0;
    const char* soundSamplePaths[RUNTIME_CONTACT_AUDIO_SAMPLE_MAX] = {};
    Runtime::Audio::ContactAudioSetTuning soundSets[RUNTIME_CONTACT_AUDIO_SET_MAX];
};

struct RuntimeViewModel
{
    bool sceneMode = false;            // True when an authored scene is active
    bool scenePhysics = false;         // Active scene physics toggle
    bool sceneText = false;            // Active scene text overlay toggle
    bool fixedStep = false;            // Active fixed-step toggle
    bool screenshotPending = false;    // True when scene capture has not completed
    int sceneIndex = -1;               // Current scene queue index
    int sceneCount = 0;                // Number of queued scene entries
    int frame = 0;                     // Current per-load frame
    int targetFrameCount = -1;         // Completion frame target (-1 = unlimited)
    int modelCount = 0;                // Current runtime model count
    float timeScale = 1.0f;            // Active simulation time scale
    RuntimeContactAudioSnapshot contactAudio;
};

class RuntimeViewModelBuilder
{
  public:
    static RuntimeViewModel Build( const EngineContext& context );
};
} // namespace Basics
} // namespace SkullbonezCore
