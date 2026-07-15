/*
File: SkullbonezSource/Physics/Stages/PhysicsNarrowphaseDispatch.inl
Purpose:
  Declares the temporary PhysicsWorld-nested island worker callable.

Summary:
  P1 moves the existing callable out of the large facade header without changing
  its access or dispatch behavior. P4 deletes this facade borrow when the
  concrete PhysicsNarrowphaseStage takes ownership of island processing.

Glossary:
  Island worker: Callable processing a collision-independent group of pair rows.

Invariants:
  - The worker is included only inside PhysicsWorld's private section.
  - WorkerPool completes the dispatch before the borrowed world/context expire.

Hazard:
  This callable still borrows PhysicsWorld because P1 cannot change ownership or
  logic. Its deletion condition is P4's concrete narrowphase-owner extraction;
  it must not survive the campaign's final ownership review.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsStageContexts.h
  - SkullbonezSource/Physics/PhysicsWorld.h
*/
struct ObjectNarrowphaseIslandStage
{
    PhysicsWorld& world;
    const ObjectNarrowphasePairStageContext& pairContext;

    void operator()( int islandIndex ) const;
};
