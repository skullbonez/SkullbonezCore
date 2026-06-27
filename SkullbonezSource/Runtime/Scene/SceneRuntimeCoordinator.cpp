/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp
Purpose:
  Implements scene lifecycle selection decisions.

Mental model:
  This file chooses which queued or browser scene should load. It deliberately
  returns scene control intents while Phase 3 continues moving object
  population, world setup, replay reset, and renderer rebuild responsibilities
  out of Run.

Glossary:
  Browser scene: Scene path discovered from `SkullbonezData/scenes`.
  Control intent: Small return object that tells Run which scene side effect to
    perform next.
  Cinematic deck: Queue range of cinematic/concept scene paths.

Invariants:
  - Coordinator methods return intents; they do not load scenes directly.
  - Browser-to-queue matching uses normalized path strings.
  - Cinematic deck navigation must match SceneRuntime's filename rules.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#include "SceneRuntimeCoordinator.h"
#include "SceneController.h"

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

std::string NormalizeScenePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    return normalized;
}

bool IsCineScenePath( const std::string& path )
{
    const char* name = FileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}
} // namespace


SceneRuntimeCoordinator::SceneRuntimeCoordinator( SceneController& sceneController )
    : m_sceneController( sceneController )
{
}


SceneRuntimeControlAction
SceneRuntimeCoordinator::LoadSceneFromBrowserIndex( int index, const std::vector<std::string>& sceneBrowserPaths )
{
    if ( index < 0 || index >= static_cast<int>( sceneBrowserPaths.size() ) )
    {
        return SceneRuntimeControlAction::None();
    }

    const std::string selectedPath = NormalizeScenePath( sceneBrowserPaths[index] );
    const int queuedIndex = m_sceneController.FindNormalizedPath( selectedPath );
    if ( queuedIndex >= 0 )
    {
        if ( queuedIndex != m_sceneController.CurrentIndex() )
        {
            return SceneRuntimeControlAction::LoadScene( queuedIndex, true, true, false, true );
        }
        return SceneRuntimeControlAction::ClearCurrentSceneAutomation( true );
    }

    return SceneRuntimeControlAction::LoadScene( m_sceneController.Append( selectedPath ), true, true, false, true );
}


SceneRuntimeControlAction SceneRuntimeCoordinator::LoadDemoSceneFromUI()
{
    const int demoIndex = m_sceneController.FindGeneratedDemo();
    if ( demoIndex >= 0 )
    {
        return SceneRuntimeControlAction::LoadScene( demoIndex, true, true, false, true );
    }

    return SceneRuntimeControlAction::LoadScene( m_sceneController.Append( "" ), true, true, false, true );
}


