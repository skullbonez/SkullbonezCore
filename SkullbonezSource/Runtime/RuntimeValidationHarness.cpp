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
  - SkullbonezSource/Runtime/RuntimeValidationHarness.h
  - SkullbonezSource/Runtime/GraphicsStressController.h
  - SkullbonezSource/Runtime/LiveStyleController.h
*/
#include "RuntimeValidationHarness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Allocation/RuntimeAllocationTracker.h"
#include "CaptureController.h"
#include "RunLaunchOptions.h"
#include "Scene/SceneController.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/ObjectContactManifold.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Physics/PhysicsEngine.h"

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Physics;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

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


void SceneAutomationGateTracker::ResetForLoad()
{
    // Lifetime: observations identify body rows from one scene load only; they
    // must be discarded before topology is rebuilt and indices are reused.
    m_requiredContacts.clear();
    m_requiredBroadphaseXCells.clear();
}


void SceneAutomationGateTracker::ReserveRequiredContacts( std::size_t count )
{
    m_requiredContacts.reserve( count );
}


void SceneAutomationGateTracker::AppendRequiredContact( const char* nameA, const char* nameB, int bodyA, int bodyB )
{
    RequiredContactState state;
    strcpy_s( state.nameA, sizeof( state.nameA ), nameA );
    strcpy_s( state.nameB, sizeof( state.nameB ), nameB );
    state.bodyA = bodyA;
    state.bodyB = bodyB;
    m_requiredContacts.push_back( state );
}


void SceneAutomationGateTracker::ReserveRequiredBroadphaseXCells( std::size_t count )
{
    m_requiredBroadphaseXCells.reserve( count );
}


void SceneAutomationGateTracker::AppendRequiredBroadphaseXCells( int minCellX, int maxCellX, int cellY, int cellZ )
{
    RequiredBroadphaseXCellsState state;
    state.minCellX = minCellX;
    state.maxCellX = maxCellX;
    state.cellY = cellY;
    state.cellZ = cellZ;
    m_requiredBroadphaseXCells.push_back( state );
}


void SceneAutomationGateTracker::UpdateRequiredContacts( SceneController& scene, float contactEpsilon )
{
    if ( m_requiredContacts.empty() )
    {
        return;
    }

    // Invariant: validation observes committed compact stores. The legacy
    // object mirror may not have been refreshed yet at this post-step point.
    const PhysicsBodyStore& bodyStore = scene.BodyStore();
    const ColliderStore& colliderStore = scene.Colliders();
    const auto bodyRecords = bodyStore.Records();
    const auto colliderRecords = colliderStore.Records();
    const int contactModelCount =
        (std::min)( bodyStore.Count(), static_cast<int>( (std::min)( bodyRecords.size(), colliderRecords.size() ) ) );
    for ( RequiredContactState& required : m_requiredContacts )
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
        if ( BuildObjectContactManifold( AutomationContactBodyView( hotFields, bodyAIndex ),
                                         colliderA.shape,
                                         AutomationContactBodyView( hotFields, bodyBIndex ),
                                         colliderB.shape,
                                         required.bodyA,
                                         required.bodyB,
                                         contactEpsilon + 0.25f,
                                         manifold ) )
        {
            required.touched = true;
        }
    }

    const std::vector<PhysicsDebugContact>& contacts = PhysicsEngine::ReadDebugContacts( scene.Physics() );
    for ( const PhysicsDebugContact& contact : contacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 )
        {
            continue;
        }
        for ( RequiredContactState& required : m_requiredContacts )
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


