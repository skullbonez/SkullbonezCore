/*
File: RuntimeOverlayDiagnostics.cpp
Purpose:
  Implements startup policy and per-frame refresh for debug presentation.

Summary:
  The owner translates startup options and committed physics stores into UI,
  render-policy, and visualizer state. This keeps Run responsible for ordering
  while the presentation domain owns its state transitions.

Glossary:
  Active cell: A populated broadphase grid bucket drawn by the spatial overlay.
  Contact linger: Seconds that contact debug lines remain visible after their
    source contact leaves the solver output.
  Pipeline cursor: Selected physics pipeline stage rendered by the debug pass.

Invariants:
  - Construction is a single bounded startup allocation outside steady play.
  - Visualizer refresh occurs after physics commits and before render samples.
  - Required validation gates are updated from the same sampled visual data.

Related:
  - SkullbonezSource/Runtime/RuntimeOverlayDiagnostics.h
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h
*/
#include "RuntimeOverlayDiagnostics.h"

#include <algorithm>

#include "Allocation/RuntimeAllocationTracker.h"
#include "Render/RuntimeRenderInputs.h"
#include "RunLaunchOptions.h"
#include "Scene/SceneController.h"
#include "../Core/Profiler.h"
#include "../Physics/PhysicsApi.h"

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::CollisionDetection::SpatialGrid;
namespace SBPhysics = SkullbonezCore::Physics;
namespace SBUI = SkullbonezCore::UI;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;


std::unique_ptr<RuntimeOverlayDiagnostics> RuntimeOverlayDiagnostics::CreateForStartup()
{
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Startup );
    // Allocation policy: Run keeps this heavyweight cohesive owner out of its
    // public header. The one process-lifetime allocation is bounded to startup.
    return std::make_unique<RuntimeOverlayDiagnostics>();
}