SceneRuntimeControlAction
SceneRuntimeCoordinator::ApplyAdjacentCinematicMode( int direction,
                                                     const std::vector<std::string>& sceneBrowserPaths,
                                                     int selectedCineModeSceneIndex,
                                                     int currentSceneBrowserIndex,
                                                     bool isCinematicTabActive )
{
    if ( direction == 0 )
    {
        return SceneRuntimeControlAction::None();
    }

    std::vector<int> cineIndices;
    cineIndices.reserve( sceneBrowserPaths.size() );
    int currentPosition = -1;
    for ( int i = 0; i < static_cast<int>( sceneBrowserPaths.size() ); ++i )
    {
        if ( IsCineScenePath( sceneBrowserPaths[i] ) )
        {
            if ( i == selectedCineModeSceneIndex )
            {
                currentPosition = static_cast<int>( cineIndices.size() );
            }
            cineIndices.push_back( i );
        }
    }

    if ( cineIndices.empty() )
    {
        return SceneRuntimeControlAction::None();
    }

    const int currentSceneIndex = currentSceneBrowserIndex;
    if ( currentPosition < 0 && currentSceneIndex >= 0 && IsCineScenePath( sceneBrowserPaths[currentSceneIndex] ) )
    {
        for ( int i = 0; i < static_cast<int>( cineIndices.size() ); ++i )
        {
            if ( cineIndices[i] == currentSceneIndex )
            {
                currentPosition = i;
                break;
            }
        }
    }

    const bool cineContext = currentPosition >= 0 || selectedCineModeSceneIndex >= 0 || isCinematicTabActive;
    if ( !cineContext )
    {
        return SceneRuntimeControlAction::None();
    }

    const int cineCount = static_cast<int>( cineIndices.size() );
    const int nextPosition = currentPosition < 0
                                 ? ( direction < 0 ? cineCount - 1 : 0 )
                                 : ( currentPosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;
    return SceneRuntimeControlAction::ApplyCinematicModeFromBrowserIndex( cineIndices[nextPosition] );
}


SceneRuntimeControlAction
SceneRuntimeCoordinator::LoadAdjacentSceneFromBrowser( int direction,
                                                       const std::vector<std::string>& sceneBrowserPaths,
                                                       int currentSceneBrowserIndex )
{
    if ( direction == 0 )
    {
        return SceneRuntimeControlAction::None();
    }

    if ( m_sceneController.CurrentQueueIsCinematicDeck() )
    {
        return SceneRuntimeControlAction::LoadScene( m_sceneController.AdjacentQueueIndex( direction ),
                                                     true,
                                                     true,
                                                     false );
    }

    const int sceneCount = static_cast<int>( sceneBrowserPaths.size() );
    if ( sceneCount <= 0 )
    {
        return SceneRuntimeControlAction::None();
    }

    const int currentIndex = currentSceneBrowserIndex;
    if ( currentIndex >= 0 && IsCineScenePath( sceneBrowserPaths[currentIndex] ) )
    {
        std::vector<int> cineIndices;
        cineIndices.reserve( sceneBrowserPaths.size() );
        int currentCinePosition = -1;
        for ( int i = 0; i < sceneCount; ++i )
        {
            if ( IsCineScenePath( sceneBrowserPaths[i] ) )
            {
                if ( i == currentIndex )
                {
                    currentCinePosition = static_cast<int>( cineIndices.size() );
                }
                cineIndices.push_back( i );
            }
        }
        if ( !cineIndices.empty() && currentCinePosition >= 0 )
        {
            const int cineCount = static_cast<int>( cineIndices.size() );
            const int nextCinePosition = ( currentCinePosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;
            return LoadSceneFromBrowserIndex( cineIndices[nextCinePosition], sceneBrowserPaths );
        }
    }

    int nextIndex = 0;
    if ( currentIndex < 0 )
    {
        nextIndex = direction < 0 ? sceneCount - 1 : 0;
    }
    else
    {
        nextIndex = ( currentIndex + ( direction < 0 ? -1 : 1 ) + sceneCount ) % sceneCount;
    }

    return LoadSceneFromBrowserIndex( nextIndex, sceneBrowserPaths );
}


SceneRuntimeControlAction SceneRuntimeCoordinator::ResetCurrentScene( bool preserveUIState,
                                                                      bool suppressExitOnComplete,
                                                                      bool preserveRuntimeState )
{
    if ( !m_sceneController.HasCurrentEntry() )
    {
        return SceneRuntimeControlAction::None();
    }

    m_sceneController.MarkManualReset();
    return SceneRuntimeControlAction::LoadScene( m_sceneController.CurrentIndex(),
                                                 preserveUIState,
                                                 suppressExitOnComplete,
                                                 preserveRuntimeState );
}


SceneRuntimeControlAction
SceneRuntimeCoordinator::AdvanceScene( bool perfTestActive, int& perfPass, bool preserveInteractiveUI )
{
    if ( perfTestActive && perfPass == 0 )
    {
        perfPass = 1;
        return SceneRuntimeControlAction::LoadScene( m_sceneController.CurrentIndex(),
                                                     preserveInteractiveUI,
                                                     preserveInteractiveUI,
                                                     preserveInteractiveUI );
    }

    perfPass = 0;

    const int nextIndex = m_sceneController.NextIndex();
    if ( !m_sceneController.HasEntry( nextIndex ) )
    {
        return SceneRuntimeControlAction::None();
    }

    return SceneRuntimeControlAction::LoadScene( nextIndex, preserveInteractiveUI, preserveInteractiveUI, false );
}

} // namespace Basics
} // namespace SkullbonezCore
