/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.h
Purpose:
  Declares scene-runtime helpers that persist UI render defaults to engine.cfg.

Mental model:
  Runtime input decides when a user wants to save defaults. This module owns the
  config-file rewrite for the explicit render config payloads, keeping Run from
  acting as a persistence wrapper.

Glossary:
  engine.cfg: User-facing engine configuration file.
  Ordinary defaults: Non-cinematic render defaults.
  Sky defaults: Cinematic sky/render defaults saved from live UI state.

Invariants:
  - Helpers persist only the config payload passed by the caller.
  - Config key spellings are compatibility surface.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.cpp
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "../../Core/Config.h"

namespace SkullbonezCore
{
namespace Basics
{

bool SaveRenderDefaults( const OrdinaryRenderConfig& ordinary );
bool SaveSkyDefaults( const CinematicRenderConfig& cinematic );

} // namespace Basics
} // namespace SkullbonezCore
