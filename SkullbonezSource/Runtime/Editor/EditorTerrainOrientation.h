/*
File: SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.h
Purpose:
  Declares the pure terrain-normal orientation policy used by editor placement.

Summary:
  Placement policy first decides whether an object follows terrain. This small
  boundary owns only the numerical conversion from a supplied normal to the
  quaternion that maps world up onto it, allowing that invariant to be tested
  without constructing editor assets or scene owners.

Glossary:
  Domain clamp: Explicit restriction of a normalized-vector dot to [-1, 1].

Invariants:
  - Zero normals produce identity because they define no orientation.
  - Antiparallel normals use world X as the deterministic 180-degree axis.
  - Every returned quaternion component is finite for finite input.

Related:
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp
  - SkullbonezSource/Maths/MathsCommon.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"

namespace SkullbonezCore::Runtime
{
Math::Orientation::Quaternion EditorTerrainOrientationFromNormal( Math::Vector::Vector3 terrainNormal );
} // namespace SkullbonezCore::Runtime
