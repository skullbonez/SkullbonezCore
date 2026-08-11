/*
File: RuntimeValidationHarness.cpp
Purpose:
  Implements validation-harness startup, frame, capture, and reload policy.

Summary:
  This owner preserves the former Run call positions while keeping controller
  state and launch normalization behind a validation-specific boundary.

Glossary:
  Control directory: Folder watched by the live-style protocol.
  Launch normalization: Bounded defaults copied from parsed CLI values into
    reusable scene-load policy.
  Exit summary: Final deterministic stress counters printed on WM_QUIT.
  Scene gate tracker: Validation-owned authored requirements observed after
    committed physics work.

Invariants:
  - Construction is one bounded process-lifetime Startup allocation.
  - A configured live-style directory is marked ready only after Run applies
    its interactive/capture side effects.
  - Graphics-stress defaults and clamps remain byte-for-byte equivalent to the
    former Run-local launch policy.
  - Gate contact checks read compact body/collider stores and debug contacts;
    no mutable scene topology escapes into validation ownership.

Related:
  - SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h
  - SkullbonezSource/Runtime/Capture/GraphicsStressController.h
  - SkullbonezSource/Runtime/Direction/LiveStyleController.h
*/
#include "RuntimeValidationHarness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Capture/CaptureController.h"
#include "../App/RunLaunchOptions.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/ObjectContactManifold.h"
#include "../../Physics/PhysicsBodyStore.h"

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Physics;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
ObjectContactBodyView AutomationContactBodyView( PhysicsBodyHotFieldsConstView hotFields, std::size_t bodyIndex )
{
    ObjectContactBodyView view;
    view.position = PhysicsBodyPosition( hotFields, bodyIndex );
    view.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
    return view;
}
} // namespace


void SceneAutomationGateConfiguration::Reset()
{
    m_requiredContacts.clear();
    m_requiredBroadphaseXCells.clear();
}


void SceneAutomationGateConfiguration::ReserveRequiredContacts( std::size_t count )
{
    m_requiredContacts.reserve( count );
}


void SceneAutomationGateConfiguration::AppendRequiredContact( const char* nameA, const char* nameB, int bodyA, int bodyB )
{
    SceneRequiredContactGate state;
    strcpy_s( state.nameA, sizeof( state.nameA ), nameA );
    strcpy_s( state.nameB, sizeof( state.nameB ), nameB );
    state.bodyA = bodyA;
    state.bodyB = bodyB;
    m_requiredContacts.push_back( state );
}


void SceneAutomationGateConfiguration::ReserveRequiredBroadphaseXCells( std::size_t count )
{
    m_requiredBroadphaseXCells.reserve( count );
}


void SceneAutomationGateConfiguration::AppendRequiredBroadphaseXCells( int minCellX, int maxCellX, int cellY, int cellZ )
{
    SceneRequiredBroadphaseXCellsGate state;
    state.minCellX = minCellX;
    state.maxCellX = maxCellX;
    state.cellY = cellY;
    state.cellZ = cellZ;
    m_requiredBroadphaseXCells.push_back( state );
}


void SceneAutomationGateTracker::ApplyConfiguration( SceneAutomationGateConfiguration configuration )
{

    // Lifetime: resolved body rows belong to exactly one activated scene. Move
    // the cold-load value so validation performs no second allocation or lookup.
    m_configuration = std::move( configuration );
}


void SceneAutomationGateTracker::ObserveSceneLifecycle( const SceneLifecyclePacket& packet,
                                                        SceneAutomationGateConfiguration&& configuration )
{

    // Lifetime: the caller may offer a detached configuration on every apply
    // boundary; only a newly cleared generation transfers its vector storage.
    if ( m_sceneLifecycleObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        ApplyConfiguration( std::move( configuration ) );
    }
}


