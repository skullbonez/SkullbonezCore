/*
File: SkullbonezSource/Physics/Stages/PhysicsTerrainDispatch.inl
Purpose:
  Declares the PhysicsWorld-nested terrain detection worker callable.

Summary:
  P1 moves the callable declaration out of the facade header while preserving
  its private access and exact worker-dispatch behavior. P5 replaces this seam
  with the concrete PhysicsTerrainStage owner.

Glossary:
  Terrain candidate: One worker-produced swept-contact result for a body slot.

Invariants:
  - The callable is included only inside PhysicsWorld's private section.
  - It borrows one immutable detection context for a synchronous dispatch.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsStageContexts.h
  - SkullbonezSource/Physics/PhysicsWorld.h
*/
struct TerrainDetectionStage
{
    const TerrainDetectionStageContext& context;

    void operator()( int bodyIndex ) const;
};
