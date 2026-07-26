/*
File: SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h
Purpose:
  Declares replay overlay drawing entry points used by the late UI/text pass.

Summary:
  RuntimeRenderer decides pass order, but replay owns UI drawing plus the
  retained prediction command-list cursors used by the geometry pass.

Glossary:
  UI (User Interface): Runtime controls and overlays drawn over the 3D scene.
  UI text pass: Late overlay pass that invokes replay overlay drawing after
    scene rendering.
  Replay overlay: UI draw pass for replay timeline, prediction controls, and
    cause-tree inspection.
  Retained prediction list: Append-only trajectory chunks reused until the
    prediction generation, source bank, palette, or topology changes.
  All-body path: Space-scene future trajectory selected by body identity rather
    than contact-derived causality.
  Overlay state view: Read-only replay publication borrowed for one late pass.
  Render context: Overlay state plus the render-command target and window facts.

Invariants:
  - Replay state reaches the context only through the published overlay view.
  - Published references and sample pointers remain valid for one frame only.
  - Overlay functions consume every publication and render borrow synchronously.
  - A stable trajectory publication returns before traversing source records.
  - Legacy scrubber and cause-tree pixels draw only while the Legacy
    development surface owns presentation; ImGui consumes the same values in
    its own exclusive surface.

Related:
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#pragma once

#include "../Replay/ReplayAuthoring.h"
#include "ReplayOverlayPackets.h"
#include "../Replay/ReplayPresentation.h"
#include "../Replay/ReplayScrubber.h"

namespace SkullbonezCore::Rendering
{
class Dx12GeometryOwner;
class Dx12TextureOwner;
} // namespace SkullbonezCore::Rendering

namespace SkullbonezCore::Core
{
class Profiler;
}

namespace SkullbonezCore::Text
{
class TextBatch;
}

namespace SkullbonezCore::UI
{
class UIDrawList;
}

namespace SkullbonezCore::Physics
{
class ColliderStore;
class PhysicsBodyStore;
class PhysicsEngine;
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Runtime
{
class EditorTracer;
class UiDrawSubmission;
} // namespace SkullbonezCore::Runtime

namespace SkullbonezCore::Runtime::ReplayOverlay
{
void RenderReplayScrubberOverlay( UiDrawSubmission& submission,
                                  Text::TextBatch& textBatch,
                                  UI::UIDrawList& drawList,
                                  const ReplayOverlayStateView& replay,
                                  Rendering::Dx12TextureOwner& renderTextures,
                                  Rendering::Dx12GeometryOwner& renderCommands,
                                  Rendering::Dx12Diagnostics& renderDiagnostics,
                                  Core::Profiler* profiler,
                                  bool scenePhysicsEnabled,
                                  RuntimeInteractionGestureKind gesture,
                                  ReplayOverlayViewport viewport,
                                  double nowSeconds );
void RenderReplayInterceptOverlay( UiDrawSubmission& submission,
                                   Text::TextBatch& textBatch,
                                   UI::UIDrawList& drawList,
                                   const ReplayOverlayStateView& replay,
                                   Rendering::Dx12TextureOwner& renderTextures,
                                   Rendering::Dx12GeometryOwner& renderCommands,
                                   Rendering::Dx12Diagnostics& renderDiagnostics,
                                   int screenW,
                                   int screenH );
void RenderReplayTripPlannerOverlay( UiDrawSubmission& submission,
                                     Text::TextBatch& textBatch,
                                     UI::UIDrawList& drawList,
                                     const ReplayOverlayStateView& replay,
                                     Rendering::Dx12TextureOwner& renderTextures,
                                     Rendering::Dx12GeometryOwner& renderCommands,
                                     Rendering::Dx12Diagnostics& renderDiagnostics,
                                     int screenW,
                                     int screenH );
void RenderReplayPorkchopOverlay( UiDrawSubmission& submission,
                                  Text::TextBatch& textBatch,
                                  UI::UIDrawList& drawList,
                                  const ReplayOverlayStateView& replay,
                                  Rendering::Dx12TextureOwner& renderTextures,
                                  Rendering::Dx12GeometryOwner& renderCommands,
                                  Rendering::Dx12Diagnostics& renderDiagnostics,
                                  int screenW,
                                  int screenH );
void RenderReplayCauseTreeOverlay( UiDrawSubmission& submission,
                                   Text::TextBatch& textBatch,
                                   UI::UIDrawList& drawList,
                                   const ReplayOverlayStateView& replay,
                                   Rendering::Dx12TextureOwner& renderTextures,
                                   Rendering::Dx12GeometryOwner& renderCommands,
                                   Rendering::Dx12Diagnostics& renderDiagnostics,
                                   Core::Profiler* profiler,
                                   int screenW,
                                   int screenH );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
