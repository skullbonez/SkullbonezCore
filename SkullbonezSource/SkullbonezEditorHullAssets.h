/*
File: SkullbonezSource/SkullbonezEditorHullAssets.h
Purpose:
  Names built-in convex hull assets used by the editor and scene snapshots.

Mental model:
  Scene files should prefer stable logical hull ids such as diamond or hex_prism.
  The asset boundary resolves those ids to the current hull files.

Related:
  - SkullbonezSource/SkullbonezRunInput.cpp
  - SkullbonezSource/SkullbonezRunScene.cpp
  - SkullbonezSource/SkullbonezSceneSnapshotWriter.cpp
*/
#pragma once

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
    { EditorHullAsset::TAPERED_BLOCK, "tapered_block", "tapered_block.hull", "SkullbonezData/hulls/tapered_block.hull" },
    { EditorHullAsset::PYRAMID, "pyramid", "pyramid.hull", "SkullbonezData/hulls/pyramid.hull" },
    { EditorHullAsset::HEX_PRISM, "hex_prism", "hex_prism.hull", "SkullbonezData/hulls/hex_prism.hull" },
    { EditorHullAsset::DIAMOND, "diamond", "diamond.hull", "SkullbonezData/hulls/diamond.hull" },
};

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
        if ( strcmp( token, info.token ) == 0 || strcmp( token, info.path ) == 0 || strcmp( base, info.token ) == 0 || strcmp( base, info.fileName ) == 0 )
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

} // namespace Assets
} // namespace SkullbonezCore
