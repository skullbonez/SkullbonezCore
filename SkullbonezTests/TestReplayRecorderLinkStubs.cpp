//
// File: SkullbonezTests/TestReplayRecorderLinkStubs.cpp
// Purpose:
//   Provide loud unit-test link stubs for ReplayRecorder integration hooks that
//   are not part of the focused ring-buffer tests.
//
// Mental model:
//   `ReplayRecorder.cpp` contains both the lightweight solver-sample mirror path
//   and the full runtime capture path. E2 tests exercise only the mirror path,
//   but the object file still references camera, world, model, and physics
//   owner methods from the uncalled full-capture functions.
//
// Glossary:
//   Link stub: Test-only method definition that satisfies unresolved symbols
//     while failing loudly if the focused test crosses into that dependency.
//   Full-capture path: Replay capture route that walks live runtime owners such
//     as GameModelCollection, PhysicsEngine, camera, and world state.
//
// Invariants:
//   - These methods are test-only link stubs, not runtime fixtures.
//   - A focused ring-buffer test reaching them is a boundary failure.
//
// Related:
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
//   - SkullbonezTests/TestReplayRecorder.cpp
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../SkullbonezSource/GameObjects/GameModel.h"
#include "../SkullbonezSource/GameObjects/GameModelCollection.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Runtime/CameraCollection.h"
#include "../SkullbonezSource/World/WorldEnvironment.h"

#include <stdexcept>
#include <string>

namespace
{
[[noreturn]] void ThrowUnexpectedReplayIntegrationCall( const char* methodName )
{
    throw std::runtime_error(
        std::string( "TestReplayRecorderLinkStubs: unexpected full replay capture call: " ) + methodName );
}
} // namespace

namespace SkullbonezCore
{
namespace Environment
{
const Math::Vector::Vector3& CameraCollection::GetCameraView()
{
    // Hazard: reaching any stub means the focused recorder test crossed from
    // solver-sample mirroring into full runtime capture without real owners.
    ThrowUnexpectedReplayIntegrationCall( "CameraCollection::GetCameraView" );
}

const Math::Vector::Vector3& CameraCollection::GetCameraTranslation()
{
    ThrowUnexpectedReplayIntegrationCall( "CameraCollection::GetCameraTranslation" );
}

const Math::Vector::Vector3& CameraCollection::GetCameraUp()
{
    ThrowUnexpectedReplayIntegrationCall( "CameraCollection::GetCameraUp" );
}

float WorldEnvironment::GetFluidSurfaceHeight()
{
    ThrowUnexpectedReplayIntegrationCall( "WorldEnvironment::GetFluidSurfaceHeight" );
}

float WorldEnvironment::GetGravity() const
{
    ThrowUnexpectedReplayIntegrationCall( "WorldEnvironment::GetGravity" );
}

float WorldEnvironment::GetFluidDensity() const
{
    ThrowUnexpectedReplayIntegrationCall( "WorldEnvironment::GetFluidDensity" );
}
} // namespace Environment

namespace GameObjects
{
const char* GameModel::GetName() const
{
    ThrowUnexpectedReplayIntegrationCall( "GameModel::GetName" );
}

const GameModel* GameModelCollection::TryGetModel( int ) const
{
    ThrowUnexpectedReplayIntegrationCall( "GameModelCollection::TryGetModel" );
}

Physics::PhysicsEngine& GameModelCollection::GetPhysicsEngine()
{
    ThrowUnexpectedReplayIntegrationCall( "GameModelCollection::GetPhysicsEngine" );
}
} // namespace GameObjects

namespace Physics
{
void PhysicsEngine::CaptureReplaySolverSnapshot( Basics::ReplaySolverWorldSnapshot&, int ) const
{
    ThrowUnexpectedReplayIntegrationCall( "PhysicsEngine::CaptureReplaySolverSnapshot" );
}

const std::vector<uint8_t>& PhysicsEngine::GetCollisionVisualContacts() const
{
    ThrowUnexpectedReplayIntegrationCall( "PhysicsEngine::GetCollisionVisualContacts" );
}

const std::vector<uint8_t>& PhysicsEngine::GetSleepStates() const
{
    ThrowUnexpectedReplayIntegrationCall( "PhysicsEngine::GetSleepStates" );
}

const std::vector<int>& PhysicsEngine::GetSleepIslandVisualIds() const
{
    ThrowUnexpectedReplayIntegrationCall( "PhysicsEngine::GetSleepIslandVisualIds" );
}

const std::vector<uint8_t>& PhysicsEngine::GetSleepSupportedStates() const
{
    ThrowUnexpectedReplayIntegrationCall( "PhysicsEngine::GetSleepSupportedStates" );
}

const std::vector<uint8_t>& PhysicsEngine::GetSleepInhibitedStates() const
{
    ThrowUnexpectedReplayIntegrationCall( "PhysicsEngine::GetSleepInhibitedStates" );
}

const std::vector<PhysicsDebugContact>& PhysicsEngine::GetPhysicsDebugContacts() const
{
    ThrowUnexpectedReplayIntegrationCall( "PhysicsEngine::GetPhysicsDebugContacts" );
}

const std::vector<PhysicsPipelineRecord>& PhysicsEngine::GetPhysicsPipelineTrace() const
{
    ThrowUnexpectedReplayIntegrationCall( "PhysicsEngine::GetPhysicsPipelineTrace" );
}
} // namespace Physics
} // namespace SkullbonezCore
