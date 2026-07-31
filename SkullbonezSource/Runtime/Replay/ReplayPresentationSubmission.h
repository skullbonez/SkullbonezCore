/*
File: ReplayPresentationSubmission.h
Purpose:
  Declares narrow helpers shared by replay geometry-submission units.

Summary:
  Presentation selection has already finished before these operations resolve
  typed Physics rows and emit tracer markers for the chosen values.

Invariants:
  - Submission resolves stable scene ids through typed Physics handles.
  - These operations cannot mutate scrub, timeline, or prediction selection.

Related:
  - ReplayPredictionDrawing.cpp
  - ReplayCauseFocusSubmission.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Physics/PhysicsHandles.h"

namespace SkullbonezCore::Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Runtime
{
class EditorTracer;
}

namespace SkullbonezCore::Runtime::ReplayPresentationSubmissionOperations
{
bool TryResolveReplayBodyModelIndex( const Physics::PhysicsBodyStore& bodyStore, Physics::PhysicsSceneObjectId id,
                                     Physics::ModelRowHint& hint, int modelCount, int& outModelIndex );
bool TryAddReplayTargetMarkerFromStores( EditorTracer& tracer, const Physics::PhysicsBodyStore& bodyStore,
                                         const Physics::ColliderStore& colliderStore, int modelIndex );
} // namespace SkullbonezCore::Runtime::ReplayPresentationSubmissionOperations
