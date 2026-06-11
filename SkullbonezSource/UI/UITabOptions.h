#pragma once

#include "UICheckBox.h"
#include "UICommands.h"
#include "UISlider.h"

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;

namespace OptionsTab
{

constexpr int SLIDER_TIME_SCALE = 1;
constexpr int SLIDER_MODEL_COUNT = 2;

struct UIOptionsTabState
{
    UICheckBox toggles[6];
    UISlider timeScaleSlider;
    UISlider modelCountSlider;
    float previewTimeScale = -1.0f;
    int previewModelCount = -1;
};

int ContentHeight();
void ResetPreviewState( UIOptionsTabState& state );

bool HandleContentClick( UIOptionsTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW );

bool UpdateActiveSlider( UIOptionsTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UIOptionsTabState& state, int activeSlider, InGameUIInputResult& result );

void Draw( UIOptionsTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int activeSlider );

} // namespace OptionsTab
} // namespace UI
} // namespace SkullbonezCore
