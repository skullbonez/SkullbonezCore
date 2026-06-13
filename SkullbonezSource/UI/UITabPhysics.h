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
