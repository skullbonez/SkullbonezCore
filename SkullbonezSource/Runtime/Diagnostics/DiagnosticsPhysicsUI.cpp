/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp
Purpose:
  Applies Physics-tab commands and builds its detached presentation status.

Summary:
  This focused Runtime owner translates UI vocabulary into Physics diagnostic
  flags, updates Runtime's overlay state, and decodes that state for UI.

Glossary:
  Physics debug flag: Physics-owned bit identifying one visual diagnostic layer.

Invariants:
  - Command application changes diagnostic presentation only, never simulation.
  - Pipeline indices are normalized before they cross into detached UI status.
  - Numeric debug values are clamped at the Runtime ownership boundary.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.h
  - SkullbonezSource/Physics/PhysicsDebugData.h
  - SkullbonezSource/Runtime/Interaction/OperatorUiCommands.h
  - Agentic/Reference/engine-glossary.md
*/
#include "DiagnosticsPhysicsUI.h"

#include "OverlayDebugState.h"
#include "../../Physics/PhysicsDebugData.h"
#include "../Interaction/OperatorUiCommands.h"

#include <algorithm>
#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
void StepDiagnosticsPhysicsPipelineStage( OverlayDebugState& debug, int direction )
{
    const int stageCount = static_cast<int>( Physics::PhysicsPipelineStage::Count );

    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    debug.physicsDebugFlags |= Physics::PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( debug.physicsDebugPipelineStageCursor + direction ) % stageCount;

    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }

    debug.physicsDebugPipelineStageCursor = nextStage;
}


DiagnosticsPhysicsOverlayUICommandResult ApplyDiagnosticsPhysicsOverlayUICommands( OverlayDebugState& debug,
                                                                                   const UI::UIPhysicsCommands& commands )
{
    // Why: UI names presentation layers, while Runtime owns the only mapping to
    // Physics flags and the overlay state consumed by the concrete visualizer.
    DiagnosticsPhysicsOverlayUICommandResult result;

    if ( commands.toggleCollisionVisualizer )
    {
        debug.isCollisionVisualizer = !debug.isCollisionVisualizer;
        result.toggledCollisionVisualizer = true;
    }

    uint32_t physicsDebugFlag = Physics::PHYSICS_DEBUG_NONE;

    switch ( commands.physicsDebugOverlayToToggle )
    {
    case UI::UIPhysicsDebugOverlay::Axes:
        physicsDebugFlag = Physics::PHYSICS_DEBUG_AXES;
        break;
    case UI::UIPhysicsDebugOverlay::Contacts:
        physicsDebugFlag = Physics::PHYSICS_DEBUG_CONTACTS;
        break;
    case UI::UIPhysicsDebugOverlay::Sleep:
        physicsDebugFlag = Physics::PHYSICS_DEBUG_SLEEP;
        break;
    case UI::UIPhysicsDebugOverlay::Pipeline:
        physicsDebugFlag = Physics::PHYSICS_DEBUG_PIPELINE;
        break;
    case UI::UIPhysicsDebugOverlay::None:
        break;
    }

    if ( physicsDebugFlag != Physics::PHYSICS_DEBUG_NONE )
    {
        debug.physicsDebugFlags ^= physicsDebugFlag;
        result.toggledPhysicsDebugFlags = true;
    }

    if ( commands.stepPhysicsPipelinePrevious )
    {
        StepDiagnosticsPhysicsPipelineStage( debug, -1 );
        result.steppedPipelinePrevious = true;
    }

    if ( commands.stepPhysicsPipelineNext )
    {
        StepDiagnosticsPhysicsPipelineStage( debug, 1 );
        result.steppedPipelineNext = true;
    }

    if ( commands.togglePhysicsDebugTransparent )
    {
        debug.isPhysicsDebugTransparent = !debug.isPhysicsDebugTransparent;
        result.toggledPhysicsDebugTransparent = true;
    }

    if ( commands.toggleBroadphaseOverlay )
    {
        debug.isBroadphaseOverlay = !debug.isBroadphaseOverlay;
        result.toggledBroadphaseOverlay = true;
    }

    return result;
}


bool ApplyDiagnosticsTerrainContactProbeUICommand( OverlayDebugState& debug, const UI::UIPhysicsCommands& commands )
{
    if ( !commands.toggleTerrainContactProbe )
    {
        return false;
    }

    debug.physicsDebugFlags ^= Physics::PHYSICS_DEBUG_TERRAIN_CONTACT;
    return true;
}


DiagnosticsPhysicsDebugValueUICommandResult
ApplyDiagnosticsPhysicsDebugValueUICommands( OverlayDebugState& debug, const UI::UIPhysicsCommands& commands )
{
    DiagnosticsPhysicsDebugValueUICommandResult result;

    if ( commands.requestedPhysicsDebugAlpha >= 0.0f )
    {
        debug.physicsDebugAlpha = std::clamp( commands.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
        result.setAlpha = true;
    }

    if ( commands.requestedPhysicsDebugContactLinger >= 0.0f )
    {
        debug.physicsDebugContactLinger = std::clamp( commands.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
        result.setContactLinger = true;
    }

    return result;
}


UI::UIPhysicsDebugStatus BuildDiagnosticsPhysicsUIStatus( const OverlayDebugState& debug )
{
    UI::UIPhysicsDebugStatus status;
    status.activeFlags = debug.physicsDebugFlags;
    status.axes = ( debug.physicsDebugFlags & Physics::PHYSICS_DEBUG_AXES ) != 0u;
    status.contacts = ( debug.physicsDebugFlags & Physics::PHYSICS_DEBUG_CONTACTS ) != 0u;
    status.sleep = ( debug.physicsDebugFlags & Physics::PHYSICS_DEBUG_SLEEP ) != 0u;
    status.pipeline = ( debug.physicsDebugFlags & Physics::PHYSICS_DEBUG_PIPELINE ) != 0u;
    status.terrainContact = ( debug.physicsDebugFlags & Physics::PHYSICS_DEBUG_TERRAIN_CONTACT ) != 0u;

    const int stageCount = static_cast<int>( Physics::PhysicsPipelineStage::Count );
    int stageIndex = stageCount > 0 ? debug.physicsDebugPipelineStageCursor % stageCount : 0;

    if ( stageIndex < 0 )
    {
        stageIndex += stageCount;
    }

    status.pipelineStageName = Physics::PhysicsPipelineStageName( static_cast<Physics::PhysicsPipelineStage>( stageIndex ) );

    status.pipelineStageIndex = stageIndex;
    status.pipelineStageCount = stageCount;

    status.alpha = debug.physicsDebugAlpha;
    status.contactLinger = debug.physicsDebugContactLinger;
    status.collisionVisualizer = debug.isCollisionVisualizer;
    status.transparent = debug.isPhysicsDebugTransparent;
    status.broadphase = debug.isBroadphaseOverlay;
    return status;
}

} // namespace Runtime
} // namespace SkullbonezCore
