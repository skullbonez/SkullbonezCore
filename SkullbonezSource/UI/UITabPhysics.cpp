#include "UITabPhysics.h"

#include "../SkullbonezPhysicsDebugVisualizer.h"
#include "SkullbonezUI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"
#include "UIStyle.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

constexpr float PHYSICS_FIRST_TOGGLE_Y = 42.0f;
constexpr float PHYSICS_PIPELINE_BUTTON_Y = 224.0f;
constexpr float PHYSICS_ALPHA_SLIDER_Y = 272.0f;
constexpr float PHYSICS_CONTACT_LINGER_SLIDER_Y = 320.0f;
constexpr float PHYSICS_WORLD_GRAVITY_SLIDER_Y = 404.0f;

void SetToggleBounds( SkullbonezCore::UI::PhysicsTab::UIPhysicsTabState& state,
                      int index,
                      int row,
                      int column,
                      float col1,
                      float col2,
                      float rowBase,
                      float colW )
{
    const float tx = column == 0 ? col1 : col2;
    state.toggles[index].SetBounds( tx, rowBase + static_cast<float>( row ) * CONTENT_TOGGLE_ROW_H, colW, 24.0f );
}

void SetContentBounds( SkullbonezCore::UI::PhysicsTab::UIPhysicsTabState& state,
                       float contentX,
                       float firstToggleY,
                       float contentW )
{
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
    SetPipelineStepButtonBounds( state.pipelinePrevButton, state.pipelineNextButton, contentX, contentW, contentBaseY + PHYSICS_PIPELINE_BUTTON_Y );
    state.alphaSlider.SetBounds( contentX, contentBaseY + PHYSICS_ALPHA_SLIDER_Y, contentW, 34.0f );
    state.contactLingerSlider.SetBounds( contentX, contentBaseY + PHYSICS_CONTACT_LINGER_SLIDER_Y, contentW, 34.0f );
    state.worldGravitySlider.SetBounds( contentX, contentBaseY + PHYSICS_WORLD_GRAVITY_SLIDER_Y, contentW, 34.0f );
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
    return 468;
}


void ResetPreviewState( UIPhysicsTabState& state )
{
    state.previewAlpha = -1.0f;
    state.previewContactLinger = -1.0f;
}


