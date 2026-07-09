/*
File: SkullbonezSource/UI/UITabPhysics.h
Purpose:
  Implements UI TabPhysics widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  UITabPhysics.h implements UI TabPhysics widgets, layout, drawing, or UI
  state for the in-engine controls. As a public header, keep edits anchored on
  UI request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UITabPhysics.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UICheckBox.h"
#include "UICommands.h"
#include "UIDraw.h"
#include "UISlider.h"

namespace SkullbonezCore
{
namespace UI
{

struct InGameUIFrameData;

namespace PhysicsTab
{

constexpr int SLIDER_PHYSICS_BASE = 3000;
constexpr int SLIDER_ALPHA = SLIDER_PHYSICS_BASE + 0;
constexpr int SLIDER_CONTACT_LINGER = SLIDER_PHYSICS_BASE + 1;
constexpr int SLIDER_WORLD_GRAVITY = SLIDER_PHYSICS_BASE + 2;
constexpr int SLIDER_TORNADO_RADIUS = SLIDER_PHYSICS_BASE + 3;
constexpr int SLIDER_TORNADO_HEIGHT = SLIDER_PHYSICS_BASE + 4;
constexpr int SLIDER_TORNADO_INWARD = SLIDER_PHYSICS_BASE + 5;
constexpr int SLIDER_TORNADO_SWIRL = SLIDER_PHYSICS_BASE + 6;
constexpr int SLIDER_TORNADO_LIFT = SLIDER_PHYSICS_BASE + 7;
constexpr int SLIDER_RAY_IMPULSE = SLIDER_PHYSICS_BASE + 8;
constexpr int SLIDER_LAUNCHER_PROJECTILE_SPEED = SLIDER_PHYSICS_BASE + 9;
constexpr int SLIDER_TERRAIN_FRICTION = SLIDER_PHYSICS_BASE + 10;
constexpr int SLIDER_OBJECT_FRICTION = SLIDER_PHYSICS_BASE + 11;
constexpr int SLIDER_ROLLING_FRICTION = SLIDER_PHYSICS_BASE + 12;

struct UIPhysicsTabState
{
    UICheckBox toggles[13];
    UIRect pipelinePrevButton;
    UIRect pipelineNextButton;
    UISlider alphaSlider;
    UISlider contactLingerSlider;
    UISlider rayImpulseSlider;
    UISlider launcherProjectileSpeedSlider;
    UISlider terrainFrictionSlider;
    UISlider objectFrictionSlider;
    UISlider rollingFrictionSlider;
    UISlider worldGravitySlider;
    UISlider tornadoRadiusSlider;
    UISlider tornadoHeightSlider;
    UISlider tornadoInwardSlider;
    UISlider tornadoSwirlSlider;
    UISlider tornadoLiftSlider;
    float previewAlpha = -1.0f;
    float previewContactLinger = -1.0f;
    float previewRayImpulse = -1.0f;
    float previewLauncherProjectileSpeed = -1.0f;
    float previewTerrainFriction = -1.0f;
    float previewObjectFriction = -1.0f;
    float previewRollingFriction = -1.0f;
    float previewTornadoRadius = -1.0f;
    float previewTornadoHeight = -1.0f;
    float previewTornadoInward = -1.0f;
    float previewTornadoSwirl = -1.0f;
    float previewTornadoLift = -1.0f;
};

int ContentHeight();
void ResetPreviewState( UIPhysicsTabState& state );

bool HandleContentClick( UIPhysicsTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW );

bool UpdateActiveSlider( UIPhysicsTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UIPhysicsTabState& state, int activeSlider, InGameUIInputResult& result );

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
           int mouseY );

} // namespace PhysicsTab
} // namespace UI
} // namespace SkullbonezCore
