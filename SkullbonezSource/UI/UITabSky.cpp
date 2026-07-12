/*
File: SkullbonezSource/UI/UITabSky.cpp
Purpose:
  Owns the Sky tab widgets, layout, and input handling for in-engine sky tuning.

Summary:
  UITabSky.cpp owns the Sky tab widgets, layout, and input handling for
  in-engine sky tuning. As an implementation unit, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Sky feature: Toggle for a render pass such as clouds, god rays, or volumetric
    light.
  Sky slider: UI row that maps mouse position to a cinematic render parameter.
  Cinematic command: Intent returned for the run loop to apply to render config.

Invariants:
  - Slider specs must stay aligned with UISkyTabState slot count.
  - The tab emits commands only; render config is mutated by runtime code.

Related:
  - SkullbonezSource/UI/UITabSky.h
  - SkullbonezSource/UI/UITabCinematic.cpp
*/
#include "UITabSky.h"

#include "UI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"
#include "UIStyle.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::UI::Layout;
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
    // Concept: One row in the Sky tab. Keeping param/range/step together
    // prevents draw, hit-test, and command mapping from drifting apart.
    const char* section;
    const char* label;
    SkullbonezCore::UI::UICinematicParam param;
    float minValue;
    float maxValue;
    float step;
    const char* valueFormat;
};

struct SkyFeatureSpec
{
    const char* label;
    SkullbonezCore::UI::UICinematicFeature feature;
};

constexpr SkySliderSpec kSkySliderSpecs[] = {
    { "Direction", "Azimuth", SkullbonezCore::UI::UICinematicParam::SunAzimuth, 0.00f, 1.00f, 0.005f, "%.3f" },
    { nullptr, "Elevation", SkullbonezCore::UI::UICinematicParam::SunElevation, 0.00f, 1.00f, 0.005f, "%.3f" },
    { "Palette", "Sun power", SkullbonezCore::UI::UICinematicParam::SunBrightness, 0.00f, 40.00f, 0.10f, "%.1f" },
    { nullptr, "Glow", SkullbonezCore::UI::UICinematicParam::SkyGlow, 0.00f, 8.00f, 0.05f, "%.2f" },
    { nullptr, "Sun R", SkullbonezCore::UI::UICinematicParam::SunRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun G", SkullbonezCore::UI::UICinematicParam::SunGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun B", SkullbonezCore::UI::UICinematicParam::SunBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Horizon R", SkullbonezCore::UI::UICinematicParam::HorizonRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Horizon G", SkullbonezCore::UI::UICinematicParam::HorizonGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Horizon B", SkullbonezCore::UI::UICinematicParam::HorizonBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Zenith R", SkullbonezCore::UI::UICinematicParam::ZenithRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Zenith G", SkullbonezCore::UI::UICinematicParam::ZenithGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Zenith B", SkullbonezCore::UI::UICinematicParam::ZenithBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { "Clouds", "Coverage", SkullbonezCore::UI::UICinematicParam::CloudCoverage, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Softness", SkullbonezCore::UI::UICinematicParam::CloudSoftness, 0.01f, 0.65f, 0.01f, "%.2f" },
    { nullptr, "Scale", SkullbonezCore::UI::UICinematicParam::CloudScale, 0.50f, 12.00f, 0.05f, "%.2f" },
    { nullptr, "Intensity", SkullbonezCore::UI::UICinematicParam::CloudIntensity, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Rays", "Shafts", SkullbonezCore::UI::UICinematicParam::ShaftStrength, 0.00f, 3.00f, 0.01f, "%.2f" },
    { nullptr, "Falloff", SkullbonezCore::UI::UICinematicParam::ShaftFalloff, 0.25f, 5.00f, 0.01f, "%.2f" },
    { nullptr, "Volume", SkullbonezCore::UI::UICinematicParam::VolumetricStrength, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Density", SkullbonezCore::UI::UICinematicParam::VolumetricDensity, 0.00f, 2.50f, 0.01f, "%.2f" },
    { "Grade", "Exposure", SkullbonezCore::UI::UICinematicParam::Exposure, 0.05f, 3.00f, 0.01f, "%.2f" },
    { nullptr, "Gamma", SkullbonezCore::UI::UICinematicParam::Gamma, 1.00f, 3.00f, 0.01f, "%.2f" },
    { nullptr, "Saturation", SkullbonezCore::UI::UICinematicParam::StyleSaturation, 0.00f, 2.50f, 0.01f, "%.2f" },
    { nullptr, "Contrast", SkullbonezCore::UI::UICinematicParam::StyleContrast, 0.00f, 2.50f, 0.01f, "%.2f" },
    { nullptr, "Vignette", SkullbonezCore::UI::UICinematicParam::StyleVignette, 0.00f, 1.00f, 0.01f, "%.2f" },
};
static_assert( sizeof( kSkySliderSpecs ) / sizeof( kSkySliderSpecs[0] ) ==
                   SkullbonezCore::UI::SkyTab::UI_SKY_SLIDER_COUNT,
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

void DrawHitboxRect( const SkullbonezCore::UI::UIDrawContext& draw,
                     const SkullbonezCore::UI::UIRect& bounds,
                     float r,
                     float g,
                     float b )
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

bool SkyFeatureEnabled( const CinematicRenderConfig& cinematic, SkullbonezCore::UI::UICinematicFeature feature )
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

float SkyValueForParam( const CinematicRenderConfig& cinematic, SkullbonezCore::UI::UICinematicParam param )
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

void SetSkySliderResult( SkullbonezCore::UI::InGameUIInputResult& result,
                         const SkullbonezCore::UI::UISlider& slider,
                         int mouseX,
                         const SkySliderSpec& spec )
{
    result.commands.cinematic.requestedParam = spec.param;
    result.commands.cinematic.requestedValue = slider.ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
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

bool HandleContentClick( UISkyTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float scrolledY,
                         float contentW )
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

void DrawHitboxes( const UISkyTabState& state,
                   const UIDrawContext& draw,
                   float contentR,
                   float contentG,
                   float contentB )
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

void Draw( UISkyTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int mouseX,
           int mouseY )
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
        DrawContentToggle( draw,
                           contentY,
                           contentH,
                           state.featureToggles[i],
                           tx,
                           toggleY,
                           colW,
                           kSkyFeatureSpecs[i].label,
                           SkyFeatureEnabled( data.cinematic, kSkyFeatureSpecs[i].feature ) );
    }

    const float baseY = scrolledY + UI_SKY_START_Y;
    for ( int i = 0; i < UI_SKY_SLIDER_COUNT; ++i )
    {
        const SkySliderSpec& spec = kSkySliderSpecs[i];
        const float sliderY = SkySliderY( i, baseY );
        if ( spec.section && IsRowVisible( contentY, contentH, sliderY - UI_SKY_SECTION_H + 4.0f, 18.0f ) )
        {
            DrawSectionTitle( draw,
                              contentX,
                              contentY,
                              contentH,
                              sliderY - UI_SKY_SECTION_H + 4.0f,
                              12.0f,
                              spec.section );
        }
        const float value = std::clamp( SkyValueForParam( data.cinematic, spec.param ), spec.minValue, spec.maxValue );
        snprintf( buf, sizeof( buf ), spec.valueFormat, value );
        state.sliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );
        if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
        {
            state.sliders[i].Draw( draw, spec.label, buf, value, spec.minValue, spec.maxValue );
        }
    }
}

} // namespace SkyTab
} // namespace UI
} // namespace SkullbonezCore
