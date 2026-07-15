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

Invariants:
  - Construction is one bounded process-lifetime Startup allocation.
  - A configured live-style directory is marked ready only after Run applies
    its interactive/capture side effects.
  - Graphics-stress defaults and clamps remain byte-for-byte equivalent to the
    former Run-local launch policy.

Related:
  - SkullbonezSource/Runtime/RuntimeValidationHarness.h
  - SkullbonezSource/Runtime/GraphicsStressController.h
  - SkullbonezSource/Runtime/LiveStyleController.h
*/
#include "RuntimeValidationHarness.h"

#include <algorithm>
#include <cstdio>

#include "Allocation/RuntimeAllocationTracker.h"
#include "CaptureController.h"
#include "RunLaunchOptions.h"

using namespace SkullbonezCore::Runtime;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;


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


GraphicsStressController& RuntimeValidationHarness::GraphicsStress()
{
    return m_graphicsStress;
}
