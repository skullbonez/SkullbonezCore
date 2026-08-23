/*
File: RunUiStress.cpp
Purpose:
  Applies one Diagnostics-owned UI-stress command batch at the App boundary.

Summary:
  Diagnostics advances the deterministic policy and publishes bounded values.
  App is the only owner that translates those values into GameUI and renderer
  mutations, so Capture and Diagnostics never recover Run or live UI owners.

Invariants:
  - Command order is applied exactly as published for the current frame.
  - UI visibility and pointer override precede every individual control command.
  - Renderer VSync policy and the native device are updated together.

Related:
  - Runtime/Diagnostics/UIStressPolicy.h
  - Runtime/App/InputFrameExecution.cpp
*/
#include "Run.h"

#include "../Diagnostics/UIStressPolicy.h"
#include "../Render/RuntimeRenderer.h"
#include "../Startup/Window.h"
#include "../../Core/SbResult.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../../UI/UI.h"

namespace SkullbonezCore::Runtime
{
SkullbonezCore::Core::SbResult Run::RunUIStressActions()
{
    UIStressPolicyOwner& policy = m_diagnosticsRuntime.UIStress();
    const RuntimeFrameMetricsSnapshot metrics = m_timers.Publish();
    UI::InGameUI& ui = *m_operatorUi;
    RuntimeRenderer& renderer = Renderer();
    const UIStressFramePlan plan = policy.PlanFrame( m_window.ClientWidth(), m_window.ClientHeight(),
                                                     static_cast<int>( UI::InGameUITab::Count ) );

    if ( !plan.active )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    ui.SetVisible( true, metrics.simulationTotalSeconds );
    ui.SetMinimized( false, metrics.simulationTotalSeconds );
    ui.SetMouseOverride( true, plan.mouseX, plan.mouseY );

    for ( std::size_t index = 0; index < plan.commandCount; ++index )
    {
        const UIStressCommand& command = plan.commands[index];

        switch ( command.kind )
        {
        case UIStressCommandKind::SetActiveTab:
            ui.SetActiveTab( static_cast<UI::InGameUITab>( command.intValue ) );
            break;
        case UIStressCommandKind::SetScrollY:
            ui.SetScrollY( command.floatValue );
            break;
        case UIStressCommandKind::SetProfilerTimeline:
            ui.SetProfilerTimelineEnabled( command.boolValue );
            break;
        case UIStressCommandKind::SetPerformanceHistogram:
            ui.SetPerformanceHistogramEnabled( command.boolValue );
            break;
        case UIStressCommandKind::SetRendererComboOpen:
            ui.SetRendererComboOpen( command.boolValue );
            break;
        case UIStressCommandKind::SetWaterComboOpen:
            ui.SetWaterComboOpen( command.boolValue );
            break;
        case UIStressCommandKind::SetSceneComboOpen:
            ui.SetSceneComboOpen( command.boolValue );
            break;
        case UIStressCommandKind::ToggleVsync:
            renderer.SetVsyncEnabled( !renderer.VsyncEnabled() );
            renderer.RenderDevice().SetVsyncEnabled( renderer.VsyncEnabled() );
            break;
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}
} // namespace SkullbonezCore::Runtime
