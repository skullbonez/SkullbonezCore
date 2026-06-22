/*
File: SkullbonezSource/Runtime/Editor/EditorTools.h
Purpose:
  Declares editor placement helpers shared by input routing and editor tools.

Mental model:
  Input owns gestures. Editor tools own how those gestures translate into
  editable object scale, clamp ranges, and placement semantics.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
*/
#pragma once

#include "EditorHullAssets.h"
#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
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
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
