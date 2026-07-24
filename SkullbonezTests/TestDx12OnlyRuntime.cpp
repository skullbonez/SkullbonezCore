/*
File: TestDx12OnlyRuntime.cpp
Purpose:
  Proves the command-line renderer option table and retained trajectory upload
  decisions preserve the DX12 runtime contract.

Summary:
  This is the small boolean product check that survives the deleted regex
  boundary apparatus. It tests the runtime startup contract directly instead of
  counting source spellings across the repository. Retained trajectory cases
  lock the token-driven stable and append-only upload plans.

Glossary:
  Renderer option table: Startup data that maps accepted `--renderer` values to
    the single production backend.
  Upload plan: Value decision describing whether retained GPU bytes changed and
    the first unit that must be copied.

Invariants:
  - The table has one entry: `dx12`.
  - `d3d12` remains a compatibility alias for older automation and scripts.
  - Equal retained stream/revision tokens never schedule an upload.

Related:
  - SkullbonezSource/Runtime/App/RunLaunchOptions.Renderer.h owns the table.
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h owns retained upload planning.
  - AGENTS.md documents the DX12-only runtime contract.
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Rendering/DX12/RenderBackendDX12.h"
#include "../SkullbonezSource/Runtime/App/RunLaunchOptions.Renderer.h"

#include <string>

using SkullbonezCore::Rendering::BuildRetainedTrajectoryUploadPlanDX12;
using SkullbonezCore::Runtime::kRuntimeRendererOptionCount;
using SkullbonezCore::Runtime::kRuntimeRendererOptions;

TEST_CASE( "Runtime renderer options: DX12 is the only launch renderer" )
{
    REQUIRE( kRuntimeRendererOptionCount == 1u );

    const SkullbonezCore::Runtime::RuntimeRendererOption& renderer = kRuntimeRendererOptions[0];
    REQUIRE( renderer.name != nullptr );
    CHECK( std::string( renderer.name ) == "dx12" );

    REQUIRE( renderer.alias != nullptr );
    CHECK( std::string( renderer.alias ) == "d3d12" );
}

TEST_CASE( "DX12 retained trajectory upload planning is stable and append-only" )
{
    const auto stable = BuildRetainedTrajectoryUploadPlanDX12( 7u, 11u, 96u, 7u, 11u, 96u, true );
    CHECK_FALSE( stable.uploadRequired );
    CHECK( stable.firstChangedUnit == 0u );

    const auto appendedLines = BuildRetainedTrajectoryUploadPlanDX12( 7u, 11u, 96u, 7u, 12u, 120u, false );
    CHECK( appendedLines.uploadRequired );
    CHECK( appendedLines.firstChangedUnit == 96u );

    const auto appendedRibbon = BuildRetainedTrajectoryUploadPlanDX12( 7u, 11u, 8u, 7u, 12u, 10u, true );
    CHECK( appendedRibbon.uploadRequired );
    CHECK( appendedRibbon.firstChangedUnit == 7u );

    const auto replacement = BuildRetainedTrajectoryUploadPlanDX12( 7u, 11u, 96u, 8u, 1u, 24u, true );
    CHECK( replacement.uploadRequired );
    CHECK( replacement.firstChangedUnit == 0u );

    const auto contraction = BuildRetainedTrajectoryUploadPlanDX12( 7u, 11u, 96u, 7u, 12u, 24u, false );
    CHECK( contraction.uploadRequired );
    CHECK( contraction.firstChangedUnit == 0u );
}
