/*
File: TestDx12OnlyRuntime.cpp
Purpose:
  Proves the command-line renderer option table and retained geometry upload
  decisions preserve the DX12 runtime contract.

Summary:
  This is the small boolean product check that survives the deleted regex
  boundary apparatus. It tests the runtime startup contract directly instead of
  counting source spellings across the repository. Retained geometry cases
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
  - SkullbonezSource/Runtime/Startup/RunLaunchOptions.h owns the table.
  - SkullbonezSource/Rendering/RenderCommandTypes.h owns retained upload planning.
  - AGENTS.md documents the DX12-only runtime contract.
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Rendering/RenderCommandTypes.h"
#include "../SkullbonezSource/Runtime/Startup/RunLaunchOptions.h"

#include <string>

using SkullbonezCore::Rendering::BuildRetainedGeometryRangeUploadPlan;
using SkullbonezCore::Rendering::BuildRetainedGeometryUploadPlan;
using SkullbonezCore::Rendering::RetainedGeometryRangeToken;
using SkullbonezCore::Rendering::RetainedGeometryStreamToken;
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

TEST_CASE( "Retained geometry upload planning is stable and append-only" )
{
    const auto stable =
        BuildRetainedGeometryUploadPlan( RetainedGeometryStreamToken { 7u, 11u },
                                         96u,
                                         RetainedGeometryStreamToken { 7u, 11u },
                                         96u,
                                         true );
    CHECK_FALSE( stable.uploadRequired );
    CHECK( stable.firstChangedUnit == 0u );

    const auto appendedLines =
        BuildRetainedGeometryUploadPlan( { 7u, 11u }, 96u, { 7u, 12u }, 120u, false );
    CHECK( appendedLines.uploadRequired );
    CHECK( appendedLines.firstChangedUnit == 96u );

    const auto appendedRibbon =
        BuildRetainedGeometryUploadPlan( { 7u, 11u }, 8u, { 7u, 12u }, 10u, true );
    CHECK( appendedRibbon.uploadRequired );
    CHECK( appendedRibbon.firstChangedUnit == 7u );

    const auto replacement =
        BuildRetainedGeometryUploadPlan( { 7u, 11u }, 96u, { 8u, 1u }, 24u, true );
    CHECK( replacement.uploadRequired );
    CHECK( replacement.firstChangedUnit == 0u );

    const auto contraction =
        BuildRetainedGeometryUploadPlan( { 7u, 11u }, 96u, { 7u, 12u }, 24u, false );
    CHECK( contraction.uploadRequired );
    CHECK( contraction.firstChangedUnit == 0u );
}

TEST_CASE( "Retained geometry ranges upload only the repaired append suffix" )
{
    const auto range = []( uint64_t identity, uint32_t sourceVersion, uint32_t recordCount )
    {
        RetainedGeometryRangeToken token;
        token.identity = identity;
        token.sourceVersion = sourceVersion;
        token.recordCount = recordCount;
        return token;
    };
    const auto stable = BuildRetainedGeometryRangeUploadPlan( range( 41u, 3u, 96u ), range( 41u, 3u, 96u ) );
    CHECK_FALSE( stable.uploadRequired );

    const auto appended = BuildRetainedGeometryRangeUploadPlan( range( 41u, 3u, 96u ),
                                                                range( 41u, 3u, 101u ) );
    CHECK( appended.uploadRequired );
    CHECK( appended.firstChangedUnit == 95u );

    const auto firstAppend =
        BuildRetainedGeometryRangeUploadPlan( range( 41u, 3u, 0u ), range( 41u, 3u, 1u ) );
    CHECK( firstAppend.uploadRequired );
    CHECK( firstAppend.firstChangedUnit == 0u );

    const auto replaced =
        BuildRetainedGeometryRangeUploadPlan( range( 41u, 3u, 96u ), range( 41u, 4u, 96u ) );
    CHECK( replaced.uploadRequired );
    CHECK( replaced.firstChangedUnit == 0u );

    const auto contracted =
        BuildRetainedGeometryRangeUploadPlan( range( 41u, 3u, 96u ), range( 41u, 3u, 24u ) );
    CHECK( contracted.uploadRequired );
    CHECK( contracted.firstChangedUnit == 0u );
}