void RuntimeOverlayDiagnostics::ApplyStartupPolicy( const RunStartupOverrides& overrides,
                                                    RunLaunchOptions& launchOptions )
{
    const RunLaunchOptions& launch = overrides.launch;
    if ( overrides.hasInitialOverlayMode )
    {
        m_presentationState.overlayMode = overrides.initialOverlayMode;
        if ( overrides.initialOverlayMode != OverlayMode::None )
        {
            m_ui.SetVisible( true );
        }
        switch ( overrides.initialOverlayMode )
        {
        case OverlayMode::SceneStats:
            m_ui.SetActiveTab( SBUI::InGameUITab::Scene );
            break;
        case OverlayMode::Keys:
            m_ui.SetActiveTab( SBUI::InGameUITab::Keys );
            break;
        case OverlayMode::BarsNormalized:
        case OverlayMode::BarsAbsolute:
        case OverlayMode::Timers:
            m_ui.SetActiveTab( SBUI::InGameUITab::Profiler );
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
        launchOptions.physicsDebugAlphaOverride =
            (std::max)( 0.05f, (std::min)( launch.physicsDebugAlphaOverride, 1.0f ) );
    }
    if ( launch.hasPhysicsDebugContactLingerOverride )
    {
        launchOptions.hasPhysicsDebugContactLingerOverride = true;
        launchOptions.physicsDebugContactLingerOverride =
            (std::max)( 0.0f, (std::min)( launch.physicsDebugContactLingerOverride, 5.0f ) );
    }
}


void RuntimeOverlayDiagnostics::UpdatePostPhysics( SceneController& scene,
                                                   float contactEpsilon,
                                                   double secondsPerFrame )
{
    PROFILE_BEGIN( "Frame/PostPhysics" );

    PROFILE_BEGIN( "Frame/PostPhysics/BroadphaseVisualizer" );
    // Why: hidden broadphase state still advances so cell fades and validation
    // gates remain coherent when the operator toggles the overlay.
    m_broadphaseOverlay.SetEnabled( m_presentationState.isBroadphaseOverlay );
    PhysicsEngine& physics = scene.Physics();
    const SpatialGrid& grid = PhysicsEngine::ReadSpatialGrid( physics );
    m_broadphaseOverlay.SetCellSize( grid.GetCellSize() );
    SpatialGrid::ActiveCell activeCells[SpatialGrid::MAX_BUCKETS];
    const int activeCellCount = grid.GetActiveCellCount();
    grid.GetActiveCells( activeCells, SpatialGrid::MAX_BUCKETS );
    const std::vector<int64_t>& collisionKeys = PhysicsEngine::ReadCollisionCellKeys( physics );
    m_broadphaseOverlay.Update( static_cast<float>( secondsPerFrame ),
                                activeCells,
                                activeCellCount,
                                collisionKeys.data(),
                                static_cast<int>( collisionKeys.size() ) );
    scene.UpdateRequiredBroadphaseXCells( activeCells, (std::min)( activeCellCount, SpatialGrid::MAX_BUCKETS ) );
    PROFILE_END( "Frame/PostPhysics/BroadphaseVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/CollisionVisualizer" );
    m_collisionOverlay.SetEnabled( m_presentationState.isCollisionVisualizer );
    const CollisionVisualizerFrameView collisionView{ scene.BodyStore(),
                                                      scene.Colliders(),
                                                      scene.RenderInstances(),
                                                      PhysicsEngine::ReadCollisionVisualContacts( physics ),
                                                      PhysicsEngine::ReadSleepStates( physics ),
                                                      PhysicsEngine::ReadSleepIslandVisualIds( physics ),
                                                      scene.BodyStore().Count() };
    m_collisionOverlay.Update( static_cast<float>( secondsPerFrame ), collisionView );
    PROFILE_END( "Frame/PostPhysics/CollisionVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/PhysicsDebugVisualizer" );
    m_physicsDebugOverlay.SetFlags( m_presentationState.physicsDebugFlags );
    m_physicsDebugOverlay.SetContactLingerSeconds( m_presentationState.physicsDebugContactLinger );
    m_physicsDebugOverlay.SetPipelineStageCursor( m_presentationState.physicsDebugPipelineStageCursor );
    const PhysicsDebugFrameView physicsDebugView{ scene.BodyStore(),
                                                  scene.Colliders(),
                                                  PhysicsEngine::ReadSleepStates( physics ),
                                                  PhysicsEngine::ReadSleepSupportedStates( physics ),
                                                  PhysicsEngine::ReadSleepInhibitedStates( physics ),
                                                  PhysicsEngine::ReadDebugContacts( physics ),
                                                  PhysicsEngine::ReadPipelineTrace( physics ),
                                                  scene.BodyStore().Count() };
    m_physicsDebugOverlay.Update( static_cast<float>( secondsPerFrame ), physicsDebugView );
    scene.UpdateRequiredContacts( contactEpsilon );
    PROFILE_END( "Frame/PostPhysics/PhysicsDebugVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/EndCollisionVisualFrame" );
    scene.Physics().EndCollisionVisualFrame();
    PROFILE_END( "Frame/PostPhysics/EndCollisionVisualFrame" );
    PROFILE_END( "Frame/PostPhysics" );
}


RuntimeRenderFramePolicy RuntimeOverlayDiagnostics::BuildFramePolicy( double simulationSeconds,
                                                                      double totalSimulationSeconds ) const
{
    RuntimeRenderFramePolicy policy;
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


SBUI::InGameUI& RuntimeOverlayDiagnostics::OperatorUi()
{
    return m_ui;
}


const SBUI::InGameUI& RuntimeOverlayDiagnostics::OperatorUi() const
{
    return m_ui;
}


RunDebugState& RuntimeOverlayDiagnostics::PresentationState()
{
    return m_presentationState;
}


const RunDebugState& RuntimeOverlayDiagnostics::PresentationState() const
{
    return m_presentationState;
}


SBPhysics::BroadphaseVisualizer& RuntimeOverlayDiagnostics::BroadphaseOverlay()
{
    return m_broadphaseOverlay;
}


SBPhysics::CollisionVisualizer& RuntimeOverlayDiagnostics::CollisionOverlay()
{
    return m_collisionOverlay;
}


SBPhysics::PhysicsDebugVisualizer& RuntimeOverlayDiagnostics::PhysicsDebugOverlay()
{
    return m_physicsDebugOverlay;
}
