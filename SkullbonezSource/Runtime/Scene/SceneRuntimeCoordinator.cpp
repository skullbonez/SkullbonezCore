/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp
Purpose:
  Implements SceneController navigation and value-only load decisions.

Mental model:
  SceneController chooses which queued or browser scene should load and returns
  an accepted value request without retaining caller behavior.

Glossary:
  Browser scene: Scene path discovered from `SkullbonezData/scenes`.
  Load decision: Value-only request naming the chosen queue row and load flags.
  Cinematic deck: Queue range of cinematic/concept scene paths.

Invariants:
  - SceneController navigation methods return values; they do not retain load callbacks.
  - Browser-to-queue matching uses normalized path strings.
  - Cinematic deck navigation must match SceneRuntime's filename rules.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#include "SceneRuntimeCoordinator.h"
#include "SceneController.h"
#include "../../UI/UICommands.h"

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


SceneLoadRequest SceneController::LoadSceneFromBrowserIndex( int index )
{
    const std::vector<std::string>& sceneBrowserPaths = m_browser.paths;
    if ( index < 0 || index >= static_cast<int>( sceneBrowserPaths.size() ) )
    {
        return SceneLoadRequest::None();
    }

    const std::string selectedPath = NormalizeScenePath( sceneBrowserPaths[index] );
    const int queuedIndex = FindNormalizedPath( selectedPath );
    if ( queuedIndex >= 0 )
    {
        if ( queuedIndex != CurrentIndex() )
        {
            return SceneLoadRequest::Load( queuedIndex, true, true, false, true );
        }
        return SceneLoadRequest::AcceptedWithoutLoad( true );
    }

    return SceneLoadRequest::Load( Append( selectedPath ), true, true, false, true );
}


SceneLoadRequest SceneController::LoadDemoSceneFromUI()
{
    const int demoIndex = FindGeneratedDemo();
    if ( demoIndex >= 0 )
    {
        return SceneLoadRequest::Load( demoIndex, true, true, false, true );
    }

    return SceneLoadRequest::Load( Append( "" ), true, true, false, true );
}


int SceneController::AdjacentCinematicModeBrowserIndex( int direction,
                                                        int selectedCineModeSceneIndex,
                                                        int currentSceneBrowserIndex,
                                                        bool isCinematicTabActive ) const
{
    const std::vector<std::string>& sceneBrowserPaths = m_browser.paths;
    if ( direction == 0 )
    {
        return -1;
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
        return -1;
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
        return -1;
    }

    const int cineCount = static_cast<int>( cineIndices.size() );
    const int nextPosition = currentPosition < 0
                                 ? ( direction < 0 ? cineCount - 1 : 0 )
                                 : ( currentPosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;
    return cineIndices[nextPosition];
}


SceneLoadRequest SceneController::LoadAdjacentSceneFromBrowser( int direction, int currentSceneBrowserIndex )
{
    const std::vector<std::string>& sceneBrowserPaths = m_browser.paths;
    if ( direction == 0 )
    {
        return SceneLoadRequest::None();
    }

    if ( CurrentQueueIsCinematicDeck() )
    {
        return SceneLoadRequest::Load( AdjacentQueueIndex( direction ), true, true, false );
    }

    const int sceneCount = static_cast<int>( sceneBrowserPaths.size() );
    if ( sceneCount <= 0 )
    {
        return SceneLoadRequest::None();
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
            return LoadSceneFromBrowserIndex( cineIndices[nextCinePosition] );
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

    return LoadSceneFromBrowserIndex( nextIndex );
}


SceneLoadRequest
SceneController::ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
{
    if ( !HasCurrentEntry() )
    {
        return SceneLoadRequest::None();
    }

    SceneLoadRequest request =
        SceneLoadRequest::Load( CurrentIndex(), preserveUIState, suppressExitOnComplete, preserveRuntimeState, true );
    request.markManualReset = true;
    return request;
}


SceneLoadRequest SceneController::AdvanceScene( bool perfTestActive, bool preserveInteractiveUI )
{
    if ( perfTestActive && m_perfPass == 0 )
    {
        m_perfPass = 1;
        return SceneLoadRequest::Load( CurrentIndex(),
                                       preserveInteractiveUI,
                                       preserveInteractiveUI,
                                       preserveInteractiveUI );
    }

    m_perfPass = 0;

    const int nextIndex = NextIndex();
    if ( !HasEntry( nextIndex ) )
    {
        return SceneLoadRequest::None();
    }

    return SceneLoadRequest::Load( nextIndex, preserveInteractiveUI, preserveInteractiveUI, false );
}


int SceneController::PerfPass() const
{
    return m_perfPass;
}

SceneRuntimeUICommandResult SubmitSceneUIRequests( SceneController& sceneController,
                                                   const UI::UISceneCommands& commands )
{
    SceneRuntimeUICommandResult result;
    if ( commands.resetScene )
    {
        sceneController.SubmitResetCurrentScene();
        result.resetScene = true;
    }
    if ( commands.resetSceneDefaults )
    {
        sceneController.SubmitResetCurrentScene( false, true, false );
        result.resetSceneDefaults = true;
    }
    if ( commands.requestDemoScene )
    {
        sceneController.SubmitLoadDemoScene();
        result.loadDemoScene = true;
    }
    if ( commands.saveSceneDefaults )
    {
        sceneController.SubmitSaveCurrentDefaults();
        result.saveSceneDefaults = true;
    }
    if ( commands.createScene )
    {
        result.status = sceneController.SubmitCreateScene( commands.requestedSceneName );
        if ( !result.status.ok )
        {
            return result;
        }
        result.createScene = true;
    }
    if ( commands.requestedSceneIndex >= 0 )
    {
        sceneController.SubmitLoadBrowserIndex( commands.requestedSceneIndex );
        result.selectScene = true;
    }
    return result;
}


} // namespace Basics
} // namespace SkullbonezCore
