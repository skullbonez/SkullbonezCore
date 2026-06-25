/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp
Purpose:
  Implements scene lifecycle selection decisions.

Mental model:
  This file chooses which queued or browser scene should load. It deliberately
  leaves object population, world setup, replay reset, and renderer rebuild side
  effects behind callbacks while Phase 3 continues moving those responsibilities
  out of Run.

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


SceneRuntimeCoordinator::SceneRuntimeCoordinator( SceneController& sceneController,
                                                  SceneRuntimeCoordinatorCallbacks callbacks )
    : m_sceneController( sceneController ), m_callbacks( callbacks )
{
}


void SceneRuntimeCoordinator::EnterInteractiveSceneRun() const
{
    m_callbacks.enterInteractiveSceneRun( m_callbacks.user );
}


void SceneRuntimeCoordinator::ClearCurrentSceneAutomation() const
{
    m_callbacks.clearCurrentSceneAutomation( m_callbacks.user );
}


void SceneRuntimeCoordinator::LoadScene( int index,
                                         bool preserveUIState,
                                         bool suppressExitOnComplete,
                                         bool preserveRuntimeState ) const
{
    m_callbacks.loadScene( m_callbacks.user, index, preserveUIState, suppressExitOnComplete, preserveRuntimeState );
}


int SceneRuntimeCoordinator::CurrentSceneBrowserIndex() const
{
    return m_callbacks.currentSceneBrowserIndex( m_callbacks.user );
}


bool SceneRuntimeCoordinator::IsCinematicTabActive() const
{
    return m_callbacks.isCinematicTabActive( m_callbacks.user );
}


bool SceneRuntimeCoordinator::ApplyCinematicModeFromBrowserIndex( int index ) const
{
    return m_callbacks.applyCinematicModeFromBrowserIndex( m_callbacks.user, index );
}


void SceneRuntimeCoordinator::LoadSceneFromBrowserIndex( int index, const std::vector<std::string>& sceneBrowserPaths )
{
    if ( index < 0 || index >= static_cast<int>( sceneBrowserPaths.size() ) )
    {
        return;
    }

    EnterInteractiveSceneRun();

    const std::string selectedPath = NormalizeScenePath( sceneBrowserPaths[index] );
    const int queuedIndex = m_sceneController.FindNormalizedPath( selectedPath );
    if ( queuedIndex >= 0 )
    {
        if ( queuedIndex != m_sceneController.CurrentIndex() )
        {
            LoadScene( queuedIndex, true, true, false );
        }
        else
        {
            ClearCurrentSceneAutomation();
        }
        return;
    }

    LoadScene( m_sceneController.Append( selectedPath ), true, true, false );
}


void SceneRuntimeCoordinator::LoadDemoSceneFromUI()
{
    EnterInteractiveSceneRun();
    const int demoIndex = m_sceneController.FindGeneratedDemo();
    if ( demoIndex >= 0 )
    {
        LoadScene( demoIndex, true, true, false );
        return;
    }

    LoadScene( m_sceneController.Append( "" ), true, true, false );
}


bool SceneRuntimeCoordinator::ApplyAdjacentCinematicMode( int direction,
                                                          const std::vector<std::string>& sceneBrowserPaths,
                                                          int selectedCineModeSceneIndex )
{
    if ( direction == 0 )
    {
        return false;
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
        return false;
    }

    const int currentSceneIndex = CurrentSceneBrowserIndex();
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

    const bool cineContext = currentPosition >= 0 || selectedCineModeSceneIndex >= 0 || IsCinematicTabActive();
    if ( !cineContext )
    {
        return false;
    }

    const int cineCount = static_cast<int>( cineIndices.size() );
    const int nextPosition = currentPosition < 0
                                 ? ( direction < 0 ? cineCount - 1 : 0 )
                                 : ( currentPosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;
    return ApplyCinematicModeFromBrowserIndex( cineIndices[nextPosition] );
}


void SceneRuntimeCoordinator::LoadAdjacentSceneFromBrowser( int direction,
                                                            const std::vector<std::string>& sceneBrowserPaths )
{
    if ( direction == 0 )
    {
        return;
    }

    if ( m_sceneController.CurrentQueueIsCinematicDeck() )
    {
        LoadScene( m_sceneController.AdjacentQueueIndex( direction ), true, true, false );
        return;
    }

    const int sceneCount = static_cast<int>( sceneBrowserPaths.size() );
    if ( sceneCount <= 0 )
    {
        return;
    }

    const int currentIndex = CurrentSceneBrowserIndex();
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
            LoadSceneFromBrowserIndex( cineIndices[nextCinePosition], sceneBrowserPaths );
            return;
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

    LoadSceneFromBrowserIndex( nextIndex, sceneBrowserPaths );
}


void SceneRuntimeCoordinator::ResetCurrentScene( bool preserveUIState,
                                                 bool suppressExitOnComplete,
                                                 bool preserveRuntimeState )
{
    if ( !m_sceneController.HasCurrentEntry() )
    {
        return;
    }

    m_sceneController.MarkManualReset();
    LoadScene( m_sceneController.CurrentIndex(), preserveUIState, suppressExitOnComplete, preserveRuntimeState );
}


bool SceneRuntimeCoordinator::AdvanceScene( bool perfTestActive, int& perfPass, bool preserveInteractiveUI )
{
    if ( perfTestActive && perfPass == 0 )
    {
        perfPass = 1;
        LoadScene( m_sceneController.CurrentIndex(),
                   preserveInteractiveUI,
                   preserveInteractiveUI,
                   preserveInteractiveUI );
        return true;
    }

    perfPass = 0;

    const int nextIndex = m_sceneController.NextIndex();
    if ( !m_sceneController.HasEntry( nextIndex ) )
    {
        return false;
    }

    LoadScene( nextIndex, preserveInteractiveUI, preserveInteractiveUI, false );
    return true;
}

} // namespace Basics
} // namespace SkullbonezCore
