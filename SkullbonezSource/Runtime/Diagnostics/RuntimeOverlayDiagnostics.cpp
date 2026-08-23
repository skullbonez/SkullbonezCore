/*
File: RuntimeOverlayDiagnostics.cpp
Purpose:
  Implements startup policy and per-frame refresh for debug presentation.

Summary:
  The owner translates startup options and committed physics stores into UI,
  render-policy, and visualizer state. Run remains responsible for ordering;
  the presentation domain owns its state transitions.

Glossary:
  Contact linger: Seconds that contact debug lines remain visible after their
    source contact leaves the solver output.

Invariants:
  - Construction is a single bounded startup allocation outside steady play.
  - Visualizer refresh occurs after physics commits and before render samples.
  - Visualizer refresh samples committed Physics-owned values without retaining
    scene or store borrows.

Related:
  - SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Runtime/App/RunRender.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "RuntimeOverlayDiagnostics.h"
#include <algorithm>
#include <span>

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Startup/RunLaunchOptions.h"
#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../UI/UI.h"

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Physics;
namespace SBUI = SkullbonezCore::UI;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;


std::unique_ptr<RuntimeOverlayDiagnostics> RuntimeOverlayDiagnostics::CreateForStartup( Core::Profiler* profiler )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Startup );

    // Runtime allocation policy: Run keeps this heavyweight cohesive owner out of its
    // public header. The one process-lifetime allocation is bounded to startup.
    return std::make_unique<RuntimeOverlayDiagnostics>( profiler );
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
        launchOptions.physicsDebugFlagsOverride = launch.physicsDebugFlagsOverride & PHYSICS_DEBUG_ALL;
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


void RuntimeOverlayDiagnostics::UpdatePostPhysics( PhysicsEngine& physics, const PhysicsBodyStore& bodyStore,
                                                   const ColliderStore& colliders,
                                                   const Rendering::RenderInstanceStore& renderInstances,
                                                   double secondsPerFrame )
{
    PROFILE_BEGIN( "Frame/PostPhysics" );

    PROFILE_BEGIN( "Frame/PostPhysics/BroadphaseVisualizer" );

    m_renderResources.m_broadphaseOverlay.SetEnabled( m_presentationState.isBroadphaseOverlay );
    if ( m_presentationState.isBroadphaseOverlay )
    {
        m_renderResources.m_broadphaseOverlay.SetCellSize( PhysicsEngine::ReadBroadphaseCellSize( physics ) );
        PhysicsBroadphaseActiveCell activeCells[PHYSICS_BROADPHASE_ACTIVE_CELL_CAPACITY];
        const int activeCellCount = PhysicsEngine::ReadBroadphaseActiveCells( physics, activeCells );
        const std::span<const PhysicsBroadphaseActiveCell> activeCellView( activeCells,
                                                                           static_cast<std::size_t>( activeCellCount ) );
        const std::span<const int64_t> collisionKeys = PhysicsEngine::ReadCollisionCellKeys( physics );
        m_renderResources.m_broadphaseOverlay.Update( static_cast<float>( secondsPerFrame ), activeCellView, collisionKeys );
    }

    PROFILE_END( "Frame/PostPhysics/BroadphaseVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/CollisionVisualizer" );
    m_renderResources.m_collisionOverlay.SetEnabled( m_presentationState.isCollisionVisualizer );
    const CollisionVisualizerFrameView collisionView { bodyStore,
                                                       colliders,
                                                       renderInstances,
                                                       PhysicsEngine::ReadCollisionVisualContacts( physics ),
                                                       PhysicsEngine::ReadSleepStates( physics ),
                                                       PhysicsEngine::ReadSleepIslandVisualIds( physics ),
                                                       bodyStore.Count() };

    m_renderResources.m_collisionOverlay.Update( static_cast<float>( secondsPerFrame ), collisionView );
    PROFILE_END( "Frame/PostPhysics/CollisionVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/PhysicsDebugVisualizer" );
    m_renderResources.m_physicsDebugOverlay.SetFlags( m_presentationState.physicsDebugFlags );
    m_renderResources.m_physicsDebugOverlay.SetContactLingerSeconds( m_presentationState.physicsDebugContactLinger );
    m_renderResources.m_physicsDebugOverlay.SetPipelineStageCursor( m_presentationState.physicsDebugPipelineStageCursor );
    const PhysicsDebugFrameView physicsDebugView { bodyStore,
                                                   colliders,
                                                   PhysicsEngine::ReadSleepStates( physics ),
                                                   PhysicsEngine::ReadSleepSupportedStates( physics ),
                                                   PhysicsEngine::ReadSleepInhibitedStates( physics ),
                                                   PhysicsEngine::ReadDebugContacts( physics ),
                                                   PhysicsEngine::ReadPipelineTrace( physics ),
                                                   bodyStore.Count() };

    m_renderResources.m_physicsDebugOverlay.Update( static_cast<float>( secondsPerFrame ), physicsDebugView );
    PROFILE_END( "Frame/PostPhysics/PhysicsDebugVisualizer" );

    PROFILE_END( "Frame/PostPhysics" );
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


RuntimeOverlayRenderResources& RuntimeOverlayDiagnostics::RenderResources()
{
    return m_renderResources;
}


void RuntimeOverlayDiagnostics::CommitPresentation( const OverlayDebugState& state )
{
    m_presentationState = state;

    // Invariant: cold scene/reset edits become visible before a no-physics
    // scene's next render, rather than waiting for post-physics refresh.
    m_renderResources.m_physicsDebugOverlay.SetFlags( state.physicsDebugFlags );
    m_renderResources.m_physicsDebugOverlay.SetContactLingerSeconds( state.physicsDebugContactLinger );
    m_renderResources.m_physicsDebugOverlay.SetPipelineStageCursor( state.physicsDebugPipelineStageCursor );
}
