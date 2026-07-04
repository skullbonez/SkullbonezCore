/*
File: SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.h
Purpose:
  Declares the narrow compatibility adapter from GameModel identity to physics handles.

Mental model:
  GameModelCollection still owns legacy scene object storage, but physics
  commands cross that boundary as PhysicsBodyHandle operations. This adapter
  resolves old model-index and scene-object-id callers while runtime, editor,
  and replay code migrate to storing handles directly.

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
  - Callers resolve handles here, then call PhysicsEngine commands directly.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - SkullbonezSource/Physics/PhysicsApi.h
  - Agentic/Plans/carmack-physics-standalone-boundary-plan.md
*/
#pragma once

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

    // Velocity edits can wake bodies, which may consult collider-derived sleep
    // locks. This keeps any required topology refresh at the model-index edge.
    Physics::PhysicsBodyHandle BodyHandleForVelocityCommand( int modelIndex, bool wakeIfMoving ) const;

  private:
    // Wakes may need collider-derived sleep locks. This helper performs the
    // count-gated topology refresh before returning the handle used by wake/apply.
    Physics::PhysicsBodyHandle BodyHandleForWakeCommand( int modelIndex ) const;

    GameModelCollection& m_collection;
};
} // namespace GameObjects
} // namespace SkullbonezCore
