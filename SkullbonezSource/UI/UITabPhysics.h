/*
File: SkullbonezSource/UI/UITabPhysics.h
Purpose:
  Implements UI TabPhysics widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

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

#include "UIButton.h"
#include "UICheckBox.h"
#include "UIComboBox.h"
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

constexpr int SPAWN_BALL = 0;
constexpr int SPAWN_SPHERE = 1;
constexpr int SPAWN_HULL_WEDGE = 2;
constexpr int SPAWN_HULL_TRI_PRISM = 3;
constexpr int SPAWN_HULL_TAPERED_BLOCK = 4;
constexpr int SPAWN_HULL_PYRAMID = 5;
constexpr int SPAWN_HULL_HEX_PRISM = 6;
constexpr int SPAWN_HULL_DIAMOND = 7;
constexpr int SPAWN_TYPE_COUNT = 8;

constexpr int SLIDER_ALPHA = 3;
constexpr int SLIDER_CONTACT_LINGER = 4;
constexpr int SLIDER_WORLD_GRAVITY = 11;
constexpr int SLIDER_TORNADO_RADIUS = 14;
constexpr int SLIDER_TORNADO_HEIGHT = 15;
constexpr int SLIDER_TORNADO_INWARD = 16;
constexpr int SLIDER_TORNADO_SWIRL = 17;
constexpr int SLIDER_TORNADO_LIFT = 18;

struct UIPhysicsTabState
{
    UICheckBox toggles[11];
    UIRect pipelinePrevButton;
    UIRect pipelineNextButton;
    UIComboBox spawnCombo;
    UIButton spawnButton;
    UISlider alphaSlider;
    UISlider contactLingerSlider;
    UISlider worldGravitySlider;
    UISlider tornadoRadiusSlider;
    UISlider tornadoHeightSlider;
    UISlider tornadoInwardSlider;
    UISlider tornadoSwirlSlider;
    UISlider tornadoLiftSlider;
    float previewAlpha = -1.0f;
    float previewContactLinger = -1.0f;
    float previewTornadoRadius = -1.0f;
    float previewTornadoHeight = -1.0f;
    float previewTornadoInward = -1.0f;
    float previewTornadoSwirl = -1.0f;
    float previewTornadoLift = -1.0f;
    int selectedSpawnType = SPAWN_BALL;
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
