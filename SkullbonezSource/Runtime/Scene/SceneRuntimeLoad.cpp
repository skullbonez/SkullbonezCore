/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp
Purpose:
  Implements scene load-begin orchestration outside Run.

Mental model:
  Preparation decides whether a queue index can load, captures live reset state,
  and flushes GPU work before old scene resources are destroyed. Commit advances
  controller bookkeeping only after preparation and unload consumers succeed.

Glossary:
  Load preparation: Scene load phase that validates queue index, preserves optional
    runtime state, and marks controller bookkeeping.
  Scene browser: UI-facing list of scene files discovered on disk.
  GPU (Graphics Processing Unit): Render device that must be flushed before old
    scene resources are torn down.

Invariants:
  - GPU flush happens before caller-owned teardown can destroy scene resources.
  - Browser paths and queue paths compare in normalized slash form.
  - Preserve-runtime-state decisions are captured before controller load state
    changes; failed preparation leaves every owner unchanged.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#include "SceneRuntimeLoad.h"
#include "../WindowConstants.h"
#include "SceneController.h"
#include "SceneRuntime.h"
#include "../../Core/Log.h"
#include "../../Rendering/IRenderDeviceLifecycle.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace SkullbonezCore
{
namespace Basics
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

bool IsCineScenePath( const std::string& path )
{
    const char* name = FileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}

std::string NormalizeScenePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    return normalized;
}

char NormalizedScenePathChar( char value )
{
    return value == '\\' ? '/' : value;
}

bool ScenePathEqualsNormalizedPath( const std::string& normalizedPath, const std::string& candidatePath )
{
    if ( normalizedPath.size() != candidatePath.size() )
    {
        return false;
    }

    for ( size_t i = 0; i < normalizedPath.size(); ++i )
    {
        if ( normalizedPath[i] != NormalizedScenePathChar( candidatePath[i] ) )
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

int SceneBrowserIndexForPath( const RunSceneBrowserState& sceneBrowser, const std::string& scenePath )
{
    for ( int i = 0; i < static_cast<int>( sceneBrowser.paths.size() ); ++i )
    {
        if ( ScenePathEqualsNormalizedPath( sceneBrowser.paths[i], scenePath ) )
        {
            return i;
        }
    }
    return -1;
}
} // namespace


void RefreshSceneBrowserList( RunSceneBrowserState& sceneBrowser )
{
    // Concept: The browser owns three parallel arrays: normalized paths, display
    // names, and stable c-string pointers into the display-name storage.
    sceneBrowser.paths.clear();
    sceneBrowser.names.clear();
    sceneBrowser.namePtrs.clear();

    const std::filesystem::path sceneDir = std::filesystem::path( DATA_ROOT ) / "scenes";
    try
    {
        if ( !std::filesystem::exists( sceneDir ) )
        {
            return;
        }

        for ( const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator( sceneDir ) )
        {
            if ( !entry.is_regular_file() || !IsSceneJsonFile( entry.path() ) )
            {
                continue;
            }
            sceneBrowser.paths.push_back( NormalizeScenePath( entry.path().generic_string() ) );
        }
    }
    catch ( const std::filesystem::filesystem_error& e )
    {
        Log().WriteEventf( "scene_browser_refresh_failed message=\"%s\"", e.what() );
        sceneBrowser.paths.clear();
    }

    std::sort( sceneBrowser.paths.begin(), sceneBrowser.paths.end() );
    sceneBrowser.paths.erase( std::unique( sceneBrowser.paths.begin(), sceneBrowser.paths.end() ),
                              sceneBrowser.paths.end() );
    sceneBrowser.names.reserve( sceneBrowser.paths.size() );
    sceneBrowser.namePtrs.reserve( sceneBrowser.paths.size() );
    for ( const std::string& path : sceneBrowser.paths )
    {
        sceneBrowser.names.emplace_back( FileNameFromPath( path.c_str() ) );
    }
    for ( const std::string& name : sceneBrowser.names )
    {
        sceneBrowser.namePtrs.push_back( name.c_str() );
    }
}


int CurrentSceneBrowserIndex( const SceneController& controller, const RunSceneBrowserState& sceneBrowser )
{
    const std::string* currentScenePath = controller.CurrentPath();
    if ( !currentScenePath )
    {
        return -1;
    }

    return SceneBrowserIndexForPath( sceneBrowser, *currentScenePath );
}


SceneRuntimeLoadBeginResult PrepareSceneRuntimeLoad( const SceneController& controller,
                                                     const RuntimeRenderer& renderer,
                                                     const RunDebugState& debug,
                                                     const RunCameraState& camera,
                                                     Rendering::IRenderDeviceLifecycle* renderLifecycle,
                                                     bool interactiveSceneRunRequested,
                                                     int index,
                                                     bool suppressExitOnComplete,
                                                     bool preserveRuntimeState )
{
    SceneRuntimeLoadBeginResult result;
    if ( !controller.HasEntry( index ) )
    {
        return result;
    }

    result.index = index;
    result.scenePath = &controller.PathAt( index );
    if ( renderLifecycle )
    {
        // Lane R: old scene resources may still be referenced by in-flight GPU
        // work. A failed drain must leave every scene/controller owner intact.
        result.status = renderLifecycle->FlushGPU();
        if ( !result.status.ok )
        {
            return result;
        }
    }

    result.makeInteractive = suppressExitOnComplete || interactiveSceneRunRequested;
    result.suppressAutomationExit = controller.State().isInteractiveRun || result.makeInteractive;
    result.shouldPreserveRuntimeState = preserveRuntimeState && controller.HasCurrentEntry();
    if ( result.shouldPreserveRuntimeState )
    {
        // Lifetime: Snapshot before BeginLoad mutates scene bookkeeping so the
        // restore policy sees the live operator-owned state from the old run.
        result.resetSnapshot = CaptureSceneRuntimeResetSnapshot( controller, renderer, debug, camera );
    }
    result.shouldLoad = true;
    return result;
}


void CommitSceneRuntimeLoad( SceneController& controller, const SceneRuntimeLoadBeginResult& prepared )
{
    // Invariant: preparation has already validated the index and drained the
    // device; this is the first mutation of scene/controller state.
    if ( prepared.makeInteractive )
    {
        controller.State().isInteractiveRun = true;
    }
    if ( !prepared.shouldPreserveRuntimeState )
    {
        ClearSceneRuntimeUIOverrides( controller );
    }
    controller.BeginLoad( prepared.index );
    if ( !prepared.shouldPreserveRuntimeState )
    {
        controller.Browser().selectedCineModeSceneIndex =
            ( !prepared.scenePath->empty() && IsCineScenePath( *prepared.scenePath ) )
                ? SceneBrowserIndexForPath( controller.Browser(), *prepared.scenePath )
                : -1;
    }
}

} // namespace Basics
} // namespace SkullbonezCore