bool HandleContentClick( UIPhysicsTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW )
{
    SetContentBounds( state, contentX, rowBase, contentW );

    if ( state.toggles[0].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.toggleCollisionVisualizer = true;
    }
    else if ( state.toggles[1].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.togglePhysicsDebugFlags = PHYSICS_DEBUG_AXES;
    }
    else if ( state.toggles[2].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.togglePhysicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
    }
    else if ( state.toggles[3].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.togglePhysicsDebugFlags = PHYSICS_DEBUG_SLEEP;
    }
    else if ( state.toggles[4].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.togglePhysicsDebugTransparent = true;
    }
    else if ( state.toggles[5].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.toggleBroadphaseOverlay = true;
    }
    else if ( state.toggles[7].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.togglePhysicsDebugFlags = PHYSICS_DEBUG_PIPELINE;
    }
    else if ( state.toggles[6].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.togglePhysicsSleepPolicy = true;
    }
    else if ( state.toggles[8].HitTest( mouseX, mouseY ) )
    {
        result.commands.physics.toggleTerrainContactProbe = true;
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
    else if ( state.worldGravitySlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_WORLD_GRAVITY;
        result.commands.water.requestWorldGravity = true;
        result.commands.water.requestedWorldGravity = WorldGravityFromStrength( state.worldGravitySlider.ValueFromMouse( mouseX,
                                                                                                                          UI_WORLD_GRAVITY_MIN,
                                                                                                                          UI_WORLD_GRAVITY_MAX,
                                                                                                                          UI_WORLD_GRAVITY_STEP ) );
        return true;
    }
    return false;
}


bool UpdateActiveSlider( UIPhysicsTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
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
    if ( activeSlider == SLIDER_WORLD_GRAVITY )
    {
        result.commands.water.requestWorldGravity = true;
        result.commands.water.requestedWorldGravity = WorldGravityFromStrength( state.worldGravitySlider.ValueFromMouse( mouseX,
                                                                                                                          UI_WORLD_GRAVITY_MIN,
                                                                                                                          UI_WORLD_GRAVITY_MAX,
                                                                                                                          UI_WORLD_GRAVITY_STEP ) );
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
    return false;
}


void Draw( UIPhysicsTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int activeSlider,
           int mouseX,
           int mouseY )
{
    char buf[128];
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const float col1 = contentX;
    const float col2 = contentX + colW + 18.0f;
    const float displayAlpha = ( activeSlider == SLIDER_ALPHA && state.previewAlpha >= 0.0f ) ? state.previewAlpha : data.physicsDebugAlpha;
    const float displayLinger = ( activeSlider == SLIDER_CONTACT_LINGER && state.previewContactLinger >= 0.0f ) ? state.previewContactLinger : data.physicsDebugContactLinger;
    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Physics Controls" );
    DrawContentToggle( draw, contentY, contentH, state.toggles[0], col1, scrolledY + PHYSICS_FIRST_TOGGLE_Y, colW, "Collision state", data.collisionVisualizer );
    DrawContentToggle( draw, contentY, contentH, state.toggles[4], col1, scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H, colW, "Transparent", data.physicsDebugTransparent );
    DrawContentToggle( draw, contentY, contentH, state.toggles[5], col1, scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 2.0f, colW, "Broadphase", data.broadphaseOverlay );
    DrawContentToggle( draw, contentY, contentH, state.toggles[7], col1, scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 3.0f, colW, "Pipeline", ( data.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0 );
    DrawContentToggle( draw, contentY, contentH, state.toggles[1], col2, scrolledY + PHYSICS_FIRST_TOGGLE_Y, colW, "Axes", ( data.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0 );
    DrawContentToggle( draw, contentY, contentH, state.toggles[2], col2, scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H, colW, "Contacts", ( data.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0 );
    DrawContentToggle( draw, contentY, contentH, state.toggles[3], col2, scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 2.0f, colW, "Sleep state", ( data.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0 );
    DrawContentToggle( draw, contentY, contentH, state.toggles[6], col2, scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 3.0f, colW, "Sleep policy", data.physicsSleepEnabled );
    DrawContentToggle( draw, contentY, contentH, state.toggles[8], col1, scrolledY + PHYSICS_FIRST_TOGGLE_Y + CONTENT_TOGGLE_ROW_H * 4.0f, colW, "Terrain probe", ( data.physicsDebugFlags & PHYSICS_DEBUG_TERRAIN_CONTACT ) != 0 );
    const Style::UIPalette& palette = Style::Palette();
    snprintf( buf, sizeof( buf ), "0x%04X", data.physicsDebugFlags );
    DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 208.0f, "Debug flags", buf, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b );
    snprintf( buf, sizeof( buf ), "%d/%d %s", data.physicsPipelineStageIndex + 1, data.physicsPipelineStageCount, data.physicsPipelineStageName );
    DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 228.0f, "Pipeline stage", buf, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b );
    SetPipelineStepButtonBounds( state.pipelinePrevButton, state.pipelineNextButton, contentX, contentW, scrolledY + PHYSICS_PIPELINE_BUTTON_Y );
    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_PIPELINE_BUTTON_Y, UI_PIPELINE_STEP_BUTTON_H ) )
    {
        DrawPipelineStepButton( draw, state.pipelinePrevButton, true, state.pipelinePrevButton.Contains( mouseX, mouseY ) );
        DrawPipelineStepButton( draw, state.pipelineNextButton, false, state.pipelineNextButton.Contains( mouseX, mouseY ) );
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + 246.0f, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 246.0f, 12.0f, "Debug Draw" );
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
        state.contactLingerSlider.Draw( draw, "Contact linger", buf, displayLinger, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX );
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + 378.0f, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 378.0f, 12.0f, "World" );
    }
    const float displayGravityStrength = GravityStrengthFromWorld( data.worldGravity );
    snprintf( buf, sizeof( buf ), "%.1f", displayGravityStrength );
    state.worldGravitySlider.SetBounds( contentX, scrolledY + PHYSICS_WORLD_GRAVITY_SLIDER_Y, contentW, 34.0f );
    if ( IsRowVisible( contentY, contentH, scrolledY + PHYSICS_WORLD_GRAVITY_SLIDER_Y, 34.0f ) )
    {
        state.worldGravitySlider.Draw( draw, "Gravity", buf, displayGravityStrength, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
    }
}

} // namespace PhysicsTab
} // namespace UI
} // namespace SkullbonezCore
