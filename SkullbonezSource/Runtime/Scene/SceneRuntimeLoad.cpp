/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp
Purpose:
  Implements scene load-begin orchestration outside Run.

Mental model:
  The begin phase decides whether a queue index can load, captures or clears
  live reset state, flushes GPU work before old scene resources are destroyed,
  and advances SceneController bookkeeping. Later cuts can move the reset body,
  authored/generated setup, diagnostics, and render reinitialization behind the
  same scene-owned boundary.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#include "SceneRuntimeLoad.h"
#include "SceneController.h"
#include "SceneRuntime.h"
#include "../../Rendering/IRenderBackend.h"

#include <algorithm>
#include <cstring>

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

int SceneBrowserIndexForPath( const RunSceneBrowserState& sceneBrowser, const std::string& scenePath )
{
    const std::string normalizedScenePath = NormalizeScenePath( scenePath );
    for ( int i = 0; i < static_cast<int>( sceneBrowser.paths.size() ); ++i )
    {
        if ( NormalizeScenePath( sceneBrowser.paths[i] ) == normalizedScenePath )
        {
            return i;
        }
    }
    return -1;
}
} // namespace


SceneRuntimeLoadBeginResult BeginSceneRuntimeLoad( SceneRuntimeLoadBeginContext& context,
                                                   int index,
                                                   bool suppressExitOnComplete,
                                                   bool preserveRuntimeState )
{
    SceneRuntimeLoadBeginResult result;
    if ( !context.controller.HasEntry( index ) )
    {
        return result;
    }

    if ( suppressExitOnComplete )
    {
        context.reset.scene.isInteractiveRun = true;
    }
    if ( context.interactiveSceneRunRequested )
    {
        context.reset.scene.isInteractiveRun = true;
    }
    result.suppressAutomationExit = context.reset.scene.isInteractiveRun || suppressExitOnComplete;
    result.shouldPreserveRuntimeState = preserveRuntimeState && context.controller.HasCurrentEntry();
    if ( result.shouldPreserveRuntimeState )
    {
        result.resetSnapshot = CaptureSceneRuntimeResetSnapshot( context.reset );
    }
    else
    {
        ClearSceneRuntimeUIOverrides( context.reset );
    }

    if ( context.renderer )
    {
        context.renderer->FlushGPU();
    }

    context.controller.BeginLoad( index );
    result.scenePath = &context.controller.PathAt( index );
    if ( !result.shouldPreserveRuntimeState )
    {
        context.sceneBrowser.selectedCineModeSceneIndex =
            ( !result.scenePath->empty() && IsCineScenePath( *result.scenePath ) )
                ? SceneBrowserIndexForPath( context.sceneBrowser, *result.scenePath )
                : -1;
    }
    result.shouldLoad = true;
    return result;
}

} // namespace Basics
} // namespace SkullbonezCore
