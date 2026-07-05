/*
File: SkullbonezSource/Physics/PhysicsModelAccess.h
Purpose:
  Defines the model-owner refresh facade between physics stores and GameModel storage.

Mental model:
  Physics hot paths use body, collider, and render stores. GameModelCollection
  still owns model-order authoring data during migration. This concrete facade
  is now restricted to importing model-owned body authoring/topology facts into
  body stores; render projection and presentation feedback stay with
  GameModelCollection.

Glossary:
  Model-owner access: Narrow refresh facade over GameModelCollection state that
    physics stores still need for model-order sync.
  Authoring refresh: Model-owner import that rebuilds body store rows after
    scene/editor/replay code changes model-owned authoring data.
  Model order: Deterministic vector order still used to align compatibility
    rows until durable entity/body/collider handles own every caller.

Invariants:
  - GameModelCollection owns the underlying model storage and SoA cache.
  - Callers must not cache model-owner references after the operation that
    requested them.
  - This facade must not grow step writeback, presentation event, diagnostic
    name, or body-stream methods again; those edges have moved to their owners.

Boundary budget:
  Owner: GameModelCollection.
  Reason: scene, editor, and replay paths still mutate model-owned authoring
    data before durable body/collider/render handles own those facts directly.
  Deletion condition: remove this facade when physics and rendering stores can
    refresh body, material, and feedback rows without model-order imports.
  Checker budget: tools/check_runtime_boundaries.py rejects step writeback,
    presentation feedback, diagnostic-name, and body-stream methods here.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/physics-game-model-authority-plan.md
*/
#pragma once

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
} // namespace GameObjects

namespace Rendering
{
class RenderInstanceStore;
} // namespace Rendering

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;

class PhysicsModelAccess
{
  public:
    explicit PhysicsModelAccess( GameObjects::GameModelCollection& collection );

    // Reloads body records after compatibility model-owned edits mutate model
    // state before the step can import them through a narrower command.
    void ReloadPhysicsBodies( PhysicsBodyStore& bodyStore, const std::vector<uint8_t>& sleepStates );
    // Store refreshes still read model-owned authoring/presentation state. Body
    // and collider stores provide physics-owned pose/shape for render records;
    // GameModel supplies render material and feedback alpha until rendering owns
    // them.
    // Captures one editor/replay-edited body into PhysicsBodyStore without
    // exposing model-order queries through this facade.
    void RefreshPhysicsBodyFromModel( PhysicsBodyStore& bodyStore, int modelIndex );

  private:
    GameObjects::GameModelCollection& m_collection;
};
} // namespace Physics
} // namespace SkullbonezCore
