//   - Agentic/Reference/engine-glossary.md//
// File: SkullbonezTests/TestAssetSystem.cpp
// Purpose:
//   Locks asset-library source registration and lookup behavior used by scene parsing.
//
// Summary:
//   AssetSystem owns logical asset names and resolves them to data-root paths.
//   Scene parsing borrows this registry when a scene references reusable
//   asset-library entries instead of spelling every object inline.
//
// Glossary:
//   Asset library: JSON recipe file containing reusable placeable assets.

// Invariants:
//   - Asset ids are assigned by AssetSystem and remain stable for the registered row.
//   - Built-in asset libraries must stay discoverable by their logical names.
//
// Related:
//   - SkullbonezSource/Assets/AssetSystem.cpp
//   - SkullbonezSource/Scene/AuthoredSceneParser.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Config.h"

#include <string>

using SkullbonezCore::Assets::AssetLibrarySourceAsset;
using SkullbonezCore::Assets::AssetSystem;
using SkullbonezCore::Core::EngineConfig;

TEST_CASE( "AssetSystem: asset-library source lookup preserves logical names and ids" )
{
    AssetSystem assets( "SkullbonezData" );
    const AssetLibrarySourceAsset& registered =
        assets.RegisterAssetLibrarySourceAsset( "assetlib.unit", "assets/unit.assets.json" );

    const AssetLibrarySourceAsset* found = assets.FindAssetLibrarySourceAsset( "assetlib.unit" );
    REQUIRE( found != nullptr );
    CHECK( found->id == registered.id );
    CHECK( found->logicalName == "assetlib.unit" );
    CHECK( found->relativePath == "assets/unit.assets.json" );
    CHECK( found->resolvedPath.find( "SkullbonezData" ) != std::string::npos );
    CHECK( assets.FindAssetLibrarySourceAsset( "assetlib.missing" ) == nullptr );
}


TEST_CASE( "AssetSystem: built-in asset libraries are registered for scene reuse" )
{
    AssetSystem assets;
    SkullbonezCore::Core::EngineConfig config;
    assets.RegisterBuiltInSourceAssets( config );

    const AssetLibrarySourceAsset* nature = assets.FindAssetLibrarySourceAsset( "assetlib.low_poly_nature" );
    const AssetLibrarySourceAsset* buildings = assets.FindAssetLibrarySourceAsset( "assetlib.buildings" );
    const AssetLibrarySourceAsset* physicsProps = assets.FindAssetLibrarySourceAsset( "assetlib.physics_props" );

    REQUIRE( nature != nullptr );
    REQUIRE( buildings != nullptr );
    REQUIRE( physicsProps != nullptr );
    CHECK( nature->relativePath == "assets/low_poly_nature.assets.json" );
    CHECK( buildings->relativePath == "assets/buildings.assets.json" );
    CHECK( physicsProps->relativePath == "assets/physics_props.assets.json" );
}


TEST_CASE( "AssetSystem: shader base-name resolution preserves source ownership order" )
{
    AssetSystem assets;

    CHECK( std::string( assets.ResolveShaderBaseName( "shaders/direct" ) ) == "shaders/direct" );
    CHECK( std::string( assets.ResolveShaderBaseName( "shader.text" ) ) == "shaders/text" );

    assets.RegisterShaderSourceAsset( "shader.text", "shaders/overridden_text" );
    CHECK( std::string( assets.ResolveShaderBaseName( "shader.text" ) ) == "shaders/overridden_text" );
    CHECK( std::string( assets.ResolveShaderBaseName( "shaders/overridden_text" ) ) == "shaders/overridden_text" );
}
