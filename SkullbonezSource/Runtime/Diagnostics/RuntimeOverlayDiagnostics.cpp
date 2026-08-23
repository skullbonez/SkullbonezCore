/*
File: RuntimeOverlayDiagnostics.cpp
Purpose:
  Implements startup and scene policy for debug presentation.

Summary:
  The owner translates startup and scene choices into an immutable frame
  policy. App publishes that value to Render at the pre-render boundary.

Glossary:
  Contact linger: Seconds that contact debug lines remain visible after their
    source contact leaves the solver output.

Invariants:
  - Construction is a single bounded startup allocation outside steady play.
  - Presentation commits never mutate Render-owned caches or GPU resources.

Related:
  - SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Runtime/App/RunRender.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "RuntimeOverlayDiagnostics.h"
#include <algorithm>

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Startup/RunLaunchOptions.h"
#include "../../Physics/PhysicsApi.h"
#include "../../UI/UI.h"

using namespace SkullbonezCore::Runtime;
namespace SBUI = SkullbonezCore::UI;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;


std::unique_ptr<RuntimeOverlayDiagnostics> RuntimeOverlayDiagnostics::CreateForStartup()
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Startup );

    // Runtime allocation policy: Run keeps this heavyweight cohesive owner out of its
    // public header. The one process-lifetime allocation is bounded to startup.
    return std::make_unique<RuntimeOverlayDiagnostics>();
}


void RuntimeOverlayDiagnostics::ApplyStartupPolicy( const RunStartupOverrides& overrides, RunLaunchOptions& launchOptions,
                                                    UI::InGameUI& operatorUi )
{
    const RunLaunchOptions& launch = overrides.launch;

    if ( overrides.hasInitialOverlayMode )
    {
        const OverlayMode overlayMode = overrides.initialOverlayMode == StartupOverlayMode::Timers ? OverlayMode::Timers
                                                                                                   : OverlayMode::None;
        m_presentationState.overlayMode = overlayMode;

        if ( overlayMode != OverlayMode::None )
        {
            operatorUi.SetVisible( true );
        }

        switch ( overlayMode )
        {
        case OverlayMode::SceneStats:
            operatorUi.SetActiveTab( SBUI::InGameUITab::Scene );
            break;
        case OverlayMode::Keys:
            operatorUi.SetActiveTab( SBUI::InGameUITab::Keys );
            break;
        case OverlayMode::BarsNormalized:
        case OverlayMode::BarsAbsolute:
        case OverlayMode::Timers:
            operatorUi.SetActiveTab( SBUI::InGameUITab::Profiler );
            break;
        default:
            break;
        }
    }

    if ( overrides.hideTopText )
    {
        m_presentationState.isTopTextHidden = true;
    }

    if ( overrides.showBroadphaseVisualizer )
    {
        m_presentationState.isBroadphaseOverlay = true;
    }

    if ( launch.generatedObjectTypeOverride != GeneratedObjectTypeOverride::Mixed )
    {
        launchOptions.generatedObjectTypeOverride = launch.generatedObjectTypeOverride;
    }

    if ( launch.hasPhysicsDebugFlagsOverride )
    {
        launchOptions.hasPhysicsDebugFlagsOverride = true;
        launchOptions.physicsDebugFlagsOverride = launch.physicsDebugFlagsOverride & Physics::PHYSICS_DEBUG_ALL;
    }

    if ( launch.hasPhysicsDebugTransparentOverride )
    {
        launchOptions.hasPhysicsDebugTransparentOverride = true;
        launchOptions.physicsDebugTransparentOverride = launch.physicsDebugTransparentOverride;
    }

    if ( launch.hasPhysicsDebugAlphaOverride )
    {
        launchOptions.hasPhysicsDebugAlphaOverride = true;
        launchOptions.physicsDebugAlphaOverride = (std::max)( 0.05f, (std::min)( launch.physicsDebugAlphaOverride, 1.0f ) );
    }

    if ( launch.hasPhysicsDebugContactLingerOverride )
    {
        launchOptions.hasPhysicsDebugContactLingerOverride = true;
        launchOptions.physicsDebugContactLingerOverride = (std::max)( 0.0f,
                                                                      (std::min)( launch.physicsDebugContactLingerOverride,
                                                                                  5.0f ) );
    }
}


RuntimeOverlayFramePolicy RuntimeOverlayDiagnostics::BuildFramePolicy( double simulationSeconds,
                                                                       double totalSimulationSeconds ) const
{
    RuntimeOverlayFramePolicy policy;
    policy.textOnly = m_presentationState.isTextOnly;
    policy.terrainHidden = m_presentationState.isTerrainHidden;
    policy.collisionVisualizer = m_presentationState.isCollisionVisualizer;
    policy.physicsDebugTransparent = m_presentationState.isPhysicsDebugTransparent;
    policy.physicsDebugAlpha = m_presentationState.physicsDebugAlpha;
    policy.waterHidden = m_presentationState.isWaterHidden;
    policy.waterFlatDebug = m_presentationState.isWaterFlatDebug;
    policy.waterNoReflect = m_presentationState.isWaterNoReflect;
    policy.waterRTReflect = m_presentationState.isWaterRTReflect;
    policy.waterFreezeDebug = m_presentationState.isWaterFreezeDebug;
    policy.frozenWaterTime = m_presentationState.frozenWaterTime;
    policy.broadphaseOverlay = m_presentationState.isBroadphaseOverlay;
    policy.physicsDebugFlags = m_presentationState.physicsDebugFlags;
    policy.physicsDebugPipelineStageCursor = m_presentationState.physicsDebugPipelineStageCursor;
    policy.physicsDebugContactLinger = m_presentationState.physicsDebugContactLinger;
    policy.simulationSeconds = simulationSeconds;
    policy.totalSimulationSeconds = totalSimulationSeconds;
    return policy;
}


void RuntimeOverlayDiagnostics::ApplyScenePresentation( const OverlayDebugState& scenePresentation )
{
    CommitPresentation( scenePresentation );
}


RuntimeOverlayPresentationEdit::RuntimeOverlayPresentationEdit( RuntimeOverlayDiagnostics& owner,
                                                                const OverlayDebugState& state )
    : m_owner( owner ), m_state( state )
{
}


RuntimeOverlayPresentationEdit::~RuntimeOverlayPresentationEdit()
{
    m_owner.CommitPresentation( m_state );
}


OverlayDebugState& RuntimeOverlayPresentationEdit::State()
{
    return m_state;
}


void RuntimeOverlayPresentationEdit::Commit()
{
    m_owner.CommitPresentation( m_state );
}


void RuntimeOverlayPresentationEdit::Refresh()
{
    m_state = m_owner.PresentationSnapshot();
}


OverlayDebugState RuntimeOverlayDiagnostics::PresentationSnapshot() const
{
    return m_presentationState;
}


RuntimeOverlayPresentationEdit RuntimeOverlayDiagnostics::EditPresentation()
{
    return RuntimeOverlayPresentationEdit( *this, m_presentationState );
}


void RuntimeOverlayDiagnostics::CommitPresentation( const OverlayDebugState& state )
{
    m_presentationState = state;
}
