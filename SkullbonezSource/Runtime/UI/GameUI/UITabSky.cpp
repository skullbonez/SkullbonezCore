/*
File: SkullbonezSource/Runtime/UI/GameUI/UITabSky.cpp
Purpose:
  Owns the Sky tab widgets, layout, and input handling for in-engine sky tuning.

Summary:
  The Sky tab projects the sky, cloud, ray, and palette subset of cinematic
  render policy into bounded widgets and emits the shared typed commands that
  Runtime applies. Local grouping and concise labels consume the canonical
  render catalog's range, step, and format for each parameter.

Glossary:
  Cinematic command: Intent returned for the run loop to apply to render config.

Invariants:
  - Slider specs must stay aligned with UISkyTabState slot count.
  - Range and quantization metadata comes only from UIRenderAuthoringCatalog.
  - The tab emits commands only; render config is mutated by runtime code.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UITabSky.h
  - SkullbonezSource/Runtime/UI/GameUI/UITabCinematic.cpp
  - SkullbonezSource/Runtime/Render/UIRenderAuthoringCatalog.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UITabSky.h"

#include "UI.h"
#include "../../Render/UIRenderAuthoringCatalog.h"
#include "../../../UI/UIDrawWidgets.h"
#include "GameUILayout.h"
#include "../../../UI/UIStyle.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::UI::GameLayout;
using namespace SkullbonezCore::UI::OperatorControlPolicy;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

constexpr int UI_SKY_SLIDER_BASE = 7000;
constexpr float UI_SKY_SAVE_BUTTON_W = 92.0f;
constexpr float UI_SKY_SAVE_BUTTON_H = 24.0f;
constexpr float UI_SKY_FEATURE_START_Y = 58.0f;
constexpr float UI_SKY_START_Y = 146.0f;
constexpr float UI_SKY_SECTION_H = 28.0f;
constexpr float UI_SKY_ROW_H = 42.0f;

struct SkySliderSpec
{
    // Concept: This row retains Sky-tab grouping and concise labels only. The
    // canonical render catalog owns range, step, and display precision.
    const char* section;
    const char* label;
    SkullbonezCore::UI::UICinematicParam param;
};

struct SkyFeatureSpec
{
    const char* label;
    SkullbonezCore::UI::UICinematicFeature feature;
};

constexpr SkySliderSpec kSkySliderSpecs[] = {
    { "Direction", "Azimuth", SkullbonezCore::UI::UICinematicParam::SunAzimuth },
    { nullptr, "Elevation", SkullbonezCore::UI::UICinematicParam::SunElevation },
    { "Palette", "Sun power", SkullbonezCore::UI::UICinematicParam::SunBrightness },
    { nullptr, "Glow", SkullbonezCore::UI::UICinematicParam::SkyGlow },
    { nullptr, "Sun R", SkullbonezCore::UI::UICinematicParam::SunRed },
    { nullptr, "Sun G", SkullbonezCore::UI::UICinematicParam::SunGreen },
    { nullptr, "Sun B", SkullbonezCore::UI::UICinematicParam::SunBlue },
    { nullptr, "Horizon R", SkullbonezCore::UI::UICinematicParam::HorizonRed },
    { nullptr, "Horizon G", SkullbonezCore::UI::UICinematicParam::HorizonGreen },
    { nullptr, "Horizon B", SkullbonezCore::UI::UICinematicParam::HorizonBlue },
    { nullptr, "Zenith R", SkullbonezCore::UI::UICinematicParam::ZenithRed },
    { nullptr, "Zenith G", SkullbonezCore::UI::UICinematicParam::ZenithGreen },
    { nullptr, "Zenith B", SkullbonezCore::UI::UICinematicParam::ZenithBlue },
    { "Clouds", "Coverage", SkullbonezCore::UI::UICinematicParam::CloudCoverage },
    { nullptr, "Softness", SkullbonezCore::UI::UICinematicParam::CloudSoftness },
    { nullptr, "Scale", SkullbonezCore::UI::UICinematicParam::CloudScale },
    { nullptr, "Intensity", SkullbonezCore::UI::UICinematicParam::CloudIntensity },
    { "Rays", "Shafts", SkullbonezCore::UI::UICinematicParam::ShaftStrength },
    { nullptr, "Falloff", SkullbonezCore::UI::UICinematicParam::ShaftFalloff },
    { nullptr, "Volume", SkullbonezCore::UI::UICinematicParam::VolumetricStrength },
    { nullptr, "Density", SkullbonezCore::UI::UICinematicParam::VolumetricDensity },
    { "Grade", "Exposure", SkullbonezCore::UI::UICinematicParam::Exposure },
    { nullptr, "Gamma", SkullbonezCore::UI::UICinematicParam::Gamma },
    { nullptr, "Saturation", SkullbonezCore::UI::UICinematicParam::StyleSaturation },
    { nullptr, "Contrast", SkullbonezCore::UI::UICinematicParam::StyleContrast },
    { nullptr, "Vignette", SkullbonezCore::UI::UICinematicParam::StyleVignette },
};
static_assert( sizeof( kSkySliderSpecs ) / sizeof( kSkySliderSpecs[0] ) == SkullbonezCore::UI::SkyTab::UI_SKY_SLIDER_COUNT,
               "Sky slider specs must match UISkyTabState." );

constexpr SkyFeatureSpec kSkyFeatureSpecs[] = {
    { "Sky", SkullbonezCore::UI::UICinematicFeature::Sky },
    { "Clouds", SkullbonezCore::UI::UICinematicFeature::Clouds },
    { "God rays", SkullbonezCore::UI::UICinematicFeature::GodRays },
    { "Volume", SkullbonezCore::UI::UICinematicFeature::VolumetricLight },
};
static_assert( sizeof( kSkyFeatureSpecs ) / sizeof( kSkyFeatureSpecs[0] ) ==
                   SkullbonezCore::UI::SkyTab::UI_SKY_FEATURE_COUNT,
               "Sky feature specs must match UISkyTabState." );

void DrawHitboxRect( const SkullbonezCore::UI::UIDrawContext& draw, const SkullbonezCore::UI::UIRect& bounds, float r,
                     float g, float b )
{
    if ( bounds.w <= 0.0f || bounds.h <= 0.0f )
    {
        return;
    }

    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, 0.060f );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, 0.94f );
}

int SkySliderIndexFromActiveSlider( int activeSlider )
{
    const int index = activeSlider - UI_SKY_SLIDER_BASE;
    return ( index >= 0 && index < SkullbonezCore::UI::SkyTab::UI_SKY_SLIDER_COUNT ) ? index : -1;
}

float SkySliderY( int index, float baseY )
{
    float y = baseY;

    for ( int i = 0; i <= index; ++i )
    {
        if ( kSkySliderSpecs[i].section )
        {
            y += UI_SKY_SECTION_H;
        }

        if ( i == index )
        {
            return y;
        }

        y += UI_SKY_ROW_H;
    }

    return y;
}

float SkyFeatureY( int index, float baseY )
{
    return baseY + static_cast<float>( index / 2 ) * CONTENT_TOGGLE_ROW_H;
}

float SkyFeatureX( int index, float contentX, float colW )
{
    return ( index % 2 == 0 ) ? contentX : contentX + colW + 18.0f;
}

SkullbonezCore::UI::UIRect SkySaveButtonBounds( float contentX, float scrolledY, float contentW )
{
    const float saveX = (std::max)( contentX, contentX + contentW - UI_SKY_SAVE_BUTTON_W );
    return { saveX, scrolledY + 12.0f, UI_SKY_SAVE_BUTTON_W, UI_SKY_SAVE_BUTTON_H };
}

bool SkyFeatureEnabled( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                        SkullbonezCore::UI::UICinematicFeature feature )
{
    switch ( feature )
    {
    case SkullbonezCore::UI::UICinematicFeature::Sky:
        return cinematic.skyAtmosphereEnabled;
    case SkullbonezCore::UI::UICinematicFeature::Clouds:
        return cinematic.cloudsEnabled;
    case SkullbonezCore::UI::UICinematicFeature::GodRays:
        return cinematic.godRaysEnabled;
    case SkullbonezCore::UI::UICinematicFeature::VolumetricLight:
        return cinematic.volumetricLightingEnabled;
    default:
        return false;
    }
}

float SkyValueForParam( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                        SkullbonezCore::UI::UICinematicParam param )
{
    switch ( param )
    {
    case SkullbonezCore::UI::UICinematicParam::Exposure:
        return cinematic.exposure;
    case SkullbonezCore::UI::UICinematicParam::Gamma:
        return cinematic.gamma;
    case SkullbonezCore::UI::UICinematicParam::StyleSaturation:
        return cinematic.styleSaturation;
    case SkullbonezCore::UI::UICinematicParam::StyleContrast:
        return cinematic.styleContrast;
    case SkullbonezCore::UI::UICinematicParam::StyleVignette:
        return cinematic.styleVignette;
    case SkullbonezCore::UI::UICinematicParam::SunAzimuth:
        return cinematic.sunAzimuth;
    case SkullbonezCore::UI::UICinematicParam::SunElevation:
        return cinematic.sunElevation;
    case SkullbonezCore::UI::UICinematicParam::SunBrightness:
        return cinematic.sunIntensity;
    case SkullbonezCore::UI::UICinematicParam::SunRed:
        return cinematic.sunColorR;
    case SkullbonezCore::UI::UICinematicParam::SunGreen:
        return cinematic.sunColorG;
    case SkullbonezCore::UI::UICinematicParam::SunBlue:
        return cinematic.sunColorB;
    case SkullbonezCore::UI::UICinematicParam::SkyGlow:
        return cinematic.skyGlowStrength;
    case SkullbonezCore::UI::UICinematicParam::HorizonRed:
        return cinematic.skyHorizonR;
    case SkullbonezCore::UI::UICinematicParam::HorizonGreen:
        return cinematic.skyHorizonG;
    case SkullbonezCore::UI::UICinematicParam::HorizonBlue:
        return cinematic.skyHorizonB;
    case SkullbonezCore::UI::UICinematicParam::ZenithRed:
        return cinematic.skyZenithR;
    case SkullbonezCore::UI::UICinematicParam::ZenithGreen:
        return cinematic.skyZenithG;
    case SkullbonezCore::UI::UICinematicParam::ZenithBlue:
        return cinematic.skyZenithB;
    case SkullbonezCore::UI::UICinematicParam::CloudCoverage:
        return cinematic.cloudCoverage;
    case SkullbonezCore::UI::UICinematicParam::CloudSoftness:
        return cinematic.cloudSoftness;
    case SkullbonezCore::UI::UICinematicParam::CloudScale:
        return cinematic.cloudScale;
    case SkullbonezCore::UI::UICinematicParam::CloudIntensity:
        return cinematic.cloudIntensity;
    case SkullbonezCore::UI::UICinematicParam::ShaftStrength:
        return cinematic.sunShaftStrength;
    case SkullbonezCore::UI::UICinematicParam::ShaftFalloff:
        return cinematic.sunShaftFalloff;
    case SkullbonezCore::UI::UICinematicParam::VolumetricStrength:
        return cinematic.volumetricStrength;
    case SkullbonezCore::UI::UICinematicParam::VolumetricDensity:
        return cinematic.volumetricDensity;
    default:
        return 0.0f;
    }
}

void SetSkySliderResult( SkullbonezCore::UI::InGameUIInputResult& result, const SkullbonezCore::UI::UISlider& slider,
                         int mouseX, const SkySliderSpec& spec )
{
    const SkullbonezCore::UI::CinematicSliderSpec&
        policy = SkullbonezCore::UI::kCinematicSliderSpecs[static_cast<int>( spec.param )];
    result.commands.cinematic.requestedParam = spec.param;
    result.commands.cinematic.requestedValue = slider.ValueFromMouse( mouseX, policy.minValue, policy.maxValue,
                                                                      policy.step );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace SkyTab
{

int ContentHeight()
{
    float height = UI_SKY_START_Y;

    for ( int i = 0; i < UI_SKY_SLIDER_COUNT; ++i )
    {
        if ( kSkySliderSpecs[i].section )
        {
            height += UI_SKY_SECTION_H;
        }

        height += UI_SKY_ROW_H;
    }

    return static_cast<int>( height + 18.0f );
}

bool HandleContentClick( UISkyTabState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY,
                         float contentX, float scrolledY, float contentW )
{
    // Invariant: Click handling sets the same bounds used by Draw, so hit boxes
    // and visible controls stay coupled.
    const UIRect saveBounds = SkySaveButtonBounds( contentX, scrolledY, contentW );
    state.saveButton.SetBounds( saveBounds.x, saveBounds.y, saveBounds.w, saveBounds.h );

    if ( state.saveButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.cinematic.saveSkyDefaults = true;
        return false;
    }

    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const float featureBaseY = scrolledY + UI_SKY_FEATURE_START_Y + 26.0f;

    for ( int i = 0; i < UI_SKY_FEATURE_COUNT; ++i )
    {
        const float tx = SkyFeatureX( i, contentX, colW );
        const float toggleY = SkyFeatureY( i, featureBaseY );
        state.featureToggles[i].SetBounds( tx, toggleY, colW, 24.0f );

        if ( state.featureToggles[i].HitTest( mouseX, mouseY ) )
        {
            result.commands.cinematic.requestedFeature = kSkyFeatureSpecs[i].feature;
            return false;
        }
    }

    const float rowBase = scrolledY + UI_SKY_START_Y;

    for ( int i = 0; i < UI_SKY_SLIDER_COUNT; ++i )
    {
        state.sliders[i].SetBounds( contentX, SkySliderY( i, rowBase ), contentW, 34.0f );

        if ( state.sliders[i].HitTest( mouseX, mouseY ) )
        {
            activeSlider = UI_SKY_SLIDER_BASE + i;
            SetSkySliderResult( result, state.sliders[i], mouseX, kSkySliderSpecs[i] );
            return true;
        }
    }

    return false;
}

bool UpdateActiveSlider( UISkyTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    // Lifetime: activeSlider is a global UI capture id. Accept only the Sky tab
    // range so dragging between tabs cannot write the wrong command.
    const int skySlider = SkySliderIndexFromActiveSlider( activeSlider );

    if ( skySlider < 0 )
    {
        return false;
    }

    SetSkySliderResult( result, state.sliders[skySlider], mouseX, kSkySliderSpecs[skySlider] );
    return true;
}

bool CommitActiveSlider( UISkyTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    return UpdateActiveSlider( state, activeSlider, mouseX, result );
}

void DrawHitboxes( const UISkyTabState& state, const UIDrawContext& draw, float contentR, float contentG, float contentB )
{
    DrawHitboxRect( draw, state.saveButton.Bounds(), contentR, contentG, contentB );

    for ( int i = 0; i < UI_SKY_FEATURE_COUNT; ++i )
    {
        DrawHitboxRect( draw, state.featureToggles[i].Bounds(), contentR, contentG, contentB );
    }

    for ( int i = 0; i < UI_SKY_SLIDER_COUNT; ++i )
    {
        DrawHitboxRect( draw, state.sliders[i].Bounds(), contentR, contentG, contentB );
    }
}

void Draw( UISkyTabState& state, const UIDrawContext& draw, const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
           float contentX, float contentY, float contentW, float contentH, float scrolledY, int mouseX, int mouseY )
{
    char buf[128];
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const UIRect saveBounds = SkySaveButtonBounds( contentX, scrolledY, contentW );

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Sky" );
    state.saveButton.SetBounds( saveBounds.x, saveBounds.y, saveBounds.w, saveBounds.h );

    if ( IsRowVisible( contentY, contentH, saveBounds.y, saveBounds.h ) )
    {
        state.saveButton.Draw( draw, "Save Sky", mouseX, mouseY );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + UI_SKY_FEATURE_START_Y, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + UI_SKY_FEATURE_START_Y, 12.0f, "Passes" );
    }

    const float featureBaseY = scrolledY + UI_SKY_FEATURE_START_Y + 26.0f;

    for ( int i = 0; i < UI_SKY_FEATURE_COUNT; ++i )
    {
        const float tx = SkyFeatureX( i, contentX, colW );
        const float toggleY = SkyFeatureY( i, featureBaseY );
        DrawContentToggle( draw, contentY, contentH, state.featureToggles[i], tx, toggleY, colW, kSkyFeatureSpecs[i].label,
                           SkyFeatureEnabled( cinematic, kSkyFeatureSpecs[i].feature ) );
    }

    const float baseY = scrolledY + UI_SKY_START_Y;

    for ( int i = 0; i < UI_SKY_SLIDER_COUNT; ++i )
    {
        const SkySliderSpec& spec = kSkySliderSpecs[i];
        const SkullbonezCore::UI::CinematicSliderSpec&
            policy = SkullbonezCore::UI::kCinematicSliderSpecs[static_cast<int>( spec.param )];
        const float sliderY = SkySliderY( i, baseY );

        if ( spec.section && IsRowVisible( contentY, contentH, sliderY - UI_SKY_SECTION_H + 4.0f, 18.0f ) )
        {
            DrawSectionTitle( draw, contentX, contentY, contentH, sliderY - UI_SKY_SECTION_H + 4.0f, 12.0f, spec.section );
        }

        const float value = std::clamp( SkyValueForParam( cinematic, spec.param ), policy.minValue, policy.maxValue );
        snprintf( buf, sizeof( buf ), policy.valueFormat, value );
        state.sliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );

        if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
        {
            state.sliders[i].Draw( draw, spec.label, buf, value, policy.minValue, policy.maxValue );
        }
    }
}

} // namespace SkyTab
} // namespace UI
} // namespace SkullbonezCore
