/*
File: SkullbonezSource/Runtime/RunRuntimeSettings.h
Purpose:
  Owns live runtime render, physics presentation, contact-audio, and tornado settings.

Mental model:
  Runtime settings are live toggles copied from config at startup and then
  changed by UI, command-line overrides, replay restore, and scene reset code.
  They are not the owners of the renderer, physics world, or audio service; they
  are the narrow value packet those owners sample at explicit synchronization
  points.

Glossary:
  Contact-audio flash mode: Render-only selector for which audio decisions get
  a body flash after physics, independent of deterministic simulation.
  Tornado visual settings: Presentation-only sparse funnel shell values paired
  with physics-owned tornado force state.
  Pipeline sync: Diagnostic render mode that forces CPU/GPU synchronization for
  validation and investigation.

Invariants:
  - Physics-affecting settings must be mirrored through owner helpers, not read
    independently by multiple hot-path owners.
  - Contact-audio flash and tornado visuals are presentation state and must not
    alter deterministic solver output.

Related:
  - SkullbonezSource/Runtime/RuntimeTuning.cpp
  - SkullbonezSource/Runtime/Run.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/Config.h"
#include "../Physics/TornadoField.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}
namespace Basics
{
struct TornadoVisualSettings
{
    bool enabled = true;                                   // Render-only sparse funnel shell; physics force state remains separate.
    bool autoEnableWithTornado = true;                     // UI/CLI tornado toggles keep the production visual paired by default.
    float shellAlpha = 0.14f;
    float dustAlpha = 0.20f;
    float ribbonWidth = 5.5f;
    int ribbonCount = 7;
    int ribbonSegments = 48;
    int particleCount = 96;
    float rotationSpeed = 1.25f;
};

enum class ContactAudioFlashMode
{
    Off = 0,
    Emitted = 1,
    Candidates = 2,
    Rejected = 3,
    Count
};

struct RunRuntimeSettings
{
    bool isVsyncEnabled = true;                            // Swap-chain sync interval (true = vsync)
    bool isPipelineSyncEnabled = false;                    // Force CPU/GPU sync via Finish() before render
    bool isPhysicsSleepEnabled =
        true;                                              // Live Catto sleep policy; false keeps bodies awake while leaving collision/solving active
    bool contactAudioDebugCounters = false;                // Live optional contact-audio counter logging toggle.
    ContactAudioFlashMode contactAudioFlashMode =
        ContactAudioFlashMode::Emitted;                    // Render-only contact-audio diagnostic flash mode.
    Physics::TornadoFieldConfig tornadoField;              // Live vortex force/debug vector field controlled by CLI/UI
    Physics::TornadoSystemConfig tornadoSystem;            // Scene-authored multi-vortex schedule and motion.
    TornadoVisualSettings tornadoVisual;                   // Render-only tornado art tuning outside deterministic physics state.

    void ApplyStartupConfig( const EngineConfig& config ); // Copies config-owned live toggles at process startup.
    // Mirrors the deterministic tornado value pair into the live scene physics
    // owner at an explicit cold/UI synchronization point.
    void ApplyTornadoPhysics( GameObjects::GameModelCollection& models ) const;
};

} // namespace Basics
} // namespace SkullbonezCore
