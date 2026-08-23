/*
File: SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Presentation.cpp
Purpose:
  Applies authored scene UI options through a scene-runtime boundary.

Summary:
  Scene load chooses whether automation scenes hide UI, whether authored window
  state wins, and which deterministic UI stress values should seed diagnostics.
  Keep those decisions here so Run only supplies owners and timing.

Invariants:
  - Authored visible/minimized/window settings apply in the same order as the
    historic RunScene code.
  - UI stress fields are clamped and applied independently of visible UI
    preservation.

Related:
  - SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneLoadTransaction.h"
#include "SceneLoadPresentation.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../../Scene/AuthoredScene.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Runtime
{
void SceneLoadTransaction::PrepareUiOptions( DiagnosticsRuntime& diagnostics, OverlayDebugState& debug,
                                             SceneUiActivation& activation, const SceneUIOptions& options, double nowSeconds,
                                             bool preserveUIState, bool automationScene )
{
    activation.authoredOptions = options;
    activation.nowSeconds = nowSeconds;
    activation.hasAuthoredOptions = true;
    activation.preserveUIState = preserveUIState;
    activation.automationScene = automationScene;

    // Why: diagnostics and debug values already have genuine load-phase owners;
    // only window presentation crosses the returned activation value.
    if ( !preserveUIState && options.hasTestPattern )
    {
        debug.isUITestPattern = options.testPatternEnabled;
    }

    if ( options.hasStress )
    {
        diagnostics.UIStress().SetEnabled( options.stressEnabled );
    }

    if ( options.hasStressSeed )
    {
        diagnostics.UIStress().SetRandomState( options.stressSeed );
    }

    if ( options.hasStressActions )
    {
        diagnostics.UIStress().SetActionsPerFrame( options.stressActionsPerFrame );
    }
}


} // namespace Runtime
} // namespace SkullbonezCore
