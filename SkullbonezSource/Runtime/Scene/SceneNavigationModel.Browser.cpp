/*
File: SkullbonezSource/Runtime/Scene/SceneNavigationModel.Browser.cpp
Purpose:
  Implements filesystem discovery and current-path lookup on the UI-owned
  scene navigation values.

Summary:
  SceneNavigationModel owns browser paths, display names, and stable pointer
  views. Runtime supplies the filesystem implementation but retains no UI
  pointer or scene-controller authority.

Glossary:
  Stable pointer view: C-string pointers rebuilt only after the owning name
    strings reach their final storage for this refresh.
  Normalized path: Scene path using forward slashes for platform-independent
    comparison.

Invariants:
  - Paths and names are populated before namePtrs is rebuilt.
  - Paths compare with normalized separators.
  - An inaccessible directory clears the browser and reports a recoverable log.

Related:
  - SkullbonezSource/UI/UISceneNavigationModel.h
  - SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "../../UI/UISceneNavigationModel.h"
#include "../../Core/Log.h"
#include "../../Core/WindowConstants.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace SkullbonezCore
{
namespace UI
{
namespace
{
const char* FileNameFromPath( const char* path )
{

    if ( !path )
    {
        return "";
    }

    const char* slash = strrchr( path, '/' );
    const char* backslash = strrchr( path, '\\' );
    const char* separator = slash;

    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }

    return separator ? separator + 1 : path;
}

std::string NormalizeScenePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    return normalized;
}

bool ScenePathEqualsNormalizedPath( const std::string& normalizedPath, const std::string& candidatePath )
{

    if ( normalizedPath.size() != candidatePath.size() )
    {
        return false;
    }

    for ( size_t i = 0; i < normalizedPath.size(); ++i )
    {

        if ( normalizedPath[i] != ( candidatePath[i] == '\\' ? '/' : candidatePath[i] ) )
        {
            return false;
        }
    }

    return true;
}

bool IsSceneJsonFile( const std::filesystem::path& path )
{
    const std::string name = path.filename().string();
    return name.size() > 11 && name.compare( name.size() - 11, 11, ".scene.json" ) == 0;
}
} // namespace

int RunSceneBrowserState::CurrentIndexForPath( const std::string* currentScenePath ) const
{

    if ( !currentScenePath )
    {
        return -1;
    }

    for ( int i = 0; i < static_cast<int>( paths.size() ); ++i )
    {

        if ( ScenePathEqualsNormalizedPath( paths[i], *currentScenePath ) )
        {
            return i;
        }
    }

    return -1;
}

void SceneNavigationModel::RefreshBrowserList()
{
    browser.paths.clear();
    browser.names.clear();
    browser.namePtrs.clear();

    const std::filesystem::path sceneDir = std::filesystem::path( DATA_ROOT ) / "scenes";
    std::error_code error;

    if ( !std::filesystem::exists( sceneDir, error ) || error )
    {

        if ( error )
        {
            SkullbonezCore::Core::Log().WriteEventf( "scene_browser_refresh_failed message=\"%s\"",
                                                     error.message().c_str() );
        }

        return;
    }

    std::filesystem::directory_iterator iterator( sceneDir, error );
    const std::filesystem::directory_iterator end;

    while ( !error && iterator != end )
    {
        const std::filesystem::directory_entry& entry = *iterator;
        std::error_code entryError;

        if ( entry.is_regular_file( entryError ) && !entryError && IsSceneJsonFile( entry.path() ) )
        {
            browser.paths.push_back( NormalizeScenePath( entry.path().generic_string() ) );
        }

        iterator.increment( error );
    }

    if ( error )
    {
        SkullbonezCore::Core::Log().WriteEventf( "scene_browser_refresh_failed message=\"%s\"", error.message().c_str() );
        browser.paths.clear();
    }

    std::sort( browser.paths.begin(), browser.paths.end() );
    browser.paths.erase( std::unique( browser.paths.begin(), browser.paths.end() ), browser.paths.end() );
    browser.names.reserve( browser.paths.size() );
    browser.namePtrs.reserve( browser.paths.size() );

    for ( const std::string& path : browser.paths )
    {
        browser.names.emplace_back( FileNameFromPath( path.c_str() ) );
    }

    for ( const std::string& name : browser.names )
    {
        browser.namePtrs.push_back( name.c_str() );
    }
}
} // namespace UI
} // namespace SkullbonezCore
