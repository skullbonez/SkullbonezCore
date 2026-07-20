/*
File: SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp
Purpose:
  Implements UI-owned browser, demo, and cinematic scene navigation policy.

Summary:
  SceneNavigationModel combines its browser selection with a synchronous borrow
  of the concrete SceneRuntime queue owner, then returns a value-only load
  request to the lifecycle boundary. It stores no scene owner pointer.

Glossary:
  Browser scene: Authored scene path discovered for the Scene UI.
  Cinematic deck: Queue or browser subset containing cinematic/concept scenes.
  Load request: Value naming the chosen queue row and load-preservation flags.

Invariants:
  - Browser paths are normalized before matching or appending to the scene queue.
  - Empty queue paths retain their meaning as the generated demo scene.
  - Returned requests retain no pointer, callback, or borrowed owner.
  - Cinematic filtering matches SceneRuntime's filename rules.

Related:
  - SkullbonezSource/Runtime/Scene/SceneControllerState.h
  - SkullbonezSource/Runtime/Scene/SceneRuntime.h
  - SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneControllerState.h"
#include "SceneRuntime.h"
#include "SceneRuntimeCoordinator.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"

#include <cstring>

namespace SkullbonezCore
{
namespace UI
{
namespace
{
bool IsCineScenePath( const std::string& path )
{
    const char* name = Runtime::SceneFileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}

Runtime::SceneLoadRequest
LoadSceneFromBrowserPaths( const std::vector<std::string>& paths, int index, Runtime::SceneRuntime& scene )
{
    if ( index < 0 || index >= static_cast<int>( paths.size() ) )
    {
        return Runtime::SceneLoadRequest::None();
    }

    const std::string selectedPath = Runtime::NormalizeSceneQueuePath( paths[index] );
    const int queuedIndex = scene.FindNormalizedPath( selectedPath );
    if ( queuedIndex >= 0 )
    {
        if ( queuedIndex != scene.CurrentIndex() )
        {
            return Runtime::SceneLoadRequest::Load( queuedIndex, true, true, false, true );
        }
        return Runtime::SceneLoadRequest::AcceptedWithoutLoad( true );
    }

    return Runtime::SceneLoadRequest::Load( scene.Append( selectedPath ), true, true, false, true );
}
} // namespace


Runtime::SceneLoadRequest SceneNavigationModel::LoadSceneFromBrowserIndex( int index, Runtime::SceneRuntime& scene )
{
    return LoadSceneFromBrowserPaths( browser.paths, index, scene );
}


Runtime::SceneLoadRequest SceneNavigationModel::LoadDemoScene( Runtime::SceneRuntime& scene )
{
    const int demoIndex = scene.FindGeneratedDemo();
    if ( demoIndex >= 0 )
    {
        return Runtime::SceneLoadRequest::Load( demoIndex, true, true, false, true );
    }

    return Runtime::SceneLoadRequest::Load( scene.Append( "" ), true, true, false, true );
}


int SceneNavigationModel::AdjacentCinematicModeBrowserIndex( int direction,
                                                             int currentSceneBrowserIndex,
                                                             bool isCinematicTabActive ) const
{
    if ( direction == 0 )
    {
        return -1;
    }

    // Why: keyboard navigation runs during steady gameplay. Two bounded passes
    // over the UI-owned path list avoid the old growable scratch vector while
    // preserving the same filtered position and wrap order.
    int currentPosition = -1;
    int currentScenePosition = -1;
    int cineCount = 0;
    for ( int i = 0; i < static_cast<int>( browser.paths.size() ); ++i )
    {
        if ( IsCineScenePath( browser.paths[i] ) )
        {
            if ( i == browser.selectedCineModeSceneIndex )
            {
                currentPosition = cineCount;
            }
            if ( i == currentSceneBrowserIndex )
            {
                currentScenePosition = cineCount;
            }
            ++cineCount;
        }
    }

    if ( cineCount == 0 )
    {
        return -1;
    }

    if ( currentPosition < 0 )
    {
        currentPosition = currentScenePosition;
    }

    const bool cineContext = currentPosition >= 0 || browser.selectedCineModeSceneIndex >= 0 || isCinematicTabActive;
    if ( !cineContext )
    {
        return -1;
    }

    const int nextPosition = currentPosition < 0
                                 ? ( direction < 0 ? cineCount - 1 : 0 )
                                 : ( currentPosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;
    int position = 0;
    for ( int i = 0; i < static_cast<int>( browser.paths.size() ); ++i )
    {
        if ( IsCineScenePath( browser.paths[i] ) )
        {
            if ( position == nextPosition )
            {
                return i;
            }
            ++position;
        }
    }
    return -1;
}


Runtime::SceneLoadRequest
SceneNavigationModel::LoadAdjacentScene( int direction, int currentSceneBrowserIndex, Runtime::SceneRuntime& scene )
{
    if ( direction == 0 )
    {
        return Runtime::SceneLoadRequest::None();
    }

    if ( scene.CurrentQueueIsCinematicDeck() )
    {
        return Runtime::SceneLoadRequest::Load( scene.AdjacentQueueIndex( direction ), true, true, false );
    }

    const int sceneCount = static_cast<int>( browser.paths.size() );
    if ( sceneCount <= 0 )
    {
        return Runtime::SceneLoadRequest::None();
    }

    if ( currentSceneBrowserIndex >= 0 && IsCineScenePath( browser.paths[currentSceneBrowserIndex] ) )
    {
        // The browser path list is stable for this synchronous decision, so a
        // count pass followed by a selection pass needs no runtime allocation.
        int currentCinePosition = -1;
        int cineCount = 0;
        for ( int i = 0; i < sceneCount; ++i )
        {
            if ( IsCineScenePath( browser.paths[i] ) )
            {
                if ( i == currentSceneBrowserIndex )
                {
                    currentCinePosition = cineCount;
                }
                ++cineCount;
            }
        }
        if ( cineCount > 0 && currentCinePosition >= 0 )
        {
            const int nextCinePosition = ( currentCinePosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;
            int position = 0;
            for ( int i = 0; i < sceneCount; ++i )
            {
                if ( IsCineScenePath( browser.paths[i] ) )
                {
                    if ( position == nextCinePosition )
                    {
                        return LoadSceneFromBrowserIndex( i, scene );
                    }
                    ++position;
                }
            }
        }
    }

    const int nextIndex = currentSceneBrowserIndex < 0
                              ? ( direction < 0 ? sceneCount - 1 : 0 )
                              : ( currentSceneBrowserIndex + ( direction < 0 ? -1 : 1 ) + sceneCount ) % sceneCount;
    return LoadSceneFromBrowserIndex( nextIndex, scene );
}
} // namespace UI

namespace Runtime
{
SceneLoadRequest SceneLoadNavigationState::LoadSceneFromBrowserIndex( int index, SceneRuntime& scene ) const
{
    return UI::LoadSceneFromBrowserPaths( browserPaths, index, scene );
}

SceneLoadRequest SceneLoadNavigationState::LoadDemoScene( SceneRuntime& scene ) const
{
    const int demoIndex = scene.FindGeneratedDemo();
    return demoIndex >= 0 ? SceneLoadRequest::Load( demoIndex, true, true, false, true )
                          : SceneLoadRequest::Load( scene.Append( "" ), true, true, false, true );
}

SceneLoadNavigationState CaptureSceneLoadNavigationState( const UI::SceneNavigationModel& navigation )
{
    Allocation::RuntimeAllocationScope allocationScope( Allocation::RuntimeAllocationPhase::SceneLoad );
    SceneLoadNavigationState state;
    state.browserPaths = navigation.browser.paths;
    state.overrides = navigation.overrides;
    state.selectedCineModeSceneIndex = navigation.browser.selectedCineModeSceneIndex;
    return state;
}

void ApplySceneLoadNavigationState( UI::SceneNavigationModel& navigation, const SceneLoadNavigationState& state )
{
    navigation.overrides = state.overrides;
    navigation.browser.selectedCineModeSceneIndex = state.selectedCineModeSceneIndex;
}
} // namespace Runtime
} // namespace SkullbonezCore