void SceneAutomationGateTracker::UpdateRequiredContacts( SceneAutomationGatePhysicsView physics, float contactEpsilon )
{
    if ( m_configuration.m_requiredContacts.empty() )
    {
        return;
    }

    // Invariant: validation observes committed compact stores. The legacy
    // object mirror may not have been refreshed yet at this post-step point.
    const PhysicsBodyStore& bodyStore = physics.bodyStore;
    const ColliderStore& colliderStore = physics.colliderStore;
    const auto bodyRecords = bodyStore.Records();
    const auto colliderRecords = colliderStore.Records();
    const int contactModelCount = (std::min)( bodyStore.Count(),
                                              static_cast<int>( (std::min)( bodyRecords.size(), colliderRecords.size() ) ) );

    for ( SceneRequiredContactGate& required : m_configuration.m_requiredContacts )
    {
        if ( required.touched || required.bodyA < 0 || required.bodyB < 0 || required.bodyA >= contactModelCount ||
             required.bodyB >= contactModelCount )
        {
            continue;
        }

        const std::size_t bodyAIndex = static_cast<std::size_t>( required.bodyA );
        const std::size_t bodyBIndex = static_cast<std::size_t>( required.bodyB );
        const ColliderRecord& colliderA = colliderRecords[static_cast<std::size_t>( required.bodyA )];
        const ColliderRecord& colliderB = colliderRecords[static_cast<std::size_t>( required.bodyB )];
        ObjectContactManifold manifold;
        const auto hotFields = bodyStore.HotFields();

        if ( BuildObjectContactManifold( AutomationContactBodyView( hotFields, bodyAIndex ), colliderA.shape,
                                         AutomationContactBodyView( hotFields, bodyBIndex ), colliderB.shape, required.bodyA,
                                         required.bodyB, contactEpsilon + 0.25f, manifold ) )
        {
            required.touched = true;
        }
    }

    for ( const PhysicsDebugContact& contact : physics.debugContacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 )
        {
            continue;
        }

        for ( SceneRequiredContactGate& required : m_configuration.m_requiredContacts )
        {
            if ( required.touched || required.bodyA < 0 || required.bodyB < 0 )
            {
                continue;
            }

            const bool sameOrder = contact.bodyA == required.bodyA && contact.bodyB == required.bodyB;
            const bool swappedOrder = contact.bodyA == required.bodyB && contact.bodyB == required.bodyA;

            if ( sameOrder || swappedOrder )
            {
                required.touched = true;
                break;
            }
        }
    }
}


bool SceneAutomationGateTracker::RequiresBroadphaseXCellObservation() const
{
    return !m_configuration.m_requiredBroadphaseXCells.empty() && !RequiredBroadphaseXCellsComplete();
}


void SceneAutomationGateTracker::UpdateRequiredBroadphaseXCells( std::span<const Physics::PhysicsBroadphaseActiveCell> activeCells )
{
    if ( m_configuration.m_requiredBroadphaseXCells.empty() || activeCells.empty() )
    {
        return;
    }

    for ( SceneRequiredBroadphaseXCellsGate& required : m_configuration.m_requiredBroadphaseXCells )
    {
        if ( required.activated )
        {
            continue;
        }

        required.lastActiveCellCount = static_cast<int>( activeCells.size() );
        required.lastMissingCellX = -1;
        required.hasObservedXRange = false;

        for ( const Physics::PhysicsBroadphaseActiveCell& active : activeCells )
        {
            if ( active.iy == required.cellY && active.iz == required.cellZ )
            {
                if ( !required.hasObservedXRange )
                {
                    required.lastObservedMinX = active.ix;
                    required.lastObservedMaxX = active.ix;
                    required.hasObservedXRange = true;
                }
                else
                {
                    required.lastObservedMinX = (std::min)( required.lastObservedMinX, static_cast<int>( active.ix ) );
                    required.lastObservedMaxX = (std::max)( required.lastObservedMaxX, static_cast<int>( active.ix ) );
                }
            }
        }

        // Why: the authored gate requires every x cell in the inclusive span,
        // not merely matching observed min/max bounds with holes between them.
        // The outer span and authored x range are both bounded by their owners.
        bool allActive = true;

        for ( int x = required.minCellX; x <= required.maxCellX; ++x )
        {
            bool found = false;

            for ( const Physics::PhysicsBroadphaseActiveCell& active : activeCells )
            {
                if ( active.ix == x && active.iy == required.cellY && active.iz == required.cellZ )
                {
                    found = true;
                    break;
                }
            }

            if ( !found )
            {
                allActive = false;
                required.lastMissingCellX = x;
                break;
            }
        }

        if ( allActive )
        {
            required.activated = true;
        }
    }
}


bool SceneAutomationGateTracker::RequiredContactsComplete() const
{
    for ( const SceneRequiredContactGate& contact : m_configuration.m_requiredContacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
        {
            return false;
        }
    }

    return true;
}


bool SceneAutomationGateTracker::RequiredBroadphaseXCellsComplete() const
{
    for ( const SceneRequiredBroadphaseXCellsGate& required : m_configuration.m_requiredBroadphaseXCells )
    {
        if ( !required.activated )
        {
            return false;
        }
    }

    return true;
}


SceneAutomationGateStatus SceneAutomationGateTracker::Status() const
{
    return SceneAutomationGateStatus { !m_configuration.m_requiredContacts.empty() ||
                                           !m_configuration.m_requiredBroadphaseXCells.empty(),
                                       RequiredContactsComplete() && RequiredBroadphaseXCellsComplete() };
}


void SceneAutomationGateTracker::PrintMissingRequirements() const
{
    for ( const SceneRequiredContactGate& contact : m_configuration.m_requiredContacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
        {
            std::fprintf( stderr, "[scene] required_contact missing: %s <-> %s\n", contact.nameA, contact.nameB );
        }
    }

    for ( const SceneRequiredBroadphaseXCellsGate& cells : m_configuration.m_requiredBroadphaseXCells )
    {
        if ( !cells.activated )
        {
            std::fprintf( stderr,
                          "[scene] required_broadphase_x_cells missing: x %d..%d y %d z %d first_missing=%d "
                          "active_cells=%d observed_x=%s%d..%d\n",
                          cells.minCellX, cells.maxCellX, cells.cellY, cells.cellZ, cells.lastMissingCellX,
                          cells.lastActiveCellCount, cells.hasObservedXRange ? "" : "none ", cells.lastObservedMinX,
                          cells.lastObservedMaxX );
        }
    }
}


