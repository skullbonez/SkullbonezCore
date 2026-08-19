/*
File: SkullbonezSource/Runtime/Scene/SceneSleepingDynamicBodyGatePolicy.h
Purpose:
  Defines the testable value policy for named dynamic-body sleep requirements.

Summary:
  Scene setup resolves a name against the final entity/fixed-body rows. During
  stepping, validation replaces each row's observed sleep bit from Physics'
  current awake bytes, so completion requires one simultaneous all-asleep view.

Invariants:
  - Missing and fixed targets are rejected during scene load.
  - Observation never latches: a later awake byte clears the row immediately.
  - The policy reads Physics-owned state and never changes body motion or sleep.

Related:
  - SkullbonezSource/Runtime/Scene/SceneAutomationGateConfiguration.h
  - SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp
*/
#pragma once

#include "SceneAutomationGateConfiguration.h"
#include "SceneEntityStore.h"
#include "../../Core/SbDiagnosticStore.h"

#include <cstdint>
#include <cstring>
#include <span>

namespace SkullbonezCore
{
namespace Runtime
{
enum class SceneSleepingDynamicBodyResolution
{
    Resolved,
    Missing,
    Fixed,
};

inline SceneSleepingDynamicBodyResolution ResolveSleepingDynamicBodyRequirement( const SceneEntityStore& entities,
                                                                                 std::span<const uint8_t> fixedBodies,
                                                                                 const char* name, int& outBody )
{
    outBody = entities.FindByDisplayName( name );

    if ( outBody < 0 || outBody >= static_cast<int>( fixedBodies.size() ) )
    {
        outBody = -1;
        return SceneSleepingDynamicBodyResolution::Missing;
    }

    if ( fixedBodies[static_cast<std::size_t>( outBody )] != 0u )
    {
        return SceneSleepingDynamicBodyResolution::Fixed;
    }

    return SceneSleepingDynamicBodyResolution::Resolved;
}

inline SkullbonezCore::Core::SbResult SceneAutomationGateConfiguration::TryAppendRequiredSleepingDynamicBody( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const SceneEntityStore& entities,
                                                                                                              std::span<const uint8_t> fixedBodies, const char* name )
{
    int body = -1;
    const SceneSleepingDynamicBodyResolution resolution = ResolveSleepingDynamicBodyRequirement( entities, fixedBodies, name,
                                                                                                 body );

    if ( resolution == SceneSleepingDynamicBodyResolution::Missing )
    {
        return diagnostics.Failure( "Runtime/SceneAutomationGateConfiguration",
                                    "Required sleeping dynamic body could not be resolved. name=%s", name ? name : "" );
    }

    if ( resolution == SceneSleepingDynamicBodyResolution::Fixed )
    {
        return diagnostics.Failure( "Runtime/SceneAutomationGateConfiguration",
                                    "Required sleeping dynamic body must not be fixed. name=%s body=%d", name, body );
    }

    SceneRequiredSleepingDynamicBodyGate state;
    strcpy_s( state.name, sizeof( state.name ), name );
    state.body = body;
    m_requiredSleepingDynamicBodies.push_back( state );
    return SkullbonezCore::Core::SbResult::Success();
}

inline void ObserveSleepingDynamicBodyRequirements( std::span<SceneRequiredSleepingDynamicBodyGate> requirements,
                                                    std::span<const uint8_t> awakeBodies )
{
    for ( SceneRequiredSleepingDynamicBodyGate& required : requirements )
    {
        required.sleeping = required.body >= 0 && required.body < static_cast<int>( awakeBodies.size() ) &&
                            awakeBodies[static_cast<std::size_t>( required.body )] == 0u;
    }
}

inline bool SleepingDynamicBodyRequirementsComplete( std::span<const SceneRequiredSleepingDynamicBodyGate> requirements )
{
    for ( const SceneRequiredSleepingDynamicBodyGate& body : requirements )
    {
        if ( body.body < 0 || !body.sleeping )
        {
            return false;
        }
    }

    return true;
}
} // namespace Runtime
} // namespace SkullbonezCore
