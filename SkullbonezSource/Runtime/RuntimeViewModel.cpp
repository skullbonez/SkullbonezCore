/*
File: SkullbonezSource/Runtime/RuntimeViewModel.cpp
Purpose:
  Builds runtime presentation snapshots from EngineContext.

Mental model:
  The builder reads existing subsystem owners and copies only scalar UI-facing
  state, so no renderer, scene, or physics owner is exposed to presentation.

Glossary:
  View model: Read-only presentation snapshot assembled from runtime owners.
  EngineContext: Bound view over subsystems owned by Run.
  Scalar state: Small copyable values such as counts, flags, and indices.

Invariants:
  - Building the view model must not mutate subsystems.
  - Debug builds assert before an unbound context can hide missing runtime
    service bindings; release builds still return a default snapshot.

Related:
  - SkullbonezSource/Runtime/RuntimeViewModel.h
  - SkullbonezSource/Runtime/EngineContext.h
*/
#include "RuntimeViewModel.h"

#include "CaptureController.h"
#include "EngineContext.h"
#include "RunState.h"
#include "Scene/SceneController.h"
#include "../Physics/PhysicsEngine.h"

#include <algorithm>
#include <cassert>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
const char* ContactAudioFlashModeLabel( ContactAudioFlashMode mode )
{
    switch ( mode )
    {
    case ContactAudioFlashMode::Off:
        return "Flash: Off";
    case ContactAudioFlashMode::Emitted:
        return "Flash: Emitted";
    case ContactAudioFlashMode::Candidates:
        return "Flash: Candidates";
    case ContactAudioFlashMode::Rejected:
        return "Flash: Rejected";
    default:
        return "Flash: Emitted";
    }
}


void FillContactAudioSnapshot( RuntimeContactAudioSnapshot& audio,
                               const Runtime::Audio::ContactAudioService& contactAudio,
                               const RunRuntimeSettings& runtimeSettings )
{
    audio.enabled = contactAudio.IsEnabled();
    audio.available = contactAudio.IsAvailable();
    audio.debugCounters = runtimeSettings.contactAudioDebugCounters;
    audio.flashMode = static_cast<int>( runtimeSettings.contactAudioFlashMode );
    audio.flashModeLabel = ContactAudioFlashModeLabel( runtimeSettings.contactAudioFlashMode );
    audio.masterGain = contactAudio.MasterGain();
    audio.maxDistanceScale = contactAudio.MaxDistanceScale();
    audio.minClosingSpeed = contactAudio.MinClosingSpeed();
    audio.minImpactScore = contactAudio.MinImpactScore();
    audio.impactScoreRangeSeconds = contactAudio.ImpactScoreRangeSeconds();
    audio.simpleMode = contactAudio.SimpleModeEnabled();
    audio.simpleMinLinearEnergy = contactAudio.SimpleMinLinearEnergy();
    audio.simpleMinLinearDeltaSpeed = contactAudio.SimpleMinLinearDeltaSpeed();
    audio.simpleLinearEnergyRange = contactAudio.SimpleLinearEnergyRange();
    audio.burstVoicesPerWindow = contactAudio.BurstVoicesPerWindow();
    audio.rollingLevelDb = contactAudio.RollingLevelDb();
    audio.rollingMaxDistance = contactAudio.RollingMaxDistance();
    audio.rollingMinSlipSpeed = contactAudio.RollingMinSlipSpeed();
    audio.rollingVoicesPerWindow = contactAudio.RollingVoicesPerWindow();
    audio.stats = contactAudio.Stats();
    audio.soundSetCount = (std::min)( contactAudio.SoundSetCount(), RUNTIME_CONTACT_AUDIO_SET_MAX );
    audio.soundSampleCount = (std::min)( contactAudio.SoundSampleCount(), RUNTIME_CONTACT_AUDIO_SAMPLE_MAX );

    // Lifetime: sample paths and set tuning strings remain borrowed from the
    // audio service. The view model is frame-local UI data, not an asset owner.
    for ( int setIndex = 0; setIndex < audio.soundSetCount; ++setIndex )
    {
        contactAudio.GetSoundSetTuning( setIndex, audio.soundSets[setIndex] );
    }
    for ( int sampleIndex = 0; sampleIndex < audio.soundSampleCount; ++sampleIndex )
    {
        audio.soundSamplePaths[sampleIndex] = contactAudio.SoundSamplePath( sampleIndex );
    }
}
} // namespace


RuntimeViewModel RuntimeViewModelBuilder::Build( const EngineContext& context )
{
    RuntimeViewModel view;
    assert( context.IsBound() && "RuntimeViewModelBuilder requires a bound EngineContext" );
    if ( !context.IsBound() )
    {
        return view;
    }

    const EngineContextBindings& bindings = context.Bindings();
    const RunSceneState& scene = bindings.scene->State();
    const RunScreenshotState& screenshot = bindings.capture->Screenshot();
    const bool screenshotConfigured = screenshot.isScreenshotAndExit || screenshot.screenshotFrame >= 0 ||
                                      screenshot.screenshotMs >= 0 || screenshot.screenshotPath[0] != '\0' ||
                                      screenshot.screenshotInterval > 0;

    view.sceneMode = scene.isSceneMode;
    view.scenePhysics = scene.isScenePhysics;
    view.sceneText = scene.isSceneText;
    view.fixedStep = scene.isFixedStep;
    view.screenshotPending = screenshotConfigured && !screenshot.isScreenshotSaved;
    view.sceneIndex = scene.currentSceneIndex;
    view.sceneCount = bindings.scene->QueueSize();
    view.frame = scene.currentFrame;
    view.targetFrameCount = scene.targetFrameCount;
    // Why: the UI displays a runtime count, but physics body rows are the
    // simulation snapshot authority. Do not ask GameModelCollection to report a
    // model-order compatibility count for this presentation value.
    view.modelCount = bindings.physics ? bindings.physics->BodyStore().Count() : 0;
    view.timeScale = scene.timeScale;
    return view;
}


RuntimeViewModel RuntimeViewModelBuilder::Build( const EngineContext& context,
                                                 const Runtime::Audio::ContactAudioService& contactAudio )
{
    RuntimeViewModel view = Build( context );
    if ( !context.IsBound() )
    {
        return view;
    }

    FillContactAudioSnapshot( view.contactAudio, contactAudio, *context.Bindings().runtimeSettings );
    return view;
}
} // namespace Basics
} // namespace SkullbonezCore
