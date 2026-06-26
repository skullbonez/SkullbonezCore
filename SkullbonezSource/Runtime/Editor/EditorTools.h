/*
File: SkullbonezSource/Runtime/Editor/EditorTools.h
Purpose:
  Declares editor placement helpers shared by input routing and editor tools.

Mental model:
  Input owns gestures. Editor tools own how those gestures translate into
  editable object scale, clamp ranges, placement semantics, and editor command
  side effects that can be described with explicit borrowed context.

Glossary:
  Placement gesture: Mouse drag and wheel input used to size an object before
    placement commits.
  Hull scale: Per-axis size multiplier for convex hull editor assets.
  Uniform scale: One shared size value applied to all axes.
  Scale lock: Rule that keeps authored multi-part tree/root proportions stable.

Invariants:
  - Scale helpers must be deterministic and side-effect free.
  - Command helpers must take every mutable service through an explicit context.
  - Object-type helpers must stay aligned with the editor tab object enum.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
*/
#pragma once

#include "EditorHullAssets.h"
#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace GameObjects
{
class GameModelCollection;
}
namespace Basics
{
class RuntimeCommandQueue;
class RuntimeInputContext;
struct RunSceneState;

namespace RunInternal
{
struct EditorSaveHotkeyContext
{
    RuntimeInputContext& input;
    GameObjects::GameModelCollection& models;
    const RunSceneState& scene;
    Environment::WorldEnvironment& world;
    Environment::CameraCollection& cameras;
    RuntimeCommandQueue& commands;
};

int EditorMouseWheelSteps( int wheelDelta );
Assets::EditorHullAsset EditorHullAssetForType( int objectType );
bool EditorPlacementUsesUniformScale( int objectType );
bool EditorPlacementUsesHullScaleFactors( int objectType );
bool EditorPlacementUsesTreeScaleLock( int objectType );
Math::Vector::Vector3 EditorDefaultPlacementScale( int objectType );
Math::Vector::Vector3 EditorClampPlacementScale( int objectType, const Math::Vector::Vector3& scale );
Math::Vector::Vector3 EditorPlacementScaleFromGesture( int objectType,
                                                       const Math::Vector::Vector3& startScale,
                                                       float dragPixelsX,
                                                       float dragPixelsY,
                                                       int wheelSteps );
void HandleEditorSaveHotkeys( EditorSaveHotkeyContext context );
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
