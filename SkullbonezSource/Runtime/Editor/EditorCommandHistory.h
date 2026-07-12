/*
File: SkullbonezSource/Runtime/Editor/EditorCommandHistory.h
Purpose:
  Defines the editor-owned fixed-capacity inverse-command history.

Mental model:
  Editor mutations publish a complete fixed-size before/after command only when
  they commit. Undo and redo move a cursor over those commands; the scene owner
  applies the selected side and advances the cursor only after success.

Glossary:
  History cursor: Boundary between applied commands and the redo suffix.
  Primitive recipe: Fixed recreation facts for one standalone sphere or box.
  Transform item: One scene-identity pose/shape pair inside a coalesced gesture.

Invariants:
  - Storage is an inline 64-entry array and never allocates.
  - New edits after undo truncate the redo suffix.
  - Overflow drops the oldest entry; exhaustion is ordinary editor behavior.
  - Entries key live objects only by PhysicsSceneObjectId, never handles or rows.

Related:
  - SkullbonezSource/Runtime/Editor/RunEditorHistory.cpp
  - SkullbonezTests/TestEditorCommandHistory.cpp
  - Agentic/Reports/2026-07-12/editor-undo-redo-closure.md
*/
#pragma once

#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/CollisionShape.h"
#include "../Scene/SceneEntityStore.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Basics
{
constexpr std::size_t EDITOR_COMMAND_HISTORY_CAPACITY = 64;
constexpr std::size_t EDITOR_COMMAND_TRANSFORM_CAPACITY = 16;

enum class EditorCommandKind : uint8_t
{
    None,
    Transform,
    Place,
    Delete
};

enum class EditorPrimitiveShapeKind : uint8_t
{
    None,
    Sphere,
    Box
};

struct EditorPrimitiveShapeSnapshot
{
    EditorPrimitiveShapeKind kind = EditorPrimitiveShapeKind::None;
    Math::Vector::Vector3 dimensions = Math::Vector::ZERO_VECTOR; // Sphere: x=radius; box: half extents.
    Math::Vector::Vector3 localPosition = Math::Vector::ZERO_VECTOR;
    float dragCoefficient = 0.0f;
};

struct EditorTransformSnapshot
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    EditorPrimitiveShapeSnapshot shape;
    bool hasShape = false;
};

struct EditorTransformHistoryItem
{
    Physics::PhysicsSceneObjectId sceneObjectId;
    EditorTransformSnapshot before;
    EditorTransformSnapshot after;
};

struct EditorPrimitiveBodySnapshot
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    float boundingRadius = 0.0f;
    float volume = 0.0f;
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
    float contactReleaseImpulseThreshold = 1.0f;
    float angularVelocityLimit = 5.0f;
    float contactEpsilon = 0.05f;
    bool isFixed = false;
    bool isSleeping = false;
    bool releasesFromFixedOnContact = false;
    bool usesWorldInertia = false;
};

struct EditorPrimitiveRecreateRecipe
{
    SceneEntityCreateDesc entity;
    EditorPrimitiveBodySnapshot body;
    EditorPrimitiveShapeSnapshot shape;
    float restitution = 0.0f;
    float friction = 0.0f;
    uint32_t contactMaterialId = 0;
    char contactMaterialName[32] = {};
};

struct EditorCommandEntry
{
    EditorCommandKind kind = EditorCommandKind::None;
    std::array<EditorTransformHistoryItem, EDITOR_COMMAND_TRANSFORM_CAPACITY> transforms = {};
    std::size_t transformCount = 0;
    EditorPrimitiveRecreateRecipe primitive;
};

bool TryCaptureEditorPrimitiveShape( const Math::CollisionDetection::CollisionShape& shape,
                                     EditorPrimitiveShapeSnapshot& outSnapshot );
bool TryBuildEditorPrimitiveShape( const EditorPrimitiveShapeSnapshot& snapshot,
                                   Math::CollisionDetection::CollisionShape& outShape );

class EditorCommandHistory
{
  public:
    void Clear();
    void InvalidateForNonUndoableEdit();
    void Push( const EditorCommandEntry& entry );
    const EditorCommandEntry* PendingUndo() const;
    const EditorCommandEntry* PendingRedo() const;
    bool CommitUndo();
    bool CommitRedo();
    std::size_t UndoDepth() const;
    std::size_t RedoDepth() const;
    std::size_t StoredCount() const;

  private:
    std::array<EditorCommandEntry, EDITOR_COMMAND_HISTORY_CAPACITY> m_entries = {};
    std::size_t m_count = 0;
    std::size_t m_cursor = 0;
};
} // namespace Basics
} // namespace SkullbonezCore
