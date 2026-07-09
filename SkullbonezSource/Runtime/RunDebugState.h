/*
File: SkullbonezSource/Runtime/RunDebugState.h
Purpose:
  Owns Run's debug overlay, water/terrain visibility, and physics visualization toggles.

Mental model:
  Debug state is presentation state. It tells HUD, diagnostics, render passes,
  and scene reset/restore code which overlays to show, but it must not become
  simulation authority or change deterministic physics behavior.

Glossary:
  Overlay mode: HUD page selected by the operator or validation command line.
  Physics debug flags: Visualization mask for axes, contacts, sleep, pipeline,
  and terrain-contact overlays.
  Cross-scene pause lock: Operator-owned scene-flow stop that survives ordinary
  frame work until explicitly released.

Invariants:
  - Physics debug fields change drawing and diagnostics only; solver ordering,
    contact rows, and sleep decisions remain physics-owned.
  - Water/terrain visibility flags are render presentation toggles, not scene
    asset lifetime or authored scene data.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp
  - SkullbonezSource/Runtime/RunUiTextPass.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Physics/PhysicsDebugData.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Basics
{
enum class OverlayMode
{
    None,                                    // Clean screen, nothing shown
    Timers,                                  // Renderer name, model count, physics solver, profiler overlay
    SceneStats,                              // Scene telemetry values used by deterministic tests
    BarsNormalized,                          // Visual profiler bars, segments fill the bar width (relative)
    BarsAbsolute,                            // Visual profiler bars, white = idle/vsync (absolute frame budget)
    Keys,                                    // Keyboard reference panel
};

struct RunDebugState
{
    OverlayMode overlayMode =
        OverlayMode::None;                   // HUD overlay cycle state (0 key advances through timers, scene stats, bars, and keys)
    bool isWaterFreezeDebug = false;         // Freeze ocean animation at current shape (toggle with 1)
    bool isWaterNoReflect = false;           // Disable ocean reflection entirely (2 cycles: FBO to DXR to none)
    bool isWaterRTReflect = false;           // Use DXR ray-traced reflection (DXR only if supported)
    bool isWaterFlatDebug = false;           // Force ocean mesh fully flat, no displacement (toggle with 3)
    bool isTerrainHidden = false;            // Hide terrain mesh (toggle with 4)
    bool isWaterHidden = false;              // Hide water mesh (toggle with 5)
    uint32_t physicsDebugFlags =
        Physics::PHYSICS_DEBUG_NONE;         // Draw object axes, contact manifolds, and sleep state (cycle with C)
    bool isPhysicsDebugTransparent =
        false;                               // Draw translucent debug collision volumes behind physics debug lines (toggle with 6)
    float physicsDebugAlpha = 0.28f;         // Translucent debug volume alpha
    float physicsDebugContactLinger = 0.45f; // Seconds to keep contact manifolds visible after their solver row disappears
    int physicsDebugPipelineStageCursor = 0; // F7/F8-selected Catto pipeline stage for PHYSICS_DEBUG_PIPELINE
    bool isCollisionVisualizer = false;      // Render solid collision/sleep colours for balls and boxes (toggle with V)
    bool isTextOnly = false;                 // Suppress all 3D rendering; show solid background with large pangram text
    bool isUITestPattern = false;            // Bright 2D backdrop behind UI for visual blur tests
    bool isTopTextHidden = false;            // Hide top-left HUD text while leaving other overlays active
    bool isBroadphaseOverlay = false;        // Broadphase spatial grid visualizer overlay (toggle with G)
    bool isCrossScenePauseLocked = false;    // P-key scene-flow lock; Space is the only way to advance while active.
    float frozenWaterTime = 0.0f;            // Simulation time captured when freeze was toggled on
#ifdef _DEBUG
    char reproSnapshotMessage[128] = {};     // Short HUD confirmation after launcher-mode repro dump
    double reproSnapshotMessageUntil = 0.0;  // Simulation timer value after which the HUD message expires
#endif
};

} // namespace Basics
} // namespace SkullbonezCore
