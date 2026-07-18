/*
File: SkullbonezSource/Runtime/RuntimeViewModel.cpp
Purpose:
  Builds runtime presentation snapshots from explicit presentation inputs.

Summary:
  The builder reads existing subsystem owners and copies only scalar UI-facing
  state, so no renderer, scene, or physics owner is exposed to presentation.

Glossary:
  View model: Read-only presentation snapshot assembled from runtime owners.
  RuntimeViewModelContext: Narrow borrowed view over the exact runtime owners
    required for scalar presentation.
  Scalar state: Small copyable values such as counts, flags, and indices.

Invariants:
  - Building the view model must not mutate subsystems.
  - The context is an explicit borrow packet; callers must pass live owners and
    the builder must copy out only presentation values.

Related:
  - SkullbonezSource/Runtime/RuntimeViewModel.h
*/
#include "RuntimeViewModel.h"

#include "CaptureController.h"
#include "Scene/SceneController.h"
#include "../Physics/PhysicsEngine.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
const char* ContactAudioFlashModeLabel( Runtime::Audio::ContactAudioFlashMode mode )
{
    using Runtime::Audio::ContactAudioFlashMode;
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
                               const Runtime::Audio::ContactAudioService& contactAudio )
{
    audio.enabled = contactAudio.IsEnabled();
    audio.available = contactAudio.IsAvailable();
    audio.debugCounters = contactAudio.DebugCountersEnabled();
    audio.flashMode = static_cast<int>( contactAudio.FlashMode() );
    audio.flashModeLabel = ContactAudioFlashModeLabel( contactAudio.FlashMode() );
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


RuntimeViewModel RuntimeViewModelBuilder::Build( const RuntimeViewModelContext& context )
{
    RuntimeViewModel view;

    const RunSceneState& scene = context.scene;
    const RunScreenshotState& screenshot = context.capture.Screenshot();
    const bool screenshotConfigured = screenshot.isScreenshotAndExit || screenshot.screenshotFrame >= 0 ||
                                      screenshot.screenshotMs >= 0 || screenshot.screenshotPath[0] != '\0' ||
                                      screenshot.screenshotInterval > 0;

    view.sceneMode = scene.isSceneMode;
    view.scenePhysics = scene.isScenePhysics;
    view.sceneText = scene.isSceneText;
    view.fixedStep = scene.isFixedStep;
    view.screenshotPending = screenshotConfigured && !screenshot.isScreenshotSaved;
    view.sceneIndex = scene.currentSceneIndex;
    view.sceneCount = context.sceneCount;
    view.frame = scene.currentFrame;
    view.targetFrameCount = scene.targetFrameCount;
    // Why: the UI displays a runtime count, but physics body rows are the
    // simulation snapshot authority. Do not ask SceneController to report a
    // model-order compatibility count for this presentation value.
    view.modelCount = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( context.world.Physics() ).Count();
    view.timeScale = scene.timeScale;
    view.presentationInterpolation = context.presentationInterpolation;
    view.presentationPinned = context.presentationPinned;
    view.presentationAlpha = std::clamp( context.presentationAlpha, 0.0f, 1.0f );
    return view;
}


RuntimeViewModel RuntimeViewModelBuilder::Build( const RuntimeViewModelContext& context,
                                                 const Runtime::Audio::ContactAudioService& contactAudio )
{
    RuntimeViewModel view = Build( context );

    FillContactAudioSnapshot( view.contactAudio, contactAudio );
    return view;
}
} // namespace Runtime
} // namespace SkullbonezCore
