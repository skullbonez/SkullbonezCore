/*
File: SkullbonezSource/Runtime/Editor/EditorOverlayTools.h
Purpose:
  Declares editor preview and tool-overlay trace helpers used by runtime input
  and render callback composition.

Summary:
  Calling owners apply runtime side effects. Editor overlay helpers refresh
  preview state and append deterministic tool geometry to the shared tracer
  from explicit borrowed state.

Glossary:
  Asset system: Runtime-owned registry borrowed by placement preview when a
    placeable recipe lives in an asset library.
  Preview state: Editor-owned placement or gizmo data refreshed from current
    input before the render overlay is built.
  Tool overlay trace: Deterministic line/ghost geometry appended for editor,
    inspect, raycast, and mouse-pickup feedback.
  UI (User Interface): Runtime panels that can claim mouse input before editor
    tools see it.
  Body store: Physics-owned body rows used for tool overlay pose authority.
  Collider store: Physics-owned shape records used for shape-accurate overlays.

Invariants:
  - Helpers borrow all mutable state through context structs; they do not cache
    pointers or own runtime services.
  - Preview updates may mutate editor state, but overlay tracing must be a
    read-only projection of the current tool state.
  - Placement preview uses the same borrowed asset registry as placement commit.

Related:
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - SkullbonezSource/Runtime/App/Run.cpp
*/
#pragma once

#include "../../Maths/Vector3.h"
#include "../Interaction/RuntimeInteractionController.h"

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Runtime
{
class SceneController;
}
namespace Geometry
{
class Terrain;
}
namespace Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
} // namespace Physics
namespace Runtime
{
class RuntimeInteractionController;
class SceneWorld;
class EditorTracer;
struct RunEditorPlacementState;
struct RunMousePickupState;
struct RunRayCastTestState;

namespace RunInternal
{
// Lifetime: preview and trace passes borrow the scene-lifetime owner once and
// retain no store pointer after the frame-local call returns.
struct EditorInteractionPreviewContext
{
    RunEditorPlacementState& editor;
    SceneWorld& world;
    RuntimeInteractionController& interaction;
    const Assets::AssetSystem& assets;
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
    const SceneWorld& world;
    const Assets::AssetSystem& assets;
    EditorTracer& tracer;
};

struct EditorToolOverlayTraceInput
{
    float rayLingerSeconds = 0.0f;
    bool inspectGizmoActive = false;
    bool scaleMode = false;
    RuntimeInteractionGesture gesture;
    int attachedCameraTargetIndex = -1;
    bool attachedCameraActiveFollow = false;
};

EditorInteractionPreviewResult
UpdateEditorInteractionPreview( EditorInteractionPreviewContext context, const EditorInteractionPreviewInput& input );
void BuildEditorToolOverlayTrace( EditorToolOverlayTraceContext context, const EditorToolOverlayTraceInput& input );
} // namespace RunInternal
} // namespace Runtime
} // namespace SkullbonezCore
