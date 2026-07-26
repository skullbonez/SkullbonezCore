/*
File: SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.h
Purpose:
  Declares the synchronous operator-editor presentation composition boundary.

Summary:
  This boundary projects concrete runtime owners into the bounded value views
  consumed by the Legacy and ImGui operator surfaces. It owns presentation
  assembly while Run owns only frame sequencing.

Mental model:
  Run lends the owners and one output record at the late UI checkpoint. The
  composer samples them synchronously, fills that record once, and submits the
  selected frontend without retaining an owner or value pointer.

Glossary:
  Operator frame: One immutable value projection shared by both development UI
    surfaces during a rendered frame.
  Presentation composition: Synchronous sampling and conversion of runtime
    owner state into UI-facing values and draw submission inputs.

Invariants:
  - All borrows are synchronous and are never retained past Render().
  - Both operator surfaces consume the same OperatorEditorFrameView instance.
  - The composer owns UI projection decisions; Run supplies order and borrows.

Related:
  - RuntimeFrameViews.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

namespace SkullbonezCore
{
namespace UI
{
struct OperatorEditorFrameView;
} // namespace UI
namespace Runtime
{
class ReplayRuntime;
class RuntimeRenderer;
struct RuntimeFrameHostView;
struct RuntimeFrameInteractionView;
struct RuntimeFrameSceneView;
struct RuntimeUiTextFrameFacts;
struct RuntimeRenderModelFrameView;
namespace ReplayOverlay
{
struct ReplayOverlayStateView;
}
namespace OperatorEditorFrameComposer
{

// Samples the borrowed runtime owners into operatorEditorView and records the
// selected Legacy UI pass. No argument or nested pointer is retained.
void Render( RuntimeFrameHostView& host, RuntimeFrameInteractionView& interactionOwners, RuntimeFrameSceneView& sceneOwners,
             RuntimeRenderer& renderer, ReplayRuntime& replayRuntime, const RuntimeUiTextFrameFacts& facts,
             UI::OperatorEditorFrameView& operatorEditorView, const ReplayOverlay::ReplayOverlayStateView& replayOverlay,
             const RuntimeRenderModelFrameView& renderModels );
} // namespace OperatorEditorFrameComposer
} // namespace Runtime
} // namespace SkullbonezCore