std::unique_ptr<RuntimeValidationHarness>
RuntimeValidationHarness::CreateForStartup( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Startup );

    // Runtime allocation policy: keep both cold harness implementations out of Run.h.
    // The single owner allocation is bounded to process startup.
    return std::make_unique<RuntimeValidationHarness>( resultDiagnostics );
}


bool RuntimeValidationHarness::ConfigureStartup( const RunStartupOverrides& overrides, RunLaunchOptions& launchOptions )
{
    bool liveStyleConfigured = false;

    if ( overrides.liveStyleControlDirectory && overrides.liveStyleControlDirectory[0] != '\0' )
    {
        liveStyleConfigured = m_liveStyle.ConfigureDirectory( overrides.liveStyleControlDirectory );
    }

    const RunLaunchOptions& launch = overrides.launch;

    if ( launch.uiStress )
    {
        launchOptions.uiStress = true;
        launchOptions.uiStressSeed = launch.uiStressSeed > 0 ? launch.uiStressSeed : 0x7F4A7C15u;
        launchOptions.uiStressActions = std::clamp( launch.uiStressActions, 1, 32 );
    }

    if ( !launch.graphicsStress )
    {
        return liveStyleConfigured;
    }

    const unsigned int resolvedSeed = launch.graphicsStressSeed > 0 ? launch.graphicsStressSeed : 0xC11E2026u;
    launchOptions.graphicsStress = true;
    launchOptions.graphicsStressSeed = resolvedSeed;
    launchOptions.graphicsStressActions = std::clamp( launch.graphicsStressActions, 1, 64 );
    launchOptions.graphicsStressSceneIntervalFrames = std::clamp( launch.graphicsStressSceneIntervalFrames, 1, 600 );
    launchOptions.graphicsStressMemoryIntervalFrames = std::clamp( launch.graphicsStressMemoryIntervalFrames, 0, 36000 );

    launchOptions.interactiveSceneRun = true;

    m_graphicsStress.Configure( resolvedSeed, launchOptions.graphicsStressActions,
                                launchOptions.graphicsStressSceneIntervalFrames,
                                launchOptions.graphicsStressMemoryIntervalFrames );

    return liveStyleConfigured;
}


void RuntimeValidationHarness::MarkLiveStyleReady()
{
    m_liveStyle.MarkReady();
}


void RuntimeValidationHarness::TickLiveStyle( RunLaunchOptions& launchOptions, SceneController& sceneController,
                                              SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser,
                                              const Assets::AssetSystem& assets,
                                              SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                              const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic )
{
    m_liveStyle.Tick( m_resultDiagnostics, launchOptions, sceneController, sceneBrowser, assets, activeCinematic,
                      defaultCinematic );
}


bool RuntimeValidationHarness::HasPendingLiveStyleCapture() const
{
    return m_liveStyle.HasPendingCapture();
}


void RuntimeValidationHarness::SavePendingLiveStyleCapture( CaptureController& capture,
                                                            Rendering::Dx12BackbufferCapture& backend )
{
    m_liveStyle.SavePendingCapture( capture, backend );
}


void RuntimeValidationHarness::ResumeGraphicsStressAfterSceneLoad( const RunLaunchOptions& launchOptions )
{
    if ( !launchOptions.graphicsStress )
    {
        return;
    }

    m_graphicsStress.ResumeAfterSceneLoad( launchOptions.graphicsStressSeed, launchOptions.graphicsStressActions,
                                           launchOptions.graphicsStressSceneIntervalFrames );
}


void RuntimeValidationHarness::ObserveSceneLifecycle( const SceneLifecyclePacket& packet,
                                                      const RunLaunchOptions& launchOptions )
{

    // Invariant: a reload may be sampled more than once, but the stress random
    // stream and cadence resume exactly once after population reaches commit.
    if ( launchOptions.graphicsStress &&
         m_graphicsStressSceneObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterScenePopulate ) )
    {
        ResumeGraphicsStressAfterSceneLoad( launchOptions );
    }
}


void RuntimeValidationHarness::PrintGraphicsStressExitSummary( int currentSceneFrame ) const
{
    if ( !m_graphicsStress.IsEnabled() )
    {
        return;
    }

    std::printf( "[graphics-stress] WM_QUIT received at frame=%d scene_frame=%d scene_loads=%d\n",
                 m_graphicsStress.FramesRun(), currentSceneFrame, m_graphicsStress.SceneLoadsRequested() );

    std::fflush( stdout );
}


SceneAutomationGateTracker& RuntimeValidationHarness::SceneGates()
{
    return m_sceneGates;
}
