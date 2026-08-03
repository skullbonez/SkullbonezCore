/*
File: SkullbonezSource/Runtime/Scene/SceneController.Navigation.cpp
Purpose:
  Implements SceneController lifecycle requests and UI command submission.

Summary:
  SceneController owns reset/advance lifecycle policy and accepts owner-specific
  UI commands. Browser and cinematic selection policy lives on the UI-owned
  SceneNavigationModel and reaches this boundary only as a value request.

Glossary:
  Load decision: Value-only request naming the chosen queue row and load flags.
  UI command: One-frame operator intent submitted to the fixed scene request
    ring for execution at the frame checkpoint.

Invariants:
  - Reset and advance requests contain values only; no callback or UI owner is
    retained by SceneController.
  - UI command submission preserves same-frame request order.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp
*/
#include "SceneController.h"
#include "../../UI/UICommands.h"

namespace SkullbonezCore
{
namespace Runtime
{
SceneLoadRequest SceneController::ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete,
                                                     bool preserveRuntimeState )
{

    if ( !HasCurrentEntry() )
    {
        return SceneLoadRequest::None();
    }

    SceneLoadRequest request = SceneLoadRequest::Load( CurrentIndex(), preserveUIState, suppressExitOnComplete,
                                                       preserveRuntimeState, true );

    request.markManualReset = true;
    return request;
}


SceneLoadRequest SceneController::AdvanceScene( bool perfTestActive, bool preserveInteractiveUI )
{

    if ( perfTestActive && m_perfPass == 0 )
    {
        m_perfPass = 1;
        return SceneLoadRequest::Load( CurrentIndex(), preserveInteractiveUI, preserveInteractiveUI, preserveInteractiveUI );
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

SceneUICommandSubmissionResult SceneController::SubmitUIRequests( const UI::UISceneCommands& commands )
{
    SceneUICommandSubmissionResult result;

    if ( commands.resetScene )
    {
        SubmitResetCurrentScene();
        result.resetScene = true;
    }

    if ( commands.resetSceneDefaults )
    {
        SubmitResetCurrentScene( false, true, false );
        result.resetSceneDefaults = true;
    }

    if ( commands.requestDemoScene )
    {
        SubmitLoadDemoScene();
        result.loadDemoScene = true;
    }

    if ( commands.saveSceneDefaults )
    {
        SubmitSaveCurrentDefaults();
        result.saveSceneDefaults = true;
    }

    if ( commands.createScene )
    {
        result.status = SubmitCreateScene( commands.requestedSceneName );

        if ( !result.status.Ok() )
        {
            return result;
        }

        result.createScene = true;
    }

    if ( commands.requestedSceneIndex >= 0 )
    {
        SubmitLoadBrowserIndex( commands.requestedSceneIndex );
        result.selectScene = true;
    }

    return result;
}


} // namespace Runtime
} // namespace SkullbonezCore