void SceneAutomationGateTracker::UpdateRequiredBroadphaseXCells( const SpatialGrid::ActiveCell* activeCells,
                                                                 int activeCellCount )
{
    if ( m_requiredBroadphaseXCells.empty() || !activeCells || activeCellCount <= 0 )
    {
        return;
    }

    for ( RequiredBroadphaseXCellsState& required : m_requiredBroadphaseXCells )
    {
        if ( required.activated )
        {
            continue;
        }

        required.lastActiveCellCount = activeCellCount;
        required.lastMissingCellX = -1;
        required.hasObservedXRange = false;
        for ( int i = 0; i < activeCellCount; ++i )
        {
            const SpatialGrid::ActiveCell& active = activeCells[i];
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
        // Both loops are bounded by SpatialGrid::MAX_BUCKETS.
        bool allActive = true;
        for ( int x = required.minCellX; x <= required.maxCellX; ++x )
        {
            bool found = false;
            for ( int i = 0; i < activeCellCount; ++i )
            {
                const SpatialGrid::ActiveCell& active = activeCells[i];
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
    for ( const RequiredContactState& contact : m_requiredContacts )
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
    for ( const RequiredBroadphaseXCellsState& required : m_requiredBroadphaseXCells )
    {
        if ( !required.activated )
        {
            return false;
        }
    }
    return true;
}


bool SceneAutomationGateTracker::HasRequirements() const
{
    return !m_requiredContacts.empty() || !m_requiredBroadphaseXCells.empty();
}


bool SceneAutomationGateTracker::Complete() const
{
    return RequiredContactsComplete() && RequiredBroadphaseXCellsComplete();
}


void SceneAutomationGateTracker::PrintMissingRequirements() const
{
    for ( const RequiredContactState& contact : m_requiredContacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
        {
            std::fprintf( stderr, "[scene] required_contact missing: %s <-> %s\n", contact.nameA, contact.nameB );
        }
    }
    for ( const RequiredBroadphaseXCellsState& cells : m_requiredBroadphaseXCells )
    {
        if ( !cells.activated )
        {
            std::fprintf( stderr,
                          "[scene] required_broadphase_x_cells missing: x %d..%d y %d z %d first_missing=%d "
                          "active_cells=%d observed_x=%s%d..%d\n",
                          cells.minCellX,
                          cells.maxCellX,
                          cells.cellY,
                          cells.cellZ,
                          cells.lastMissingCellX,
                          cells.lastActiveCellCount,
                          cells.hasObservedXRange ? "" : "none ",
                          cells.lastObservedMinX,
                          cells.lastObservedMaxX );
        }
    }
}


std::unique_ptr<RuntimeValidationHarness> RuntimeValidationHarness::CreateForStartup()
{
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Startup );
    // Allocation policy: keep both cold harness implementations out of Run.h.
    // The single owner allocation is bounded to process startup.
    return std::make_unique<RuntimeValidationHarness>();
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
    launchOptions.graphicsStressMemoryIntervalFrames =
        std::clamp( launch.graphicsStressMemoryIntervalFrames, 0, 36000 );
    launchOptions.interactiveSceneRun = true;

    m_graphicsStress.Configure( resolvedSeed,
                                launchOptions.graphicsStressActions,
                                launchOptions.graphicsStressSceneIntervalFrames,
                                launchOptions.graphicsStressMemoryIntervalFrames );
    return liveStyleConfigured;
}


void RuntimeValidationHarness::MarkLiveStyleReady()
{
    m_liveStyle.MarkReady();
}


void RuntimeValidationHarness::TickLiveStyle( SceneRuntimeStyleContext context )
{
    m_liveStyle.Tick( context );
}


bool RuntimeValidationHarness::HasPendingLiveStyleCapture() const
{
    return m_liveStyle.HasPendingCapture();
}


void RuntimeValidationHarness::SavePendingLiveStyleCapture( CaptureController& capture,
                                                            Rendering::IRenderCaptureBackend& backend )
{
    m_liveStyle.SavePendingCapture( capture, backend );
}


void RuntimeValidationHarness::ResumeGraphicsStressAfterSceneLoad( const RunLaunchOptions& launchOptions )
{
    if ( !launchOptions.graphicsStress )
    {
        return;
    }
    m_graphicsStress.ResumeAfterSceneLoad( launchOptions.graphicsStressSeed,
                                           launchOptions.graphicsStressActions,
                                           launchOptions.graphicsStressSceneIntervalFrames );
}


void RuntimeValidationHarness::PrintGraphicsStressExitSummary( int currentSceneFrame ) const
{
    if ( !m_graphicsStress.IsEnabled() )
    {
        return;
    }
    std::printf( "[graphics-stress] WM_QUIT received at frame=%d scene_frame=%d scene_loads=%d\n",
                 m_graphicsStress.FramesRun(),
                 currentSceneFrame,
                 m_graphicsStress.SceneLoadsRequested() );
    std::fflush( stdout );
}


SceneAutomationGateTracker& RuntimeValidationHarness::SceneGates()
{
    return m_sceneGates;
}


const SceneAutomationGateTracker& RuntimeValidationHarness::SceneGates() const
{
    return m_sceneGates;
}
