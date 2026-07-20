/*
File: SkullbonezSource/Core/SceneCapacity.h
Purpose:
  Owns compile-time scene and presentation capacity constants shared by model,
  camera, texture, physics, replay, and render storage.

Summary:
  These are fixed storage budgets, not runtime policy decisions. Config and UI
  may choose lower active counts, but dense runtime stores use these constants
  as their hard compile-time ceilings.

Glossary:
  Scene capacity: Maximum number of model/body/collider rows the runtime can
    address in one loaded scene.
  Active capacity: Configured runtime limit clamped below the hard scene
    capacity before append/reserve boundaries.
  Slot count: Fixed camera or texture table size used by hash-key lookup.

Invariants:
  - MAX_SCENE_OBJECTS is the hard ceiling used by fixed arrays, handle stores,
    replay masks, and render instance buffers.
  - DEFAULT_SCENE_OBJECT_CAPACITY may be lower than MAX_SCENE_OBJECTS so startup
    reserves stay cheaper unless config raises the active cap.
  - TOTAL_CAMERA_COUNT and TOTAL_TEXTURE_COUNT must remain compile-time values
    because camera and texture owners use fixed-size arrays.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h applies active capacity.
  - SkullbonezSource/Physics/PhysicsBodyStore.h fixes body storage to the scene ceiling.
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

namespace SkullbonezCore::Scene::Capacity
{
constexpr int TOTAL_CAMERA_COUNT = 8;
constexpr int TOTAL_TEXTURE_COUNT = 8;
constexpr int DEFAULT_SCENE_OBJECT_CAPACITY = 4000;
constexpr int MAX_SCENE_OBJECTS = 8192;
constexpr int DEFAULT_SCENE_OBJECTS = 300;
} // namespace SkullbonezCore::Scene::Capacity
