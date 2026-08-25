/*
File: TestReplaySolverHashWitness.cpp
Purpose:
  Applies the Runtime-owned production Replay solver fingerprint to Physics
  determinism fixtures in the full MSVC test lane.

Summary:
  TestDeterminism keeps its renderer-free state comparisons portable. This
  file alone constructs Replay samples and checks their production hashes, so
  Runtime ownership stays visible while the original worker-count assertion
  remains active in SKULLBONEZ_TESTS.

Invariants:
  - Both samples capture the complete current Physics solver snapshot.
  - Contact and pipeline counts join the snapshot before hashing.
  - The comparison is byte-sensitive determinism evidence and does not own or
    authorize any Physics golden-baseline change.

Related:
  - SkullbonezTests/TestDeterminism.cpp
  - SkullbonezTests/TestReplaySolverHashWitness.h
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "TestReplaySolverHashWitness.h"

#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"

#include <cstdint>

namespace
{
uint64_t CaptureProductionReplaySolverHash( const SkullbonezCore::Physics::PhysicsEngine& engine )
{
    // Invariant: Replay's production fingerprint consumes the complete solver
    // snapshot plus the current contact and pipeline counts. Omitting any row
    // would recreate the incomplete determinism oracle this witness replaced.
    SkullbonezCore::Runtime::ReplaySolverFrameSample sample;
    const int bodyCount = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count();
    sample.contactCount = static_cast<uint16_t>(
        SkullbonezCore::Physics::PhysicsEngine::ReadDebugContacts( engine ).size() );
    sample.pipelineRecordCount = static_cast<uint16_t>(
        SkullbonezCore::Physics::PhysicsEngine::ReadPipelineRecordCount( engine ) );
    engine.CaptureReplaySolverSnapshot( sample.worldSnapshot.physics,
                                        SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt( bodyCount ) );
    return SkullbonezCore::Runtime::ReplaySolverHashForSample( sample );
}
} // namespace

namespace SkullbonezTests
{
void CheckProductionReplaySolverHashEqual( const SkullbonezCore::Physics::PhysicsEngine& left,
                                           const SkullbonezCore::Physics::PhysicsEngine& right )
{
    CHECK( CaptureProductionReplaySolverHash( left ) == CaptureProductionReplaySolverHash( right ) );
}
} // namespace SkullbonezTests
