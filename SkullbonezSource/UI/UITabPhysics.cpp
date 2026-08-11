/*
File: SkullbonezSource/UI/UITabPhysics.cpp
Purpose:
  Implements UI TabPhysics widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  Maps physics toggles and parameter sliders to
  typed one-frame commands with preview/commit state.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UITabPhysics.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UITabPhysics.h"

#include "UI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"
#include "UIStyle.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

constexpr float PHYSICS_FIRST_TOGGLE_Y = 42.0f;
constexpr float PHYSICS_PIPELINE_BUTTON_Y = 254.0f;
constexpr float PHYSICS_ALPHA_SLIDER_Y = 302.0f;
constexpr float PHYSICS_CONTACT_LINGER_SLIDER_Y = 350.0f;
constexpr float PHYSICS_RAY_SECTION_Y = 390.0f;
constexpr float PHYSICS_RAY_IMPULSE_SLIDER_Y = 414.0f;
constexpr float PHYSICS_LAUNCHER_PROJECTILE_SPEED_SLIDER_Y = 454.0f;
constexpr float PHYSICS_WORLD_SECTION_Y = 500.0f;
constexpr float PHYSICS_WORLD_GRAVITY_SLIDER_Y = 526.0f;
constexpr float PHYSICS_FRICTION_SECTION_Y = 586.0f;
constexpr float PHYSICS_TERRAIN_FRICTION_SLIDER_Y = 612.0f;
constexpr float PHYSICS_OBJECT_FRICTION_SLIDER_Y = 652.0f;
constexpr float PHYSICS_ROLLING_FRICTION_SLIDER_Y = 692.0f;
constexpr float PHYSICS_TORNADO_SECTION_Y = 752.0f;
constexpr float PHYSICS_TORNADO_RADIUS_SLIDER_Y = 778.0f;
constexpr float PHYSICS_TORNADO_HEIGHT_SLIDER_Y = 818.0f;
constexpr float PHYSICS_TORNADO_INWARD_SLIDER_Y = 858.0f;
constexpr float PHYSICS_TORNADO_SWIRL_SLIDER_Y = 898.0f;
constexpr float PHYSICS_TORNADO_LIFT_SLIDER_Y = 938.0f;

void SetToggleBounds( SkullbonezCore::UI::PhysicsTab::UIPhysicsTabState& state, int index, int row, int column, float col1,
                      float col2, float rowBase, float colW )
{
    const float tx = column == 0 ? col1 : col2;
    state.toggles[index].SetBounds( tx, rowBase + static_cast<float>( row ) * CONTENT_TOGGLE_ROW_H, colW, 24.0f );
}

void SetContentBounds( SkullbonezCore::UI::PhysicsTab::UIPhysicsTabState& state, float contentX, float firstToggleY,
                       float contentW )
{

    // Invariant: Physics tab draw and hit testing both use this layout helper,
    // so moving a row here moves the visible control and its interaction box.
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const float col1 = contentX;
    const float col2 = contentX + colW + 18.0f;
    const float contentBaseY = firstToggleY - PHYSICS_FIRST_TOGGLE_Y;
    SetToggleBounds( state, 0, 0, 0, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 4, 1, 0, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 5, 2, 0, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 7, 3, 0, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 1, 0, 1, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 2, 1, 1, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 3, 2, 1, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 6, 3, 1, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 8, 4, 0, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 9, 4, 1, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 10, 5, 0, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 11, 5, 1, col1, col2, firstToggleY, colW );
    SetToggleBounds( state, 12, 6, 1, col1, col2, firstToggleY, colW );
    SetPipelineStepButtonBounds( state.pipelinePrevButton, state.pipelineNextButton, contentX, contentW,
                                 contentBaseY + PHYSICS_PIPELINE_BUTTON_Y );

    state.alphaSlider.SetBounds( contentX, contentBaseY + PHYSICS_ALPHA_SLIDER_Y, contentW, 34.0f );
    state.contactLingerSlider.SetBounds( contentX, contentBaseY + PHYSICS_CONTACT_LINGER_SLIDER_Y, contentW, 34.0f );
    state.rayImpulseSlider.SetBounds( contentX, contentBaseY + PHYSICS_RAY_IMPULSE_SLIDER_Y, contentW, 34.0f );
    state.launcherProjectileSpeedSlider.SetBounds( contentX, contentBaseY + PHYSICS_LAUNCHER_PROJECTILE_SPEED_SLIDER_Y,
                                                   contentW, 34.0f );

    state.worldGravitySlider.SetBounds( contentX, contentBaseY + PHYSICS_WORLD_GRAVITY_SLIDER_Y, contentW, 34.0f );
    state.terrainFrictionSlider.SetBounds( contentX, contentBaseY + PHYSICS_TERRAIN_FRICTION_SLIDER_Y, contentW, 34.0f );

    state.objectFrictionSlider.SetBounds( contentX, contentBaseY + PHYSICS_OBJECT_FRICTION_SLIDER_Y, contentW, 34.0f );
    state.rollingFrictionSlider.SetBounds( contentX, contentBaseY + PHYSICS_ROLLING_FRICTION_SLIDER_Y, contentW, 34.0f );

    state.tornadoRadiusSlider.SetBounds( contentX, contentBaseY + PHYSICS_TORNADO_RADIUS_SLIDER_Y, contentW, 34.0f );
    state.tornadoHeightSlider.SetBounds( contentX, contentBaseY + PHYSICS_TORNADO_HEIGHT_SLIDER_Y, contentW, 34.0f );
    state.tornadoInwardSlider.SetBounds( contentX, contentBaseY + PHYSICS_TORNADO_INWARD_SLIDER_Y, contentW, 34.0f );
    state.tornadoSwirlSlider.SetBounds( contentX, contentBaseY + PHYSICS_TORNADO_SWIRL_SLIDER_Y, contentW, 34.0f );
    state.tornadoLiftSlider.SetBounds( contentX, contentBaseY + PHYSICS_TORNADO_LIFT_SLIDER_Y, contentW, 34.0f );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace PhysicsTab
{

int ContentHeight()
{
    return 990;
}


void ResetPreviewState( UIPhysicsTabState& state )
{

    // Concept: Preview values are transient drag feedback. Runtime settings
    // change only after command handling applies the result.
    state.previewAlpha = -1.0f;
    state.previewContactLinger = -1.0f;
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


bool HandleContentClick( UIPhysicsTabState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY,
                         float contentX, float rowBase, float contentW )
{

    // Invariant: This function emits commands only. Physics debug state and
    // world forces are mutated by runtime command handling.
    SetContentBounds( state, contentX, rowBase, contentW );

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
        state.previewAlpha = state.alphaSlider.ValueFromMouse( mouseX, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX,
                                                               UI_PHYSICS_ALPHA_STEP );

        result.commands.physics.requestedPhysicsDebugAlpha = state.previewAlpha;
        return true;
    }
    else if ( state.contactLingerSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_CONTACT_LINGER;
        state.previewContactLinger = state.contactLingerSlider.ValueFromMouse( mouseX, UI_CONTACT_LINGER_MIN,
                                                                               UI_CONTACT_LINGER_MAX,
                                                                               UI_CONTACT_LINGER_STEP );

        result.commands.physics.requestedPhysicsDebugContactLinger = state.previewContactLinger;
        return true;
    }
    else if ( state.rayImpulseSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_RAY_IMPULSE;
        state.previewRayImpulse = state.rayImpulseSlider.ValueFromMouse( mouseX, UI_RAY_IMPULSE_MIN, UI_RAY_IMPULSE_MAX,
                                                                         UI_RAY_IMPULSE_STEP );

        result.commands.physics.requestRayCastImpulseStrength = true;
        result.commands.physics.requestedRayCastImpulseStrength = state.previewRayImpulse;
        return true;
    }
    else if ( state.launcherProjectileSpeedSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_LAUNCHER_PROJECTILE_SPEED;
        state.previewLauncherProjectileSpeed = state.launcherProjectileSpeedSlider
                                                   .ValueFromMouse( mouseX, UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                                    UI_LAUNCHER_PROJECTILE_SPEED_MAX,
                                                                    UI_LAUNCHER_PROJECTILE_SPEED_STEP );

        result.commands.physics.requestLauncherProjectileSpeed = true;
        result.commands.physics.requestedLauncherProjectileSpeed = state.previewLauncherProjectileSpeed;
        return true;
    }
    else if ( state.worldGravitySlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_WORLD_GRAVITY;
        result.commands.water.requestWorldGravity = true;
        result.commands.water.requestedWorldGravity = WorldGravityFromStrength( state.worldGravitySlider.ValueFromMouse( mouseX, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX,
                                                                                                                         UI_WORLD_GRAVITY_STEP ) );

        return true;
    }
    else if ( state.terrainFrictionSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TERRAIN_FRICTION;
        state.previewTerrainFriction = state.terrainFrictionSlider.ValueFromMouse( mouseX, UI_FRICTION_COEFF_MIN,
                                                                                   UI_FRICTION_COEFF_MAX,
                                                                                   UI_FRICTION_COEFF_STEP );

        result.commands.physics.requestTerrainFrictionCoeff = true;
        result.commands.physics.requestedTerrainFrictionCoeff = state.previewTerrainFriction;
        return true;
    }
    else if ( state.objectFrictionSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_OBJECT_FRICTION;
        state.previewObjectFriction = state.objectFrictionSlider.ValueFromMouse( mouseX, UI_FRICTION_COEFF_MIN,
                                                                                 UI_FRICTION_COEFF_MAX,
                                                                                 UI_FRICTION_COEFF_STEP );

        result.commands.physics.requestObjectFrictionCoeff = true;
        result.commands.physics.requestedObjectFrictionCoeff = state.previewObjectFriction;
        return true;
    }
    else if ( state.rollingFrictionSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_ROLLING_FRICTION;
        state.previewRollingFriction = state.rollingFrictionSlider.ValueFromMouse( mouseX, UI_ROLLING_FRICTION_COEFF_MIN,
                                                                                   UI_ROLLING_FRICTION_COEFF_MAX,
                                                                                   UI_ROLLING_FRICTION_COEFF_STEP );

        result.commands.physics.requestRollingFrictionCoeff = true;
        result.commands.physics.requestedRollingFrictionCoeff = state.previewRollingFriction;
        return true;
    }
    else if ( state.tornadoRadiusSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_RADIUS;
        state.previewTornadoRadius = state.tornadoRadiusSlider.ValueFromMouse( mouseX, UI_TORNADO_RADIUS_MIN,
                                                                               UI_TORNADO_RADIUS_MAX,
                                                                               UI_TORNADO_RADIUS_STEP );

        result.commands.physics.requestTornadoRadius = true;
        result.commands.physics.requestedTornadoRadius = state.previewTornadoRadius;
        return true;
    }
    else if ( state.tornadoHeightSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_HEIGHT;
        state.previewTornadoHeight = state.tornadoHeightSlider.ValueFromMouse( mouseX, UI_TORNADO_HEIGHT_MIN,
                                                                               UI_TORNADO_HEIGHT_MAX,
                                                                               UI_TORNADO_HEIGHT_STEP );

        result.commands.physics.requestTornadoHeight = true;
        result.commands.physics.requestedTornadoHeight = state.previewTornadoHeight;
        return true;
    }
    else if ( state.tornadoInwardSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_INWARD;
        state.previewTornadoInward = state.tornadoInwardSlider.ValueFromMouse( mouseX, UI_TORNADO_INWARD_MIN,
                                                                               UI_TORNADO_INWARD_MAX,
                                                                               UI_TORNADO_INWARD_STEP );

        result.commands.physics.requestTornadoInward = true;
        result.commands.physics.requestedTornadoInward = state.previewTornadoInward;
        return true;
    }
    else if ( state.tornadoSwirlSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_SWIRL;
        state.previewTornadoSwirl = state.tornadoSwirlSlider.ValueFromMouse( mouseX, UI_TORNADO_SWIRL_MIN,
                                                                             UI_TORNADO_SWIRL_MAX, UI_TORNADO_SWIRL_STEP );

        result.commands.physics.requestTornadoSwirl = true;
        result.commands.physics.requestedTornadoSwirl = state.previewTornadoSwirl;
        return true;
    }
    else if ( state.tornadoLiftSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TORNADO_LIFT;
        state.previewTornadoLift = state.tornadoLiftSlider.ValueFromMouse( mouseX, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX,
                                                                           UI_TORNADO_LIFT_STEP );

        result.commands.physics.requestTornadoLift = true;
        result.commands.physics.requestedTornadoLift = state.previewTornadoLift;
        return true;
    }

    return false;
}


bool UpdateActiveSlider( UIPhysicsTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    if ( activeSlider == SLIDER_ALPHA )
    {
        state.previewAlpha = state.alphaSlider.ValueFromMouse( mouseX, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX,
                                                               UI_PHYSICS_ALPHA_STEP );

        result.commands.physics.requestedPhysicsDebugAlpha = state.previewAlpha;
        return true;
    }

    if ( activeSlider == SLIDER_CONTACT_LINGER )
    {
        state.previewContactLinger = state.contactLingerSlider.ValueFromMouse( mouseX, UI_CONTACT_LINGER_MIN,
                                                                               UI_CONTACT_LINGER_MAX,
                                                                               UI_CONTACT_LINGER_STEP );

        result.commands.physics.requestedPhysicsDebugContactLinger = state.previewContactLinger;
        return true;
    }

    if ( activeSlider == SLIDER_RAY_IMPULSE )
    {
        state.previewRayImpulse = state.rayImpulseSlider.ValueFromMouse( mouseX, UI_RAY_IMPULSE_MIN, UI_RAY_IMPULSE_MAX,
                                                                         UI_RAY_IMPULSE_STEP );

        result.commands.physics.requestRayCastImpulseStrength = true;
        result.commands.physics.requestedRayCastImpulseStrength = state.previewRayImpulse;
        return true;
    }

    if ( activeSlider == SLIDER_LAUNCHER_PROJECTILE_SPEED )
    {
        state.previewLauncherProjectileSpeed = state.launcherProjectileSpeedSlider
                                                   .ValueFromMouse( mouseX, UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                                    UI_LAUNCHER_PROJECTILE_SPEED_MAX,
                                                                    UI_LAUNCHER_PROJECTILE_SPEED_STEP );

        result.commands.physics.requestLauncherProjectileSpeed = true;
        result.commands.physics.requestedLauncherProjectileSpeed = state.previewLauncherProjectileSpeed;
        return true;
    }

    if ( activeSlider == SLIDER_WORLD_GRAVITY )
    {
        result.commands.water.requestWorldGravity = true;
        result.commands.water.requestedWorldGravity = WorldGravityFromStrength( state.worldGravitySlider.ValueFromMouse( mouseX, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX,
                                                                                                                         UI_WORLD_GRAVITY_STEP ) );

        return true;
    }

    if ( activeSlider == SLIDER_TERRAIN_FRICTION )
    {
        state.previewTerrainFriction = state.terrainFrictionSlider.ValueFromMouse( mouseX, UI_FRICTION_COEFF_MIN,
                                                                                   UI_FRICTION_COEFF_MAX,
                                                                                   UI_FRICTION_COEFF_STEP );

        result.commands.physics.requestTerrainFrictionCoeff = true;
        result.commands.physics.requestedTerrainFrictionCoeff = state.previewTerrainFriction;
        return true;
    }

    if ( activeSlider == SLIDER_OBJECT_FRICTION )
    {
        state.previewObjectFriction = state.objectFrictionSlider.ValueFromMouse( mouseX, UI_FRICTION_COEFF_MIN,
                                                                                 UI_FRICTION_COEFF_MAX,
                                                                                 UI_FRICTION_COEFF_STEP );

        result.commands.physics.requestObjectFrictionCoeff = true;
        result.commands.physics.requestedObjectFrictionCoeff = state.previewObjectFriction;
        return true;
    }

    if ( activeSlider == SLIDER_ROLLING_FRICTION )
    {
        state.previewRollingFriction = state.rollingFrictionSlider.ValueFromMouse( mouseX, UI_ROLLING_FRICTION_COEFF_MIN,
                                                                                   UI_ROLLING_FRICTION_COEFF_MAX,
                                                                                   UI_ROLLING_FRICTION_COEFF_STEP );

        result.commands.physics.requestRollingFrictionCoeff = true;
        result.commands.physics.requestedRollingFrictionCoeff = state.previewRollingFriction;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_RADIUS )
    {
        state.previewTornadoRadius = state.tornadoRadiusSlider.ValueFromMouse( mouseX, UI_TORNADO_RADIUS_MIN,
                                                                               UI_TORNADO_RADIUS_MAX,
                                                                               UI_TORNADO_RADIUS_STEP );

        result.commands.physics.requestTornadoRadius = true;
        result.commands.physics.requestedTornadoRadius = state.previewTornadoRadius;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_HEIGHT )
    {
        state.previewTornadoHeight = state.tornadoHeightSlider.ValueFromMouse( mouseX, UI_TORNADO_HEIGHT_MIN,
                                                                               UI_TORNADO_HEIGHT_MAX,
                                                                               UI_TORNADO_HEIGHT_STEP );

        result.commands.physics.requestTornadoHeight = true;
        result.commands.physics.requestedTornadoHeight = state.previewTornadoHeight;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_INWARD )
    {
        state.previewTornadoInward = state.tornadoInwardSlider.ValueFromMouse( mouseX, UI_TORNADO_INWARD_MIN,
                                                                               UI_TORNADO_INWARD_MAX,
                                                                               UI_TORNADO_INWARD_STEP );

        result.commands.physics.requestTornadoInward = true;
        result.commands.physics.requestedTornadoInward = state.previewTornadoInward;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_SWIRL )
    {
        state.previewTornadoSwirl = state.tornadoSwirlSlider.ValueFromMouse( mouseX, UI_TORNADO_SWIRL_MIN,
                                                                             UI_TORNADO_SWIRL_MAX, UI_TORNADO_SWIRL_STEP );

        result.commands.physics.requestTornadoSwirl = true;
        result.commands.physics.requestedTornadoSwirl = state.previewTornadoSwirl;
        return true;
    }

    if ( activeSlider == SLIDER_TORNADO_LIFT )
    {
        state.previewTornadoLift = state.tornadoLiftSlider.ValueFromMouse( mouseX, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX,
                                                                           UI_TORNADO_LIFT_STEP );

        result.commands.physics.requestTornadoLift = true;
        result.commands.physics.requestedTornadoLift = state.previewTornadoLift;
        return true;
    }


    return false;
}


bool CommitActiveSlider( UIPhysicsTabState& state, int activeSlider, InGameUIInputResult& result )
{
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


void Draw( UIPhysicsTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data, float contentX,
           float contentY, float contentW, float contentH, float scrolledY, int activeSlider, int mouseX, int mouseY )
{
    char buf[128];
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const float col1 = contentX;
    const float col2 = contentX + colW + 18.0f;
    const float displayAlpha = ( activeSlider == SLIDER_ALPHA && state.previewAlpha >= 0.0f ) ? state.previewAlpha
                                                                                              : data.physicsDebug.alpha;

    const float displayLinger = ( activeSlider == SLIDER_CONTACT_LINGER && state.previewContactLinger >= 0.0f )
                                    ? state.previewContactLinger
                                    : data.physicsDebug.contactLinger;

    const float displayRayImpulse = ( activeSlider == SLIDER_RAY_IMPULSE && state.previewRayImpulse >= 0.0f )
                                        ? state.previewRayImpulse
                                        : data.rayCastImpulseStrength;

    const float displayLauncherProjectileSpeed = ( activeSlider == SLIDER_LAUNCHER_PROJECTILE_SPEED &&
                                                   state.previewLauncherProjectileSpeed >= 0.0f )
                                                     ? state.previewLauncherProjectileSpeed
                                                     : data.launcherProjectileSpeed;

    const float displayTerrainFriction = ( activeSlider == SLIDER_TERRAIN_FRICTION && state.previewTerrainFriction >= 0.0f )
                                             ? state.previewTerrainFriction
                                             : data.terrainFrictionCoeff;

    const float displayObjectFriction = ( activeSlider == SLIDER_OBJECT_FRICTION && state.previewObjectFriction >= 0.0f )
                                            ? state.previewObjectFriction
                                            : data.objectFrictionCoeff;

    const float displayRollingFriction = ( activeSlider == SLIDER_ROLLING_FRICTION && state.previewRollingFriction >= 0.0f )
                                             ? state.previewRollingFriction
                                             : data.rollingFrictionCoeff;

    const float displayTornadoRadius = ( activeSlider == SLIDER_TORNADO_RADIUS && state.previewTornadoRadius >= 0.0f )
                                           ? state.previewTornadoRadius
                                           : data.tornadoRadius;

    const float displayTornadoHeight = ( activeSlider == SLIDER_TORNADO_HEIGHT && state.previewTornadoHeight >= 0.0f )
                                           ? state.previewTornadoHeight
                                           : data.tornadoHeight;

    const float displayTornadoInward = ( activeSlider == SLIDER_TORNADO_INWARD && state.previewTornadoInward >= 0.0f )
                                           ? state.previewTornadoInward
                                           : data.tornadoInwardAcceleration;

    const float displayTornadoSwirl = ( activeSlider == SLIDER_TORNADO_SWIRL && state.previewTornadoSwirl >= 0.0f )
                                          ? state.previewTornadoSwirl
                                          : data.tornadoSwirlAcceleration;

    const float displayTornadoLift = ( activeSlider == SLIDER_TORNADO_LIFT && state.previewTornadoLift >= 0.0f )
                                         ? state.previewTornadoLift
                                         : data.tornadoLiftAcceleration;

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Physics Controls" );
    DrawContentToggle( draw, contentY, contentH, state.toggles[0], col1, scrolledY + PHYSICS_FIRST_TOGGLE_Y, colW,
                       "Collision state", data.physicsDebug.collisionVisualizer );

    DrawContentToggle( draw, contentY, contentH, state.toggles[4], col1,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H, colW, "Transparent",
                       data.physicsDebug.transparent );

    DrawContentToggle( draw, contentY, contentH, state.toggles[5], col1,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 2.0f, colW, "Broadphase",
                       data.physicsDebug.broadphase );

    DrawContentToggle( draw, contentY, contentH, state.toggles[7], col1,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 3.0f, colW, "Pipeline",
                       data.physicsDebug.pipeline );

    DrawContentToggle( draw, contentY, contentH, state.toggles[1], col2, scrolledY + PHYSICS_FIRST_TOGGLE_Y, colW, "Axes",
                       data.physicsDebug.axes );

    DrawContentToggle( draw, contentY, contentH, state.toggles[2], col2,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H, colW, "Contacts",
                       data.physicsDebug.contacts );

    DrawContentToggle( draw, contentY, contentH, state.toggles[3], col2,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 2.0f, colW, "Sleep state",
                       data.physicsDebug.sleep );

    DrawContentToggle( draw, contentY, contentH, state.toggles[6], col2,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 3.0f, colW, "Sleep policy",
                       data.physicsSleepEnabled );

    DrawContentToggle( draw, contentY, contentH, state.toggles[8], col1,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 4.0f, colW, "Terrain probe",
                       data.physicsDebug.terrainContact );

    DrawContentToggle( draw, contentY, contentH, state.toggles[9], col2,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 4.0f, colW, "Tornado",
                       data.tornadoEnabled );

    DrawContentToggle( draw, contentY, contentH, state.toggles[10], col1,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 5.0f, colW, "Visual shell",
                       data.tornadoVisualShell );

    DrawContentToggle( draw, contentY, contentH, state.toggles[11], col2,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 5.0f, colW, "Field vectors",
                       data.tornadoFieldVectors );

    DrawContentToggle( draw, contentY, contentH, state.toggles[12], col2,
                       scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 6.0f, colW, "Ray visual",
                       data.rayCastVisualization );

    const Style::UIPalette& palette = Style::Palette();
    snprintf( buf, sizeof( buf ), "0x%04X", data.physicsDebug.activeFlags );
    DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 230.0f, "Debug flags", buf, palette.accentStrong.r,
                      palette.accentStrong.g, palette.accentStrong.b );

    snprintf( buf, sizeof( buf ), "%d/%d %s", data.physicsDebug.pipelineStageIndex + 1, data.physicsDebug.pipelineStageCount,
              data.physicsDebug.pipelineStageName );

    DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 250.0f, "Pipeline stage", buf, palette.accentStrong.r,
                      palette.accentStrong.g, palette.accentStrong.b );

    SetPipelineStepButtonBounds( state.pipelinePrevButton, state.pipelineNextButton, contentX, contentW,
                                 scrolledY + PHYSICS_PIPELINE_BUTTON_Y );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_PIPELINE_BUTTON_Y, UI_PIPELINE_STEP_BUTTON_H ) )
    {
        DrawPipelineStepButton( draw, state.pipelinePrevButton, true, state.pipelinePrevButton.Contains( mouseX, mouseY ) );

        DrawPipelineStepButton( draw, state.pipelineNextButton, false, state.pipelineNextButton.Contains( mouseX, mouseY ) );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + 276.0f, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 276.0f, 12.0f, "Debug Draw" );
    }

    snprintf( buf, sizeof( buf ), "%.2f", displayAlpha );
    state.alphaSlider.SetBounds( contentX, scrolledY + PHYSICS_ALPHA_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_ALPHA_SLIDER_Y, 34.0f ) )
    {
        state.alphaSlider.Draw( draw, "Body alpha", buf, displayAlpha, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX );
    }

    snprintf( buf, sizeof( buf ), "%.2fs", displayLinger );
    state.contactLingerSlider.SetBounds( contentX, scrolledY + PHYSICS_CONTACT_LINGER_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_CONTACT_LINGER_SLIDER_Y, 34.0f ) )
    {
        state.contactLingerSlider.Draw( draw, "Contact linger", buf, displayLinger, UI_CONTACT_LINGER_MIN,
                                        UI_CONTACT_LINGER_MAX );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_RAY_SECTION_Y, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + PHYSICS_RAY_SECTION_Y, 12.0f, "Ray Test" );
    }

    snprintf( buf, sizeof( buf ), "%.0f", displayRayImpulse );
    state.rayImpulseSlider.SetBounds( contentX, scrolledY + PHYSICS_RAY_IMPULSE_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_RAY_IMPULSE_SLIDER_Y, 34.0f ) )
    {
        state.rayImpulseSlider.Draw( draw, "Ray impulse", buf, displayRayImpulse, UI_RAY_IMPULSE_MIN, UI_RAY_IMPULSE_MAX );
    }

    snprintf( buf, sizeof( buf ), "%.0f", displayLauncherProjectileSpeed );
    state.launcherProjectileSpeedSlider.SetBounds( contentX, scrolledY + PHYSICS_LAUNCHER_PROJECTILE_SPEED_SLIDER_Y,
                                                   contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_LAUNCHER_PROJECTILE_SPEED_SLIDER_Y, 34.0f ) )
    {
        state.launcherProjectileSpeedSlider.Draw( draw, "Projectile speed", buf, displayLauncherProjectileSpeed,
                                                  UI_LAUNCHER_PROJECTILE_SPEED_MIN, UI_LAUNCHER_PROJECTILE_SPEED_MAX );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_WORLD_SECTION_Y, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + PHYSICS_WORLD_SECTION_Y, 12.0f, "World" );
    }

    const float displayGravityStrength = GravityStrengthFromWorld( data.worldGravity );
    snprintf( buf, sizeof( buf ), "%.1f", displayGravityStrength );
    state.worldGravitySlider.SetBounds( contentX, scrolledY + PHYSICS_WORLD_GRAVITY_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_WORLD_GRAVITY_SLIDER_Y, 34.0f ) )
    {
        state.worldGravitySlider.Draw( draw, "Gravity", buf, displayGravityStrength, UI_WORLD_GRAVITY_MIN,
                                       UI_WORLD_GRAVITY_MAX );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_FRICTION_SECTION_Y, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + PHYSICS_FRICTION_SECTION_Y, 12.0f, "Friction" );
    }

    snprintf( buf, sizeof( buf ), "%.2f", displayTerrainFriction );
    state.terrainFrictionSlider.SetBounds( contentX, scrolledY + PHYSICS_TERRAIN_FRICTION_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_TERRAIN_FRICTION_SLIDER_Y, 34.0f ) )
    {
        state.terrainFrictionSlider.Draw( draw, "Terrain friction", buf, displayTerrainFriction, UI_FRICTION_COEFF_MIN,
                                          UI_FRICTION_COEFF_MAX );
    }

    snprintf( buf, sizeof( buf ), "%.2f", displayObjectFriction );
    state.objectFrictionSlider.SetBounds( contentX, scrolledY + PHYSICS_OBJECT_FRICTION_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_OBJECT_FRICTION_SLIDER_Y, 34.0f ) )
    {
        state.objectFrictionSlider.Draw( draw, "Object friction", buf, displayObjectFriction, UI_FRICTION_COEFF_MIN,
                                         UI_FRICTION_COEFF_MAX );
    }

    snprintf( buf, sizeof( buf ), "%.3f", displayRollingFriction );
    state.rollingFrictionSlider.SetBounds( contentX, scrolledY + PHYSICS_ROLLING_FRICTION_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_ROLLING_FRICTION_SLIDER_Y, 34.0f ) )
    {
        state.rollingFrictionSlider.Draw( draw, "Rolling friction", buf, displayRollingFriction,
                                          UI_ROLLING_FRICTION_COEFF_MIN, UI_ROLLING_FRICTION_COEFF_MAX );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_TORNADO_SECTION_Y, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + PHYSICS_TORNADO_SECTION_Y, 12.0f,
                          "Tornado Field" );
    }

    snprintf( buf, sizeof( buf ), "%.0f", displayTornadoRadius );
    state.tornadoRadiusSlider.SetBounds( contentX, scrolledY + PHYSICS_TORNADO_RADIUS_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_TORNADO_RADIUS_SLIDER_Y, 34.0f ) )
    {
        state.tornadoRadiusSlider.Draw( draw, "Radius", buf, displayTornadoRadius, UI_TORNADO_RADIUS_MIN,
                                        UI_TORNADO_RADIUS_MAX );
    }

    snprintf( buf, sizeof( buf ), "%.0f", displayTornadoHeight );
    state.tornadoHeightSlider.SetBounds( contentX, scrolledY + PHYSICS_TORNADO_HEIGHT_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_TORNADO_HEIGHT_SLIDER_Y, 34.0f ) )
    {
        state.tornadoHeightSlider.Draw( draw, "Height", buf, displayTornadoHeight, UI_TORNADO_HEIGHT_MIN,
                                        UI_TORNADO_HEIGHT_MAX );
    }

    snprintf( buf, sizeof( buf ), "%.0f", displayTornadoInward );
    state.tornadoInwardSlider.SetBounds( contentX, scrolledY + PHYSICS_TORNADO_INWARD_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_TORNADO_INWARD_SLIDER_Y, 34.0f ) )
    {
        state.tornadoInwardSlider.Draw( draw, "Inward force", buf, displayTornadoInward, UI_TORNADO_INWARD_MIN,
                                        UI_TORNADO_INWARD_MAX );
    }

    snprintf( buf, sizeof( buf ), "%.0f", displayTornadoSwirl );
    state.tornadoSwirlSlider.SetBounds( contentX, scrolledY + PHYSICS_TORNADO_SWIRL_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_TORNADO_SWIRL_SLIDER_Y, 34.0f ) )
    {
        state.tornadoSwirlSlider.Draw( draw, "Swirl force", buf, displayTornadoSwirl, UI_TORNADO_SWIRL_MIN,
                                       UI_TORNADO_SWIRL_MAX );
    }

    snprintf( buf, sizeof( buf ), "%.0f", displayTornadoLift );
    state.tornadoLiftSlider.SetBounds( contentX, scrolledY + PHYSICS_TORNADO_LIFT_SLIDER_Y, contentW, 34.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_TORNADO_LIFT_SLIDER_Y, 34.0f ) )
    {
        state.tornadoLiftSlider.Draw( draw, "Lift force", buf, displayTornadoLift, UI_TORNADO_LIFT_MIN,
                                      UI_TORNADO_LIFT_MAX );
    }
}

} // namespace PhysicsTab
} // namespace UI
} // namespace SkullbonezCore
