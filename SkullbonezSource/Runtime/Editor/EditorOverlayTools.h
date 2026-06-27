/*
File: SkullbonezSource/Runtime/Editor/EditorOverlayTools.h
Purpose:
  Declares editor preview and tool-overlay trace helpers used by runtime input
  and render callback composition.

Mental model:
  Run still owns runtime side effects. Editor overlay helpers refresh editor
  preview state and append deterministic tool geometry to the shared tracer from
  explicit borrowed state.

Glossary:
  Preview state: Editor-owned placement or gizmo data refreshed from current
    input before the render overlay is built.
  Tool overlay trace: Deterministic line/ghost geometry appended for editor,
    inspect, raycast, and mouse-pickup feedback.
  UI (User Interface): Runtime panels that can claim mouse input before editor
    tools see it.

Invariants:
  - Helpers borrow all mutable state through context structs; they do not cache
    pointers or own runtime services.
  - Preview updates may mutate editor state, but overlay tracing must be a
    read-only projection of the current tool state.

Related:
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - SkullbonezSource/Runtime/Run.cpp
*/
#pragma once

#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}
namespace Geometry
{
class Terrain;
}
namespace Basics
{
class RuntimeInteractionController;
class RunEditorTracer;
struct RunEditorPlacementState;
struct RunMousePickupState;
struct RunRayCastTestState;

namespace RunInternal
{
struct EditorInteractionPreviewContext
{
    RunEditorPlacementState& editor;
    GameObjects::GameModelCollection& models;
    RuntimeInteractionController& interaction;
    Geometry::Terrain* terrain;
};

struct EditorInteractionPreviewInput
{
    bool uiBlocksCameraMouse = false;
    bool inspectGizmoActive = false;
    bool hasMouseRay = false;
    Math::Vector::Vector3 mouseRayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 mouseRayDirection = Math::Vector::ZERO_VECTOR;
    bool scaleMode = false;
};

struct EditorInteractionPreviewResult
{
    bool clearInvalidSelection = false;
    bool inspectSelectionScope = false;
};

struct EditorToolOverlayTraceContext
{
    const RunEditorPlacementState& editor;
    const RunRayCastTestState& rayCastTest;
    const RunMousePickupState& mousePickup;
    const GameObjects::GameModelCollection& models;
    RunEditorTracer& tracer;
};

struct EditorToolOverlayTraceInput
{
    float rayLingerSeconds = 0.0f;
    bool inspectGizmoActive = false;
    bool scaleMode = false;
    int attachedCameraTargetIndex = -1;
    bool attachedCameraActiveFollow = false;
};

EditorInteractionPreviewResult UpdateEditorInteractionPreview( EditorInteractionPreviewContext context,
                                                               const EditorInteractionPreviewInput& input );
void BuildEditorToolOverlayTrace( EditorToolOverlayTraceContext context, const EditorToolOverlayTraceInput& input );
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
