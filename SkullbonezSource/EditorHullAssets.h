/*
File: SkullbonezSource/EditorHullAssets.h
Purpose:
  Names built-in convex hull assets used by the editor and scene snapshots.

Mental model:
  Scene files should prefer stable logical hull ids such as diamond or hex_prism.
  The asset boundary resolves those ids to the current hull files.

Related:
  - SkullbonezSource/RunInput.cpp
  - SkullbonezSource/RunScene.cpp
  - SkullbonezSource/SceneSnapshotWriter.cpp
*/
#pragma once

#include <cstddef>
#include <cstring>

namespace SkullbonezCore
{
namespace Assets
{

enum class EditorHullAsset
{
    UNKNOWN = -1,
    WEDGE,
    TRI_PRISM,
    TAPERED_BLOCK,
    PYRAMID,
    HEX_PRISM,
    DIAMOND,
    ROCK_SLAB_FLAT,
    ROCK_LUMP_LARGE,
    ROCK_SHARD_TALL,
    ROCK_CHIPPED_BLOCK,
    TREE_TRUNK_SMALL_FACETED,
    TREE_TRUNK_FACETED,
    PINE_TIER_LARGE,
    PINE_TIER_MID,
    PINE_TIER_TOP,
    CEDAR_TIER_LOW,
    CEDAR_TIER_MID,
    CEDAR_TIER_TALL_LOW,
    CEDAR_TIER_TALL_MID,
    CEDAR_TIER_TOP,
    TREE_ROOT_SMALL,
    TREE_ROOT_LARGE,
    PINE_NEEDLE_CLUSTER,
};

struct EditorHullAssetInfo
{
    EditorHullAsset asset;
    const char* token;
    const char* fileName;
    const char* path;
};

inline constexpr EditorHullAssetInfo EDITOR_HULL_ASSETS[] = {
    { EditorHullAsset::WEDGE, "wedge", "wedge.hull", "SkullbonezData/hulls/wedge.hull" },
    { EditorHullAsset::TRI_PRISM, "tri_prism", "tri_prism.hull", "SkullbonezData/hulls/tri_prism.hull" },
    { EditorHullAsset::TAPERED_BLOCK,
      "tapered_block",
      "tapered_block.hull",
      "SkullbonezData/hulls/tapered_block.hull" },
    { EditorHullAsset::PYRAMID, "pyramid", "pyramid.hull", "SkullbonezData/hulls/pyramid.hull" },
    { EditorHullAsset::HEX_PRISM, "hex_prism", "hex_prism.hull", "SkullbonezData/hulls/hex_prism.hull" },
    { EditorHullAsset::DIAMOND, "diamond", "diamond.hull", "SkullbonezData/hulls/diamond.hull" },
    { EditorHullAsset::ROCK_SLAB_FLAT,
      "rock_slab_flat",
      "rock_slab_flat.hull",
      "SkullbonezData/hulls/rock_slab_flat.hull" },
    { EditorHullAsset::ROCK_LUMP_LARGE,
      "rock_lump_large",
      "rock_lump_large.hull",
      "SkullbonezData/hulls/rock_lump_large.hull" },
    { EditorHullAsset::ROCK_SHARD_TALL,
      "rock_shard_tall",
      "rock_shard_tall.hull",
      "SkullbonezData/hulls/rock_shard_tall.hull" },
    { EditorHullAsset::ROCK_CHIPPED_BLOCK,
      "rock_chipped_block",
      "rock_chipped_block.hull",
      "SkullbonezData/hulls/rock_chipped_block.hull" },
    { EditorHullAsset::TREE_TRUNK_SMALL_FACETED,
      "tree_trunk_small_faceted",
      "tree_trunk_small_faceted.hull",
      "SkullbonezData/hulls/tree_trunk_small_faceted.hull" },
    { EditorHullAsset::TREE_TRUNK_FACETED,
      "tree_trunk_faceted",
      "tree_trunk_faceted.hull",
      "SkullbonezData/hulls/tree_trunk_faceted.hull" },
    { EditorHullAsset::PINE_TIER_LARGE,
      "pine_tier_large",
      "pine_tier_large.hull",
      "SkullbonezData/hulls/pine_tier_large.hull" },
    { EditorHullAsset::PINE_TIER_MID,
      "pine_tier_mid",
      "pine_tier_mid.hull",
      "SkullbonezData/hulls/pine_tier_mid.hull" },
    { EditorHullAsset::PINE_TIER_TOP,
      "pine_tier_top",
      "pine_tier_top.hull",
      "SkullbonezData/hulls/pine_tier_top.hull" },
    { EditorHullAsset::CEDAR_TIER_LOW,
      "cedar_tier_low",
      "cedar_tier_low.hull",
      "SkullbonezData/hulls/cedar_tier_low.hull" },
    { EditorHullAsset::CEDAR_TIER_MID,
      "cedar_tier_mid",
      "cedar_tier_mid.hull",
      "SkullbonezData/hulls/cedar_tier_mid.hull" },
    { EditorHullAsset::CEDAR_TIER_TALL_LOW,
      "cedar_tier_tall_low",
      "cedar_tier_tall_low.hull",
      "SkullbonezData/hulls/cedar_tier_tall_low.hull" },
    { EditorHullAsset::CEDAR_TIER_TALL_MID,
      "cedar_tier_tall_mid",
      "cedar_tier_tall_mid.hull",
      "SkullbonezData/hulls/cedar_tier_tall_mid.hull" },
    { EditorHullAsset::CEDAR_TIER_TOP,
      "cedar_tier_top",
      "cedar_tier_top.hull",
      "SkullbonezData/hulls/cedar_tier_top.hull" },
    { EditorHullAsset::TREE_ROOT_SMALL,
      "tree_root_small",
      "tree_root_small.hull",
      "SkullbonezData/hulls/tree_root_small.hull" },
    { EditorHullAsset::TREE_ROOT_LARGE,
      "tree_root_large",
      "tree_root_large.hull",
      "SkullbonezData/hulls/tree_root_large.hull" },
    { EditorHullAsset::PINE_NEEDLE_CLUSTER,
      "pine_needle_cluster",
      "pine_needle_cluster.hull",
      "SkullbonezData/hulls/pine_needle_cluster.hull" },
};

inline constexpr std::size_t EDITOR_HULL_ASSET_COUNT = sizeof( EDITOR_HULL_ASSETS ) / sizeof( EDITOR_HULL_ASSETS[0] );
inline constexpr float EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y = 1.2f;
inline constexpr float EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y = 1.6f;
inline constexpr float EDITOR_TREE_SMALL_ROOTED_LEGACY_ROOT_TO_TRUNK_DELTA_Y = 6.02921677f;
inline constexpr float EDITOR_TREE_LARGE_ROOTED_LEGACY_ROOT_TO_TRUNK_DELTA_Y = 8.35688591f;

inline const char* HullAssetBaseName( const char* token )
{
    const char* base = token ? token : "";
    for ( const char* cursor = base; *cursor != '\0'; ++cursor )
    {
        if ( *cursor == '/' || *cursor == '\\' )
        {
            base = cursor + 1;
        }
    }
    return base;
}

inline const EditorHullAssetInfo* FindEditorHullAssetInfo( EditorHullAsset asset )
{
    for ( const EditorHullAssetInfo& info : EDITOR_HULL_ASSETS )
    {
        if ( info.asset == asset )
        {
            return &info;
        }
    }
    return nullptr;
}

inline EditorHullAsset EditorHullAssetFromToken( const char* token )
{
    if ( !token || token[0] == '\0' )
    {
        return EditorHullAsset::UNKNOWN;
    }

    const char* base = HullAssetBaseName( token );
    for ( const EditorHullAssetInfo& info : EDITOR_HULL_ASSETS )
    {
        if ( strcmp( token, info.token ) == 0 || strcmp( token, info.path ) == 0 || strcmp( base, info.token ) == 0 ||
             strcmp( base, info.fileName ) == 0 )
        {
            return info.asset;
        }
    }
    if ( strcmp( base, "tapered" ) == 0 )
    {
        return EditorHullAsset::TAPERED_BLOCK;
    }
    if ( strcmp( base, "hexagon" ) == 0 )
    {
        return EditorHullAsset::HEX_PRISM;
    }
    return EditorHullAsset::UNKNOWN;
}

inline const char* EditorHullAssetToken( EditorHullAsset asset )
{
    const EditorHullAssetInfo* info = FindEditorHullAssetInfo( asset );
    return info ? info->token : "";
}

inline const char* EditorHullAssetPath( EditorHullAsset asset )
{
    const EditorHullAssetInfo* info = FindEditorHullAssetInfo( asset );
    return info ? info->path : nullptr;
}

inline const char* ResolveEditorHullAssetPath( const char* token )
{
    const EditorHullAsset asset = EditorHullAssetFromToken( token );
    const char* path = EditorHullAssetPath( asset );
    return path ? path : ( token ? token : "" );
}

inline constexpr bool EditorHullAssetDefaultsToContactRelease( EditorHullAsset asset )
{
    switch ( asset )
    {
    case EditorHullAsset::TREE_TRUNK_SMALL_FACETED:
    case EditorHullAsset::TREE_TRUNK_FACETED:
    case EditorHullAsset::PINE_TIER_LARGE:
    case EditorHullAsset::PINE_TIER_MID:
    case EditorHullAsset::PINE_TIER_TOP:
    case EditorHullAsset::CEDAR_TIER_LOW:
    case EditorHullAsset::CEDAR_TIER_MID:
    case EditorHullAsset::CEDAR_TIER_TALL_LOW:
    case EditorHullAsset::CEDAR_TIER_TALL_MID:
    case EditorHullAsset::CEDAR_TIER_TOP:
    case EditorHullAsset::PINE_NEEDLE_CLUSTER:
        return true;
    default:
        return false;
    }
}

inline constexpr float EditorHullAssetDefaultContactReleaseThreshold( EditorHullAsset asset )
{
    return asset == EditorHullAsset::PINE_NEEDLE_CLUSTER ? 1.25f : 1.0f;
}

inline bool HullAssetTokenDefaultsToContactRelease( const char* token )
{
    return EditorHullAssetDefaultsToContactRelease( EditorHullAssetFromToken( token ) );
}

inline float HullAssetTokenDefaultContactReleaseThreshold( const char* token )
{
    return EditorHullAssetDefaultContactReleaseThreshold( EditorHullAssetFromToken( token ) );
}

inline float EditorTreeRootedAboveRootLiftY( const char* instanceName )
{
    if ( !instanceName )
    {
        return 0.0f;
    }
    if ( strstr( instanceName, "tree_small_rooted_" ) )
    {
        return EDITOR_TREE_SMALL_ROOTED_ABOVE_ROOT_LIFT_Y;
    }
    if ( strstr( instanceName, "tree_pine_rooted_" ) || strstr( instanceName, "tree_cedar_rooted_" ) ||
         strstr( instanceName, "tree_pine_shedding_" ) )
    {
        return EDITOR_TREE_LARGE_ROOTED_ABOVE_ROOT_LIFT_Y;
    }
    return 0.0f;
}

inline float EditorTreeRootedLegacyRootToTrunkDeltaY( const char* instanceName )
{
    if ( !instanceName )
    {
        return 0.0f;
    }
    if ( strstr( instanceName, "tree_small_rooted_" ) )
    {
        return EDITOR_TREE_SMALL_ROOTED_LEGACY_ROOT_TO_TRUNK_DELTA_Y;
    }
    if ( strstr( instanceName, "tree_pine_rooted_" ) || strstr( instanceName, "tree_cedar_rooted_" ) ||
         strstr( instanceName, "tree_pine_shedding_" ) )
    {
        return EDITOR_TREE_LARGE_ROOTED_LEGACY_ROOT_TO_TRUNK_DELTA_Y;
    }
    return 0.0f;
}

} // namespace Assets
} // namespace SkullbonezCore
