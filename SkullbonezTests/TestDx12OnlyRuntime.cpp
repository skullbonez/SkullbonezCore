/*
File: TestDx12OnlyRuntime.cpp
Purpose:
  Proves the command-line renderer option table still exposes DX12 as the only
  runtime renderer choice.

Mental model:
  This is the small boolean product check that survives the deleted regex
  boundary apparatus. It tests the runtime startup contract directly instead of
  counting source spellings across the repository.

Glossary:
  Renderer option table: Startup data that maps accepted `--renderer` values to
    the single production backend.

Invariants:
  - The table has one entry: `dx12`.
  - `d3d12` remains a compatibility alias for older automation and scripts.

Related:
  - SkullbonezSource/Runtime/RunLaunchOptions.Renderer.h owns the table.
  - engine-cleanup-plans/03-governance-apparatus-reduction.md step 1.1.
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/RunLaunchOptions.Renderer.h"

#include <string>

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
