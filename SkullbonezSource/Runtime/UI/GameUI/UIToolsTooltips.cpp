// Tools hover help uses the same retained widget rectangles as drawing and input.
#include "UI.h"
#include "UIRenderTooltipText.h"

#include <array>
#include <span>

namespace SkullbonezCore::UI
{
namespace
{
UITooltipTarget HitTooltip( std::span<const UITooltipTarget> candidates, int mouseX, int mouseY )
{
    for ( const auto& candidate : candidates )
    {
        if ( candidate.bounds.w > 0.0f && candidate.bounds.h > 0.0f && candidate.bounds.Contains( mouseX, mouseY ) )
        {
            UITooltipTarget target = candidate;
            target.hovered = true;
            return target;
        }
    }
    return {};
}

UITooltipTarget PhysicsTooltip( const PhysicsTab::UIPhysicsTabState& state, int mouseX, int mouseY )
{
    const UITooltipTarget candidates[] = { { 2000, state.toggles[0].Bounds(), { "Show collision geometry and physics diagnostics." } },
                                           { 2001, state.toggles[1].Bounds(), { "Show local body axes in the physics overlay." } },
                                           { 2002, state.toggles[2].Bounds(), { "Show contact points and normals." } },
                                           { 2003, state.toggles[3].Bounds(), { "Colour bodies by their sleeping state." } },
                                           { 2004, state.toggles[4].Bounds(), { "Draw physics geometry transparently." } },
                                           { 2005, state.toggles[5].Bounds(), { "Show broadphase collision bounds." } },
                                           { 2006, state.toggles[6].Bounds(), { "Allow settled physics bodies to sleep." } },
                                           { 2007, state.toggles[7].Bounds(), { "Show the selected physics pipeline stage." } },
                                           { 2008, state.toggles[8].Bounds(), { "Show the terrain contact probe diagnostics." } },
                                           { 2009, state.toggles[9].Bounds(), { "Enable or disable the tornado force field." } },
                                           { 2010, state.toggles[10].Bounds(), { "Show the tornado field's boundary shell." } },
                                           { 2011, state.toggles[11].Bounds(), { "Show the tornado field's acceleration vectors." } },
                                           { 2012, state.toggles[12].Bounds(), { "Show ray-cast hit diagnostics." } },
                                           { 2013, state.pipelinePrevButton, { "Inspect the previous physics pipeline stage." } },
                                           { 2014, state.pipelineNextButton, { "Inspect the next physics pipeline stage." } },
                                           { 2015, state.alphaSlider.Bounds(), { "Set the opacity of physics debug bodies.", "0 transparent to 1 opaque" } },
                                           { 2016, state.contactLingerSlider.Bounds(), { "Keep contact diagnostics visible for this duration.", "Seconds" } },
                                           { 2017, state.rayImpulseSlider.Bounds(), { "Set the impulse applied by the ray test.", "Impulse strength" } },
                                           { 2018, state.launcherProjectileSpeedSlider.Bounds(), { "Set the launcher's initial projectile speed.", "Metres per second" } },
                                           { 2019, state.worldGravitySlider.Bounds(), { "Set downward world gravity strength.", "Metres per second squared" } },
                                           { 2020, state.terrainFrictionSlider.Bounds(), { "Set friction for terrain contacts.", "Friction coefficient" } },
                                           { 2021, state.objectFrictionSlider.Bounds(), { "Set friction for object contacts.", "Friction coefficient" } },
                                           { 2022, state.rollingFrictionSlider.Bounds(), { "Set rolling resistance for contacts.", "Rolling friction coefficient" } },
                                           { 2023, state.tornadoRadiusSlider.Bounds(), { "Set the radius of the tornado field.", "Metres" } },
                                           { 2024, state.tornadoHeightSlider.Bounds(), { "Set the height of the tornado field.", "Metres" } },
                                           { 2025, state.tornadoInwardSlider.Bounds(), { "Set acceleration toward the tornado's centre.", "Metres per second squared" } },
                                           { 2026, state.tornadoSwirlSlider.Bounds(), { "Set acceleration around the tornado's centre.", "Metres per second squared" } },
                                           { 2027, state.tornadoLiftSlider.Bounds(), { "Set upward acceleration inside the tornado.", "Metres per second squared" } }, };
    return HitTooltip( candidates, mouseX, mouseY );
}

UITooltipTarget OptionsTooltip( const OptionsTab::UIOptionsTabState& state, int mouseX, int mouseY )
{
    const UITooltipTarget candidates[] = { { 2100, state.toggles[0].Bounds(), { "Use capture lockstep for fixed simulation advancement." } },
                                           { 2101, state.toggles[1].Bounds(), { "Show or hide rendered terrain." } },
                                           { 2102, state.toggles[2].Bounds(), { "Show or hide rendered water." } },
                                           { 2103, state.toggles[3].Bounds(), { "Freeze animated water for inspection." } },
                                           { 2104, state.toggles[4].Bounds(), { "Use the flat-water debug display." } },
                                           { 2105, state.toggles[5].Bounds(), { "Enable or disable shadows in the active renderer." } },
                                           { 2106, state.timeScaleSlider.Bounds(), { "Control how quickly the simulation advances.", "Multiplier of real time" } },
                                           { 2107, state.modelCountSlider.Bounds(), { "Change the generated model count. Release to rebuild the scene.", "Objects" } }, };
    return HitTooltip( candidates, mouseX, mouseY );
}

UITooltipTarget MemoryTooltip( const MemoryTab::UIMemoryOverlayState& state, int mouseX, int mouseY )
{
    const UITooltipTarget candidates[] = { { 2200, state.replayPresetButtons[0].Bounds(), { "Request the Lossless replay memory preset." } },
                                           { 2201, state.replayPresetButtons[1].Bounds(), { "Request the Balanced replay memory preset." } },
                                           { 2202, state.replayPresetButtons[2].Bounds(), { "Request the Compact replay memory preset." } },
                                           { 2203, state.replayRetentionSlider.Bounds(), { "Request replay history duration. Actual retained windows are shown below.", "Seconds" } },
                                           { 2204, state.replayBudgetSlider.Bounds(), { "Request a replay memory budget. The replay owner applies capacity limits.", "MiB (1,048,576 bytes)" } }, };
    return HitTooltip( candidates, mouseX, mouseY );
}
} // namespace

UITooltipTarget UIWindowInteractionOwner::FindRenderTooltip() const
{
    const UITooltipTarget actions[] = { { 2800, m_renderShadowToggle.Bounds(), { "Enable or disable ordinary rendering shadows." } }, { 2801, m_saveRenderDefaultsButton.Bounds(), { "Save ordinary rendering settings as defaults." } }, { 2802, m_saveTrajectoryStyleButton.Bounds(), { "Save edited path appearance through the render defaults profile." } }, };
    const auto action = HitTooltip( actions, m_mouseX, m_mouseY );
    if ( action.id != 0 )
    {
        return action;
    }
    for ( int index = 0; index < static_cast<int>( UIRenderParam::Count ); ++index )
    {
        if ( m_renderSliders[index].HitTest( m_mouseX, m_mouseY ) )
        {
            return { static_cast<uint32_t>( 2820 + index ), m_renderSliders[index].Bounds(), kRenderTooltipText[index], true };
        }
    }
    return {};
}

UITooltipTarget UIWindowInteractionOwner::FindToolsTooltip( const UIRect& content ) const
{
    if ( !m_window.isVisible || m_window.isMinimized || HasOpenPopup() )
    {
        return {};
    }

    // Invariant: only the visible tab's content may provide a target. Retained
    // rectangles from another tab or scrolled-out rows cannot claim this hover.
    if ( content.Contains( m_mouseX, m_mouseY ) )
    {
        switch ( m_activeTab )
        {
        case InGameUITab::Scene:
        {
            const UITooltipTarget playback[] = { { 2520, m_sceneTab.pauseLockToggle.Bounds(), { "Keep scene simulation paused across scene changes." } }, { 2521,
                                                                                                                                                            m_sceneTab.singleStepButton.Bounds(),
                                                                                                                                                            { "Advance one scene turn while Pause lock is enabled.", "One turn", "Space holds stepping", "Enable Pause lock to step once." },
                                                                                                                                                            false,
                                                                                                                                                            false,
                                                                                                                                                            m_sceneTab.lastPauseLocked }, };
            return HitTooltip( playback, m_mouseX, m_mouseY );
        }
        case InGameUITab::Physics:
            return PhysicsTooltip( m_physicsTab, m_mouseX, m_mouseY );
        case InGameUITab::Sky:
            return SkyTab::TooltipAt( m_skyTab, m_mouseX, m_mouseY );
        case InGameUITab::Cinematic:
            return CinematicTab::TooltipAt( m_cinematicTab, m_mouseX, m_mouseY );
        case InGameUITab::Render:
            return FindRenderTooltip();
        case InGameUITab::Keys:
        {
            const UITooltipTarget candidates[] = { { 2700, m_controlsTab.seedSlider.Bounds(), { "Change the generated scene seed. Release to rebuild.", "Seed" } },
                                                   { 2701, m_controlsTab.solverBallSlider.Bounds(), { "Change the generated sphere count. Release to rebuild.", "Objects" } },
                                                   { 2702, m_controlsTab.solverBoxSlider.Bounds(), { "Change the generated box count. Release to rebuild.", "Objects" } },
                                                   { 2703, m_controlsTab.worldFluidHeightSlider.Bounds(), { "Set the fluid surface height.", "World units" } },
                                                   { 2704, m_controlsTab.worldFluidDensitySlider.Bounds(), { "Set fluid density used for buoyancy.", "Density" } }, };
            return HitTooltip( candidates, m_mouseX, m_mouseY );
        }
        case InGameUITab::Options:
            return OptionsTooltip( m_optionsTab, m_mouseX, m_mouseY );
        case InGameUITab::Memory:
            return MemoryTooltip( m_memoryOverlay, m_mouseX, m_mouseY );
        case InGameUITab::Profiler:
        {
            const UITooltipTarget candidates[] = { { 2300,
                                                     m_profilerTab.workerToggle.Bounds(),
                                                     { "Enable workers, or return work to the calling thread.", "", "", "No worker threads are available." },
                                                     false,
                                                     false,
                                                     m_lastMaxWorkerThreadCount > 0 }, { 2301,
                                                                                         m_profilerTab.workerThreadSlider.Bounds(),
                                                                                         { "Set the active worker count.", "Threads", "", "No worker threads are available." },
                                                                                         false,
                                                                                         false,
                                                                                         m_lastMaxWorkerThreadCount > 0 }, };
            const auto target = HitTooltip( candidates, m_mouseX, m_mouseY );
            return target.id != 0 ? target : ProfilerTab::TooltipAt( m_profilerTab, content, m_scrollY, m_mouseX, m_mouseY );
        }
        case InGameUITab::Targets:
            return HitTooltip( std::array { UITooltipTarget { 2400, m_renderTargetCombo.Bounds(), { "Inspect an available render target. Unavailable targets " "are disabled." } } }, m_mouseX, m_mouseY );
        default:
            return {};
        }
    }

    const UIRect bounds { static_cast<float>( m_window.x ), static_cast<float>( m_window.y ), static_cast<float>( m_window.width ), static_cast<float>( m_window.height ) };
    if ( GameLayout::ComputeToolsChromeRects( bounds, m_presentationEnabled ).compact )
    {
        const UITooltipTarget compact[] = { { 2510, m_toolsTabCombo.Bounds(), { "Open any Tools tab. Scroll the list to see all tools." } }, { 2511, m_toolsDisplayCombo.Bounds(), { "Open renderer, reflection, blur, VSync and diagnostic display settings." } }, };
        return HitTooltip( compact, m_mouseX, m_mouseY );
    }
    const UITooltipTarget footer[] = { { 2500, m_rendererCombo.Bounds(), { "The active rendering backend is DirectX 12." } },
                                       { 2501, m_reflectionCombo.Bounds(), { "Choose the water reflection method. Unsupported methods are disabled." } },
                                       { 2502, m_blurToggle.Bounds(), { "Enable or disable the Tools backdrop blur preview." } },
                                       { 2503, m_vsyncToggle.Bounds(), { "Synchronize presentation with the display's refresh." } },
                                       { 2504, m_hitboxToggle.Bounds(), { "Show UI interaction bounds for diagnostics." } },
                                       { 2505, m_histogramToggle.Bounds(), { "Show or hide the Canvas marker-history chart. Editor keeps it pinned.", "Milliseconds", "F5" } },
                                       { 2506, m_timelineToggle.Bounds(), { "Switch the detailed Profiler between its table and timeline views.", "Milliseconds" } }, };
    return HitTooltip( footer, m_mouseX, m_mouseY );
}
} // namespace SkullbonezCore::UI
