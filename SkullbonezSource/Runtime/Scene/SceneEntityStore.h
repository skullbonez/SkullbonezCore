/*
File: SkullbonezSource/Runtime/Scene/SceneEntityStore.h
Purpose:
  Defines fixed-capacity scene-lifetime identity and durable presentation metadata.

Summary:
  A scene entity is the durable join row between authored identity, a live
  physics body, render material intent, optional asset provenance, and behavior
  grouping. Dense model indices are temporary row hints; PhysicsSceneObjectId
  is identity.

Glossary:
  Scene entity: Scene-owned record that joins stable identity to live owner rows.
  Asset affiliation: Library/asset/instance/part provenance kept separately from
    behavior grouping.
  Behavior group: Ragdoll/tree membership keyed by stable root object id and part order.
  Commit: Append after capacity, identity, body, collider, and render preflight.

Invariants:
  - Storage reserves its configured hard capacity before scene population and
    never grows during steady gameplay.
  - Every committed record has unique nonzero scene identity and a live body handle.
  - A behavior-group root is part zero and is committed before later members.
  - Display names and provenance strings are fixed-capacity values.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/GameObjects/SceneController.cpp
  - Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md
*/
#pragma once

#include "../../Core/SbResult.h"
#include "../../Core/SceneCapacity.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Rendering/RenderMaterial.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
struct SceneAssetAffiliation
{
    Physics::PhysicsSceneObjectId rootObjectId;
    char libraryToken[260] = {};
    char assetName[128] = {};
    char instanceName[64] = {};
    char partName[128] = {};
    uint32_t partIndex = 0;
    bool isAssetBacked = false;
};

enum class SceneBehaviorGroupKind : uint8_t
{
    None = 0,
    SimpleRagdoll,
    ReleasableTree,
};

struct SceneBehaviorGroup
{
    SceneBehaviorGroupKind kind = SceneBehaviorGroupKind::None;
    Physics::PhysicsSceneObjectId rootObjectId;
    int partIndex = -1;
};

struct SceneEntityCreateDesc
{
    Physics::PhysicsSceneObjectId sceneObjectId;
    Rendering::RenderMaterial renderMaterial;
    SceneAssetAffiliation asset;
    SceneBehaviorGroup behaviorGroup;
    char displayName[64] = {};
    bool editorVisible = true;
    bool editorLocked = false;

    SceneEntityCreateDesc();
    void SetName( const char* name );
    const char* GetName() const;
    void SetRenderTint( float tintR, float tintG, float tintB, float colorOverride );
    void SetRenderMaterial( const Rendering::RenderMaterial& material );
    const Rendering::RenderMaterial& GetRenderMaterial() const;
    void SetAssetAffiliation( Physics::PhysicsSceneObjectId rootObjectId,
                              const char* libraryToken,
                              const char* assetName,
                              const char* instanceName,
                              const char* partName,
                              uint32_t partIndex );
    void SetBehaviorGroup( SceneBehaviorGroupKind kind, Physics::PhysicsSceneObjectId rootObjectId, int partIndex );
};

struct SceneEntityRecord
{
    Physics::PhysicsSceneObjectId sceneObjectId;
    Physics::PhysicsBodyHandle body;
    Rendering::RenderMaterial renderMaterial;
    SceneAssetAffiliation asset;
    SceneBehaviorGroup behaviorGroup;
    char displayName[64] = {};
    // Editor-only session metadata follows stable scene identity through
    // swap-last compaction but is deliberately absent from authored schema.
    bool editorVisible = true;
    bool editorLocked = false;
};

class SceneEntityStore
{
  public:
    SceneEntityStore();
    void ConfigureCapacity( int capacity );
    void Clear();
    SkullbonezCore::Core::SbResult PreflightAppend( const SceneEntityCreateDesc& entity ) const;
    void CommitAppend( const SceneEntityCreateDesc& entity, Physics::PhysicsBodyHandle body );
    // Called only inside the collection's coordinated cross-store deletion.
    bool DestroyAtSwapLast( int index );
    void UpdateBodyHandleAt( int index, Physics::PhysicsBodyHandle body, Physics::PhysicsSceneObjectId sceneObjectId );
    bool TrimToCount( int count );

    int Count() const;
    int Capacity() const;
    uint64_t CapacityBytes() const;
    const SceneEntityRecord& At( int index ) const;
    SceneEntityRecord& MutableAt( int index );
    const SceneEntityRecord* TryGet( int index ) const;
    SceneEntityRecord* TryGetMutable( int index );
    int FindByDisplayName( const char* name ) const;
    int FindBySceneObjectId( Physics::PhysicsSceneObjectId sceneObjectId ) const;

    // Concept: behavior membership is durable scene metadata. These queries
    // accept dense indices only at the synchronous caller boundary and resolve
    // group roots from stable PhysicsSceneObjectId values instead of caching a
    // row that can move during swap-last deletion.
    const SceneBehaviorGroup& BehaviorGroupAt( int modelIndex ) const;
    int ResolveBehaviorGroupRootModelIndex( const SceneBehaviorGroup& group ) const;
    SceneBehaviorGroupKind GroupKindAt( int modelIndex ) const;
    Physics::PhysicsSceneObjectId GroupRootObjectIdAt( int modelIndex ) const;
    int GroupPartIndexAt( int modelIndex ) const;
    bool IsSimpleRagdollPart( int modelIndex ) const;
    bool IsSimpleRagdollTorso( int modelIndex ) const;
    int RagdollRootModelIndexForPart( int modelIndex ) const;
    bool TryFindSimpleRagdollPart( int selectedModelIndex, int partIndex, int& outModelIndex ) const;
    // Writes at most maxIndices dense rows and returns the number written. A
    // non-group selection yields itself; invalid output storage yields zero.
    int GatherGroupMemberIndices( int selectedModelIndex, int* outIndices, int maxIndices ) const;

  private:
    // Why: reserving cold scene metadata avoids touching SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS worth
    // of provenance pages in ordinary scenes while retaining a strict logical
    // ceiling. ConfigureCapacity is a scene-load preallocation boundary; all
    // CommitAppend calls must fit the existing reservation.
    std::vector<SceneEntityRecord> m_records;
    int m_capacity = SkullbonezCore::Scene::Capacity::DEFAULT_SCENE_OBJECT_CAPACITY;
};
} // namespace Runtime
} // namespace SkullbonezCore
