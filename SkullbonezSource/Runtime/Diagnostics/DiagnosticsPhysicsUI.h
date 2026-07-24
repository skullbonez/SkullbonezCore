/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.h
Purpose:
  Declares the Runtime-owned boundary between Physics-tab commands and debug presentation state.

Summary:
  UI emits typed, owner-neutral commands. Runtime maps those commands to
  Physics-owned diagnostic flags and publishes a detached UI status snapshot.

Glossary:
  Overlay command: One-frame UI intent to toggle a diagnostic presentation layer.
  Detached status: Value-only UI data decoded from Runtime and Physics state.

Invariants:
  - UI never interprets Physics flags or mutates OverlayDebugState directly.
  - Returned status contains no retained Runtime or Physics owner reference.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp
  - SkullbonezSource/UI/UICommands.h
  - SkullbonezSource/Runtime/Diagnostics/OverlayDebugState.h
*/
#pragma once

namespace SkullbonezCore
{
namespace UI
{
struct UIPhysicsCommands;
struct UIPhysicsDebugStatus;
} // namespace UI
namespace Runtime
{
struct OverlayDebugState;

struct DiagnosticsPhysicsOverlayUICommandResult
{
    bool toggledCollisionVisualizer = false;
    bool toggledPhysicsDebugFlags = false;
    bool steppedPipelinePrevious = false;
    bool steppedPipelineNext = false;
    bool toggledPhysicsDebugTransparent = false;
    bool toggledBroadphaseOverlay = false;
};

struct DiagnosticsPhysicsDebugValueUICommandResult
{
    bool setAlpha = false;
    bool setContactLinger = false;
};

void StepDiagnosticsPhysicsPipelineStage( OverlayDebugState& debug, int direction );
DiagnosticsPhysicsOverlayUICommandResult
ApplyDiagnosticsPhysicsOverlayUICommands( OverlayDebugState& debug, const UI::UIPhysicsCommands& commands );
bool ApplyDiagnosticsTerrainContactProbeUICommand( OverlayDebugState& debug, const UI::UIPhysicsCommands& commands );
DiagnosticsPhysicsDebugValueUICommandResult
ApplyDiagnosticsPhysicsDebugValueUICommands( OverlayDebugState& debug, const UI::UIPhysicsCommands& commands );
UI::UIPhysicsDebugStatus BuildDiagnosticsPhysicsUIStatus( const OverlayDebugState& debug );

} // namespace Runtime
} // namespace SkullbonezCore
