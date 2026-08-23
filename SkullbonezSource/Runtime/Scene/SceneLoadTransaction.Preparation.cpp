/*
File: SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Preparation.cpp
Purpose:
  Implements scene load-begin orchestration outside Run.

Summary:
  Preparation decides whether a queue index can load, captures live reset state,
  and flushes GPU work before old scene resources are destroyed. Commit advances
  controller bookkeeping only after preparation and unload consumers succeed.

Invariants:
  - GPU flush happens before caller-owned teardown can destroy scene resources.
  - Browser paths and queue paths compare in normalized slash form.
  - Preserve-runtime-state decisions are captured before controller load state
    changes; failed preparation leaves every owner unchanged.

Related:
  - SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneLoadTransaction.h"
#include "SceneLoadPreparation.h"
#include "../../Core/WindowConstants.h"
#include "SceneController.h"
#include "SceneSessionState.h"
#include "../../Core/Log.h"
#include "../../Rendering/DX12/Dx12FrameOwner.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace SkullbonezCore
{
namespace Runtime
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

int SceneBrowserIndexForPath( const std::vector<std::string>& browserPaths, const std::string& scenePath )
{
    for ( int i = 0; i < static_cast<int>( browserPaths.size() ); ++i )
    {
        if ( ScenePathEqualsNormalizedPath( browserPaths[i], scenePath ) )
        {
            return i;
        }
    }

    return -1;
}
} // namespace


SceneLoadBeginResult SceneLoadTransaction::PrepareLoad( const SceneController& controller,
                                                        const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                                        SceneRenderPolicyState renderPolicy, const OverlayDebugState& debug,
                                                        const CameraControlState& camera,
                                                        Rendering::Dx12FrameOwner* renderFrame,
                                                        bool interactiveSceneRunRequested, int index,
                                                        bool suppressExitOnComplete, bool preserveRuntimeState )
{
    SceneLoadBeginResult result;

    if ( !controller.HasEntry( index ) )
    {
        return result;
    }

    result.index = index;
    result.scenePath = &controller.PathAt( index );

    if ( renderFrame )
    {
        // Recoverable error: old scene resources may still be referenced by in-flight GPU
        // work. A failed drain must leave every scene/controller owner intact.
        result.status = renderFrame->FlushGPU();

        if ( !result.status.Ok() )
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
        result.resetSnapshot = CaptureResetSnapshot( controller, uiOverrides, renderPolicy, debug, camera );
    }

    result.shouldLoad = true;
    return result;
}


void SceneLoadTransaction::CommitLoad( SceneController& controller, SceneLoadNavigationState& navigation,
                                       const SceneLoadBeginResult& prepared )
{
    // Invariant: preparation has validated the index and drained the device;
    // the caller has already opened the lifecycle generation and completed the
    // BeforeSceneUnload phase before committing these navigation mutations.
    if ( prepared.makeInteractive )
    {
        controller.State().isInteractiveRun = true;
    }

    if ( !prepared.shouldPreserveRuntimeState )
    {
        ClearUiOverrides( navigation.overrides );
    }

    controller.BeginLoad( prepared.index );

    if ( !prepared.shouldPreserveRuntimeState )
    {
        navigation.selectedCineModeSceneIndex = ( !prepared.scenePath->empty() && IsCineScenePath( *prepared.scenePath ) )
                                                    ? SceneBrowserIndexForPath( navigation.browserPaths,
                                                                                *prepared.scenePath )
                                                    : -1;
    }
}

} // namespace Runtime
} // namespace SkullbonezCore
