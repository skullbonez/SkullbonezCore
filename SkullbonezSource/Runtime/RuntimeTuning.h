/*
File: SkullbonezSource/Runtime/RuntimeTuning.h
Purpose:
  Declares runtime tuning helpers for UI-driven render and worker settings.

Mental model:
  UI emits raw parameter changes. Runtime tuning clamps those values, updates
  live config, and records scene override bits so persistence remains explicit.

Glossary:
  Cinematic config: HDR/post-processing and style settings for the active look.
  Ordinary render config: Non-cinematic renderer settings saved in engine.cfg.
  Override mask: Bitset recording which UI-touched scene values should persist.
  Worker override: Runtime request for the worker-pool thread count.

Invariants:
  - Helpers clamp raw UI values before writing runtime config.
  - Scene override bits and the changed value must stay paired.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/UI/UICommands.h
*/
#pragma once

#include "RunInternal.h"

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param );
uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature );
Math::Vector::Vector3 CinematicSkySunDirection( const CinematicRenderConfig& cinematic );
void ApplyWorkerThreadCountOverride( int requestedWorkerThreads );
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
void ToggleCinematicUIFeature( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicFeature feature );
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
