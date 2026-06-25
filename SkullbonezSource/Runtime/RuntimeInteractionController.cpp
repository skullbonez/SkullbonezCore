/*
File: SkullbonezSource/Runtime/RuntimeInteractionController.cpp
Purpose:
  Implements authoritative runtime workspace and frame policy transitions.

Mental model:
  The controller is intentionally side-effect free. It records ownership and
  produces transition/policy records; Run performs subsystem-specific cleanup
  from those records.

Related:
  - SkullbonezSource/Runtime/RuntimeInteractionController.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "RuntimeInteractionController.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
bool WorkspaceUsesInspectControls( RuntimeWorkspace workspace )
{
    return workspace == RuntimeWorkspace::Inspect || workspace == RuntimeWorkspace::Edit ||
           workspace == RuntimeWorkspace::Replay;
}
} // namespace

RuntimeWorkspace RuntimeInteractionController::Workspace() const
{
    return m_workspace;
}


WorldInteractionOwner RuntimeInteractionController::Owner() const
{
    return m_owner;
}


CameraLookState RuntimeInteractionController::CameraLook() const
{
    return m_cameraLook;
}


PhysicsAdvanceState RuntimeInteractionController::PhysicsAdvance() const
{
    return m_physicsAdvance;
}


RuntimeInteractionTransition RuntimeInteractionController::EnterLive()
{
    return TransitionTo( RuntimeWorkspace::Live, WorldInteractionOwner::None, InteractionExitReason::EnterLive );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterInspect()
{
    return TransitionTo( RuntimeWorkspace::Inspect, WorldInteractionOwner::None, InteractionExitReason::EnterInspect );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterEdit()
{
    return TransitionTo( RuntimeWorkspace::Edit,
                         WorldInteractionOwner::EditorPlacement,
                         InteractionExitReason::EnterEdit );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterReplay()
{
    return TransitionTo( RuntimeWorkspace::Replay,
                         WorldInteractionOwner::ReplayScrub,
                         InteractionExitReason::EnterReplay );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterLauncher()
{
    return TransitionTo( RuntimeWorkspace::Live,
                         WorldInteractionOwner::Launcher,
                         InteractionExitReason::EnterLauncher );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterManipulator()
{
    return TransitionTo( RuntimeWorkspace::Live,
                         WorldInteractionOwner::Manipulator,
                         InteractionExitReason::EnterManipulator );
}


RuntimeInteractionTransition RuntimeInteractionController::SetWorldInteractionOwner( WorldInteractionOwner owner,
                                                                                     InteractionExitReason reason )
{
    return TransitionTo( m_workspace, owner, reason );
}


RuntimeInteractionTransition RuntimeInteractionController::ResetForScene( InteractionExitReason reason )
{
    return TransitionTo( RuntimeWorkspace::Live, WorldInteractionOwner::None, reason );
}


RuntimeInteractionFramePolicy
RuntimeInteractionController::BuildFramePolicy( const RuntimeInteractionFrameInput& input ) const
{
    RuntimeInteractionFramePolicy policy;
    policy.workspace = m_workspace;
    policy.owner = m_owner;
    policy.launcherActive = m_owner == WorldInteractionOwner::Launcher;
    policy.manipulatorActive = m_owner == WorldInteractionOwner::Manipulator;
    policy.physicsTimeScale = (std::max)( 0.0f, input.sceneTimeScale );

    if ( !input.scenePhysicsEnabled || input.replayScrubbedHistoricalSample )
    {
        policy.physicsAdvance = PhysicsAdvanceState::Disabled;
        policy.physicsTimeScale = 0.0f;
    }
    else if ( input.forcePhysicsRunning || policy.launcherActive || policy.manipulatorActive )
    {
        policy.physicsAdvance = PhysicsAdvanceState::Running;
    }
    else if ( input.replayLiveHeldAtCurrentFrame )
    {
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
        if ( !input.stepHeld )
        {
            policy.physicsTimeScale = 0.0f;
        }
    }
    else if ( m_workspace == RuntimeWorkspace::Inspect || m_workspace == RuntimeWorkspace::Edit ||
              m_workspace == RuntimeWorkspace::Replay )
    {
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
    }
    else
    {
        policy.physicsAdvance = PhysicsAdvanceState::Running;
    }

    if ( input.editorViewportLookActive )
    {
        policy.cameraLook = CameraLookState::EditorViewportLook;
    }
    else if ( input.replayInspectionLookActive )
    {
        policy.cameraLook = CameraLookState::ReplayInspectionLook;
    }
    else if ( input.rightMouseLookHeld )
    {
        policy.cameraLook = CameraLookState::RightMouseLook;
    }
    else
    {
        policy.cameraLook = CameraLookState::Passive;
    }

    policy.cameraMouseLookActive = policy.cameraLook != CameraLookState::Passive;
    policy.cameraKeyboardControlsActive = input.rightMouseLookHeld || WorkspaceUsesInspectControls( m_workspace ) ||
                                          policy.launcherActive || policy.manipulatorActive;

    return policy;
}


RuntimeInteractionTransition RuntimeInteractionController::TransitionTo( RuntimeWorkspace workspace,
                                                                         WorldInteractionOwner owner,
                                                                         InteractionExitReason reason )
{
    RuntimeInteractionTransition transition;
    transition.previousWorkspace = m_workspace;
    transition.previousOwner = m_owner;
    transition.previousCameraLook = m_cameraLook;
    transition.previousPhysicsAdvance = m_physicsAdvance;
    transition.workspace = workspace;
    transition.owner = owner;
    transition.cameraLook = CameraLookState::Passive;
    transition.physicsAdvance = PhysicsAdvanceState::Running;
    transition.reason = reason;

    m_workspace = workspace;
    m_owner = owner;
    m_cameraLook = transition.cameraLook;
    m_physicsAdvance = transition.physicsAdvance;

    transition.workspaceChanged = transition.previousWorkspace != transition.workspace;
    transition.ownerChanged = transition.previousOwner != transition.owner;
    transition.cameraLookChanged = transition.previousCameraLook != transition.cameraLook;
    transition.physicsAdvanceChanged = transition.previousPhysicsAdvance != transition.physicsAdvance;
    return transition;
}
} // namespace Basics
} // namespace SkullbonezCore
