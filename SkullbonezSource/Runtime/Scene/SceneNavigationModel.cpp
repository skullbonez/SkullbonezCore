/*
File: SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp
Purpose:
  Implements Runtime queue policy over the UI-owned scene navigation model.

Summary:
  Runtime combines borrowed UI browser values with the concrete SceneController
  queue owner, then returns a value-only request to the lifecycle boundary.
  UI remains passive and stores no Runtime owner or policy method.

Glossary:
  Browser scene: Authored scene path discovered for the Scene UI.

Invariants:
  - Browser paths are normalized before matching or appending to the scene queue.
  - Empty queue paths retain their meaning as the generated demo scene.
  - Returned requests retain no pointer, callback, or borrowed owner.
  - Cinematic filtering matches SceneController's filename rules.

Related:
  - SkullbonezSource/UI/UISceneNavigationModel.h
  - SkullbonezSource/Runtime/Scene/SceneControllerState.h
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneLoadRequest.h
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneControllerState.h"
#include "SceneSessionState.h"
#include "SceneLoadRequest.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"

#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
bool IsCineScenePath( const std::string& path )
{
    const char* name = SceneFileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}

SceneLoadRequest LoadSceneFromBrowserPaths( const std::vector<std::string>& paths, int index, SceneSession& scene )
{
    if ( index < 0 || index >= static_cast<int>( paths.size() ) )
    {
        return SceneLoadRequest::None();
    }

    const std::string selectedPath = NormalizeSceneQueuePath( paths[index] );
    const int queuedIndex = scene.FindNormalizedPath( selectedPath );

    if ( queuedIndex >= 0 )
    {
        if ( queuedIndex != scene.CurrentIndex() )
        {
            return SceneLoadRequest::Load( queuedIndex, true, true, false, true );
        }

        return SceneLoadRequest::AcceptedWithoutLoad( true );
    }

    return SceneLoadRequest::Load( scene.Append( selectedPath ), true, true, false, true );
}
} // namespace


SceneLoadRequest LoadSceneFromBrowserIndex( const SkullbonezCore::UI::SceneNavigationModel& navigation, int index,
                                            SceneSession& scene )
{
    return LoadSceneFromBrowserPaths( navigation.browser.paths, index, scene );
}


int AdjacentCinematicModeBrowserIndex( const SkullbonezCore::UI::SceneNavigationModel& navigation, int direction,
                                       int currentSceneBrowserIndex, bool isCinematicTabActive )
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

    for ( int i = 0; i < static_cast<int>( navigation.browser.paths.size() ); ++i )
    {
        if ( IsCineScenePath( navigation.browser.paths[i] ) )
        {
            if ( i == navigation.browser.selectedCineModeSceneIndex )
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

    const bool cineContext = currentPosition >= 0 || navigation.browser.selectedCineModeSceneIndex >= 0 ||
                             isCinematicTabActive;

    if ( !cineContext )
    {
        return -1;
    }

    const int nextPosition = currentPosition < 0 ? ( direction < 0 ? cineCount - 1 : 0 )
                                                 : ( currentPosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;

    int position = 0;

    for ( int i = 0; i < static_cast<int>( navigation.browser.paths.size() ); ++i )
    {
        if ( IsCineScenePath( navigation.browser.paths[i] ) )
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


SceneLoadRequest LoadAdjacentScene( const SkullbonezCore::UI::SceneNavigationModel& navigation, int direction,
                                    int currentSceneBrowserIndex, SceneSession& scene )
{
    if ( direction == 0 )
    {
        return SceneLoadRequest::None();
    }

    if ( scene.CurrentQueueIsCinematicDeck() )
    {
        return SceneLoadRequest::Load( scene.AdjacentQueueIndex( direction ), true, true, false );
    }

    const int sceneCount = static_cast<int>( navigation.browser.paths.size() );

    if ( sceneCount <= 0 )
    {
        return SceneLoadRequest::None();
    }

    if ( currentSceneBrowserIndex >= 0 && IsCineScenePath( navigation.browser.paths[currentSceneBrowserIndex] ) )
    {

        // The browser path list is stable for this synchronous decision, so a
        // count pass followed by a selection pass needs no runtime allocation.
        int currentCinePosition = -1;
        int cineCount = 0;

        for ( int i = 0; i < sceneCount; ++i )
        {
            if ( IsCineScenePath( navigation.browser.paths[i] ) )
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
                if ( IsCineScenePath( navigation.browser.paths[i] ) )
                {
                    if ( position == nextCinePosition )
                    {
                        return LoadSceneFromBrowserIndex( navigation, i, scene );
                    }

                    ++position;
                }
            }
        }
    }

    const int nextIndex = currentSceneBrowserIndex < 0
                              ? ( direction < 0 ? sceneCount - 1 : 0 )
                              : ( currentSceneBrowserIndex + ( direction < 0 ? -1 : 1 ) + sceneCount ) % sceneCount;

    return LoadSceneFromBrowserIndex( navigation, nextIndex, scene );
}

SceneLoadRequest SceneLoadNavigationState::LoadSceneFromBrowserIndex( int index, SceneSession& scene ) const
{
    return LoadSceneFromBrowserPaths( browserPaths, index, scene );
}

SceneLoadRequest SceneLoadNavigationState::LoadDemoScene( SceneSession& scene ) const
{
    const int demoIndex = scene.FindGeneratedDemo();
    return demoIndex >= 0 ? SceneLoadRequest::Load( demoIndex, true, true, false, true )
                          : SceneLoadRequest::Load( scene.Append( "" ), true, true, false, true );
}

SceneLoadNavigationState CaptureSceneLoadNavigationState( const SkullbonezCore::UI::SceneNavigationModel& navigation )
{
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    SceneLoadNavigationState state;
    state.browserPaths = navigation.browser.paths;
    state.overrides = navigation.overrides;
    state.selectedCineModeSceneIndex = navigation.browser.selectedCineModeSceneIndex;
    return state;
}

void ApplySceneLoadNavigationState( SkullbonezCore::UI::SceneNavigationModel& navigation,
                                    const SceneLoadNavigationState& state )
{
    navigation.overrides = state.overrides;
    navigation.browser.selectedCineModeSceneIndex = state.selectedCineModeSceneIndex;
}
} // namespace Runtime
} // namespace SkullbonezCore
