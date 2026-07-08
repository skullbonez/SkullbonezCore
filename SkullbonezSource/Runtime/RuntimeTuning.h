/*
File: SkullbonezSource/Runtime/RuntimeTuning.h
Purpose:
  Declares runtime tuning helpers for UI-driven render, audio, and worker settings.

Mental model:
  UI emits raw parameter changes. Runtime tuning clamps those values or
  delegates them to bounded owner APIs, updates live config, and records scene
  override bits where persistence remains explicit.

Glossary:
  Cinematic config: HDR/post-processing and style settings for the active look.
  Ordinary render config: Non-cinematic renderer settings saved in engine.cfg.
  Override mask: Bitset recording which UI-touched scene values should persist.
  Sound command: One-frame UI packet that edits contact-audio presentation state.
  Worker override: Runtime request for the worker-pool thread count.

Invariants:
  - Render and cinematic helpers clamp raw UI values before writing runtime config.
  - Scene override bits and the changed value must stay paired.
  - Sound commands delegate value limits to ContactAudioService setters.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/UI/UICommands.h
*/
#pragma once

#include "RunInternal.h"

namespace SkullbonezCore
{
namespace Threading
{
class WorkerPool;
}

namespace Basics
{
namespace RunInternal
{
uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param );
uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature );
Math::Vector::Vector3 CinematicSkySunDirection( const CinematicRenderConfig& cinematic );
struct SoundUICommandContext
{
    // Lifetime: borrowed only while one Sound-tab command packet is applied.
    // The helper may lazily initialize contact audio, but it does not store any
    // service or settings references after returning.
    SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio;
    RunRuntimeSettings& runtimeSettings;
    bool contactAudioDisabledByLaunch = false;
};

void ApplyWorkerThreadCountOverride( EngineConfig& config,
                                     Threading::WorkerPool& workerPool,
                                     int requestedWorkerThreads );
void ApplyUIWorldOverride( WorldEnvironment& world,
                           ReplayRuntime& replayRuntime,
                           float gravity,
                           float fluidHeight,
                           float fluidDensity );
void ApplyCinematicUIParam( CinematicRenderConfig& cinematic,
                            RunSceneState& scene,
                            UICinematicParam param,
                            float rawValue );
void SetCinematicShadowsEnabledFromUI( CinematicRenderConfig& cinematic, RunSceneState& scene, bool enabled );
void ApplyOrdinaryRenderUIParam( OrdinaryRenderConfig& ordinary, UIRenderParam param, float rawValue );
bool ApplySoundUICommands( SoundUICommandContext context, const UI::UISoundCommands& commands );
void ToggleCinematicUIFeature( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicFeature feature );
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
