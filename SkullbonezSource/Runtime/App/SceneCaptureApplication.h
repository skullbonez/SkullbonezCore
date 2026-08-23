/*
File: SkullbonezSource/Runtime/App/SceneCaptureApplication.h
Purpose:
  Applies detached scene-load capture reactions to the Capture owner.

Summary:
  App preserves Scene's published reaction order while synchronously invoking
  the sibling CaptureController; neither owner retains the other's authority.

Invariants:
  - Reactions are applied in publication order before later scene follow-ups.
  - The fixed reaction batch is borrowed only for this synchronous call.

Related:
  - SkullbonezSource/Runtime/App/SceneLoadApplication.h
  - SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
  - SkullbonezSource/Runtime/Capture/CaptureController.h
*/
#pragma once

#include "../Capture/CaptureController.h"
#include "../Scene/SceneLoadTransaction.h"

namespace SkullbonezCore
{
namespace Runtime
{
inline void ApplySceneCaptureReactions( CaptureController& capture, const SceneCaptureReactionBatch& reactions )
{
    for ( std::size_t index = 0; index < reactions.count; ++index )
    {
        const SceneCaptureReaction& reaction = reactions.reactions[index];
        switch ( reaction.kind )
        {
        case SceneCaptureReactionKind::DisableAutomationExit:
            capture.DisableAutomationExit();
            break;
        case SceneCaptureReactionKind::ResetScreenshot:
            capture.ResetScreenshot();
            break;
        case SceneCaptureReactionKind::ApplyAutomation:
            capture.ApplySceneAutomation( reaction.automation.screenshotFrame, reaction.automation.screenshotMs,
                                          reaction.automation.screenshotAndExit, reaction.automation.screenshotPath,
                                          reaction.automation.screenshotInterval,
                                          reaction.automation.screenshotDirectory );
            break;
        }
    }
}
} // namespace Runtime
} // namespace SkullbonezCore
