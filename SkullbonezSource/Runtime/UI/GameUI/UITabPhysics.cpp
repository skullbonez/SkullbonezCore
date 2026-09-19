/*
File: SkullbonezSource/Runtime/UI/GameUI/UITabPhysics.cpp
Purpose:
  Implements physics-policy toggles and parameter-slider command projection.

Summary:
  Maps physics toggles and parameter sliders to
  typed one-frame commands with preview/commit state.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UITabPhysics.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UITabPhysics.h"

#include "UI.h"
#include "../../../UI/UIDrawWidgets.h"
#include "GameUILayout.h"
#include "../../../UI/UIStyle.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

using namespace SkullbonezCore::UI::GameLayout;
using namespace SkullbonezCore::UI::OperatorControlPolicy;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

float GravityStrengthFromWorld( float gravity )
{
    return std::clamp( -gravity, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
}

float WorldGravityFromStrength( float strength )
{
    return -std::clamp( strength, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
}

using PhysicsState = SkullbonezCore::UI::PhysicsTab::UIPhysicsTabState;
struct SliderLayout
{
    SkullbonezCore::UI::UISlider PhysicsState::* widget;
    int section;
    float y;
};
constexpr SliderLayout SLIDERS[] = { { &PhysicsState::alphaSlider, 1, 374 },
                                     { &PhysicsState::contactLingerSlider, 1, 418 },
                                     { &PhysicsState::rayImpulseSlider, 0, 312 },
                                     { &PhysicsState::launcherProjectileSpeedSlider, 0, 356 },
                                     { &PhysicsState::worldGravitySlider, 0, 268 },
                                     { &PhysicsState::terrainFrictionSlider, 0, 482 },
                                     { &PhysicsState::objectFrictionSlider, 0, 526 },
                                     { &PhysicsState::rollingFrictionSlider, 0, 570 },
                                     { &PhysicsState::tornadoRadiusSlider, 0, 68 },
                                     { &PhysicsState::tornadoHeightSlider, 0, 112 },
                                     { &PhysicsState::tornadoInwardSlider, 0, 156 },
                                     { &PhysicsState::tornadoSwirlSlider, 0, 200 },
                                     { &PhysicsState::tornadoLiftSlider, 0, 244 }, };
constexpr int TOGGLE_SECTION[] = { 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1 };
constexpr float TOGGLE_Y[] = { 564, 624, 80, 110, 140, 170, 614, 200, 230, 34, 260, 290, 320 };
float LayerY( int index )
{
    return index == 1 ? 20.0f : index == 3 ? 50.0f : 534.0f + static_cast<float>( index ) * 30;
}
float TornadoY( bool advancedOpen )
{
    return advancedOpen ? 1012.0f : 482.0f;
}
float RestoreY( bool advancedOpen, bool tornadoOpen )
{
    return TornadoY( advancedOpen ) + ( tornadoOpen ? 300.0f : 40.0f );
}
float NumericalY( int index )
{
    return index < 4 ? 24.0f + index * 44 : index == 12 ? 200.0f : 650.0f + ( index - 4 ) * 44;
}
void SetContentBounds( PhysicsState& state, float x, float firstToggleY, float width )
{
    // Hidden sections have empty bounds. Draw and input rebuild the same layout.
    const float base = firstToggleY - 42;
    for ( int i = 0; i < 13; ++i )
    {
        const bool shown = state.section == TOGGLE_SECTION[i] && ( i != 6 || state.advancedOpen ) && ( i != 9 || state.tornadoOpen );
        state.toggles[i].SetBounds( shown ? x : -10000, shown ? base + TOGGLE_Y[i] + ( i == 9 ? TornadoY( state.advancedOpen ) : 0 ) : -10000, shown ? width : 0, shown ? 24.0f : 0 );
    }
    for ( std::size_t i = 0; i < std::size( SLIDERS ); ++i )
    {
        const auto& row = SLIDERS[i];
        const bool shown = state.section == row.section && ( i < 5 || i > 7 || state.advancedOpen ) && ( i < 8 || state.tornadoOpen );
        ( state.*row.widget ).SetBounds( shown ? x : -10000, shown ? base + row.y + ( i >= 8 ? TornadoY( state.advancedOpen ) : 0 ) : -10000, shown ? width : 0, shown ? 34.0f : 0 );
    }
    for ( int i = 0; i < 13; ++i )
    {
        const bool shown = state.section == 0 && ( i < 4 || i == 12 || state.advancedOpen );
        state.numericalSliders[i].SetBounds( shown ? x : -10000, shown ? base + NumericalY( i ) : -10000, shown ? width : 0, shown ? 34.0f : 0 );
    }
    for ( int i = 0; i < 4; ++i )
    {
        state.hzButtons[i] = state.section == 0 ? SkullbonezCore::UI::UIRect { x + i * width / 4, base, width / 4 - 2, 20 } : SkullbonezCore::UI::UIRect {};
    }
    state.advancedButton = state.section == 0 ? SkullbonezCore::UI::UIRect { x, base + 444, width, 26 } : SkullbonezCore::UI::UIRect {};
    state.tornadoButton = state.section == 0 ? SkullbonezCore::UI::UIRect { x, base + TornadoY( state.advancedOpen ), width, 26 } : SkullbonezCore::UI::UIRect {};
    state.timeScaleSlider.SetBounds( state.section == 0 ? x : -10000, base + 400, state.section == 0 ? width : 0, 34 );
    state.restoreStartupButton = state.section == 0 ? SkullbonezCore::UI::UIRect { x, base + RestoreY( state.advancedOpen, state.tornadoOpen ), width, 26 } : SkullbonezCore::UI::UIRect {};
    state.saveDefaultsButton = state.section == 0 ? SkullbonezCore::UI::UIRect { x, base + RestoreY( state.advancedOpen, state.tornadoOpen ) + 32, width, 26 } : SkullbonezCore::UI::UIRect {};
    for ( int i = 0; i < 10; ++i )
    {
        state.additionalLayers[i].SetBounds( state.section == 1 ? x : -10000, state.section == 1 ? base + LayerY( i ) : -10000, state.section == 1 ? width : 0, 24 );
    }
    state.impulseScaleSlider.SetBounds( state.section == 1 ? x : -10000, base + 928, state.section == 1 ? width : 0, 34 );
    state.impulseThresholdSlider.SetBounds( state.section == 1 ? x : -10000, base + 972, state.section == 1 ? width : 0, 34 );
    state.massSlider.SetBounds( state.section == 2 ? x : -10000, base + 12, state.section == 2 ? width : 0, 34 );
    state.undoMassButton = state.section == 2 ? SkullbonezCore::UI::UIRect { x, base + 52, width / 2 - 2, 26 } : SkullbonezCore::UI::UIRect {};
    state.redoMassButton = state.section == 2 ? SkullbonezCore::UI::UIRect { x + width / 2, base + 52, width / 2 - 2, 26 } : SkullbonezCore::UI::UIRect {};
    state.saveBodyButton = state.section == 2 ? SkullbonezCore::UI::UIRect { x, base + 84, width, 26 } : SkullbonezCore::UI::UIRect {};
    for ( int i = 0; i < 6; ++i )
    {
        state.impulseSliders[i].SetBounds( state.section == 2 ? x : -10000, base + 214 + i * 44, state.section == 2 ? width : 0, 34 );
    }
    state.impulseSpaceButton = state.section == 2 ? SkullbonezCore::UI::UIRect { x, base + 176, width, 26 } : SkullbonezCore::UI::UIRect {};
    state.centerImpulseButton = state.section == 2 ? SkullbonezCore::UI::UIRect { x, base + 480, width, 26 } : SkullbonezCore::UI::UIRect {};
    state.applyImpulseButton = state.section == 2 ? SkullbonezCore::UI::UIRect { x, base + 512, width, 26 } : SkullbonezCore::UI::UIRect {};
    state.pipelinePrevButton = {};
    state.pipelineNextButton = {};
    if ( state.section == 1 )
    {
        SetPipelineStepButtonBounds( state.pipelinePrevButton, state.pipelineNextButton, x, width, base + 466 );
    }
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace PhysicsTab
{

int ContentHeight( int section, bool advancedOpen, bool tornadoOpen )
{
    return section == 0 ? static_cast<int>( RestoreY( advancedOpen, tornadoOpen ) + 118 ) : section == 1 ? 1120 : section == 2 ? 2400 : 520;
}


void ResetPreviewState( UIPhysicsTabState& state )
{
    state.numericalPreviews.fill( -1 );
    // Concept: Preview values are transient drag feedback. Runtime settings
    // change only after command handling applies the result.
    state.previewAlpha = -1.0f;
    state.previewContactLinger = -1.0f;
    state.previewWorldGravity = -1.0f;
    state.previewRayImpulse = -1.0f;
    state.previewLauncherProjectileSpeed = -1.0f;
    state.previewTerrainFriction = -1.0f;
    state.previewObjectFriction = -1.0f;
    state.previewRollingFriction = -1.0f;
    state.previewTornadoRadius = -1.0f;
    state.previewTornadoHeight = -1.0f;
    state.previewTornadoInward = -1.0f;
    state.previewTornadoSwirl = -1.0f;
    state.previewTornadoLift = -1.0f;
}


bool HandleContentClick( UIPhysicsTabState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY, float contentX, float rowBase, float contentW )
{
    // Invariant: This function emits commands only. Physics debug state and
    // world forces are mutated by runtime command handling.
    SetContentBounds( state, contentX, rowBase, contentW );
    if ( state.section == 2 && state.selectedBody == 0 )
    {
        return true;
    }
    if ( state.section == 2 && state.liveEditable )
    {
        if ( state.impulseEditable )
        {
            if ( state.impulseSpaceButton.Contains( mouseX, mouseY ) )
            {
                state.pointImpulse.local = !state.pointImpulse.local;
                state.pointImpulse.point = state.pointImpulse.local ? std::array<float, 3> {} : state.selectedCenter;
                return true;
            }
            if ( state.centerImpulseButton.Contains( mouseX, mouseY ) )
            {
                state.pointImpulse.point = state.pointImpulse.local ? std::array<float, 3> {} : state.selectedCenter;
                return true;
            }
            if ( state.applyImpulseButton.Contains( mouseX, mouseY ) )
            {
                result.commands.physics.applyPointImpulse = true;
                result.commands.physics.pointImpulse = state.pointImpulse;
                return true;
            }
            for ( int i = 0; i < 6; ++i )
            {
                if ( state.impulseSliders[i].HitTest( mouseX, mouseY ) )
                {
                    activeSlider = SLIDER_PHYSICS_BASE + 60 + i;
                    return UpdateActiveSlider( state, activeSlider, mouseX, result );
                }
            }
        }
        if ( state.massEditable && state.massSlider.HitTest( mouseX, mouseY ) )
        {
            activeSlider = SLIDER_PHYSICS_BASE + 50;
            state.massDragBody = state.selectedBody;
            return UpdateActiveSlider( state, activeSlider, mouseX, result );
        }
        if ( state.undoMassButton.Contains( mouseX, mouseY ) )
        {
            result.commands.editor.requestUndo = true;
            return true;
        }
        if ( state.redoMassButton.Contains( mouseX, mouseY ) )
        {
            result.commands.editor.requestRedo = true;
            return true;
        }
        if ( state.saveBodyButton.Contains( mouseX, mouseY ) )
        {
            result.commands.physics.saveBodyScene = true;
            return true;
        }
    }
    if ( state.section == 0 )
    {
        if ( state.advancedButton.Contains( mouseX, mouseY ) )
        {
            state.advancedOpen = !state.advancedOpen;
            return true;
        }
        if ( state.tornadoButton.Contains( mouseX, mouseY ) )
        {
            state.tornadoOpen = !state.tornadoOpen;
            return true;
        }
        if ( !state.liveEditable )
        {
            return true;
        }
        for ( int i = 0; i < 4; ++i )
        {
            if ( state.hzButtons[i].Contains( mouseX, mouseY ) )
            {
                result.commands.physics.requestedHz = 30 << i;
                return true;
            }
        }
        if ( state.timeScaleSlider.HitTest( mouseX, mouseY ) )
        {
            activeSlider = SLIDER_PHYSICS_BASE + 70;
            return UpdateActiveSlider( state, activeSlider, mouseX, result );
        }
    }
    if ( state.liveEditable && state.section == 0 )
    {
        if ( state.restoreStartupButton.Contains( mouseX, mouseY ) )
        {
            result.commands.physics.restoreStartup = true;
            return true;
        }
        if ( state.saveDefaultsButton.Contains( mouseX, mouseY ) )
        {
            result.commands.physics.saveDefaults = true;
            return true;
        }
        for ( int i = 0; i < 13; ++i )
        {
            if ( !state.numericalSliders[i].HitTest( mouseX, mouseY ) )
            {
                continue;
            }
            const auto range = Physics::INTERACTIVE_PHYSICS_RANGES[i];
            activeSlider = SLIDER_PHYSICS_BASE + 20 + i;
            state.numericalPreviews[i] = state.numericalSliders[i].ValueFromMouse( mouseX, range.minimum, range.maximum, range.integer ? 1.0f : .001f );
            return true;
        }
    }


    for ( int i = 0; i < 10; ++i )
    {
        if ( state.additionalLayers[i].HitTest( mouseX, mouseY ) )
        {
            result.commands.physics.physicsDebugOverlayToToggle = static_cast<UIPhysicsDebugOverlay>( static_cast<int>( UIPhysicsDebugOverlay::Normals ) + i );
            return true;
        }
    }

    if ( state.section == 1 && ( state.impulseScaleSlider.HitTest( mouseX, mouseY ) || state.impulseThresholdSlider.HitTest( mouseX, mouseY ) ) )
    {
        activeSlider = SLIDER_PHYSICS_BASE + ( state.impulseScaleSlider.HitTest( mouseX, mouseY ) ? 40 : 41 );
        return UpdateActiveSlider( state, activeSlider, mouseX, result );
    }
    if ( state.toggles[0].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 0, result.commands.physics );
    }
    else if ( state.toggles[1].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 1, result.commands.physics );
    }
    else if ( state.toggles[2].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 2, result.commands.physics );
    }
    else if ( state.toggles[3].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 3, result.commands.physics );
    }
    else if ( state.toggles[4].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 4, result.commands.physics );
    }
    else if ( state.toggles[5].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 5, result.commands.physics );
    }
    else if ( state.toggles[7].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 7, result.commands.physics );
    }
    else if ( state.toggles[6].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 6, result.commands.physics );
    }
    else if ( state.toggles[8].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 8, result.commands.physics );
    }
    else if ( state.toggles[9].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 9, result.commands.physics );
    }
    else if ( state.toggles[10].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 10, result.commands.physics );
    }
    else if ( state.toggles[11].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 11, result.commands.physics );
    }
    else if ( state.toggles[12].HitTest( mouseX, mouseY ) )
    {
        EmitPhysicsToggleCommand( 12, result.commands.physics );
    }
    else if ( state.pipelinePrevButton.Contains( mouseX, mouseY ) )
    {
        result.commands.physics.stepPhysicsPipelinePrevious = true;
    }
    else if ( state.pipelineNextButton.Contains( mouseX, mouseY ) )
    {
        result.commands.physics.stepPhysicsPipelineNext = true;
    }
    else if ( state.alphaSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_ALPHA;
        state.previewAlpha = state.alphaSlider.ValueFromMouse( mouseX, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX, UI_PHYSICS_ALPHA_STEP );

        result.commands.physics.requestedPhysicsDebugAlpha = state.previewAlpha;
        return true;
    }
    else if ( state.contactLingerSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_CONTACT_LINGER;
        state.previewContactLinger = state.contactLingerSlider.ValueFromMouse( mouseX, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX, UI_CONTACT_LINGER_STEP );

        result.commands.physics.requestedPhysicsDebugContactLinger = state.previewContactLinger;
        return true;
    }
    else if ( state.rayImpulseSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_RAY_IMPULSE;
        state.previewRayImpulse = state.rayImpulseSlider.ValueFromMouse( mouseX, UI_RAY_IMPULSE_MIN, UI_RAY_IMPULSE_MAX, UI_RAY_IMPULSE_STEP );

        return true;
    }
    else if ( state.launcherProjectileSpeedSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_LAUNCHER_PROJECTILE_SPEED;
        state.previewLauncherProjectileSpeed = state.launcherProjectileSpeedSlider.ValueFromMouse( mouseX,
                                                                                                   UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                                                                   UI_LAUNCHER_PROJECTILE_SPEED_MAX,
                                                                                                   UI_LAUNCHER_PROJECTILE_SPEED_STEP );

        return true;
    }
    else if ( state.worldGravitySlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_WORLD_GRAVITY;
        state.previewWorldGravity = state.worldGravitySlider.ValueFromMouse( mouseX, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX, UI_WORLD_GRAVITY_STEP );

        return true;
    }
    else if ( state.terrainFrictionSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TERRAIN_FRICTION;
        state.previewTerrainFriction = state.terrainFrictionSlider.ValueFromMouse( mouseX, UI_FRICTION_COEFF_MIN, UI_FRICTION_COEFF_MAX, UI_FRICTION_COEFF_STEP );

        return true;
    }
    else if ( state.objectFrictionSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_OBJECT_FRICTION;
        state.previewObjectFriction = state.objectFrictionSlider.ValueFromMouse( mouseX, UI_FRICTION_COEFF_MIN, UI_FRICTION_COEFF_MAX, UI_FRICTION_COEFF_STEP );

        return true;
    }
    else if ( state.rollingFrictionSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_ROLLING_FRICTION;
        state.previewRollingFriction = state.rollingFrictionSlider.ValueFromMouse( mouseX, UI_ROLLING_FRICTION_COEFF_MIN, UI_ROLLING_FRICTION_COEFF_MAX, UI_ROLLING_FRICTION_COEFF_STEP );

        return true;
    }
    else if ( state.tornadoRadiusSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_RADIUS;
        state.previewTornadoRadius = state.tornadoRadiusSlider.ValueFromMouse( mouseX, UI_TORNADO_RADIUS_MIN, UI_TORNADO_RADIUS_MAX, UI_TORNADO_RADIUS_STEP );

        return true;
    }
    else if ( state.tornadoHeightSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_HEIGHT;
        state.previewTornadoHeight = state.tornadoHeightSlider.ValueFromMouse( mouseX, UI_TORNADO_HEIGHT_MIN, UI_TORNADO_HEIGHT_MAX, UI_TORNADO_HEIGHT_STEP );

        return true;
    }
    else if ( state.tornadoInwardSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_INWARD;
        state.previewTornadoInward = state.tornadoInwardSlider.ValueFromMouse( mouseX, UI_TORNADO_INWARD_MIN, UI_TORNADO_INWARD_MAX, UI_TORNADO_INWARD_STEP );

        return true;
    }
    else if ( state.tornadoSwirlSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_SWIRL;
        state.previewTornadoSwirl = state.tornadoSwirlSlider.ValueFromMouse( mouseX, UI_TORNADO_SWIRL_MIN, UI_TORNADO_SWIRL_MAX, UI_TORNADO_SWIRL_STEP );

        return true;
    }
    else if ( state.tornadoLiftSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_LIFT;
        state.previewTornadoLift = state.tornadoLiftSlider.ValueFromMouse( mouseX, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX, UI_TORNADO_LIFT_STEP );

        return true;
    }

    return false;
}


bool UpdateActiveSlider( UIPhysicsTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    if ( activeSlider == SLIDER_PHYSICS_BASE + 70 )
    {
        state.timeScalePreview = state.timeScaleSlider.ValueFromMouse( mouseX,
                                                                       OperatorControlPolicy::UI_TIME_SCALE_MIN,
                                                                       OperatorControlPolicy::UI_TIME_SCALE_MAX,
                                                                       OperatorControlPolicy::UI_TIME_SCALE_STEP );
        return true;
    }
    const int impulseAxis = activeSlider - SLIDER_PHYSICS_BASE - 60;
    if ( impulseAxis >= 0 && impulseAxis < 6 )
    {
        const int axis = impulseAxis % 3;
        const float center = impulseAxis >= 3 && !state.pointImpulse.local ? state.selectedCenter[axis] : 0;
        const float radius = impulseAxis < 3 ? 100.0f : 10.0f;
        const float value = state.impulseSliders[impulseAxis].ValueFromMouse( mouseX, center - radius, center + radius, .01f );
        ( impulseAxis < 3 ? state.pointImpulse.impulse : state.pointImpulse.point )[axis] = value;
        return true;
    }
    if ( activeSlider == SLIDER_PHYSICS_BASE + 50 )
    {
        state.massPreview = std::pow( 10.0f, state.massSlider.ValueFromMouse( mouseX, -3, 6, .001f ) );
        return true;
    }

    if ( activeSlider == SLIDER_PHYSICS_BASE + 40 )
    {
        result.commands.physics.requestedImpulseScale = state.impulseScaleSlider.ValueFromMouse( mouseX, .001f, 2, .001f );
        return true;
    }
    if ( activeSlider == SLIDER_PHYSICS_BASE + 41 )
    {
        result.commands.physics.requestedImpulseThreshold = state.impulseThresholdSlider.ValueFromMouse( mouseX, 0, 100, .001f );
        return true;
    }

    const int parameter = activeSlider - SLIDER_PHYSICS_BASE - 20;
    if ( parameter >= 0 && parameter < 13 )
    {
        const auto range = Physics::INTERACTIVE_PHYSICS_RANGES[parameter];
        state.numericalPreviews[parameter] = state.numericalSliders[parameter].ValueFromMouse( mouseX, range.minimum, range.maximum, range.integer ? 1.0f : .001f );
        return true;
    }

    if ( activeSlider == SLIDER_ALPHA )
    {
        state.previewAlpha = state.alphaSlider.ValueFromMouse( mouseX, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX, UI_PHYSICS_ALPHA_STEP );

        result.commands.physics.requestedPhysicsDebugAlpha = state.previewAlpha;
        return true;
    }

    if ( activeSlider == SLIDER_CONTACT_LINGER )
    {
        state.previewContactLinger = state.contactLingerSlider.ValueFromMouse( mouseX, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX, UI_CONTACT_LINGER_STEP );

        result.commands.physics.requestedPhysicsDebugContactLinger = state.previewContactLinger;
        return true;
    }

    if ( activeSlider == SLIDER_RAY_IMPULSE )
    {
        state.previewRayImpulse = state.rayImpulseSlider.ValueFromMouse( mouseX, UI_RAY_IMPULSE_MIN, UI_RAY_IMPULSE_MAX, UI_RAY_IMPULSE_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_LAUNCHER_PROJECTILE_SPEED )
    {
        state.previewLauncherProjectileSpeed = state.launcherProjectileSpeedSlider.ValueFromMouse( mouseX,
                                                                                                   UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                                                                   UI_LAUNCHER_PROJECTILE_SPEED_MAX,
                                                                                                   UI_LAUNCHER_PROJECTILE_SPEED_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_WORLD_GRAVITY )
    {
        state.previewWorldGravity = state.worldGravitySlider.ValueFromMouse( mouseX, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX, UI_WORLD_GRAVITY_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_TERRAIN_FRICTION )
    {
        state.previewTerrainFriction = state.terrainFrictionSlider.ValueFromMouse( mouseX, UI_FRICTION_COEFF_MIN, UI_FRICTION_COEFF_MAX, UI_FRICTION_COEFF_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_OBJECT_FRICTION )
    {
        state.previewObjectFriction = state.objectFrictionSlider.ValueFromMouse( mouseX, UI_FRICTION_COEFF_MIN, UI_FRICTION_COEFF_MAX, UI_FRICTION_COEFF_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_ROLLING_FRICTION )
    {
        state.previewRollingFriction = state.rollingFrictionSlider.ValueFromMouse( mouseX, UI_ROLLING_FRICTION_COEFF_MIN, UI_ROLLING_FRICTION_COEFF_MAX, UI_ROLLING_FRICTION_COEFF_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_RADIUS )
    {
        state.previewTornadoRadius = state.tornadoRadiusSlider.ValueFromMouse( mouseX, UI_TORNADO_RADIUS_MIN, UI_TORNADO_RADIUS_MAX, UI_TORNADO_RADIUS_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_HEIGHT )
    {
        state.previewTornadoHeight = state.tornadoHeightSlider.ValueFromMouse( mouseX, UI_TORNADO_HEIGHT_MIN, UI_TORNADO_HEIGHT_MAX, UI_TORNADO_HEIGHT_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_INWARD )
    {
        state.previewTornadoInward = state.tornadoInwardSlider.ValueFromMouse( mouseX, UI_TORNADO_INWARD_MIN, UI_TORNADO_INWARD_MAX, UI_TORNADO_INWARD_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_SWIRL )
    {
        state.previewTornadoSwirl = state.tornadoSwirlSlider.ValueFromMouse( mouseX, UI_TORNADO_SWIRL_MIN, UI_TORNADO_SWIRL_MAX, UI_TORNADO_SWIRL_STEP );

        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_LIFT )
    {
        state.previewTornadoLift = state.tornadoLiftSlider.ValueFromMouse( mouseX, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX, UI_TORNADO_LIFT_STEP );

        return true;
    }


    return false;
}


bool CommitActiveSlider( UIPhysicsTabState& state, int activeSlider, InGameUIInputResult& result )
{
    if ( !state.liveEditable && state.section != 1 )
    {
        return false;
    }
    if ( activeSlider == SLIDER_PHYSICS_BASE + 70 )
    {
        result.commands.sceneOptions.requestedTimeScale = state.timeScalePreview;
        return true;
    }
    if ( activeSlider == SLIDER_PHYSICS_BASE + 50 && state.liveEditable && state.massEditable && state.massDragBody == state.selectedBody )
    {
        result.commands.physics.requestBodyMass = true;
        result.commands.physics.bodySceneObjectId = state.massDragBody;
        result.commands.physics.bodyMass = state.massPreview;
        return true;
    }

    if ( activeSlider == SLIDER_WORLD_GRAVITY && state.previewWorldGravity >= 0 )
    {
        result.commands.water.requestWorldGravity = true;
        result.commands.water.requestedWorldGravity = WorldGravityFromStrength( state.previewWorldGravity );
        return true;
    }
    const int parameter = activeSlider - SLIDER_PHYSICS_BASE - 20;
    if ( parameter >= 0 && parameter < 13 && state.liveEditable )
    {
        result.commands.physics.setting = static_cast<Physics::InteractivePhysicsSetting>( parameter );
        result.commands.physics.settingValue = state.numericalPreviews[parameter];
        return true;
    }

    if ( activeSlider == SLIDER_ALPHA && state.previewAlpha >= 0.0f )
    {
        result.commands.physics.requestedPhysicsDebugAlpha = state.previewAlpha;
        return true;
    }

    if ( activeSlider == SLIDER_CONTACT_LINGER && state.previewContactLinger >= 0.0f )
    {
        result.commands.physics.requestedPhysicsDebugContactLinger = state.previewContactLinger;
        return true;
    }

    if ( activeSlider == SLIDER_RAY_IMPULSE && state.previewRayImpulse >= 0.0f )
    {
        result.commands.physics.requestRayCastImpulseStrength = true;
        result.commands.physics.requestedRayCastImpulseStrength = state.previewRayImpulse;
        return true;
    }

    if ( activeSlider == SLIDER_LAUNCHER_PROJECTILE_SPEED && state.previewLauncherProjectileSpeed >= 0.0f )
    {
        result.commands.physics.requestLauncherProjectileSpeed = true;
        result.commands.physics.requestedLauncherProjectileSpeed = state.previewLauncherProjectileSpeed;
        return true;
    }

    if ( activeSlider == SLIDER_TERRAIN_FRICTION && state.previewTerrainFriction >= 0.0f )
    {
        result.commands.physics.requestTerrainFrictionCoeff = true;
        result.commands.physics.requestedTerrainFrictionCoeff = state.previewTerrainFriction;
        return true;
    }

    if ( activeSlider == SLIDER_OBJECT_FRICTION && state.previewObjectFriction >= 0.0f )
    {
        result.commands.physics.requestObjectFrictionCoeff = true;
        result.commands.physics.requestedObjectFrictionCoeff = state.previewObjectFriction;
        return true;
    }

    if ( activeSlider == SLIDER_ROLLING_FRICTION && state.previewRollingFriction >= 0.0f )
    {
        result.commands.physics.requestRollingFrictionCoeff = true;
        result.commands.physics.requestedRollingFrictionCoeff = state.previewRollingFriction;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_RADIUS && state.previewTornadoRadius >= 0.0f )
    {
        result.commands.physics.requestTornadoRadius = true;
        result.commands.physics.requestedTornadoRadius = state.previewTornadoRadius;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_HEIGHT && state.previewTornadoHeight >= 0.0f )
    {
        result.commands.physics.requestTornadoHeight = true;
        result.commands.physics.requestedTornadoHeight = state.previewTornadoHeight;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_INWARD && state.previewTornadoInward >= 0.0f )
    {
        result.commands.physics.requestTornadoInward = true;
        result.commands.physics.requestedTornadoInward = state.previewTornadoInward;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_SWIRL && state.previewTornadoSwirl >= 0.0f )
    {
        result.commands.physics.requestTornadoSwirl = true;
        result.commands.physics.requestedTornadoSwirl = state.previewTornadoSwirl;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_LIFT && state.previewTornadoLift >= 0.0f )
    {
        result.commands.physics.requestTornadoLift = true;
        result.commands.physics.requestedTornadoLift = state.previewTornadoLift;
        return true;
    }

    return false;
}


int BodyContentHeight( const UIPhysicsInspector& body )
{
    if ( !body.selected )
    {
        return 48;
    }
    // Keep the last actual inspector row reachable without allowing scrolling
    // through the unused part of the bounded contact array.
    const int lines = 12 + ( body.volume > 0 ? 1 : 0 ) + ( body.historicalContext ? 1 : 0 ) + body.contactCount * 4 + ( body.droppedContacts > 0 ? 1 : 0 );
    return 578 + 24 + lines * 22;
}

void DrawInspector( const UIDrawContext& draw, const UIPhysicsInspector& body, const UIRect& bounds, bool statistics )
{
    const auto& color = Style::Palette().textPrimary;
    float y = bounds.y + 12;
    const auto line = [&]( const char* text )
    {
        draw.Text( bounds.x, y, 11, color.r, color.g, color.b, text );
        y += 22;
    };
    char text[160] {};
    if ( statistics )
    {
        std::snprintf( text, sizeof( text ), "Bodies: %d awake / %d asleep / %d fixed", body.awakeBodies, body.sleepingBodies, body.fixedBodies );
        line( text );
        std::snprintf( text, sizeof( text ), "Contact rows: %d", body.rows );
        line( text );
        std::snprintf( text, sizeof( text ), "Contact budget %d / actual sweeps %d", body.requestedIterations, body.actualIterations );
        line( text );
        line( "Joint floors may extend the contact budget." );
        if ( body.actualIterations > 0 )
        {
            if ( body.sweepBudget > 0 )
            {
                std::snprintf( text, sizeof( text ), "Sweep limit %d | stopped early: %s", body.sweepBudget, body.actualIterations < body.sweepBudget ? "yes" : "no" );
            }
            else
            {
                std::snprintf( text, sizeof( text ), "Sweep limit / early stop unavailable" );
            }
            line( text );
        }
        else
        {
            line( "No solver sweeps in the last sampled tick." );
        }
        std::snprintf( text, sizeof( text ), "Overlay lines %zu / dropped %zu", body.geometry[0], body.geometry[1] );
        line( text );
        std::snprintf( text, sizeof( text ), "Contact history dropped %zu | capped %zu", body.geometry[2], body.geometry[3] );
        line( text );
        std::snprintf( text, sizeof( text ), "Frame Physics %.3g ms | joints %d", body.physicsMs, body.joints );
        line( text );
        std::snprintf( text, sizeof( text ), "Scheduler dropped %llu ticks", static_cast<unsigned long long>( body.droppedTicks ) );
        line( text );
        std::snprintf( text, sizeof( text ), "Cache hits %d / misses %d / warm rows %d", body.cacheHits, body.cacheMisses, body.warmRows );
        line( text );
        std::snprintf( text, sizeof( text ), "Correction sum %.4g / max %.4g", body.correctionTotal, body.correctionMax );
        line( text );
        std::snprintf( text, sizeof( text ), "Convergence: %d samples / %llu dropped", body.convergenceCount, static_cast<unsigned long long>( body.droppedIterations ) );
        line( text );
        if ( body.convergenceCount == 0 )
        {
            line( "Convergence unavailable for this tick." );
            return;
        }
        line( "Max row impulse delta squared (not energy)" );
        float maximum = 0;
        for ( int i = 0; i < body.convergenceCount; ++i )
        {
            maximum = (std::max)( maximum, body.convergence[i] );
        }
        for ( int i = 0; i < body.convergenceCount; ++i )
        {
            const float width = bounds.w / body.convergenceCount;
            const float height = maximum > 0 ? 100 * body.convergence[i] / maximum : 0;
            draw.Rect( bounds.x + i * width, y + 100 - height, (std::max)( 1.0f, width - 1 ), height, .3f, .7f, 1, 1 );
        }
        return;
    }
    if ( body.historicalContext )
    {
        line( "Live snapshot; historical evidence is in Causal." );
    }
    if ( !body.selected )
    {
        line( "Select a body with the existing editor picker." );
        return;
    }
    std::snprintf( text, sizeof( text ), "Body %u | %s | %s", body.selectedId, body.fixed ? "Fixed" : "Dynamic", body.awake ? "Awake" : "Sleeping" );
    line( text );
    std::snprintf( text, sizeof( text ), "Authored mass %.6g | inverse %.6g", body.mass, body.inverseMass );
    line( text );
    std::snprintf( text, sizeof( text ), "Shape: %s", body.shape == 0 ? "Sphere" : body.shape == 1 ? "Box" : "Convex hull" );
    line( text );
    std::snprintf( text, sizeof( text ), "Orientation xyzw: %.3g %.3g %.3g %.3g", body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
    line( text );
    if ( body.volume > 0 )
    {
        std::snprintf( text, sizeof( text ), "Volume %.4g | derived density %.4g", body.volume, body.density );
        line( text );
    }
    const std::array<float, 3> vectors[] = { body.origin, body.center, body.velocity, body.angularVelocity, body.inertia, body.inertiaProducts };
    const char* labels[] = { "Authored origin", "World COM", "Velocity", "Angular velocity", "Inertia diagonal", "Inertia products" };
    for ( int i = 0; i < 6; ++i )
    {
        std::snprintf( text, sizeof( text ), "%s: %.4g, %.4g, %.4g", labels[i], vectors[i][0], vectors[i][1], vectors[i][2] );
        line( text );
    }
    std::snprintf( text, sizeof( text ), "Restitution %.4g | material friction %.4g", body.restitution, body.friction );
    line( text );
    line( "Contacts: engine impulse units; body B 0 = terrain" );
    for ( int i = 0; i < body.contactCount; ++i )
    {
        const auto& c = body.contacts[i];
        std::snprintf( text, sizeof( text ), "%u / %u | feature %u | %s", c.bodyA, c.bodyB, c.feature, c.warmStarted ? "warm" : "cold" );
        line( text );
        std::snprintf( text, sizeof( text ), "Impulse N %.4g / T %.4g, %.4g", c.normalImpulse, c.tangentImpulse1, c.tangentImpulse2 );
        line( text );
        std::snprintf( text, sizeof( text ), "Effective mass N %.4g / T %.4g, %.4g", c.normalMass, c.tangentMass1, c.tangentMass2 );
        line( text );
        if ( c.frictionLimit >= 0 )
        {
            std::snprintf( text, sizeof( text ), "Penetration %.4g / friction limit %.4g", c.penetration, c.frictionLimit );
        }
        else
        {
            std::snprintf( text, sizeof( text ), "Penetration %.4g / friction limit unavailable", c.penetration );
        }
        line( text );
    }
    if ( body.droppedContacts > 0 )
    {
        std::snprintf( text, sizeof( text ), "%d more contacts omitted", body.droppedContacts );
        line( text );
    }
}

void Draw( UIPhysicsTabState& state,
           const UIDrawContext& draw,
           const UIPhysicsTabFrameView& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int activeSlider,
           int mouseX,
           int mouseY )
{
    SetContentBounds( state, contentX, scrolledY + 42, contentW );
    state.massEditable = data.inspector.massEditable;
    state.selectedBody = data.inspector.selectedId;
    state.selectedCenter = data.inspector.center;
    state.impulseEditable = data.inspector.selected && !data.inspector.fixed && data.inspector.liveEditable;
    if ( state.pointImpulse.sceneObjectId != state.selectedBody )
    {
        state.pointImpulse = {};
        state.pointImpulse.sceneObjectId = state.selectedBody;
    }
    if ( state.section == 2 && !data.inspector.selected )
    {
        draw.Text( contentX, contentY + 12, 11, 1, 1, 1, "Select a body to inspect or apply an impulse." );
        return;
    }
    if ( state.section >= 2 )
    {
        if ( state.section == 2 )
        {
            const float mass = activeSlider == SLIDER_PHYSICS_BASE + 50 ? state.massPreview : (std::max)( .001f, data.inspector.mass );
            char massText[64] {};
            std::snprintf( massText, sizeof( massText ), "%.6g", mass );
            state.massSlider.Draw( draw, "Mass (log scale)", massText, std::log10( mass ), -3, 6 );
            const auto& palette = Style::Palette();
            draw.RoundedPanel( state.undoMassButton, 3, palette.control, palette.border );
            draw.RoundedPanel( state.redoMassButton, 3, palette.control, palette.border );
            draw.RoundedPanel( state.saveBodyButton, 3, palette.control, palette.border );
            draw.Text( contentX + 5, scrolledY + 58, 11, 1, 1, 1, "Undo" );
            draw.Text( contentX + contentW / 2 + 5, scrolledY + 58, 11, 1, 1, 1, "Redo" );
            draw.Text( contentX + 5, scrolledY + 90, 11, 1, 1, 1, "Save authored scene" );
            draw.Text( contentX, scrolledY + 120, 10, 1, 1, 1, data.inspector.massEditable ? "Uniform density; inertia scales with mass." : "Mass edit: standalone dynamic body in Edit." );
            draw.Text( contentX, scrolledY + 138, 10, 1, 1, 1, data.inspector.settingsNotice.data() );
            draw.Text( contentX, scrolledY + 158, 11, 1, 1, 1, "Test impulse (engine impulse units)" );
            draw.RoundedPanel( state.impulseSpaceButton, 3, palette.control, palette.border );
            draw.Text( contentX + 5, scrolledY + 182, 11, 1, 1, 1, state.pointImpulse.local ? "Local: vector + point relative to COM" : "World: vector + absolute point" );
            const char* names[] = { "Impulse X", "Impulse Y", "Impulse Z", "Point X", "Point Y", "Point Z" };
            for ( int i = 0; i < 6; ++i )
            {
                const int axis = i % 3;
                const float value = ( i < 3 ? state.pointImpulse.impulse : state.pointImpulse.point )[axis];
                const float center = i >= 3 && !state.pointImpulse.local ? state.selectedCenter[axis] : 0;
                const float radius = i < 3 ? 100.0f : 10.0f;
                char text[48] {};
                std::snprintf( text, sizeof( text ), "%.2f", value );
                state.impulseSliders[i].Draw( draw, names[i], text, value, center - radius, center + radius );
            }
            draw.RoundedPanel( state.centerImpulseButton, 3, palette.control, palette.border );
            draw.Text( contentX + 5, scrolledY + 486, 11, 1, 1, 1, "Set point to COM" );
            draw.RoundedPanel( state.applyImpulseButton, 3, palette.control, palette.border );
            draw.Text( contentX + 5, scrolledY + 518, 11, 1, 1, 1, state.impulseEditable ? "Apply once on next fixed tick" : "Impulse unavailable for fixed/missing body" );
            draw.Text( contentX, scrolledY + 548, 10, 1, .8f, .3f, "Yellow lever arm; cyan test impulse (0.1 scale)." );
        }
        DrawInspector( draw, data.inspector, { contentX, scrolledY + ( state.section == 2 ? 578 : 0 ), contentW, contentH }, state.section == 3 );
        return;
    }

    const bool values[] = { data.physicsDebug.collisionVisualizer,
                            data.physicsDebug.axes,
                            data.physicsDebug.contacts,
                            data.physicsDebug.sleep,
                            data.physicsDebug.transparent,
                            data.physicsDebug.broadphase,
                            data.physicsSleepEnabled,
                            data.physicsDebug.pipeline,
                            data.physicsDebug.terrainContact,
                            data.tornadoEnabled,
                            data.tornadoVisualShell,
                            data.tornadoFieldVectors,
                            data.rayCastVisualization };
    const char* labels[] = { "Collider state",
                             "Body axes",
                             "Contact points",
                             "Sleep state",
                             "Transparent",
                             "Broadphase grid",
                             "Sleep policy",
                             "Pipeline",
                             "Terrain probe",
                             "Tornado",
                             "Tornado shell",
                             "Field vectors",
                             "Ray visual" };
    for ( int i = 0; i < 13; ++i )
    {
        if ( state.toggles[i].Bounds().w > 0 )
        {
            DrawContentToggle( draw, contentY, contentH, state.toggles[i], contentX, state.toggles[i].Bounds().y, contentW, labels[i], values[i] );
        }
    }
    struct SliderValue
    {
        const char* label;
        float value;
        float minimum;
        float maximum;
    };
    const SliderValue sliders[] = { { "Body alpha",
                                      activeSlider == SLIDER_ALPHA && state.previewAlpha >= 0 ? state.previewAlpha : data.physicsDebug.alpha,
                                      UI_PHYSICS_ALPHA_MIN,
                                      UI_PHYSICS_ALPHA_MAX },
                                    { "Contact linger",
                                                                activeSlider == SLIDER_CONTACT_LINGER && state.previewContactLinger >= 0 ? state.previewContactLinger : data.physicsDebug.contactLinger,
                                                                UI_CONTACT_LINGER_MIN,
                                                                UI_CONTACT_LINGER_MAX },
                                    { "Ray impulse",
                                                                                           activeSlider == SLIDER_RAY_IMPULSE && state.previewRayImpulse >= 0 ? state.previewRayImpulse : data.rayCastImpulseStrength,
                                                                                           UI_RAY_IMPULSE_MIN,
                                                                                           UI_RAY_IMPULSE_MAX },
                                    { "Projectile speed",
                                                                                                                   activeSlider == SLIDER_LAUNCHER_PROJECTILE_SPEED && state.previewLauncherProjectileSpeed >= 0 ? state.previewLauncherProjectileSpeed : data.launcherProjectileSpeed,
                                                                                                                   UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                                                                                   UI_LAUNCHER_PROJECTILE_SPEED_MAX },
                                    { "Gravity",
                                                                                                                                                         activeSlider == SLIDER_WORLD_GRAVITY && state.previewWorldGravity >= 0 ? state.previewWorldGravity : GravityStrengthFromWorld( data.worldGravity ),
                                                                                                                                                         UI_WORLD_GRAVITY_MIN,
                                                                                                                                                         UI_WORLD_GRAVITY_MAX },
                                    { "Terrain friction",
                                                                                                                                                                                   activeSlider == SLIDER_TERRAIN_FRICTION && state.previewTerrainFriction >= 0 ? state.previewTerrainFriction : data.terrainFrictionCoeff,
                                                                                                                                                                                   UI_FRICTION_COEFF_MIN,
                                                                                                                                                                                   UI_FRICTION_COEFF_MAX },
                                    { "Object friction",
                                                                                                                                                                                                              activeSlider == SLIDER_OBJECT_FRICTION && state.previewObjectFriction >= 0 ? state.previewObjectFriction : data.objectFrictionCoeff,
                                                                                                                                                                                                              UI_FRICTION_COEFF_MIN,
                                                                                                                                                                                                              UI_FRICTION_COEFF_MAX },
                                    { "Rolling friction",
                                                                                                                                                                                                                                         activeSlider == SLIDER_ROLLING_FRICTION && state.previewRollingFriction >= 0 ? state.previewRollingFriction : data.rollingFrictionCoeff,
                                                                                                                                                                                                                                         UI_ROLLING_FRICTION_COEFF_MIN,
                                                                                                                                                                                                                                         UI_ROLLING_FRICTION_COEFF_MAX },
                                    { "Tornado radius",
                                                                                                                                                                                                                                                                            activeSlider == SLIDER_TORNADO_RADIUS && state.previewTornadoRadius >= 0 ? state.previewTornadoRadius : data.tornadoRadius,
                                                                                                                                                                                                                                                                            UI_TORNADO_RADIUS_MIN,
                                                                                                                                                                                                                                                                            UI_TORNADO_RADIUS_MAX },
                                    { "Tornado height",
                                                                                                                                                                                                                                                                                                       activeSlider == SLIDER_TORNADO_HEIGHT && state.previewTornadoHeight >= 0 ? state.previewTornadoHeight : data.tornadoHeight,
                                                                                                                                                                                                                                                                                                       UI_TORNADO_HEIGHT_MIN,
                                                                                                                                                                                                                                                                                                       UI_TORNADO_HEIGHT_MAX },
                                    { "Inward acceleration",
                                                                                                                                                                                                                                                                                                                                  activeSlider == SLIDER_TORNADO_INWARD && state.previewTornadoInward >= 0 ? state.previewTornadoInward : data.tornadoInwardAcceleration,
                                                                                                                                                                                                                                                                                                                                  UI_TORNADO_INWARD_MIN,
                                                                                                                                                                                                                                                                                                                                  UI_TORNADO_INWARD_MAX },
                                    { "Swirl acceleration",
                                                                                                                                                                                                                                                                                                                                                             activeSlider == SLIDER_TORNADO_SWIRL && state.previewTornadoSwirl >= 0 ? state.previewTornadoSwirl : data.tornadoSwirlAcceleration,
                                                                                                                                                                                                                                                                                                                                                             UI_TORNADO_SWIRL_MIN,
                                                                                                                                                                                                                                                                                                                                                             UI_TORNADO_SWIRL_MAX },
                                    { "Lift acceleration",
                                                                                                                                                                                                                                                                                                                                                                                       activeSlider == SLIDER_TORNADO_LIFT && state.previewTornadoLift >= 0 ? state.previewTornadoLift : data.tornadoLiftAcceleration,
                                                                                                                                                                                                                                                                                                                                                                                       UI_TORNADO_LIFT_MIN,
                                                                                                                                                                                                                                                                                                                                                                                       UI_TORNADO_LIFT_MAX }, };
    char text[128] {};
    for ( int i = 0; i < 13; ++i )
    {
        if ( state.section != SLIDERS[i].section || !IsRowVisible( contentY, contentH, scrolledY + SLIDERS[i].y + ( state.section == 0 ? 240 : 0 ), 34 ) )
        {
            continue;
        }
        std::snprintf( text, sizeof( text ), "%.3g", sliders[i].value );
        ( state.*SLIDERS[i].widget ).Draw( draw, sliders[i].label, text, sliders[i].value, sliders[i].minimum, sliders[i].maximum );
    }
    if ( state.section == 0 )
    {
        const auto& palette = Style::Palette();
        draw.RoundedPanel( state.restoreStartupButton, 3, palette.control, palette.border );
        draw.RoundedPanel( state.saveDefaultsButton, 3, palette.control, palette.border );
        draw.Text( contentX + 5, state.restoreStartupButton.y + 6, 11, 1, 1, 1, "Restore startup Physics" );
        draw.Text( contentX + 5, state.saveDefaultsButton.y + 6, 11, 1, 1, 1, "Save Physics defaults" );
        draw.Text( contentX, state.saveDefaultsButton.y + 40, 10, 1, 1, 1, data.inspector.settingsNotice.data() );
        const char* numericalLabels[] = { "Contact iterations",
                                          "Object slop",
                                          "Object bias",
                                          "Position correction",
                                          "Terrain slop",
                                          "Terrain bias",
                                          "Terrain max bias",
                                          "Spin friction length",
                                          "Restitution threshold",
                                          "Sleep linear speed",
                                          "Sleep angular speed",
                                          "Sleep ticks",
                                          "Warm start (0/1)" };
        for ( int i = 0; i < 4; ++i )
        {
            const auto bounds = state.hzButtons[i];
            const bool selected = data.inspector.physicsHz == ( 30 << i );
            draw.RoundedPanel( bounds, 3, selected ? palette.selection : palette.control, palette.border );
            std::snprintf( text, sizeof( text ), "%d Hz", 30 << i );
            draw.Text( bounds.x + 4, bounds.y + 4, 10, 1, 1, 1, text );
        }
        draw.Text( contentX, scrolledY + 242, 10, 1, 1, 1, data.inspector.liveEditable ? "Edits apply on release; new recording." : "Live values; inspection is read-only." );
        draw.RoundedPanel( state.advancedButton, 3, palette.control, palette.border );
        draw.RoundedPanel( state.tornadoButton, 3, palette.control, palette.border );
        draw.Text( contentX + 5, state.advancedButton.y + 6, 11, 1, 1, 1, state.advancedOpen ? "- Advanced material / terrain / sleep" : "+ Advanced material / terrain / sleep" );
        draw.Text( contentX + 5, state.tornadoButton.y + 6, 11, 1, 1, 1, state.tornadoOpen ? "- Tornado" : "+ Tornado" );
        const float scale = activeSlider == SLIDER_PHYSICS_BASE + 70 ? state.timeScalePreview : data.inspector.timeScale;
        std::snprintf( text, sizeof( text ), "%.2fx", scale );
        state.timeScaleSlider.Draw( draw, "Time scale", text, scale, OperatorControlPolicy::UI_TIME_SCALE_MIN, OperatorControlPolicy::UI_TIME_SCALE_MAX );
        for ( int i = 0; i < 13; ++i )
        {
            if ( state.numericalSliders[i].Bounds().w <= 0 )
            {
                continue;
            }
            const auto range = Physics::INTERACTIVE_PHYSICS_RANGES[i];
            const float value = activeSlider == SLIDER_PHYSICS_BASE + 20 + i ? state.numericalPreviews[i] : data.inspector.settings[i];
            std::snprintf( text, sizeof( text ), "%.4g", value );
            state.numericalSliders[i].Draw( draw, numericalLabels[i], text, value, range.minimum, range.maximum );
        }
    }
    if ( state.section == 1 )
    {
        const char* layerLabels[] = { "Normal direction",
                                      "Impulse arrows",
                                      "Friction impulses",
                                      "Mass labels / center of mass",
                                      "Body AABBs",
                                      "Joint anchors / links",
                                      "Joint error",
                                      "Velocity vectors",
                                      "Selected body only",
                                      "Collider wireframes" };
        for ( int i = 0; i < 10; ++i )
        {
            DrawContentToggle( draw, contentY, contentH, state.additionalLayers[i], contentX, scrolledY + LayerY( i ), contentW, layerLabels[i], data.physicsDebug.additionalLayers[i] );
        }
        draw.Text( contentX, scrolledY + 844, 10, 1, 1, 1, "Cyan: direction | green: normal impulse" );
        draw.Text( contentX, scrolledY + 862, 10, 1, 1, 1, "Orange: friction | faded: contact history" );
        draw.Text( contentX, scrolledY + 880, 10, 1, 1, 1, "Impulse vectors: +B / -A (terrain B=0)" );
        draw.Text( contentX, scrolledY + 898, 10, 1, 1, 1, "Cap 16 world units (red tip); values unchanged" );
        std::snprintf( text, sizeof( text ), "%.3g", data.physicsDebug.impulseScale );
        state.impulseScaleSlider.Draw( draw, "Impulse scale", text, data.physicsDebug.impulseScale, .001f, 2 );
        std::snprintf( text, sizeof( text ), "%.3g", data.physicsDebug.impulseThreshold );
        state.impulseThresholdSlider.Draw( draw, "Impulse threshold", text, data.physicsDebug.impulseThreshold, 0, 100 );
        std::snprintf( text, sizeof( text ), "Lines %zu / 73728 | dropped %zu", data.inspector.geometry[0], data.inspector.geometry[1] );
        draw.Text( contentX, scrolledY + 1020, 10, 1, 1, 1, text );
        std::snprintf( text, sizeof( text ), "History dropped %zu | capped arrows %zu", data.inspector.geometry[2], data.inspector.geometry[3] );
        draw.Text( contentX, scrolledY + 1040, 10, 1, 1, 1, text );
        std::snprintf( text, sizeof( text ), "Mass labels %zu / 32 | omitted %zu", data.inspector.geometry[4], data.inspector.geometry[5] );
        draw.Text( contentX, scrolledY + 1060, 10, 1, 1, 1, text );
        draw.Text( contentX, scrolledY + 1080, 10, 1, 1, 1, "Mass labels show body id and kilograms." );
        DrawPipelineStepButton( draw, state.pipelinePrevButton, true, state.pipelinePrevButton.Contains( mouseX, mouseY ) );
        DrawPipelineStepButton( draw, state.pipelineNextButton, false, state.pipelineNextButton.Contains( mouseX, mouseY ) );
        std::snprintf( text, sizeof( text ), "Stage %d/%d: %s", data.physicsDebug.pipelineStageIndex + 1, data.physicsDebug.pipelineStageCount, data.physicsDebug.pipelineStageName );
        draw.Text( contentX, scrolledY + 495, 10, 1, 1, 1, text );
    }
}

} // namespace PhysicsTab
} // namespace UI
} // namespace SkullbonezCore
