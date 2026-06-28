/*
File: SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.h
Purpose:
  Declares the narrow compatibility adapter from GameModel identity to physics handles.

Mental model:
  GameModelCollection still owns legacy scene object storage, but physics
  commands should cross that boundary as PhysicsBodyHandle operations. This
  adapter is the named bridge for old model-index and scene-object-id callers
  while runtime/editor/replay code migrates to storing handles directly.

Glossary:
  Compatibility adapter: Temporary boundary that translates legacy identities
    into public physics handles without exposing GameModel vectors.
  Model index: Legacy vector slot used by editor, replay, and scene code.
  Physics body handle: Public physics identifier that remains the command
    target after a legacy model index has been validated.
  Scene object id: Stable physics-facing identity derived from replay body id.

Invariants:
  - The adapter never exposes mutable model storage.
  - Invalid model indices, unknown scene object ids, or ambiguous scene object
    ids resolve to invalid handles.
  - Command methods preserve the old GameModelCollection command behavior while
    centralizing handle conversion in one deletion target.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - SkullbonezSource/Physics/PhysicsApi.h
  - Agentic/Plans/carmack-physics-standalone-boundary-plan.md
*/
#pragma once

#include "../Maths/Vector3.h"
#include "../Physics/PhysicsHandles.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;

class GameModelCollectionPhysicsAdapter
{
  public:
    explicit GameModelCollectionPhysicsAdapter( GameModelCollection& collection );

    Physics::PhysicsBodyHandle BodyHandleForModelIndex( int modelIndex ) const;

    // Resolves authored/runtime scene identity without letting callers borrow
    // the model vector. Unknown or duplicate ids return an invalid handle.
    Physics::PhysicsBodyHandle BodyHandleForSceneObjectId( Physics::PhysicsSceneObjectId sceneObjectId ) const;

    // These methods preserve the old model-index command surface while forcing
    // physics mutation to target handles before it reaches PhysicsEngine.
    void WakeBodyForModelIndex( int modelIndex ) const;
    void SeedBodyAsleepForModelIndex( int modelIndex ) const;
    void ApplyBodyImpulseForModelIndex( int modelIndex,
                                        const Math::Vector::Vector3& impulse,
                                        const Math::Vector::Vector3& localApplicationPoint ) const;
    void SetPendingBodyImpulseForModelIndex( int modelIndex,
                                             const Math::Vector::Vector3& impulse,
                                             const Math::Vector::Vector3& localApplicationPoint ) const;

  private:
    GameModelCollection& m_collection;
};
} // namespace GameObjects
} // namespace SkullbonezCore
