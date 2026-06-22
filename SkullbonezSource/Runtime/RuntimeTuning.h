/*
File: SkullbonezSource/Runtime/RuntimeTuning.h
Purpose:
  Declares runtime tuning helpers for UI-driven render and worker settings.

Mental model:
  UI emits raw parameter changes. Runtime tuning clamps those values, updates
  live config, and records scene override bits so persistence remains explicit.

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
void ApplyWorkerThreadCountOverride( int requestedWorkerThreads );
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
