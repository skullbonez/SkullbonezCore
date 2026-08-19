/*
File: SkullbonezTests/TestSceneAutomationGates.cpp
Purpose:
  Proves authored scene completion requirements resolve and observe body state safely.

Summary:
  These tests exercise the value policy used between cold scene setup and the
  validation harness. Names resolve against final scene rows, fixed bodies are
  rejected, and each post-step observation replaces—not latches—the current
  Physics-owned sleeping state.

Invariants:
  - A missing or fixed body cannot become a sleeping-body completion target.
  - All targets must be simultaneously asleep; one awake target blocks completion.
  - Waking a previously observed sleeping target clears completion.

Related:
  - SkullbonezSource/Runtime/Scene/SceneSleepingDynamicBodyGatePolicy.h
  - SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Runtime/Scene/SceneEntityStore.h"
#include "../SkullbonezSource/Runtime/Scene/SceneSleepingDynamicBodyGatePolicy.h"

#include <array>

using SkullbonezCore::Physics::PHYSICS_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Runtime::ObserveSleepingDynamicBodyRequirements;
using SkullbonezCore::Runtime::ResolveSleepingDynamicBodyRequirement;
using SkullbonezCore::Runtime::SceneAutomationGateConfiguration;
using SkullbonezCore::Runtime::SceneEntityCreateDesc;
using SkullbonezCore::Runtime::SceneEntityStore;
using SkullbonezCore::Runtime::SceneRequiredSleepingDynamicBodyGate;
using SkullbonezCore::Runtime::SceneSleepingDynamicBodyResolution;
using SkullbonezCore::Runtime::SleepingDynamicBodyRequirementsComplete;

namespace
{
void AppendEntity( SceneEntityStore& entities, uint32_t id, const char* name )
{
    SceneEntityCreateDesc entity;
    entity.sceneObjectId.value = id;
    entity.SetName( name );
    REQUIRE( entities.PreflightAppend( entity ).Ok() );
    entities.CommitAppend( entity, PhysicsBodyHandle { id - 1u, PHYSICS_HANDLE_INITIAL_GENERATION } );
}
} // namespace


TEST_CASE( "Scene sleeping-body requirements reject missing and fixed targets" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    SceneEntityStore entities( diagnostics );
    AppendEntity( entities, 1u, "dynamic_ball" );
    AppendEntity( entities, 2u, "fixed_ball" );
    const std::array<uint8_t, 2> fixedBodies = { 0u, 1u };
    int body = -1;

    CHECK( ResolveSleepingDynamicBodyRequirement( entities, fixedBodies, "missing_ball", body ) ==
           SceneSleepingDynamicBodyResolution::Missing );
    CHECK( body == -1 );
    CHECK( ResolveSleepingDynamicBodyRequirement( entities, fixedBodies, "fixed_ball", body ) ==
           SceneSleepingDynamicBodyResolution::Fixed );
    CHECK( body == 1 );
    CHECK( ResolveSleepingDynamicBodyRequirement( entities, fixedBodies, "dynamic_ball", body ) ==
           SceneSleepingDynamicBodyResolution::Resolved );
    CHECK( body == 0 );

    SceneAutomationGateConfiguration configuration;
    SkullbonezCore::Core::SbDiagnosticStore missingDiagnostics;
    SkullbonezCore::Core::SbDiagnosticStore fixedDiagnostics;
    SkullbonezCore::Core::SbDiagnosticStore resolvedDiagnostics;
    CHECK_FALSE( configuration.TryAppendRequiredSleepingDynamicBody( missingDiagnostics, entities, fixedBodies, "missing_ball" )
                     .Ok() );
    CHECK( configuration.RequiredSleepingDynamicBodyCount() == 0u );
    CHECK_FALSE( configuration.TryAppendRequiredSleepingDynamicBody( fixedDiagnostics, entities, fixedBodies, "fixed_ball" ).Ok() );
    CHECK( configuration.RequiredSleepingDynamicBodyCount() == 0u );
    CHECK( configuration.TryAppendRequiredSleepingDynamicBody( resolvedDiagnostics, entities, fixedBodies, "dynamic_ball" )
               .Ok() );
    CHECK( configuration.RequiredSleepingDynamicBodyCount() == 1u );
}


TEST_CASE( "Scene sleeping-body requirements need one simultaneous current all-asleep view" )
{
    std::array<SceneRequiredSleepingDynamicBodyGate, 3> requirements = {};

    for ( std::size_t index = 0; index < requirements.size(); ++index )
    {
        requirements[index].body = static_cast<int>( index );
    }

    std::array<uint8_t, 3> awakeBodies = { 0u, 1u, 1u };
    ObserveSleepingDynamicBodyRequirements( requirements, awakeBodies );
    CHECK_FALSE( SleepingDynamicBodyRequirementsComplete( requirements ) );

    // Hazard: body 0 sleeps before the other requirements, then wakes before
    // they do. A latched implementation would now falsely report all complete.
    awakeBodies = { 1u, 0u, 0u };
    ObserveSleepingDynamicBodyRequirements( requirements, awakeBodies );
    CHECK_FALSE( SleepingDynamicBodyRequirementsComplete( requirements ) );

    awakeBodies[0] = 0u;
    ObserveSleepingDynamicBodyRequirements( requirements, awakeBodies );
    CHECK( SleepingDynamicBodyRequirementsComplete( requirements ) );
}
