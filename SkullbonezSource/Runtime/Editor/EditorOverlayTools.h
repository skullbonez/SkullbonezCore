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
  Preview state: Editor-owned placement or gizmo data refreshed from current
    input before the render overlay is built.
  Tool overlay trace: Deterministic line/ghost geometry appended for editor,
    inspect, raycast, and mouse-pickup feedback.

Invariants:
  - Helpers take explicit frame-local borrows; they do not cache pointers or
    own runtime services.
  - Preview updates may mutate editor state, but overlay tracing must be a
    read-only projection of the current tool state.
  - Placement preview uses the same borrowed asset registry as placement commit.

Related:
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - SkullbonezSource/Runtime/App/Run.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Maths/Vector3.h"
#include "../Interaction/RuntimeInteractionController.h"

namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore;
}
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
struct RunEditorPlacementState;


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

EditorInteractionPreviewResult UpdateEditorInteractionPreview( Core::SbDiagnosticStore& diagnostics,
                                                               RunEditorPlacementState& editor, SceneWorld& world,
                                                               RuntimeInteractionController& interaction,
                                                               const Assets::AssetSystem& assets,
                                                               const EditorInteractionPreviewInput& input );
} // namespace Runtime
} // namespace SkullbonezCore
