//
// File: SkullbonezTests/TestSceneParserLinkStubs.cpp
// Purpose:
//   Provide unit-test link stubs for asset-system hooks that are not part of the
//   focused committed-scene parse tests.
//
// Mental model:
//   `TestSceneParser.cpp` supports both simple path-based scene JSON and
//   runtime asset-library resolution through AssetSystem. E3 tests exercise the
//   committed path-based scene contract, while later physics fixtures need only
//   a constructible AssetSystem token for borrowed Terrain signatures.
//
// Glossary:
//   Asset-system hook: Runtime lookup used when a scene resolves logical asset
//     library names through an initialized AssetSystem.
//   Link stub: Test-only definition that satisfies unresolved symbols while
//     failing loudly if a focused unit crosses into that dependency.
//
// Invariants:
//   - These methods are test-only link stubs, not runtime fixtures.
//   - A focused path-based scene parse must not reach AssetSystem lookup.
//
// Related:
//   - SkullbonezSource/Scene/TestSceneParser.cpp
//   - SkullbonezTests/TestSceneParserUnit.cpp
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../SkullbonezSource/Assets/AssetSystem.h"

#include <stdexcept>
#include <utility>

namespace SkullbonezCore
{
namespace Assets
{
AssetSystem::AssetSystem( std::string dataRoot ) : m_dataRoot( std::move( dataRoot ) )
{
}

const AssetLibrarySourceAsset* AssetSystem::FindAssetLibrarySourceAsset( const char* ) const
{
    // Hazard: reaching this stub means a focused path-based parser test crossed
    // into runtime asset-library resolution without a real AssetSystem.
    throw std::runtime_error( "TestSceneParserLinkStubs: unexpected AssetSystem asset-library lookup" );
}
} // namespace Assets
} // namespace SkullbonezCore
