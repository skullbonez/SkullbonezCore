/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsKeyboardShortcuts.h
Purpose:
  Applies typed operator shortcuts to detached diagnostics presentation state.

Summary:
  Diagnostics owns shortcut policy and consumes only the bounded scene and
  renderer capability facts App supplies for the current input event.

Invariants:
  - Shortcut handling mutates diagnostics presentation values only.
  - Renderer support crosses this boundary as a boolean fact, never as an owner.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
  - SkullbonezSource/Runtime/App/InputFrameExecution.cpp
*/
#pragma once

#include "DiagnosticsPhysicsUI.h"
#include "OverlayDebugState.h"
#include "../../Physics/PhysicsDebugData.h"

#include <cstdio>

namespace SkullbonezCore
{
namespace Runtime
{
enum class DiagnosticsKeyboardCommand : uint8_t
{
    ToggleWaterFreeze,
    CycleWaterReflection,
    ToggleWaterFlat,
    ToggleTerrainHidden,
    ToggleWaterHidden,
    ToggleCollisionVisualizer,
    CyclePhysicsDebugOverlay,
    ToggleTerrainContactProbe,
    StepPhysicsPipelinePrevious,
    StepPhysicsPipelineNext,
    TogglePhysicsDebugTransparent,
    ReportRendererRuntimeRetired,
    ToggleBroadphaseOverlay
};

inline bool HandleDiagnosticsKeyboardShortcut( OverlayDebugState& debug, int& cameraTrackBallIndex, int sceneEntityCount,
                                               bool dxrReflectionSupported, bool sceneMode, double simulationSeconds,
                                               DiagnosticsKeyboardCommand command, bool wasPressed )
{
    if ( !wasPressed )
    {
        return true;
    }

    switch ( command )
    {
    case DiagnosticsKeyboardCommand::ToggleWaterFreeze:
        debug.isWaterFreezeDebug = !debug.isWaterFreezeDebug;
        if ( debug.isWaterFreezeDebug )
        {
            debug.frozenWaterTime = static_cast<float>( simulationSeconds );
        }
        return true;
    case DiagnosticsKeyboardCommand::CycleWaterReflection:
        if ( !debug.isWaterRTReflect && !debug.isWaterNoReflect )
        {
            if ( dxrReflectionSupported )
            {
                debug.isWaterRTReflect = true;
            }
            else
            {
                debug.isWaterNoReflect = true;
            }
        }
        else if ( debug.isWaterRTReflect )
        {
            debug.isWaterRTReflect = false;
            debug.isWaterNoReflect = true;
        }
        else
        {
            debug.isWaterNoReflect = false;
        }
        return true;
    case DiagnosticsKeyboardCommand::ToggleWaterFlat:
        debug.isWaterFlatDebug = !debug.isWaterFlatDebug;
        return true;
    case DiagnosticsKeyboardCommand::ToggleTerrainHidden:
        debug.isTerrainHidden = !debug.isTerrainHidden;
        return true;
    case DiagnosticsKeyboardCommand::ToggleWaterHidden:
        debug.isWaterHidden = !debug.isWaterHidden;
        return true;
    case DiagnosticsKeyboardCommand::ToggleCollisionVisualizer:
        debug.isCollisionVisualizer = !debug.isCollisionVisualizer;
        return true;
    case DiagnosticsKeyboardCommand::CyclePhysicsDebugOverlay:
        switch ( debug.physicsDebugFlags )
        {
        case Physics::PHYSICS_DEBUG_NONE:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_AXES;
            break;
        case Physics::PHYSICS_DEBUG_AXES:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_CONTACTS;
            break;
        case Physics::PHYSICS_DEBUG_CONTACTS:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_SLEEP;
            break;
        case Physics::PHYSICS_DEBUG_SLEEP:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_ALL;
            break;
        default:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE;
            break;
        }
        return true;
    case DiagnosticsKeyboardCommand::ToggleTerrainContactProbe:
        debug.physicsDebugFlags ^= Physics::PHYSICS_DEBUG_TERRAIN_CONTACT;
        return true;
    case DiagnosticsKeyboardCommand::StepPhysicsPipelinePrevious:
        StepDiagnosticsPhysicsPipelineStage( debug, -1 );
        return true;
    case DiagnosticsKeyboardCommand::StepPhysicsPipelineNext:
        StepDiagnosticsPhysicsPipelineStage( debug, 1 );
        return true;
    case DiagnosticsKeyboardCommand::TogglePhysicsDebugTransparent:
        debug.isPhysicsDebugTransparent = !debug.isPhysicsDebugTransparent;
        return true;
    case DiagnosticsKeyboardCommand::ReportRendererRuntimeRetired:
        fprintf( stderr, "Renderer switch ignored: DX12 is the only runtime renderer.\n" );
        return true;
    case DiagnosticsKeyboardCommand::ToggleBroadphaseOverlay:
        if ( sceneMode && cameraTrackBallIndex >= 0 && !debug.isBroadphaseOverlay )
        {
            if ( sceneEntityCount > 0 )
            {
                cameraTrackBallIndex = ( cameraTrackBallIndex + 1 ) % sceneEntityCount;
            }
        }
        else
        {
            debug.isBroadphaseOverlay = !debug.isBroadphaseOverlay;
        }
        return true;
    }

    return false;
}
} // namespace Runtime
} // namespace SkullbonezCore
