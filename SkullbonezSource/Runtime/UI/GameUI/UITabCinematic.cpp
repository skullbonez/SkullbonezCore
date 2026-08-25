/*
File: SkullbonezSource/Runtime/UI/GameUI/UITabCinematic.cpp
Purpose:
  Owns the Cinematic tab widgets, layout, and input handling for the in-engine controls.

Summary:
  The Cinematic tab owns bounded widget state and one shared layout model for
  drawing and hit testing, then emits typed render-policy commands for Runtime
  to apply. Its legacy grouping and labels remain local, while the canonical
  render catalog supplies every range, step, and display format.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - Range and quantization metadata comes only from UIRenderAuthoringCatalog.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UITabCinematic.h
  - SkullbonezSource/Runtime/Render/UIRenderAuthoringCatalog.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UITabCinematic.h"

#include "UI.h"
#include "../../Render/UIRenderAuthoringCatalog.h"
#include "../../../UI/UIDrawWidgets.h"
#include "GameUILayout.h"
#include "../../../UI/UIStyle.h"
#include "UITabScene.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::UI::GameLayout;
using namespace SkullbonezCore::UI::OperatorControlPolicy;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

constexpr int UI_CINEMATIC_SLIDER_BASE = 5000;
constexpr int UI_CINE_SCENE_MAX_OPTIONS = 32;
constexpr float UI_CINEMATIC_SCENE_Y = 42.0f;
constexpr float UI_CINEMATIC_FEATURE_START_Y = 96.0f;
constexpr float UI_CINEMATIC_START_Y = 266.0f;
constexpr float UI_CINEMATIC_SECTION_H = 28.0f;
constexpr float UI_CINEMATIC_ROW_H = 42.0f;

struct GameCinematicSliderSpec
{
    // Concept: This row retains only the legacy tab grouping and concise label.
    // The canonical catalog owns the parameter's range, step, and format.
    const char* section;
    const char* label;
    SkullbonezCore::UI::UICinematicParam param;
};

struct GameCinematicFeatureSpec
{
    // One toggle in the Cine tab, such as Bloom or Fog.
    const char* label;
    SkullbonezCore::UI::UICinematicFeature feature;
};

constexpr GameCinematicSliderSpec kGameCinematicSliderSpecs[] = {
    { "Tonemap", "Exposure", SkullbonezCore::UI::UICinematicParam::Exposure },
    { nullptr, "Gamma", SkullbonezCore::UI::UICinematicParam::Gamma },
    { "Style", "Sky mode", SkullbonezCore::UI::UICinematicParam::SkyMode },
    { nullptr, "Terrain mode", SkullbonezCore::UI::UICinematicParam::TerrainMode },
    { nullptr, "Object style", SkullbonezCore::UI::UICinematicParam::ObjectStyle },
    { nullptr, "Water mode", SkullbonezCore::UI::UICinematicParam::WaterMode },
    { nullptr, "Saturation", SkullbonezCore::UI::UICinematicParam::StyleSaturation },
    { nullptr, "Contrast", SkullbonezCore::UI::UICinematicParam::StyleContrast },
    { nullptr, "Vignette", SkullbonezCore::UI::UICinematicParam::StyleVignette },
    { "Sun", "Azimuth", SkullbonezCore::UI::UICinematicParam::SunAzimuth },
    { nullptr, "Elevation", SkullbonezCore::UI::UICinematicParam::SunElevation },
    { nullptr, "Brightness", SkullbonezCore::UI::UICinematicParam::SunBrightness },
    { nullptr, "Sun R", SkullbonezCore::UI::UICinematicParam::SunRed },
    { nullptr, "Sun G", SkullbonezCore::UI::UICinematicParam::SunGreen },
    { nullptr, "Sun B", SkullbonezCore::UI::UICinematicParam::SunBlue },
    { "Sky", "Glow", SkullbonezCore::UI::UICinematicParam::SkyGlow },
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
    { "Shafts", "Strength", SkullbonezCore::UI::UICinematicParam::ShaftStrength },
    { nullptr, "Falloff", SkullbonezCore::UI::UICinematicParam::ShaftFalloff },
    { "Volume", "Strength", SkullbonezCore::UI::UICinematicParam::VolumetricStrength },
    { nullptr, "Density", SkullbonezCore::UI::UICinematicParam::VolumetricDensity },
    { nullptr, "Decay", SkullbonezCore::UI::UICinematicParam::VolumetricDecay },
    { "Bloom", "Threshold", SkullbonezCore::UI::UICinematicParam::BloomThreshold },
    { nullptr, "Knee", SkullbonezCore::UI::UICinematicParam::BloomKnee },
    { nullptr, "Strength", SkullbonezCore::UI::UICinematicParam::BloomStrength },
    { nullptr, "Radius", SkullbonezCore::UI::UICinematicParam::BloomRadius },
    { "Terrain", "Relief", SkullbonezCore::UI::UICinematicParam::TerrainRelief },
    { nullptr, "Ground R", SkullbonezCore::UI::UICinematicParam::TerrainTintRed },
    { nullptr, "Ground G", SkullbonezCore::UI::UICinematicParam::TerrainTintGreen },
    { nullptr, "Ground B", SkullbonezCore::UI::UICinematicParam::TerrainTintBlue },
    { nullptr, "Accent R", SkullbonezCore::UI::UICinematicParam::TerrainAccentRed },
    { nullptr, "Accent G", SkullbonezCore::UI::UICinematicParam::TerrainAccentGreen },
    { nullptr, "Accent B", SkullbonezCore::UI::UICinematicParam::TerrainAccentBlue },
    { nullptr, "Grid scale", SkullbonezCore::UI::UICinematicParam::TerrainGridScale },
    { nullptr, "Grid strength", SkullbonezCore::UI::UICinematicParam::TerrainGridStrength },
    { "Water", "Water R", SkullbonezCore::UI::UICinematicParam::WaterTintRed },
    { nullptr, "Water G", SkullbonezCore::UI::UICinematicParam::WaterTintGreen },
    { nullptr, "Water B", SkullbonezCore::UI::UICinematicParam::WaterTintBlue },
    { nullptr, "Alpha", SkullbonezCore::UI::UICinematicParam::WaterAlpha },
    { nullptr, "Reflection", SkullbonezCore::UI::UICinematicParam::WaterReflection },
    { nullptr, "Glint", SkullbonezCore::UI::UICinematicParam::WaterGlint },
    { "Basin", "Center X", SkullbonezCore::UI::UICinematicParam::BasinCenterX },
    { nullptr, "Center Z", SkullbonezCore::UI::UICinematicParam::BasinCenterZ },
    { nullptr, "Radius X", SkullbonezCore::UI::UICinematicParam::BasinRadiusX },
    { nullptr, "Radius Z", SkullbonezCore::UI::UICinematicParam::BasinRadiusZ },
    { nullptr, "Feather", SkullbonezCore::UI::UICinematicParam::BasinFeather },
    { nullptr, "Basin Depth", SkullbonezCore::UI::UICinematicParam::BasinDepth },
    { nullptr, "Rim Lift", SkullbonezCore::UI::UICinematicParam::BasinRimLift },
    { "Fog", "Density", SkullbonezCore::UI::UICinematicParam::FogDensity },
    { nullptr, "Opacity", SkullbonezCore::UI::UICinematicParam::FogOpacity },
    { nullptr, "Start", SkullbonezCore::UI::UICinematicParam::FogStart },
    { nullptr, "End", SkullbonezCore::UI::UICinematicParam::FogEnd },
    { nullptr, "Fog R", SkullbonezCore::UI::UICinematicParam::FogRed },
    { nullptr, "Fog G", SkullbonezCore::UI::UICinematicParam::FogGreen },
    { nullptr, "Fog B", SkullbonezCore::UI::UICinematicParam::FogBlue },
};
static_assert( sizeof( kGameCinematicSliderSpecs ) / sizeof( kGameCinematicSliderSpecs[0] ) ==
                   static_cast<int>( SkullbonezCore::UI::UICinematicParam::Count ),
               "Cinematic slider specs must match UICinematicParam." );

constexpr bool CinematicTabSpecsAreEnumIndexed()
{
    for ( int index = 0; index < static_cast<int>( SkullbonezCore::UI::UICinematicParam::Count ); ++index )
    {
        if ( static_cast<int>( kGameCinematicSliderSpecs[index].param ) != index )
        {
            return false;
        }
    }

    return true;
}
static_assert( CinematicTabSpecsAreEnumIndexed(), "Cinematic tab rows must remain enum-indexed." );

constexpr GameCinematicFeatureSpec kGameCinematicFeatureSpecs[] = {
    { "Sky", SkullbonezCore::UI::UICinematicFeature::Sky },
    { "Clouds", SkullbonezCore::UI::UICinematicFeature::Clouds },
    { "God rays", SkullbonezCore::UI::UICinematicFeature::GodRays },
    { "Volume", SkullbonezCore::UI::UICinematicFeature::VolumetricLight },
    { "Bloom", SkullbonezCore::UI::UICinematicFeature::Bloom },
    { "Fog", SkullbonezCore::UI::UICinematicFeature::Fog },
    { "Relief", SkullbonezCore::UI::UICinematicFeature::TerrainRelief },
    { "Shadows", SkullbonezCore::UI::UICinematicFeature::Shadows },
};
static_assert( sizeof( kGameCinematicFeatureSpecs ) / sizeof( kGameCinematicFeatureSpecs[0] ) ==
                   static_cast<int>( SkullbonezCore::UI::UICinematicFeature::Count ),
               "Cinematic feature specs must match UICinematicFeature." );

void DrawHitboxRect( const SkullbonezCore::UI::UIDrawContext& draw, const SkullbonezCore::UI::UIRect& bounds, float r,
                     float g, float b, float fillA = 0.060f, float outlineA = 0.94f )
{
    if ( bounds.w <= 0.0f || bounds.h <= 0.0f )
    {
        return;
    }

    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, fillA );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, outlineA );

    if ( bounds.w > 4.0f && bounds.h > 4.0f )
    {
        draw.Outline( bounds.x + 1.0f, bounds.y + 1.0f, bounds.w - 2.0f, bounds.h - 2.0f, r, g, b, outlineA * 0.42f );
    }
}

void DrawComboHitboxes( const SkullbonezCore::UI::UIDrawContext& draw, const SkullbonezCore::UI::UIComboBox& combo,
                        int optionCount, float r, float g, float b )
{
    DrawHitboxRect( draw, combo.Bounds(), r, g, b );

    if ( combo.IsOpen() )
    {
        DrawHitboxRect( draw, combo.DropdownBounds( optionCount ), 0.18f, 0.58f, 1.0f, 0.078f, 0.96f );
    }
}

bool IsCineSceneOptionName( const char* name )
{
    if ( !name )
    {
        return false;
    }

    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}

int BuildCineSceneOptions( const char* const* sceneOptions, int sceneOptionCount,
                           const char* labels[UI_CINE_SCENE_MAX_OPTIONS], int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS] )
{
    int count = 0;
    labels[count] = SkullbonezCore::UI::SceneTab::DEMO_SCENE_OPTION;
    sceneIndices[count] = -1;
    ++count;

    for ( int i = 0; i < sceneOptionCount && sceneOptions && count < UI_CINE_SCENE_MAX_OPTIONS; ++i )
    {
        if ( IsCineSceneOptionName( sceneOptions[i] ) )
        {
            labels[count] = sceneOptions[i];
            sceneIndices[count] = i;
            ++count;
        }
    }

    return count;
}

int SelectedCineSceneOption( const int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS], int cineOptionCount,
                             int selectedSceneOption )
{
    for ( int i = 0; i < cineOptionCount; ++i )
    {
        if ( sceneIndices[i] == selectedSceneOption )
        {
            return i;
        }
    }

    return 0;
}

int CinematicSliderIndexFromActiveSlider( int activeSlider )
{
    // Other UI tabs already use activeSlider. Give Cine sliders their own id
    // range so dragging can continue even if the mouse leaves the slider bounds.
    const int index = activeSlider - UI_CINEMATIC_SLIDER_BASE;
    return ( index >= 0 && index < static_cast<int>( SkullbonezCore::UI::UICinematicParam::Count ) ) ? index : -1;
}

float CinematicSliderY( int index, float baseY )
{
    // Sections add extra vertical space. Calculating this from the spec array
    // keeps hit testing and drawing in lockstep.
    float y = baseY;

    for ( int i = 0; i <= index; ++i )
    {
        if ( kGameCinematicSliderSpecs[i].section )
        {
            y += UI_CINEMATIC_SECTION_H;
        }

        if ( i == index )
        {
            return y;
        }

        y += UI_CINEMATIC_ROW_H;
    }

    return y;
}

float CinematicValueForParam( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                              SkullbonezCore::UI::UICinematicParam param )
{
    // Read the live value for a Cine slider. This is the inverse of the command
    // application in InputRouter.Interactions.cpp.
    switch ( param )
    {
    case SkullbonezCore::UI::UICinematicParam::Exposure:
        return cinematic.exposure;
    case SkullbonezCore::UI::UICinematicParam::Gamma:
        return cinematic.gamma;
    case SkullbonezCore::UI::UICinematicParam::SkyMode:
        return static_cast<float>( cinematic.skyMode );
    case SkullbonezCore::UI::UICinematicParam::TerrainMode:
        return static_cast<float>( cinematic.terrainMode );
    case SkullbonezCore::UI::UICinematicParam::ObjectStyle:
        return static_cast<float>( cinematic.objectStyle );
    case SkullbonezCore::UI::UICinematicParam::WaterMode:
        return static_cast<float>( cinematic.waterMode );
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
    case SkullbonezCore::UI::UICinematicParam::VolumetricDecay:
        return cinematic.volumetricDecay;
    case SkullbonezCore::UI::UICinematicParam::BloomThreshold:
        return cinematic.bloomThreshold;
    case SkullbonezCore::UI::UICinematicParam::BloomKnee:
        return cinematic.bloomKnee;
    case SkullbonezCore::UI::UICinematicParam::BloomStrength:
        return cinematic.bloomStrength;
    case SkullbonezCore::UI::UICinematicParam::BloomRadius:
        return cinematic.bloomRadius;
    case SkullbonezCore::UI::UICinematicParam::TerrainRelief:
        return cinematic.terrainRelief;
    case SkullbonezCore::UI::UICinematicParam::TerrainTintRed:
        return cinematic.terrainTintR;
    case SkullbonezCore::UI::UICinematicParam::TerrainTintGreen:
        return cinematic.terrainTintG;
    case SkullbonezCore::UI::UICinematicParam::TerrainTintBlue:
        return cinematic.terrainTintB;
    case SkullbonezCore::UI::UICinematicParam::TerrainAccentRed:
        return cinematic.terrainAccentR;
    case SkullbonezCore::UI::UICinematicParam::TerrainAccentGreen:
        return cinematic.terrainAccentG;
    case SkullbonezCore::UI::UICinematicParam::TerrainAccentBlue:
        return cinematic.terrainAccentB;
    case SkullbonezCore::UI::UICinematicParam::TerrainGridScale:
        return cinematic.terrainGridScale;
    case SkullbonezCore::UI::UICinematicParam::TerrainGridStrength:
        return cinematic.terrainGridStrength;
    case SkullbonezCore::UI::UICinematicParam::WaterTintRed:
        return cinematic.waterTintR;
    case SkullbonezCore::UI::UICinematicParam::WaterTintGreen:
        return cinematic.waterTintG;
    case SkullbonezCore::UI::UICinematicParam::WaterTintBlue:
        return cinematic.waterTintB;
    case SkullbonezCore::UI::UICinematicParam::WaterAlpha:
        return cinematic.waterAlpha;
    case SkullbonezCore::UI::UICinematicParam::WaterReflection:
        return cinematic.waterReflectionStrength;
    case SkullbonezCore::UI::UICinematicParam::WaterGlint:
        return cinematic.waterGlintStrength;
    case SkullbonezCore::UI::UICinematicParam::BasinCenterX:
        return cinematic.basinCenterX;
    case SkullbonezCore::UI::UICinematicParam::BasinCenterZ:
        return cinematic.basinCenterZ;
    case SkullbonezCore::UI::UICinematicParam::BasinRadiusX:
        return cinematic.basinRadiusX;
    case SkullbonezCore::UI::UICinematicParam::BasinRadiusZ:
        return cinematic.basinRadiusZ;
    case SkullbonezCore::UI::UICinematicParam::BasinFeather:
        return cinematic.basinFeather;
    case SkullbonezCore::UI::UICinematicParam::BasinDepth:
        return cinematic.basinDepth;
    case SkullbonezCore::UI::UICinematicParam::BasinRimLift:
        return cinematic.basinRimLift;
    case SkullbonezCore::UI::UICinematicParam::FogDensity:
        return cinematic.fogDensity;
    case SkullbonezCore::UI::UICinematicParam::FogOpacity:
        return cinematic.fogMaxOpacity;
    case SkullbonezCore::UI::UICinematicParam::FogStart:
        return cinematic.fogStart;
    case SkullbonezCore::UI::UICinematicParam::FogEnd:
        return cinematic.fogEnd;
    case SkullbonezCore::UI::UICinematicParam::FogRed:
        return cinematic.fogColorR;
    case SkullbonezCore::UI::UICinematicParam::FogGreen:
        return cinematic.fogColorG;
    case SkullbonezCore::UI::UICinematicParam::FogBlue:
        return cinematic.fogColorB;
    default:
        return 0.0f;
    }
}

void SetCinematicSliderResult( SkullbonezCore::UI::InGameUIInputResult& result, const SkullbonezCore::UI::UISlider& slider,
                               int mouseX, const GameCinematicSliderSpec& spec )
{
    const SkullbonezCore::UI::CinematicSliderSpec&
        policy = SkullbonezCore::UI::kCinematicSliderSpecs[static_cast<int>( spec.param )];
    result.commands.cinematic.requestedParam = spec.param;
    result.commands.cinematic.requestedValue = slider.ValueFromMouse( mouseX, policy.minValue, policy.maxValue,
                                                                      policy.step );
}

float CinematicFeatureY( int index, float baseY )
{
    return baseY + static_cast<float>( index / 2 ) * CONTENT_TOGGLE_ROW_H;
}

float CinematicFeatureX( int index, float contentX, float colW )
{
    return ( index % 2 == 0 ) ? contentX : contentX + colW + 18.0f;
}

bool CinematicFeatureEnabled( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
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
    case SkullbonezCore::UI::UICinematicFeature::Bloom:
        return cinematic.bloomEnabled;
    case SkullbonezCore::UI::UICinematicFeature::Fog:
        return cinematic.fogEnabled;
    case SkullbonezCore::UI::UICinematicFeature::TerrainRelief:
        return cinematic.terrainReliefEnabled;
    case SkullbonezCore::UI::UICinematicFeature::Shadows:
        return cinematic.shadow.enabled;
    default:
        return false;
    }
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace CinematicTab
{

int ContentHeight()
{
    float height = UI_CINEMATIC_START_Y;

    for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
    {
        if ( kGameCinematicSliderSpecs[i].section )
        {
            height += UI_CINEMATIC_SECTION_H;
        }

        height += UI_CINEMATIC_ROW_H;
    }

    return static_cast<int>( height + 18.0f );
}

bool IsComboOpen( const UICinematicTabState& state )
{
    return state.modeCombo.IsOpen();
}

void CloseCombo( UICinematicTabState& state )
{
    state.modeCombo.Close();
}

bool HandleOpenComboClick( UICinematicTabState& state, InGameUIInputResult& result, const char* const* sceneOptions,
                           int sceneOptionCount, int mouseX, int mouseY )
{
    const char* cineSceneOptions[UI_CINE_SCENE_MAX_OPTIONS] = {};
    int cineSceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};

    const int cineSceneOptionCount = BuildCineSceneOptions( sceneOptions, sceneOptionCount, cineSceneOptions,
                                                            cineSceneIndices );

    const int option = state.modeCombo.HitOption( mouseX, mouseY, cineSceneOptionCount );

    if ( option >= 0 && option < cineSceneOptionCount )
    {
        result.commands.cinematic.requestedModeSceneIndex = cineSceneIndices[option];
        state.modeCombo.Close();
        return true;
    }

    if ( state.modeCombo.HitBox( mouseX, mouseY ) )
    {
        state.modeCombo.ToggleOpen();
        return true;
    }

    state.modeCombo.Close();
    return false;
}

bool HandleContentClick( UICinematicTabState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY,
                         float contentX, float scrolledY, float contentW )
{
    const float colW = (std::max)( 148.0f, contentW * 0.46f );

    state.modeCombo.SetBounds( contentX, scrolledY + UI_CINEMATIC_SCENE_Y, contentW, 24.0f );

    if ( state.modeCombo.HitBox( mouseX, mouseY ) )
    {
        state.modeCombo.ToggleOpen();
        return false;
    }

    const float featureBaseY = scrolledY + UI_CINEMATIC_FEATURE_START_Y + 26.0f;

    for ( int i = 0; i < static_cast<int>( UICinematicFeature::Count ); ++i )
    {
        const float tx = CinematicFeatureX( i, contentX, colW );
        const float toggleY = CinematicFeatureY( i, featureBaseY );
        state.featureToggles[i].SetBounds( tx, toggleY, colW, 24.0f );

        if ( state.featureToggles[i].HitTest( mouseX, mouseY ) )
        {
            result.commands.cinematic.requestedFeature = kGameCinematicFeatureSpecs[i].feature;
            return false;
        }
    }

    const float rowBase = scrolledY + UI_CINEMATIC_START_Y;

    for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
    {
        state.sliders[i].SetBounds( contentX, CinematicSliderY( i, rowBase ), contentW, 34.0f );

        if ( state.sliders[i].HitTest( mouseX, mouseY ) )
        {
            activeSlider = UI_CINEMATIC_SLIDER_BASE + i;
            SetCinematicSliderResult( result, state.sliders[i], mouseX, kGameCinematicSliderSpecs[i] );
            state.modeCombo.Close();
            return true;
        }
    }

    return false;
}

bool UpdateActiveSlider( UICinematicTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    // Lifetime: activeSlider is shared across all tabs. The Cine tab accepts
    // only its own id range before writing a cinematic command.
    const int cinematicSlider = CinematicSliderIndexFromActiveSlider( activeSlider );

    if ( cinematicSlider < 0 )
    {
        return false;
    }

    SetCinematicSliderResult( result, state.sliders[cinematicSlider], mouseX, kGameCinematicSliderSpecs[cinematicSlider] );
    return true;
}

bool CommitActiveSlider( UICinematicTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    return UpdateActiveSlider( state, activeSlider, mouseX, result );
}

void DrawHitboxes( const UICinematicTabState& state, const UIDrawContext& draw, const UICinematicTabFrameView& data,
                   float contentR, float contentG, float contentB )
{
    const char* labels[UI_CINE_SCENE_MAX_OPTIONS] = {};
    int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};

    const int cineSceneOptionCount = BuildCineSceneOptions( data.sceneOptions, data.sceneOptionCount, labels, sceneIndices );

    DrawComboHitboxes( draw, state.modeCombo, cineSceneOptionCount, contentR, contentG, contentB );

    for ( int i = 0; i < static_cast<int>( UICinematicFeature::Count ); ++i )
    {
        DrawHitboxRect( draw, state.featureToggles[i].Bounds(), contentR, contentG, contentB );
    }

    for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
    {
        DrawHitboxRect( draw, state.sliders[i].Bounds(), contentR, contentG, contentB );
    }
}

void Draw( UICinematicTabState& state, const UIDrawContext& draw, const UICinematicTabFrameView& data, float contentX,
           float contentY, float contentW, float contentH, float scrolledY, int mouseX, int mouseY )
{
    char buf[128];
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const char* cineSceneOptions[UI_CINE_SCENE_MAX_OPTIONS] = {};

    int cineSceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};

    const int cineSceneOptionCount = BuildCineSceneOptions( data.sceneOptions, data.sceneOptionCount, cineSceneOptions,
                                                            cineSceneIndices );

    const int selectedCineSceneOption = SelectedCineSceneOption( cineSceneIndices, cineSceneOptionCount,
                                                                 data.selectedCineModeSceneOption );

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Cine" );
    state.modeCombo.SetBounds( contentX, scrolledY + UI_CINEMATIC_SCENE_Y, contentW, 24.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + UI_CINEMATIC_SCENE_Y, 24.0f ) )
    {
        state.modeCombo.Draw( draw, "Mode", cineSceneOptions, cineSceneOptionCount, selectedCineSceneOption, mouseX,
                              mouseY );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + UI_CINEMATIC_FEATURE_START_Y, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + UI_CINEMATIC_FEATURE_START_Y, 12.0f, "Passes" );
    }

    const float featureBaseY = scrolledY + UI_CINEMATIC_FEATURE_START_Y + 26.0f;

    for ( int i = 0; i < static_cast<int>( UICinematicFeature::Count ); ++i )
    {
        const float tx = CinematicFeatureX( i, contentX, colW );
        const float toggleY = CinematicFeatureY( i, featureBaseY );
        DrawContentToggle( draw, contentY, contentH, state.featureToggles[i], tx, toggleY, colW,
                           kGameCinematicFeatureSpecs[i].label,
                           CinematicFeatureEnabled( data.cinematic, kGameCinematicFeatureSpecs[i].feature ) );
    }

    const float baseY = scrolledY + UI_CINEMATIC_START_Y;

    for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
    {
        const GameCinematicSliderSpec& spec = kGameCinematicSliderSpecs[i];
        const SkullbonezCore::UI::CinematicSliderSpec&
            policy = SkullbonezCore::UI::kCinematicSliderSpecs[static_cast<int>( spec.param )];
        const float sliderY = CinematicSliderY( i, baseY );

        if ( spec.section && IsRowVisible( contentY, contentH, sliderY - UI_CINEMATIC_SECTION_H + 4.0f, 18.0f ) )
        {
            DrawSectionTitle( draw, contentX, contentY, contentH, sliderY - UI_CINEMATIC_SECTION_H + 4.0f, 12.0f,
                              spec.section );
        }

        const float value = std::clamp( CinematicValueForParam( data.cinematic, spec.param ), policy.minValue,
                                        policy.maxValue );

        snprintf( buf, sizeof( buf ), policy.valueFormat, value );
        state.sliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );

        if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
        {
            state.sliders[i].Draw( draw, spec.label, buf, value, policy.minValue, policy.maxValue );
        }
    }
}

} // namespace CinematicTab
} // namespace UI
} // namespace SkullbonezCore
