/*
File: SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Presentation.cpp
Purpose:
  Applies authored scene UI options through a scene-runtime boundary.

Summary:
  Scene load chooses whether automation scenes hide UI, whether authored window
  state wins, and publishes deterministic UI stress reactions for App to apply.

Invariants:
  - Authored visible/minimized/window settings apply in the same order as the
    historic RunScene code.
  - UI stress fields are clamped and applied independently of visible UI
    preservation.

Related:
  - SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h
  - SkullbonezSource/Runtime/App/SceneLoadApplication.cpp
  - SkullbonezSource/Runtime/UI/GameUI/UI.h
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneLoadTransaction.h"
#include "SceneLoadPresentation.h"
#include "../../Scene/AuthoredScene.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Runtime
{
void SceneLoadTransaction::AppendCaptureReaction( const SceneCaptureReaction& reaction )
{
    if ( m_outputs.captureReactions.count >= m_outputs.captureReactions.reactions.size() )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Fixed capture reaction capacity exhausted." );
    }
    m_outputs.captureReactions.reactions[m_outputs.captureReactions.count++] = reaction;
}


void SceneLoadTransaction::AppendDiagnosticsReaction( const SceneDiagnosticsReaction& reaction )
{
    if ( m_outputs.diagnosticsReactions.count >= m_outputs.diagnosticsReactions.reactions.size() )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Fixed diagnostics reaction capacity exhausted." );
    }

    m_outputs.diagnosticsReactions.reactions[m_outputs.diagnosticsReactions.count++] = reaction;
}


void SceneLoadTransaction::PrepareUiOptions( ScenePresentationValues& presentation,
                                             SceneUiActivation& activation, const SceneUIOptions& options, double nowSeconds,
                                             bool preserveUIState, bool automationScene )
{
    activation.authoredOptions = options;
    activation.nowSeconds = nowSeconds;
    activation.hasAuthoredOptions = true;
    activation.preserveUIState = preserveUIState;
    activation.automationScene = automationScene;

    const SceneUiOptionDiagnosticsProjection projection = ProjectSceneUiOptionDiagnostics( options, preserveUIState );
    if ( projection.applyTestPattern )
    {
        presentation.uiTestPattern = projection.testPatternEnabled;
    }

    for ( std::size_t index = 0; index < projection.reactions.count; ++index )
    {
        AppendDiagnosticsReaction( projection.reactions.reactions[index] );
    }
}


} // namespace Runtime
} // namespace SkullbonezCore
