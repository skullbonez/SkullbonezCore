/*
File: SkullbonezSource/UI/SkullbonezUI.cpp
Purpose:
  Implements SkullbonezUI widgets, layout, drawing, or UI state for the in-engine controls.

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
  - SkullbonezSource/UI/SkullbonezUI.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezUI.h"
#include "../SkullbonezPhysicsDebugVisualizer.h"
#include "../SkullbonezProfiler.h"
#include "../SkullbonezText.h"
#include "UIDraw.h"
#include "UIDrawList.h"
#include "UIDrawWidgets.h"
#include "UIInput.h"
#include "UILayout.h"
#include "UITabControls.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include "UIStyle.h"
#include "UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::Layout;

namespace
{
uint32_t HashCombine( uint32_t seed, uint32_t value )
{
    seed ^= value;
    seed *= 16777619u;
    return seed;
}


uint32_t HashTextValue( uint32_t seed, const char* value )
{
    if ( !value )
    {
        return HashCombine( seed, 0u );
    }

    while ( *value != '\0' )
    {
        seed = HashCombine( seed, static_cast<uint8_t>( *value ) );
        ++value;
    }
    return HashCombine( seed, 0u );
}


uint32_t HashBool( uint32_t seed, bool value )
{
    return HashCombine( seed, value ? 1u : 0u );
}


uint32_t HashInt( uint32_t seed, int value )
{
    return HashCombine( seed, static_cast<uint32_t>( value ) );
}


uint32_t HashFloat( uint32_t seed, float value, float scale = 100.0f )
{
    return HashInt( seed, static_cast<int>( std::round( value * scale ) ) );
}


int GetRendererIndexFromName( const char* rendererName )
{
    if ( rendererName && strstr( rendererName, "12" ) )
    {
        return RENDERER_DX12;
    }
    if ( rendererName && strstr( rendererName, "11" ) )
    {
        return RENDERER_DX11;
    }
    return RENDERER_GL;
}


uint32_t BuildUIContentSignature( const InGameUIFrameData& data, int currentRendererIndex )
{
    uint32_t hash = 2166136261u;
    hash = HashTextValue( hash, data.rendererName );
    hash = HashTextValue( hash, data.sceneName );
    hash = HashInt( hash, currentRendererIndex );
    hash = HashInt( hash, data.sceneOptionCount );
    hash = HashInt( hash, data.selectedSceneOption );
    hash = HashInt( hash, data.selectedCineModeSceneOption );
    for ( int i = 0; i < data.sceneOptionCount && data.sceneOptions; ++i )
    {
        hash = HashTextValue( hash, data.sceneOptions[i] );
    }
    hash = HashInt( hash, data.drawCallsBeforeUI );
    hash = HashInt( hash, data.UIDrawCalls );
    hash = HashFloat( hash, data.fps );
    hash = HashFloat( hash, data.renderMs, 1000.0f );
    hash = HashFloat( hash, data.physicsMs, 1000.0f );
    hash = HashFloat( hash, data.cpuFrameMs, 1000.0f );
    hash = HashFloat( hash, data.gpuFrameMs, 1000.0f );
    hash = HashInt( hash, data.modelCount );
    hash = HashInt( hash, data.currentFrame );
    hash = HashInt( hash, data.targetFrameCount );
    hash = HashInt( hash, static_cast<int>( data.rngSeed ) );
    hash = HashInt( hash, data.solverBallCount );
    hash = HashInt( hash, data.solverBoxCount );
    hash = HashInt( hash, data.currentSceneIndex );
    hash = HashInt( hash, data.sceneCount );
    hash = HashInt( hash, static_cast<int>( std::round( data.now * 1000.0 ) ) );
    hash = HashBool( hash, data.sceneMode );
    hash = HashBool( hash, data.scenePhysicsEnabled );
    hash = HashBool( hash, data.sceneTextEnabled );
    hash = HashBool( hash, data.textOnly );
    hash = HashBool( hash, data.fixedStep );
    hash = HashBool( hash, data.exitOnComplete );
    hash = HashBool( hash, data.testComplete );
    hash = HashBool( hash, data.vsyncEnabled );
    hash = HashBool( hash, data.pipelineSyncEnabled );
    hash = HashFloat( hash, data.sceneEnergy, 1000.0f );
    hash = HashFloat( hash, data.timeScale, 1000.0f );
    hash = HashFloat( hash, data.trackHeight, 1000.0f );
    hash = HashFloat( hash, data.autoCycleInterval, 1000.0f );
    hash = HashFloat( hash, data.worldGravity, 1000.0f );
    hash = HashFloat( hash, data.worldFluidHeight, 1000.0f );
    hash = HashFloat( hash, data.worldFluidDensity, 1000.0f );
    hash = HashInt( hash, static_cast<int>( data.physicsDebugFlags ) );
    hash = HashTextValue( hash, data.physicsPipelineStageName );
    hash = HashInt( hash, data.physicsPipelineStageIndex );
    hash = HashInt( hash, data.physicsPipelineStageCount );
    hash = HashFloat( hash, data.physicsDebugAlpha, 1000.0f );
    hash = HashFloat( hash, data.physicsDebugContactLinger, 1000.0f );
    hash = HashBool( hash, data.physicsSleepEnabled );
    hash = HashBool( hash, data.collisionVisualizer );
    hash = HashBool( hash, data.physicsDebugTransparent );
    hash = HashBool( hash, data.broadphaseOverlay );
    hash = HashBool( hash, data.tornadoEnabled );
    hash = HashBool( hash, data.tornadoFieldVectors );
    hash = HashFloat( hash, data.tornadoRadius, 100.0f );
    hash = HashFloat( hash, data.tornadoHeight, 100.0f );
    hash = HashFloat( hash, data.tornadoInwardAcceleration, 100.0f );
    hash = HashFloat( hash, data.tornadoSwirlAcceleration, 100.0f );
    hash = HashFloat( hash, data.tornadoLiftAcceleration, 100.0f );
    hash = HashBool( hash, data.waterFreezeDebug );
    hash = HashBool( hash, data.waterFlatDebug );
    hash = HashBool( hash, data.terrainHidden );
    hash = HashBool( hash, data.waterHidden );
    hash = HashBool( hash, data.waterNoReflect );
    hash = HashBool( hash, data.waterRTReflect );
    hash = HashBool( hash, data.cameraMouseActive );
    hash = HashBool( hash, data.nativeCursorVisible );
    hash = HashBool( hash, data.canSaveSceneDefaults );
    hash = HashBool( hash, data.cinematicRendering );
    hash = HashBool( hash, data.cinematic.enabled );
    hash = HashBool( hash, data.cinematic.skyAtmosphereEnabled );
    hash = HashBool( hash, data.cinematic.cloudsEnabled );
    hash = HashBool( hash, data.cinematic.godRaysEnabled );
    hash = HashBool( hash, data.cinematic.volumetricLightingEnabled );
    hash = HashBool( hash, data.cinematic.bloomEnabled );
    hash = HashBool( hash, data.cinematic.fogEnabled );
    hash = HashBool( hash, data.cinematic.terrainReliefEnabled );
    hash = HashBool( hash, data.cinematic.shadowsEnabled );
    hash = HashFloat( hash, data.cinematic.exposure, 1000.0f );
    hash = HashFloat( hash, data.cinematic.gamma, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunScreenX, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunScreenY, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunIntensity, 100.0f );
    hash = HashFloat( hash, data.cinematic.skyGlowStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudCoverage, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudSoftness, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudScale, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudIntensity, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunShaftStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunShaftFalloff, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricDensity, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricDecay, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomThreshold, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomKnee, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomRadius, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainRelief, 1000.0f );
    hash = HashFloat( hash, data.cinematic.basinDepth, 100.0f );
    hash = HashFloat( hash, data.cinematic.basinRimLift, 100.0f );
    hash = HashFloat( hash, data.cinematic.fogColorR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogColorG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogColorB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogStart, 10.0f );
    hash = HashFloat( hash, data.cinematic.fogEnd, 10.0f );
    hash = HashFloat( hash, data.cinematic.fogDensity, 100000.0f );
    hash = HashFloat( hash, data.cinematic.fogMaxOpacity, 1000.0f );
    hash = HashInt( hash, data.cinematic.skyMode );
    hash = HashInt( hash, data.cinematic.terrainMode );
    hash = HashInt( hash, data.cinematic.objectStyle );
    hash = HashInt( hash, data.cinematic.waterMode );
    hash = HashFloat( hash, data.cinematic.styleSaturation, 1000.0f );
    hash = HashFloat( hash, data.cinematic.styleContrast, 1000.0f );
    hash = HashFloat( hash, data.cinematic.styleVignette, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainGridScale, 100.0f );
    hash = HashFloat( hash, data.cinematic.terrainGridStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterAlpha, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterReflectionStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterGlintStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.basinCenterX, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinCenterZ, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinRadiusX, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinRadiusZ, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinFeather, 1000.0f );
    return hash;
}


uint32_t BuildUIInteractionSignature( int mouseX, int mouseY, bool rendererOpen, bool reflectionOpen, bool sceneOpen, bool cineSceneOpen, int activeSlider )
{
    uint32_t hash = 2166136261u;
    hash = HashInt( hash, mouseX );
    hash = HashInt( hash, mouseY );
    hash = HashBool( hash, rendererOpen );
    hash = HashBool( hash, reflectionOpen );
    hash = HashBool( hash, sceneOpen );
    hash = HashBool( hash, cineSceneOpen );
    hash = HashInt( hash, activeSlider );
    return hash;
}


void FlushUIDrawList( const UIDrawList& drawList, int screenW, int screenH, float offsetX = 0.0f, float offsetY = 0.0f )
{
    PROFILE_GPU_BEGIN( "Frame/UI/Draw" );
    const UIDrawContext immediateDraw( screenW, screenH );
    drawList.Flush( immediateDraw, offsetX, offsetY );
    Text2d::FlushQuads();
    Text2d::FlushText();
    PROFILE_GPU_END( "Frame/UI/Draw" );
}

int WaterReflectionModeFromData( const InGameUIFrameData& data )
{
    if ( data.waterNoReflect )
    {
        return 2;
    }
    return data.waterRTReflect ? 1 : 0;
}

constexpr int UI_CINEMATIC_SLIDER_BASE = 5000;
constexpr int UI_CINE_SCENE_MAX_OPTIONS = 32;
constexpr float UI_CINEMATIC_SCENE_Y = 42.0f;
constexpr float UI_CINEMATIC_FEATURE_START_Y = 96.0f;
constexpr float UI_CINEMATIC_START_Y = 266.0f;
constexpr float UI_CINEMATIC_SECTION_H = 28.0f;
constexpr float UI_CINEMATIC_ROW_H = 42.0f;

struct CinematicSliderSpec
{
    // One row in the Cine tab. Keeping label/range/step together makes it clear
    // which UI slider controls which render setting.
    const char* section;
    const char* label;
    UICinematicParam param;
    float minValue;
    float maxValue;
    float step;
    const char* valueFormat;
};

struct CinematicFeatureSpec
{
    // One toggle in the Cine tab, such as Bloom or Fog.
    const char* label;
    UICinematicFeature feature;
};

constexpr CinematicSliderSpec kCinematicSliderSpecs[] = {
    { "Tonemap", "Exposure", UICinematicParam::Exposure, 0.05f, 3.00f, 0.01f, "%.2f" },
    { nullptr, "Gamma", UICinematicParam::Gamma, 1.00f, 3.00f, 0.01f, "%.2f" },
    { "Style", "Sky mode", UICinematicParam::SkyMode, 0.00f, 32.00f, 1.00f, "%.0f" },
    { nullptr, "Terrain mode", UICinematicParam::TerrainMode, 0.00f, 32.00f, 1.00f, "%.0f" },
    { nullptr, "Object style", UICinematicParam::ObjectStyle, 0.00f, 32.00f, 1.00f, "%.0f" },
    { nullptr, "Water mode", UICinematicParam::WaterMode, 0.00f, 4.00f, 1.00f, "%.0f" },
    { nullptr, "Saturation", UICinematicParam::StyleSaturation, 0.00f, 2.50f, 0.01f, "%.2f" },
    { nullptr, "Contrast", UICinematicParam::StyleContrast, 0.00f, 2.50f, 0.01f, "%.2f" },
    { nullptr, "Vignette", UICinematicParam::StyleVignette, 0.00f, 1.00f, 0.01f, "%.2f" },
    { "Sun", "Sun X", UICinematicParam::SunX, 0.00f, 1.00f, 0.005f, "%.3f" },
    { nullptr, "Sun Y", UICinematicParam::SunY, 0.00f, 1.00f, 0.005f, "%.3f" },
    { nullptr, "Brightness", UICinematicParam::SunBrightness, 0.00f, 40.00f, 0.10f, "%.1f" },
    { nullptr, "Sun R", UICinematicParam::SunRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun G", UICinematicParam::SunGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun B", UICinematicParam::SunBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { "Sky", "Glow", UICinematicParam::SkyGlow, 0.00f, 8.00f, 0.05f, "%.2f" },
    { nullptr, "Horizon R", UICinematicParam::HorizonRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Horizon G", UICinematicParam::HorizonGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Horizon B", UICinematicParam::HorizonBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Zenith R", UICinematicParam::ZenithRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Zenith G", UICinematicParam::ZenithGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Zenith B", UICinematicParam::ZenithBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Clouds", "Coverage", UICinematicParam::CloudCoverage, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Softness", UICinematicParam::CloudSoftness, 0.01f, 0.65f, 0.01f, "%.2f" },
    { nullptr, "Scale", UICinematicParam::CloudScale, 0.50f, 12.00f, 0.05f, "%.2f" },
    { nullptr, "Intensity", UICinematicParam::CloudIntensity, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Shafts", "Strength", UICinematicParam::ShaftStrength, 0.00f, 3.00f, 0.01f, "%.2f" },
    { nullptr, "Falloff", UICinematicParam::ShaftFalloff, 0.25f, 5.00f, 0.01f, "%.2f" },
    { "Volume", "Strength", UICinematicParam::VolumetricStrength, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Density", UICinematicParam::VolumetricDensity, 0.00f, 2.50f, 0.01f, "%.2f" },
    { nullptr, "Decay", UICinematicParam::VolumetricDecay, 0.800f, 0.995f, 0.001f, "%.3f" },
    { "Bloom", "Threshold", UICinematicParam::BloomThreshold, 0.00f, 4.00f, 0.01f, "%.2f" },
    { nullptr, "Knee", UICinematicParam::BloomKnee, 0.01f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Strength", UICinematicParam::BloomStrength, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Radius", UICinematicParam::BloomRadius, 0.25f, 8.00f, 0.05f, "%.2f" },
    { "Terrain", "Relief", UICinematicParam::TerrainRelief, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground R", UICinematicParam::TerrainTintRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground G", UICinematicParam::TerrainTintGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground B", UICinematicParam::TerrainTintBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Accent R", UICinematicParam::TerrainAccentRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Accent G", UICinematicParam::TerrainAccentGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Accent B", UICinematicParam::TerrainAccentBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Grid scale", UICinematicParam::TerrainGridScale, 0.10f, 120.00f, 0.10f, "%.1f" },
    { nullptr, "Grid strength", UICinematicParam::TerrainGridStrength, 0.00f, 4.00f, 0.01f, "%.2f" },
    { "Water", "Water R", UICinematicParam::WaterTintRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Water G", UICinematicParam::WaterTintGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Water B", UICinematicParam::WaterTintBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Alpha", UICinematicParam::WaterAlpha, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Reflection", UICinematicParam::WaterReflection, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Glint", UICinematicParam::WaterGlint, 0.00f, 4.00f, 0.01f, "%.2f" },
    { "Basin", "Center X", UICinematicParam::BasinCenterX, 0.00f, 1200.00f, 1.00f, "%.0f" },
    { nullptr, "Center Z", UICinematicParam::BasinCenterZ, 0.00f, 1200.00f, 1.00f, "%.0f" },
    { nullptr, "Radius X", UICinematicParam::BasinRadiusX, 1.00f, 500.00f, 1.00f, "%.0f" },
    { nullptr, "Radius Z", UICinematicParam::BasinRadiusZ, 1.00f, 500.00f, 1.00f, "%.0f" },
    { nullptr, "Feather", UICinematicParam::BasinFeather, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Basin Depth", UICinematicParam::BasinDepth, 0.00f, 80.00f, 1.00f, "%.0f" },
    { nullptr, "Rim Lift", UICinematicParam::BasinRimLift, 0.00f, 60.00f, 1.00f, "%.0f" },
    { "Fog", "Density", UICinematicParam::FogDensity, 0.00000f, 0.00600f, 0.00005f, "%.5f" },
    { nullptr, "Opacity", UICinematicParam::FogOpacity, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Start", UICinematicParam::FogStart, 0.00f, 500.00f, 1.00f, "%.0f" },
    { nullptr, "End", UICinematicParam::FogEnd, 100.00f, 4000.00f, 10.00f, "%.0f" },
    { nullptr, "Fog R", UICinematicParam::FogRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Fog G", UICinematicParam::FogGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Fog B", UICinematicParam::FogBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
};
static_assert( sizeof( kCinematicSliderSpecs ) / sizeof( kCinematicSliderSpecs[0] ) == static_cast<int>( UICinematicParam::Count ),
               "Cinematic slider specs must match UICinematicParam." );

constexpr CinematicFeatureSpec kCinematicFeatureSpecs[] = {
    { "Sky", UICinematicFeature::Sky },
    { "Clouds", UICinematicFeature::Clouds },
    { "God rays", UICinematicFeature::GodRays },
    { "Volume", UICinematicFeature::VolumetricLight },
    { "Bloom", UICinematicFeature::Bloom },
    { "Fog", UICinematicFeature::Fog },
    { "Relief", UICinematicFeature::TerrainRelief },
    { "Shadows", UICinematicFeature::Shadows },
};
static_assert( sizeof( kCinematicFeatureSpecs ) / sizeof( kCinematicFeatureSpecs[0] ) == static_cast<int>( UICinematicFeature::Count ),
               "Cinematic feature specs must match UICinematicFeature." );

constexpr int UI_WHATS_NEW_CONTENT_HEIGHT = 470;
constexpr float UI_WHATS_NEW_CARD_H = 134.0f;
constexpr float UI_WHATS_NEW_CARD_GAP = 8.0f;
constexpr float UI_WHATS_NEW_CONTROL_Y = 66.0f;
constexpr float UI_WHATS_NEW_SLIDER_Y = 90.0f;
constexpr float UI_WHATS_NEW_SLIDER_DESC_Y = 122.0f;
constexpr int UI_WHATS_NEW_TOGGLE_GRAPHITE = 0;
constexpr int UI_WHATS_NEW_TOGGLE_ASSET_REGISTRY = 1;
constexpr int UI_WHATS_NEW_TOGGLE_DX12_GATE = 2;
constexpr int UI_WHATS_NEW_SLIDER_UI_FILES = 0;
constexpr int UI_WHATS_NEW_SLIDER_TEXTURES = 1;
constexpr int UI_WHATS_NEW_SLIDER_PARITY = 2;

bool IsBlockVisible( float contentY, float contentH, float blockY, float blockH )
{
    return blockY + blockH >= contentY && blockY <= contentY + contentH;
}

void DrawHitboxRect( const UIDrawContext& draw, const UIRect& bounds, float r, float g, float b, float fillA = 0.060f, float outlineA = 0.94f )
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

void DrawComboHitboxes( const UIDrawContext& draw, const UIComboBox& combo, int optionCount, float r, float g, float b )
{
    DrawHitboxRect( draw, combo.Bounds(), r, g, b );
    if ( combo.IsOpen() )
    {
        DrawHitboxRect( draw, combo.DropdownBounds( optionCount ), 0.18f, 0.58f, 1.0f, 0.078f, 0.96f );
    }
}

void DrawTabHitboxes( const UIDrawContext& draw, const UITabBar& tabBar, int tabCount )
{
    const UIRect tabs = tabBar.Bounds();
    if ( tabCount <= 0 || tabs.w <= 0.0f || tabs.h <= 0.0f )
    {
        return;
    }

    const float tabW = tabs.w / static_cast<float>( tabCount );
    for ( int i = 0; i < tabCount; ++i )
    {
        DrawHitboxRect( draw, { tabs.x + static_cast<float>( i ) * tabW, tabs.y, tabW, tabs.h }, 1.0f, 0.80f, 0.18f, 0.052f, 0.84f );
    }
}

int SceneDropdownHitboxOptionCount( const SceneTab::UISceneTabState& state, const InGameUIFrameData& data )
{
    const int filteredSceneCount = SceneTab::CountFilteredOptions( data.sceneOptions, data.sceneOptionCount, state.filter );
    const int sceneVisibleCount = SceneComboVisibleCount( filteredSceneCount );
    return sceneVisibleCount == 0 && state.filter[0] != '\0' ? 1 : sceneVisibleCount;
}

void EllipsizeToWidth( char* text, size_t textSize, float pxSize, float maxWidth )
{
    if ( !text || textSize == 0 || Text2d::MeasureText( pxSize, text ) <= maxWidth )
    {
        return;
    }

    size_t len = strlen( text );
    while ( len > 3 && Text2d::MeasureText( pxSize, text ) > maxWidth )
    {
        text[len - 3] = '.';
        text[len - 2] = '.';
        text[len - 1] = '.';
        text[len] = '\0';
        --len;
    }
}

void DrawFittedText( const UIDrawContext& draw, float x, float y, float pxSize, const Style::UIColor& color, const char* value, float maxWidth )
{
    char text[192] = {};
    snprintf( text, sizeof( text ), "%s", value ? value : "" );
    EllipsizeToWidth( text, sizeof( text ), pxSize, maxWidth );
    draw.Text( x, y, pxSize, color.r, color.g, color.b, text );
}

void DrawFittedContentText( const UIDrawContext& draw,
                            float contentY,
                            float contentH,
                            float x,
                            float y,
                            float pxSize,
                            const Style::UIColor& color,
                            const char* value,
                            float maxWidth )
{
    if ( !IsRowVisible( contentY, contentH, y, pxSize + 4.0f ) )
    {
        return;
    }

    DrawFittedText( draw, x, y, pxSize, color, value, maxWidth );
}

void DrawWhatsNewCard( const UIDrawContext& draw,
                       float contentY,
                       float contentH,
                       float x,
                       float y,
                       float w,
                       float h,
                       const char* title,
                       const char* tag,
                       const char* line1,
                       const char* line2 )
{
    if ( !IsRowVisible( contentY, contentH, y, h ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedPanel( { x, y, w, h }, Style::Radii().window, palette.windowSubtle, palette.border );
    DrawFittedText( draw, x + 14.0f, y + 12.0f, 12.5f, palette.textPrimary, title, w - 132.0f );
    DrawFittedText( draw, x + w - 112.0f, y + 13.0f, 9.5f, palette.textMuted, tag, 100.0f );
    DrawFittedText( draw, x + 14.0f, y + 34.0f, 10.0f, palette.textSecondary, line1, w - 28.0f );
    DrawFittedText( draw, x + 14.0f, y + 49.0f, 10.0f, palette.textMuted, line2, w - 28.0f );
}

void DrawWhatsNewDescription( const UIDrawContext& draw, float contentY, float contentH, float x, float y, float w, const char* text )
{
    if ( !IsRowVisible( contentY, contentH, y, 14.0f ) )
    {
        return;
    }

    DrawFittedText( draw, x, y, 9.5f, Style::Palette().textMuted, text, w );
}

void SetWhatsNewControlBounds( UICheckBox toggles[3],
                               UISlider statusSliders[3],
                               float contentX,
                               float rowBase,
                               float contentW )
{
    const float innerX = contentX + 16.0f;
    const float innerW = (std::max)( 180.0f, contentW - 32.0f );
    const float firstCardY = 42.0f;
    const float secondCardY = firstCardY + UI_WHATS_NEW_CARD_H + UI_WHATS_NEW_CARD_GAP;
    const float thirdCardY = secondCardY + UI_WHATS_NEW_CARD_H + UI_WHATS_NEW_CARD_GAP;

    toggles[UI_WHATS_NEW_TOGGLE_GRAPHITE].SetBounds( innerX, rowBase + firstCardY + UI_WHATS_NEW_CONTROL_Y, 188.0f, 24.0f );
    statusSliders[UI_WHATS_NEW_SLIDER_UI_FILES].SetBounds( innerX, rowBase + firstCardY + UI_WHATS_NEW_SLIDER_Y, innerW, 34.0f );

    toggles[UI_WHATS_NEW_TOGGLE_ASSET_REGISTRY].SetBounds( innerX, rowBase + secondCardY + UI_WHATS_NEW_CONTROL_Y, 188.0f, 24.0f );
    statusSliders[UI_WHATS_NEW_SLIDER_TEXTURES].SetBounds( innerX, rowBase + secondCardY + UI_WHATS_NEW_SLIDER_Y, innerW, 34.0f );

    toggles[UI_WHATS_NEW_TOGGLE_DX12_GATE].SetBounds( innerX, rowBase + thirdCardY + UI_WHATS_NEW_CONTROL_Y, 188.0f, 24.0f );
    statusSliders[UI_WHATS_NEW_SLIDER_PARITY].SetBounds( innerX, rowBase + thirdCardY + UI_WHATS_NEW_SLIDER_Y, innerW, 34.0f );
}

void DrawWhatsNewTab( UICheckBox toggles[3],
                      UISlider statusSliders[3],
                      const UIDrawContext& draw,
                      float contentX,
                      float contentY,
                      float contentW,
                      float contentH,
                      float scrolledY )
{
    const Style::UIPalette& palette = Style::Palette();
    const float innerX = contentX + 16.0f;
    const float descX = innerX + 210.0f;
    const float descW = (std::max)( 120.0f, contentX + contentW - descX - 16.0f );
    const float firstCardY = 42.0f;
    const float secondCardY = firstCardY + UI_WHATS_NEW_CARD_H + UI_WHATS_NEW_CARD_GAP;
    const float thirdCardY = secondCardY + UI_WHATS_NEW_CARD_H + UI_WHATS_NEW_CARD_GAP;

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "WHATS NEW" );
    DrawFittedContentText( draw,
                           contentY,
                           contentH,
                           contentX,
                           scrolledY + 20.0f,
                           10.0f,
                           palette.textSecondary,
                           "Latest completed implementation PRs only.",
                           contentW );

    DrawWhatsNewCard( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + firstCardY,
                      contentW,
                      UI_WHATS_NEW_CARD_H,
                      "Graphite overlay UI",
                      "PR #58",
                      "Matte graphite surfaces, rounded controls, sage accents, and the WHATS NEW landing tab.",
                      "The shared UI draw path now uses style tokens and rounded panel primitives." );
    if ( IsRowVisible( contentY, contentH, scrolledY + firstCardY + UI_WHATS_NEW_CONTROL_Y, 24.0f ) )
    {
        toggles[UI_WHATS_NEW_TOGGLE_GRAPHITE].DrawToggle( draw, "Graphite restyle", true, palette.accent.r, palette.accent.g, palette.accent.b );
        DrawWhatsNewDescription( draw, contentY, contentH, descX, scrolledY + firstCardY + UI_WHATS_NEW_CONTROL_Y + 4.0f, descW, "Shows that the overlay styling pass is active." );
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + firstCardY + UI_WHATS_NEW_SLIDER_Y, 34.0f ) )
    {
        statusSliders[UI_WHATS_NEW_SLIDER_UI_FILES].Draw( draw, "UI files touched", "22 / 22", 22.0f, 0.0f, 22.0f );
        DrawWhatsNewDescription( draw, contentY, contentH, innerX, scrolledY + firstCardY + UI_WHATS_NEW_SLIDER_DESC_Y, contentW - 32.0f, "Tracks the graphite PR surface area instead of changing scene state." );
    }

    DrawWhatsNewCard( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + secondCardY,
                      contentW,
                      UI_WHATS_NEW_CARD_H,
                      "Asset texture registry",
                      "PR #57",
                      "Textures now have stable source records while legacy numeric texture hashes keep working.",
                      "Renderer switches can rebuild registered GPU handles from the source registry." );
    if ( IsRowVisible( contentY, contentH, scrolledY + secondCardY + UI_WHATS_NEW_CONTROL_Y, 24.0f ) )
    {
        toggles[UI_WHATS_NEW_TOGGLE_ASSET_REGISTRY].DrawToggle( draw, "Source records", true, palette.accent.r, palette.accent.g, palette.accent.b );
        DrawWhatsNewDescription( draw, contentY, contentH, descX, scrolledY + secondCardY + UI_WHATS_NEW_CONTROL_Y + 4.0f, descW, "Indicates built-in texture sources are registered." );
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + secondCardY + UI_WHATS_NEW_SLIDER_Y, 34.0f ) )
    {
        statusSliders[UI_WHATS_NEW_SLIDER_TEXTURES].Draw( draw, "Built-ins indexed", "8 / 8", 8.0f, 0.0f, 8.0f );
        DrawWhatsNewDescription( draw, contentY, contentH, innerX, scrolledY + secondCardY + UI_WHATS_NEW_SLIDER_DESC_Y, contentW - 32.0f, "Eight default texture assets are available to dump and rebuild." );
    }

    DrawWhatsNewCard( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + thirdCardY,
                      contentW,
                      UI_WHATS_NEW_CARD_H,
                      "Validation harness upgrade",
                      "PR #56",
                      "Renderer validation now writes manifests, summaries, heatmaps, and explicit DX12 gate output.",
                      "The parity budget is visible here so the gate is easy to interpret." );
    if ( IsRowVisible( contentY, contentH, scrolledY + thirdCardY + UI_WHATS_NEW_CONTROL_Y, 24.0f ) )
    {
        toggles[UI_WHATS_NEW_TOGGLE_DX12_GATE].DrawToggle( draw, "DX12 gate", true, palette.accent.r, palette.accent.g, palette.accent.b );
        DrawWhatsNewDescription( draw, contentY, contentH, descX, scrolledY + thirdCardY + UI_WHATS_NEW_CONTROL_Y + 4.0f, descW, "Clean runs report zero DX12 validation errors." );
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + thirdCardY + UI_WHATS_NEW_SLIDER_Y, 34.0f ) )
    {
        statusSliders[UI_WHATS_NEW_SLIDER_PARITY].Draw( draw, "Pixel diff budget", "avg < 10", 10.0f, 0.0f, 10.0f );
        DrawWhatsNewDescription( draw, contentY, contentH, innerX, scrolledY + thirdCardY + UI_WHATS_NEW_SLIDER_DESC_Y, contentW - 32.0f, "Renderer pairs must remain under this average pixel difference." );
    }
}

bool IsCineSceneOptionName( const char* name )
{
    if ( !name )
    {
        return false;
    }
    return strncmp( name, "concept_", 8 ) == 0 ||
           strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr ||
           strstr( name, "cine_" ) == name;
}

int BuildCineSceneOptions( const char* const* sceneOptions,
                           int sceneOptionCount,
                           const char* labels[UI_CINE_SCENE_MAX_OPTIONS],
                           int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS] )
{
    int count = 0;
    labels[count] = SceneTab::DEMO_SCENE_OPTION;
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

int SelectedCineSceneOption( const int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS], int cineOptionCount, int selectedSceneOption )
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
    // Other UI tabs already use m_activeSlider. Give Cine sliders their own id
    // range so dragging can continue even if the mouse leaves the slider bounds.
    const int index = activeSlider - UI_CINEMATIC_SLIDER_BASE;
    return ( index >= 0 && index < static_cast<int>( UICinematicParam::Count ) ) ? index : -1;
}

float CinematicSliderY( int index, float baseY )
{
    // Sections add extra vertical space. Calculating this from the spec array
    // keeps hit testing and drawing in lockstep.
    float y = baseY;
    for ( int i = 0; i <= index; ++i )
    {
        if ( kCinematicSliderSpecs[i].section )
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

int CinematicContentHeight()
{
    float height = UI_CINEMATIC_START_Y;
    for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
    {
        if ( kCinematicSliderSpecs[i].section )
        {
            height += UI_CINEMATIC_SECTION_H;
        }
        height += UI_CINEMATIC_ROW_H;
    }
    return static_cast<int>( height + 18.0f );
}

float CinematicValueForParam( const CinematicRenderConfig& cinematic, UICinematicParam param )
{
    // Read the live value for a Cine slider. This is the inverse of the command
    // application in SkullbonezRunInput.cpp.
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return cinematic.exposure;
    case UICinematicParam::Gamma:
        return cinematic.gamma;
    case UICinematicParam::SkyMode:
        return static_cast<float>( cinematic.skyMode );
    case UICinematicParam::TerrainMode:
        return static_cast<float>( cinematic.terrainMode );
    case UICinematicParam::ObjectStyle:
        return static_cast<float>( cinematic.objectStyle );
    case UICinematicParam::WaterMode:
        return static_cast<float>( cinematic.waterMode );
    case UICinematicParam::StyleSaturation:
        return cinematic.styleSaturation;
    case UICinematicParam::StyleContrast:
        return cinematic.styleContrast;
    case UICinematicParam::StyleVignette:
        return cinematic.styleVignette;
    case UICinematicParam::SunX:
        return cinematic.sunScreenX;
    case UICinematicParam::SunY:
        return cinematic.sunScreenY;
    case UICinematicParam::SunBrightness:
        return cinematic.sunIntensity;
    case UICinematicParam::SunRed:
        return cinematic.sunColorR;
    case UICinematicParam::SunGreen:
        return cinematic.sunColorG;
    case UICinematicParam::SunBlue:
        return cinematic.sunColorB;
    case UICinematicParam::SkyGlow:
        return cinematic.skyGlowStrength;
    case UICinematicParam::HorizonRed:
        return cinematic.skyHorizonR;
    case UICinematicParam::HorizonGreen:
        return cinematic.skyHorizonG;
    case UICinematicParam::HorizonBlue:
        return cinematic.skyHorizonB;
    case UICinematicParam::ZenithRed:
        return cinematic.skyZenithR;
    case UICinematicParam::ZenithGreen:
        return cinematic.skyZenithG;
    case UICinematicParam::ZenithBlue:
        return cinematic.skyZenithB;
    case UICinematicParam::CloudCoverage:
        return cinematic.cloudCoverage;
    case UICinematicParam::CloudSoftness:
        return cinematic.cloudSoftness;
    case UICinematicParam::CloudScale:
        return cinematic.cloudScale;
    case UICinematicParam::CloudIntensity:
        return cinematic.cloudIntensity;
    case UICinematicParam::ShaftStrength:
        return cinematic.sunShaftStrength;
    case UICinematicParam::ShaftFalloff:
        return cinematic.sunShaftFalloff;
    case UICinematicParam::VolumetricStrength:
        return cinematic.volumetricStrength;
    case UICinematicParam::VolumetricDensity:
        return cinematic.volumetricDensity;
    case UICinematicParam::VolumetricDecay:
        return cinematic.volumetricDecay;
    case UICinematicParam::BloomThreshold:
        return cinematic.bloomThreshold;
    case UICinematicParam::BloomKnee:
        return cinematic.bloomKnee;
    case UICinematicParam::BloomStrength:
        return cinematic.bloomStrength;
    case UICinematicParam::BloomRadius:
        return cinematic.bloomRadius;
    case UICinematicParam::TerrainRelief:
        return cinematic.terrainRelief;
    case UICinematicParam::TerrainTintRed:
        return cinematic.terrainTintR;
    case UICinematicParam::TerrainTintGreen:
        return cinematic.terrainTintG;
    case UICinematicParam::TerrainTintBlue:
        return cinematic.terrainTintB;
    case UICinematicParam::TerrainAccentRed:
        return cinematic.terrainAccentR;
    case UICinematicParam::TerrainAccentGreen:
        return cinematic.terrainAccentG;
    case UICinematicParam::TerrainAccentBlue:
        return cinematic.terrainAccentB;
    case UICinematicParam::TerrainGridScale:
        return cinematic.terrainGridScale;
    case UICinematicParam::TerrainGridStrength:
        return cinematic.terrainGridStrength;
    case UICinematicParam::WaterTintRed:
        return cinematic.waterTintR;
    case UICinematicParam::WaterTintGreen:
        return cinematic.waterTintG;
    case UICinematicParam::WaterTintBlue:
        return cinematic.waterTintB;
    case UICinematicParam::WaterAlpha:
        return cinematic.waterAlpha;
    case UICinematicParam::WaterReflection:
        return cinematic.waterReflectionStrength;
    case UICinematicParam::WaterGlint:
        return cinematic.waterGlintStrength;
    case UICinematicParam::BasinCenterX:
        return cinematic.basinCenterX;
    case UICinematicParam::BasinCenterZ:
        return cinematic.basinCenterZ;
    case UICinematicParam::BasinRadiusX:
        return cinematic.basinRadiusX;
    case UICinematicParam::BasinRadiusZ:
        return cinematic.basinRadiusZ;
    case UICinematicParam::BasinFeather:
        return cinematic.basinFeather;
    case UICinematicParam::BasinDepth:
        return cinematic.basinDepth;
    case UICinematicParam::BasinRimLift:
        return cinematic.basinRimLift;
    case UICinematicParam::FogDensity:
        return cinematic.fogDensity;
    case UICinematicParam::FogOpacity:
        return cinematic.fogMaxOpacity;
    case UICinematicParam::FogStart:
        return cinematic.fogStart;
    case UICinematicParam::FogEnd:
        return cinematic.fogEnd;
    case UICinematicParam::FogRed:
        return cinematic.fogColorR;
    case UICinematicParam::FogGreen:
        return cinematic.fogColorG;
    case UICinematicParam::FogBlue:
        return cinematic.fogColorB;
    default:
        return 0.0f;
    }
}

void SetCinematicSliderResult( InGameUIInputResult& result, const UISlider& slider, int mouseX, const CinematicSliderSpec& spec )
{
    result.commands.cinematic.requestedParam = spec.param;
    result.commands.cinematic.requestedValue = slider.ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
}

float CinematicFeatureY( int index, float baseY )
{
    return baseY + static_cast<float>( index / 2 ) * CONTENT_TOGGLE_ROW_H;
}

float CinematicFeatureX( int index, float contentX, float colW )
{
    return ( index % 2 == 0 ) ? contentX : contentX + colW + 18.0f;
}

bool CinematicFeatureEnabled( const CinematicRenderConfig& cinematic, UICinematicFeature feature )
{
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        return cinematic.skyAtmosphereEnabled;
    case UICinematicFeature::Clouds:
        return cinematic.cloudsEnabled;
    case UICinematicFeature::GodRays:
        return cinematic.godRaysEnabled;
    case UICinematicFeature::VolumetricLight:
        return cinematic.volumetricLightingEnabled;
    case UICinematicFeature::Bloom:
        return cinematic.bloomEnabled;
    case UICinematicFeature::Fog:
        return cinematic.fogEnabled;
    case UICinematicFeature::TerrainRelief:
        return cinematic.terrainReliefEnabled;
    case UICinematicFeature::Shadows:
        return cinematic.shadowsEnabled;
    default:
        return false;
    }
}


} // namespace

bool InGameUI::IsVisible() const
{
    return m_window.isVisible;
}


void InGameUI::SetVisible( bool visible, double now )
{
    m_window.isVisible = visible;
    m_cache.Reset();
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Visibility );
    if ( visible )
    {
        m_window.isMinimized = false;
        m_scrollbarVisibleUntil = now + 1.2;
    }
    else
    {
        m_window.isMinimized = true;
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        m_interaction.blocksCameraMouse = false;
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
    }
}


void InGameUI::ToggleVisible( double now )
{
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }
    SetMinimized( !m_window.isMinimized, now );
}


void InGameUI::SetMinimized( bool minimized, double now )
{
    if ( m_window.isMinimized == minimized )
    {
        return;
    }

    const UIRect currentBounds = Chrome::WindowRect( m_window );
    const UIRect minimizedBounds = MinimizedRect( m_lastScreenW, m_lastScreenH, m_window.minimizedWidth );
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    if ( minimized )
    {
        m_window.isMinimized = true;
        Chrome::BeginWindowAnimation( m_window, currentBounds, minimizedBounds, now, true );
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        m_activeSlider = 0;
    }
    else
    {
        m_window.isMinimized = false;
        Chrome::BeginWindowAnimation( m_window, minimizedBounds, Chrome::WindowRect( m_window ), now, false );
        m_scrollbarVisibleUntil = now + 1.2;
    }
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::WindowState );
    m_cache.Reset();
}


void InGameUI::ToggleMaximizeMinimize( int screenW, int screenH, double now )
{
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }

    if ( m_window.isMinimized )
    {
        SetMinimized( false, now );
        return;
    }

    SetMaximized( !m_window.isMaximized, screenW, screenH, now );
}


void InGameUI::SetActiveTab( InGameUITab tab )
{
    m_activeTab = tab;
    m_scrollY = 0.0f;
    m_rendererCombo.Close();
    m_reflectionCombo.Close();
    CloseSceneCombo();
    m_cineSceneCombo.Close();
    m_activeSlider = 0;
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Content );
    m_cache.Reset();
}


InGameUITab InGameUI::GetActiveTab() const
{
    return m_activeTab;
}


void InGameUI::CancelInputCapture()
{
    m_interaction.leftWasDown = false;
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    m_activeSlider = 0;
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
}


bool InGameUI::BlocksCameraMouse() const
{
    return m_interaction.blocksCameraMouse;
}


bool InGameUI::BlocksKeyboard() const
{
    return m_window.isVisible && !m_window.isMinimized && ( m_sceneCombo.IsOpen() || m_cineSceneCombo.IsOpen() );
}


bool InGameUI::WantsNativeMouseCursor() const
{
    return m_window.isVisible && !m_window.isMinimized;
}


void InGameUI::SetWindowBounds( int x, int y, int width, int height )
{
    m_window.x = x;
    m_window.y = y;
    m_window.width = width;
    m_window.height = height;
    m_window.restoreX = x;
    m_window.restoreY = y;
    m_window.restoreW = width;
    m_window.restoreH = height;
    m_window.hasAppliedDefaultPlacement = true;
    m_window.isMaximized = false;
    m_window.animationActive = false;
    m_scrollY = 0.0f;
    m_scrollbarVisibleUntil = 0.0;
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
    m_cache.Reset();
}


void InGameUI::SetBlurEnabled( bool enabled )
{
    if ( m_blurPreviewEnabled != enabled )
    {
        m_blurPreviewEnabled = enabled;
        m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Toggle );
        m_cache.Reset();
    }
}


void InGameUI::SetRendererComboOpen( bool open )
{
    m_rendererCombo.SetOpen( open );
    if ( open )
    {
        m_reflectionCombo.Close();
        CloseSceneCombo();
        m_cineSceneCombo.Close();
    }
}


void InGameUI::SetWaterComboOpen( bool open )
{
    m_reflectionCombo.SetOpen( open );
    if ( open )
    {
        m_rendererCombo.Close();
        CloseSceneCombo();
        m_cineSceneCombo.Close();
    }
}


void InGameUI::SetSceneComboOpen( bool open )
{
    m_sceneCombo.SetOpen( open );
    if ( open )
    {
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        m_cineSceneCombo.Close();
        SceneTab::CaptureFilterKeyState( m_sceneTab );
    }
    else
    {
        SceneTab::ClearFilter( m_sceneTab );
    }
}


void InGameUI::SetSceneFilter( const char* filter )
{
    SceneTab::SetFilter( m_sceneTab, filter );
}


void InGameUI::SetProfilerExpandAll( bool expandAll )
{
    ProfilerTab::SetExpandAll( m_profilerTab, expandAll );
    m_cache.Reset();
}


void InGameUI::SetProfilerTimelineEnabled( bool enabled )
{
    ProfilerTab::SetTimelineEnabled( m_profilerTab, enabled );
    m_cache.Reset();
}


void InGameUI::SetPerformanceHistogramEnabled( bool enabled )
{
    ProfilerTab::SetPerformanceHistogramEnabled( m_profilerTab, enabled );
    m_cache.Reset();
}


void InGameUI::SetHitboxOverlayEnabled( bool enabled )
{
    if ( m_hitboxOverlayEnabled != enabled )
    {
        m_hitboxOverlayEnabled = enabled;
        m_cache.Reset();
    }
}


void InGameUI::SetScrollY( float scrollY )
{
    m_scrollY = (std::max)( 0.0f, scrollY );
    m_scrollbarVisibleUntil = 1.2;
    m_cache.Reset();
}


void InGameUI::SetMouseOverride( bool enabled, int x, int y )
{
    m_hasMouseOverride = enabled;
    m_mouseOverrideX = x;
    m_mouseOverrideY = y;
    if ( enabled )
    {
        m_mouseX = x;
        m_mouseY = y;
    }
}


void InGameUI::SetMaximized( bool maximized, int screenW, int screenH, double now )
{
    if ( Chrome::SetMaximized( m_window, maximized, screenW, screenH, now ) )
    {
        m_scrollbarVisibleUntil = 0.0;
        m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
    }
}


void InGameUI::ResetResources()
{
    m_backdropBlur.ResetResources();
    m_cache.Reset();
}


void InGameUI::DrawHitboxOverlay( const UIDrawContext& draw, const InGameUIFrameData& data, const UIRect& windowBounds, const UIRect& contentBounds, const UIRect& footerBounds ) const
{
    if ( !m_hitboxOverlayEnabled )
    {
        return;
    }

    constexpr float chromeR = 0.16f;
    constexpr float chromeG = 0.86f;
    constexpr float chromeB = 1.00f;
    constexpr float contentR = 0.30f;
    constexpr float contentG = 1.00f;
    constexpr float contentB = 0.42f;
    constexpr float footerR = 1.00f;
    constexpr float footerG = 0.22f;
    constexpr float footerB = 0.82f;
    constexpr float buttonR = 1.00f;
    constexpr float buttonG = 0.62f;
    constexpr float buttonB = 0.18f;

    DrawHitboxRect( draw, windowBounds, chromeR, chromeG, chromeB, 0.018f, 0.44f );

    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    DrawHitboxRect( draw, titleButtons.minimize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.maximize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.close, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    if ( !m_window.isMaximized )
    {
        DrawHitboxRect( draw, { windowBounds.x + windowBounds.w - 26.0f, windowBounds.y + windowBounds.h - 26.0f, 26.0f, 26.0f }, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    }

    DrawTabHitboxes( draw, m_tabBar, static_cast<int>( InGameUITab::Count ) );
    DrawHitboxRect( draw, contentBounds, contentR, contentG, contentB, 0.018f, 0.48f );

    switch ( m_activeTab )
    {
    case InGameUITab::WhatsNew:
        for ( int i = 0; i < 3; ++i )
        {
            DrawHitboxRect( draw, m_whatsNewToggles[i].Bounds(), contentR, contentG, contentB );
            DrawHitboxRect( draw, m_whatsNewSliders[i].Bounds(), contentR, contentG, contentB );
        }
        break;
    case InGameUITab::Scene:
        DrawComboHitboxes( draw, m_sceneCombo, SceneDropdownHitboxOptionCount( m_sceneTab, data ), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_resetSceneButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_resetDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_saveDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        break;
    case InGameUITab::Physics:
        for ( int i = 0; i < 11; ++i )
        {
            DrawHitboxRect( draw, m_physicsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }
        DrawHitboxRect( draw, m_physicsTab.pipelinePrevButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_physicsTab.pipelineNextButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_physicsTab.alphaSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.contactLingerSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.worldGravitySlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoRadiusSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoInwardSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoSwirlSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoLiftSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Options:
        for ( int i = 0; i < 6; ++i )
        {
            DrawHitboxRect( draw, m_optionsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }
        DrawHitboxRect( draw, m_optionsTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_optionsTab.modelCountSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Keys:
        DrawHitboxRect( draw, m_controlsTab.seedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.solverBallSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.solverBoxSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.worldFluidHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.worldFluidDensitySlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Cinematic:
        {
            const char* labels[UI_CINE_SCENE_MAX_OPTIONS] = {};
            int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};
            const int cineSceneOptionCount = BuildCineSceneOptions( data.sceneOptions, data.sceneOptionCount, labels, sceneIndices );
            DrawComboHitboxes( draw, m_cineSceneCombo, cineSceneOptionCount, contentR, contentG, contentB );
            for ( int i = 0; i < static_cast<int>( UICinematicFeature::Count ); ++i )
            {
                DrawHitboxRect( draw, m_cinematicFeatureToggles[i].Bounds(), contentR, contentG, contentB );
            }
            for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
            {
                DrawHitboxRect( draw, m_cinematicSliders[i].Bounds(), contentR, contentG, contentB );
            }
        }
        break;
    case InGameUITab::Profiler:
    default:
        break;
    }

    if ( ContentHeight() > static_cast<int>( contentBounds.h ) )
    {
        DrawHitboxRect( draw, m_scrollBar.Bounds(), 0.18f, 0.82f, 0.95f, 0.060f, 0.86f );
    }

    DrawHitboxRect( draw, footerBounds, footerR, footerG, footerB, 0.020f, 0.54f );
    DrawComboHitboxes( draw, m_rendererCombo, 3, footerR, footerG, footerB );
    DrawComboHitboxes( draw, m_reflectionCombo, 3, footerR, footerG, footerB );
    DrawHitboxRect( draw, m_blurToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_vsyncToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_histogramToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_timelineToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_hitboxToggle.Bounds(), footerR, footerG, footerB );
}


int InGameUI::ContentHeight() const
{
    switch ( m_activeTab )
    {
    case InGameUITab::WhatsNew:
        return UI_WHATS_NEW_CONTENT_HEIGHT;
    case InGameUITab::Keys:
        return ControlsTab::ContentHeight();
    case InGameUITab::Profiler:
        return ProfilerTab::ContentHeight( m_profilerTab );
    case InGameUITab::Physics:
        return PhysicsTab::ContentHeight();
    case InGameUITab::Options:
        return OptionsTab::ContentHeight();
    case InGameUITab::Cinematic:
        return CinematicContentHeight();
    default:
        return ControlsTab::ContentHeight();
    }
}


void InGameUI::CloseSceneCombo()
{
    SceneTab::CloseCombo( m_sceneTab, m_sceneCombo );
}


InGameUIInputResult InGameUI::UpdateInput( HWND hwnd, int screenW, int screenH, double now, const char* const* sceneOptions, int sceneOptionCount, int selectedSceneOption )
{
    PROFILE_SCOPED( "Frame/UI/Input" );
    InGameUIInputResult result;
    m_interaction.blocksCameraMouse = false;
    const InputControl::UIInputSnapshot input = InputControl::CaptureSnapshot( m_interaction.leftWasDown, m_hasMouseOverride, m_mouseOverrideX, m_mouseOverrideY );
    const int wheelDelta = input.wheelDelta;
    if ( !m_window.isVisible )
    {
        return result;
    }
    ProfilerTab::ApplyDefaultExpansion( m_profilerTab );

    m_mouseX = input.mouseX;
    m_mouseY = input.mouseY;

    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    const int minW = 520;
    const int minH = 250;
    const int margin = 10;
    const int titleH = 44;
    const int tabH = 44;
    const int bottomH = 78;
    const int contentPad = 18;
    const int maxW = (std::max)( minW, screenW - margin * 2 );
    const int maxH = (std::max)( minH, screenH - margin * 2 );

    if ( !m_window.hasAppliedDefaultPlacement )
    {
        Chrome::ApplyDefaultWindowPlacement( m_window, screenW, screenH );
    }
    Chrome::ClampWindowToScreen( m_window, screenW, screenH, minW, minH, margin );

    const bool leftNow = input.leftDown;
    if ( m_window.isMinimized )
    {
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        const bool insideMinimized = minimized.Contains( m_mouseX, m_mouseY );
        if ( input.leftPressed && insideMinimized )
        {
            SetMinimized( false, now );
            result.commands.ui.userInteracted = true;
        }
        m_interaction.leftWasDown = leftNow;
        m_interaction.blocksCameraMouse = insideMinimized;
        return result;
    }

    const UIRect inputBounds = Chrome::CurrentWindowRect( m_window, now );
    const int inputX = static_cast<int>( std::round( inputBounds.x ) );
    const int inputY = static_cast<int>( std::round( inputBounds.y ) );
    const int inputW = static_cast<int>( std::round( inputBounds.w ) );
    const int inputH = static_cast<int>( std::round( inputBounds.h ) );
    const UIRect inputHitBounds = { static_cast<float>( inputX ), static_cast<float>( inputY ), static_cast<float>( inputW ), static_cast<float>( inputH ) };
    const bool inside = m_mouseX >= inputX && m_mouseX <= inputX + inputW &&
                        m_mouseY >= inputY && m_mouseY <= inputY + inputH;
    const bool inTitle = inside && m_mouseY < inputY + titleH;
    const bool inTabs = inside && m_mouseY >= inputY + titleH && m_mouseY < inputY + titleH + tabH;
    const bool inResize = !m_window.isMaximized && inside && Chrome::IsResizeHotspot( inputHitBounds, m_mouseX, m_mouseY );
    const int contentY = inputY + titleH + tabH + 12;
    const int contentH = (std::max)( 24, inputH - titleH - tabH - bottomH - contentPad );
    const int bottomY = inputY + inputH - bottomH;
    const bool inContent = inside && m_mouseY >= contentY && m_mouseY <= contentY + contentH;
    const float maxScroll = static_cast<float>( (std::max)( 0, ContentHeight() - contentH ) );
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( inputHitBounds );

    m_tabBar.SetBounds( static_cast<float>( inputX + 14 ), static_cast<float>( inputY + titleH ), static_cast<float>( inputW - 28 ), static_cast<float>( tabH ) );
    const float footerX = static_cast<float>( inputX );
    const float footerY = static_cast<float>( bottomY );
    const UIRect rendererComboBounds = FooterRendererComboBounds( footerX, footerY );
    const UIRect waterComboBounds = FooterWaterComboBounds( footerX, footerY );
    const UIRect blurBounds = FooterBlurBounds( footerX, footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( footerX, footerY );
    const UIRect hitboxBounds = FooterHitboxBounds( footerX, footerY );
    const UIRect timelineBounds = FooterTimelineBounds( footerX, footerY );
    const UIRect perfBounds = FooterPerfBounds( footerX, footerY );
    m_rendererCombo.SetBounds( rendererComboBounds.x, rendererComboBounds.y, rendererComboBounds.w, rendererComboBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurBounds.x, blurBounds.y, blurBounds.w, blurBounds.h );
    m_vsyncToggle.SetBounds( vsyncBounds.x, vsyncBounds.y, vsyncBounds.w, vsyncBounds.h );
    m_hitboxToggle.SetBounds( hitboxBounds.x, hitboxBounds.y, hitboxBounds.w, hitboxBounds.h );
    m_histogramToggle.SetBounds( perfBounds.x, perfBounds.y, perfBounds.w, perfBounds.h );
    m_timelineToggle.SetBounds( timelineBounds.x, timelineBounds.y, timelineBounds.w, timelineBounds.h );

    const char* cineSceneOptions[UI_CINE_SCENE_MAX_OPTIONS] = {};
    int cineSceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};
    const int cineSceneOptionCount = BuildCineSceneOptions( sceneOptions, sceneOptionCount, cineSceneOptions, cineSceneIndices );

    if ( ( leftNow && ( inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0 ) ) ||
         ( wheelDelta != 0 && inside ) )
    {
        result.commands.ui.userInteracted = true;
    }

    if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::UpdateFilterTyping( m_sceneTab, m_sceneCombo, result, sceneOptions, sceneOptionCount );
    }

    bool wheelHandled = false;
    if ( wheelDelta != 0 && m_sceneCombo.IsOpen() && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = static_cast<float>( inputX + contentPad );
        const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
        const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
        wheelHandled = SceneTab::HandleComboWheel( m_sceneTab, m_sceneCombo, sceneOptions, sceneOptionCount, m_mouseX, m_mouseY, wheelDelta, contentX, rowBase, contentW );
    }

    if ( wheelDelta != 0 && inContent && !wheelHandled )
    {
        m_scrollY -= static_cast<float>( wheelDelta ) / static_cast<float>( WHEEL_DELTA ) * 42.0f;
        m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
        m_scrollbarVisibleUntil = now + 1.4;
    }

    if ( input.leftPressed )
    {
        if ( titleButtons.minimize.Contains( m_mouseX, m_mouseY ) || titleButtons.close.Contains( m_mouseX, m_mouseY ) )
        {
            SetMinimized( true, now );
        }
        else if ( titleButtons.maximize.Contains( m_mouseX, m_mouseY ) )
        {
            SetMaximized( !m_window.isMaximized, screenW, screenH, now );
        }
        else if ( inResize )
        {
            m_interaction.isResizing = true;
            m_interaction.resizeStartMouseX = m_mouseX;
            m_interaction.resizeStartMouseY = m_mouseY;
            m_interaction.resizeStartW = inputW;
            m_interaction.resizeStartH = inputH;
            InputControl::BeginMouseCapture( hwnd );
        }
        else if ( inTitle )
        {
            m_interaction.isDragging = true;
            m_interaction.dragOffsetX = m_mouseX - inputX;
            m_interaction.dragOffsetY = m_mouseY - inputY;
            InputControl::BeginMouseCapture( hwnd );
        }
        else if ( inTabs )
        {
            static const int kTabCount = static_cast<int>( InGameUITab::Count );
            const int index = m_tabBar.HitTest( m_mouseX, m_mouseY, kTabCount );
            if ( index >= 0 && index < kTabCount )
            {
                SetActiveTab( static_cast<InGameUITab>( index ) );
                m_scrollbarVisibleUntil = now + 1.0;
            }
        }
        else if ( m_sceneCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Scene )
            {
                const float contentX = static_cast<float>( inputX + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
                SceneTab::HandleOpenComboClick( m_sceneTab,
                                                m_sceneCombo,
                                                m_resetSceneButton,
                                                m_resetDefaultsButton,
                                                m_saveDefaultsButton,
                                                result,
                                                sceneOptions,
                                                sceneOptionCount,
                                                m_mouseX,
                                                m_mouseY,
                                                contentX,
                                                rowBase,
                                                contentW );
            }
            else
            {
                CloseSceneCombo();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            m_cineSceneCombo.Close();
        }
        else if ( m_cineSceneCombo.IsOpen() )
        {
            const int option = m_cineSceneCombo.HitOption( m_mouseX, m_mouseY, cineSceneOptionCount );
            if ( option >= 0 && option < cineSceneOptionCount )
            {
                result.commands.cinematic.requestedModeSceneIndex = cineSceneIndices[option];
                m_cineSceneCombo.Close();
            }
            else if ( m_cineSceneCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_cineSceneCombo.ToggleOpen();
            }
            else
            {
                m_cineSceneCombo.Close();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
        }
        else if ( m_reflectionCombo.IsOpen() )
        {
            const int option = m_reflectionCombo.HitOption( m_mouseX, m_mouseY, 3 );
            const bool isDXRDisabled = option == 1 && m_lastRendererIndex != RENDERER_DX12;
            if ( option >= 0 && option < 3 && !isDXRDisabled )
            {
                result.commands.water.requestedWaterReflectionMode = option;
                m_reflectionCombo.Close();
            }
            else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
            }
            else
            {
                m_reflectionCombo.Close();
            }
            m_rendererCombo.Close();
            CloseSceneCombo();
            m_cineSceneCombo.Close();
        }
        else if ( m_rendererCombo.IsOpen() )
        {
            const int option = m_rendererCombo.HitOption( m_mouseX, m_mouseY, 3 );
            if ( option >= 0 && option < 3 )
            {
                result.commands.renderer.requestedRendererIndex = option;
                m_rendererCombo.Close();
            }
            else if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                m_cineSceneCombo.Close();
            }
            else if ( !m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::WhatsNew )
        {
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            m_cineSceneCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Profiler )
        {
            if ( ProfilerTab::HandleContentClick( m_profilerTab,
                                                  inputX + contentPad,
                                                  contentY,
                                                  m_scrollY,
                                                  m_mouseX,
                                                  m_mouseY ) )
            {
                m_scrollbarVisibleUntil = now + 1.2;
            }
            m_rendererCombo.Close();
            CloseSceneCombo();
            m_cineSceneCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Scene )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const bool sceneClickHandled = SceneTab::HandleContentClick( m_sceneTab,
                                                                         m_sceneCombo,
                                                                         m_resetSceneButton,
                                                                         m_resetDefaultsButton,
                                                                         m_saveDefaultsButton,
                                                                         result,
                                                                         sceneOptions,
                                                                         sceneOptionCount,
                                                                         selectedSceneOption,
                                                                         m_mouseX,
                                                                         m_mouseY,
                                                                         contentX,
                                                                         rowBase,
                                                                         contentW );
            m_rendererCombo.Close();
            if ( sceneClickHandled )
            {
                m_reflectionCombo.Close();
                m_cineSceneCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Physics )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            if ( PhysicsTab::HandleContentClick( m_physicsTab,
                                                 result,
                                                 m_activeSlider,
                                                 m_mouseX,
                                                 m_mouseY,
                                                 contentX,
                                                 rowBase,
                                                 contentW ) )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_cineSceneCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Options )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            if ( OptionsTab::HandleContentClick( m_optionsTab,
                                                 result,
                                                 m_activeSlider,
                                                 m_mouseX,
                                                 m_mouseY,
                                                 contentX,
                                                 rowBase,
                                                 contentW ) )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            m_cineSceneCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Cinematic )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
            const float colW = (std::max)( 148.0f, contentW * 0.46f );
            bool capturedSlider = false;

            m_cineSceneCombo.SetBounds( contentX, scrolledY + UI_CINEMATIC_SCENE_Y, contentW, 24.0f );
            if ( m_cineSceneCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_cineSceneCombo.ToggleOpen();
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                CloseSceneCombo();
            }
            else
            {
                const float featureBaseY = scrolledY + UI_CINEMATIC_FEATURE_START_Y + 26.0f;
                for ( int i = 0; i < static_cast<int>( UICinematicFeature::Count ); ++i )
                {
                    const float tx = CinematicFeatureX( i, contentX, colW );
                    const float toggleY = CinematicFeatureY( i, featureBaseY );
                    m_cinematicFeatureToggles[i].SetBounds( tx, toggleY, colW, 24.0f );
                    if ( m_cinematicFeatureToggles[i].HitTest( m_mouseX, m_mouseY ) )
                    {
                        result.commands.cinematic.requestedFeature = kCinematicFeatureSpecs[i].feature;
                        break;
                    }
                }

                if ( result.commands.cinematic.requestedFeature == UICinematicFeature::None )
                {
                    const float rowBase = scrolledY + UI_CINEMATIC_START_Y;
                    for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
                    {
                        m_cinematicSliders[i].SetBounds( contentX, CinematicSliderY( i, rowBase ), contentW, 34.0f );
                        if ( m_cinematicSliders[i].HitTest( m_mouseX, m_mouseY ) )
                        {
                            m_activeSlider = UI_CINEMATIC_SLIDER_BASE + i;
                            SetCinematicSliderResult( result, m_cinematicSliders[i], m_mouseX, kCinematicSliderSpecs[i] );
                            capturedSlider = true;
                            break;
                        }
                    }
                }
            }

            if ( capturedSlider )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            if ( capturedSlider )
            {
                m_cineSceneCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Keys )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            if ( ControlsTab::HandleContentClick( m_controlsTab,
                                                  result,
                                                  m_activeSlider,
                                                  m_mouseX,
                                                  m_mouseY,
                                                  contentX,
                                                  rowBase,
                                                  contentW,
                                                  m_lastSolverBallCount,
                                                  m_lastSolverBoxCount ) )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            m_cineSceneCombo.Close();
        }
        else if ( inside && m_mouseY >= inputY + inputH - bottomH )
        {
            if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                m_cineSceneCombo.Close();
            }
            else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
                m_rendererCombo.Close();
                CloseSceneCombo();
                m_cineSceneCombo.Close();
            }
            else if ( m_blurToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                m_blurPreviewEnabled = !m_blurPreviewEnabled;
                m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Toggle );
            }
            else if ( m_vsyncToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.renderer.toggleVsync = true;
            }
            else if ( m_hitboxToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetHitboxOverlayEnabled( !m_hitboxOverlayEnabled );
            }
            else if ( m_histogramToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetPerformanceHistogramEnabled( !ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) );
            }
            else if ( m_timelineToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetProfilerTimelineEnabled( !ProfilerTab::TimelineEnabled( m_profilerTab ) );
            }
        }
        else
        {
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            m_cineSceneCombo.Close();
        }
    }

    if ( leftNow && m_activeSlider != 0 )
    {
        // Sliders update previews continuously while dragged.  Heavy operations
        // such as rebuilding generated bodies are delayed until mouse release,
        // but cheap scalar controls are emitted every frame for immediate feedback.
        if ( !OptionsTab::UpdateActiveSlider( m_optionsTab, m_activeSlider, m_mouseX, result ) &&
             !PhysicsTab::UpdateActiveSlider( m_physicsTab, m_activeSlider, m_mouseX, result ) )
        {
            const int cinematicSlider = CinematicSliderIndexFromActiveSlider( m_activeSlider );
            if ( cinematicSlider >= 0 )
            {
                SetCinematicSliderResult( result, m_cinematicSliders[cinematicSlider], m_mouseX, kCinematicSliderSpecs[cinematicSlider] );
            }
            else
            {
                ControlsTab::UpdateActiveSlider( m_controlsTab,
                                                 m_activeSlider,
                                                 m_mouseX,
                                                 m_lastSolverBallCount,
                                                 m_lastSolverBoxCount,
                                                 result );
            }
        }
    }

    if ( leftNow && m_interaction.isDragging )
    {
        const int oldX = m_window.x;
        const int oldY = m_window.y;
        m_window.x = std::clamp( m_mouseX - m_interaction.dragOffsetX, margin, (std::max)( margin, screenW - m_window.width - margin ) );
        m_window.y = std::clamp( m_mouseY - m_interaction.dragOffsetY, margin, (std::max)( margin, screenH - m_window.height - margin ) );
        if ( oldX != m_window.x || oldY != m_window.y )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }
    if ( leftNow && m_interaction.isResizing )
    {
        const int oldW = m_window.width;
        const int oldH = m_window.height;
        m_window.width = std::clamp( m_interaction.resizeStartW + m_mouseX - m_interaction.resizeStartMouseX, minW, maxW );
        m_window.height = std::clamp( m_interaction.resizeStartH + m_mouseY - m_interaction.resizeStartMouseY, minH, maxH );
        m_scrollbarVisibleUntil = now + 1.4;
        if ( oldW != m_window.width || oldH != m_window.height )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }

    if ( input.leftReleased )
    {
        // Commit deferred slider previews exactly once on release.  This avoids
        // rebuilding solver objects or generated model pools every mouse-move
        // while still letting the drawn slider thumb track the user's drag.
        if ( !OptionsTab::CommitActiveSlider( m_optionsTab, m_activeSlider, result ) &&
             !PhysicsTab::CommitActiveSlider( m_physicsTab, m_activeSlider, result ) )
        {
            const int cinematicSlider = CinematicSliderIndexFromActiveSlider( m_activeSlider );
            if ( cinematicSlider >= 0 )
            {
                SetCinematicSliderResult( result, m_cinematicSliders[cinematicSlider], m_mouseX, kCinematicSliderSpecs[cinematicSlider] );
            }
            else
            {
                ControlsTab::CommitActiveSlider( m_controlsTab, m_activeSlider, result );
            }
        }
        m_activeSlider = 0;
        OptionsTab::ResetPreviewState( m_optionsTab );
        PhysicsTab::ResetPreviewState( m_physicsTab );
        ControlsTab::ResetPreviewState( m_controlsTab );
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        InputControl::EndMouseCapture();
    }

    m_interaction.leftWasDown = leftNow;
    m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
    m_interaction.blocksCameraMouse = inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0;
    return result;
}


void InGameUI::Draw( const InGameUIFrameData& data )
{
    if ( !m_window.isVisible )
    {
        return;
    }

    const int screenW = (std::max)( 1, data.screenW );
    const int screenH = (std::max)( 1, data.screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    m_lastSolverBallCount = std::clamp( data.solverBallCount, UI_SOLVER_COUNT_MIN, UI_GAME_MODEL_TOTAL_MAX );
    m_lastSolverBoxCount = std::clamp( data.solverBoxCount, UI_SOLVER_COUNT_MIN, UI_GAME_MODEL_TOTAL_MAX );
    const int currentRendererIndex = GetRendererIndexFromName( data.rendererName );
    m_lastRendererIndex = currentRendererIndex;
    if ( ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) )
    {
        ProfilerTab::PushPerformanceHistogramSample( m_profilerTab, data.cpuFrameMs, data.gpuFrameMs );
    }

    if ( m_window.isMinimized )
    {
        m_cache.Reset();
        UIDrawList& drawList = m_cache.MutableDrawList();
        drawList.Clear();
        const UIDrawContext draw( screenW, screenH, &drawList );
        if ( m_window.animationActive && m_window.animationToMinimized )
        {
            const UIRect animBounds = Chrome::CurrentWindowRect( m_window, data.now );
            if ( m_window.animationActive )
            {
                Chrome::DrawWindowAnimationShell( draw, animBounds );
                if ( ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) )
                {
                    ProfilerTab::DrawPerformanceHistogram( m_profilerTab, draw, data );
                }
                FlushUIDrawList( drawList, screenW, screenH );
                return;
            }
        }

        char titleText[192] = {};
        Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
        m_window.minimizedWidth = MinimizedWidthForTitle( titleText, screenW );
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        Chrome::FitTitleText( titleText, sizeof( titleText ), 12.5f, minimized.w - 76.0f );
        Chrome::DrawMinimizedWindow( draw, minimized, titleText );
        if ( ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) )
        {
            ProfilerTab::DrawPerformanceHistogram( m_profilerTab, draw, data );
        }
        FlushUIDrawList( drawList, screenW, screenH );
        return;
    }

    PROFILE_BEGIN( "Frame/UI/Layout" );
    const UIRect windowBounds = Chrome::CurrentWindowRect( m_window, data.now );
    const float x = windowBounds.x;
    const float y = windowBounds.y;
    const float w = windowBounds.w;
    const float h = windowBounds.h;
    const float titleH = 44.0f;
    const float tabH = 44.0f;
    const float bottomH = 78.0f;
    const float pad = 18.0f;
    const float contentX = x + pad;
    const float contentY = y + titleH + tabH + 12.0f;
    const float contentW = w - pad * 2.0f - 8.0f;
    const float contentH = (std::max)( 30.0f, h - titleH - tabH - bottomH - pad );
    const float scrolledY = contentY - m_scrollY;
    char titleText[192] = {};
    Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
    const bool useTitleStats = w - 36.0f < 560.0f;
    char titleStat[32] = {};
    float titleStatW = 0.0f;
    float titleStatX = 0.0f;
    float titleMaxW = w - 150.0f;
    if ( useTitleStats )
    {
        snprintf( titleStat, sizeof( titleStat ), "%.0f FPS", data.fps );
        titleStatW = Text2d::MeasureText( 10.5f, titleStat );
        titleStatX = (std::max)( x + 148.0f, x + w - 128.0f - titleStatW );
        titleMaxW = titleStatX - ( x + 20.0f ) - 10.0f;
    }
    Chrome::FitTitleText( titleText, sizeof( titleText ), 15.5f, (std::max)( 40.0f, titleMaxW ) );
    ProfilerTab::ApplyDefaultExpansion( m_profilerTab );
    ProfilerTab::ApplyExpandAll( m_profilerTab );

    UICacheFrameKey cacheKey;
    cacheKey.screenW = screenW;
    cacheKey.screenH = screenH;
    cacheKey.windowBounds = windowBounds;
    cacheKey.activeTab = static_cast<int>( m_activeTab );
    cacheKey.scrollY = m_scrollY;
    cacheKey.blurEnabled = m_blurPreviewEnabled;
    cacheKey.contentSignature = BuildUIContentSignature( data, currentRendererIndex );
    cacheKey.styleSignature = HashBool( HashBool( 2166136261u, m_blurPreviewEnabled ), m_hitboxOverlayEnabled );
    cacheKey.interactionSignature = BuildUIInteractionSignature( m_mouseX, m_mouseY, m_rendererCombo.IsOpen(), m_reflectionCombo.IsOpen(), m_sceneCombo.IsOpen(), m_cineSceneCombo.IsOpen(), m_activeSlider );
    m_cache.BeginFrame( cacheKey );
    PROFILE_END( "Frame/UI/Layout" );

    if ( m_cache.CanReplayPositionOnly( cacheKey ) )
    {
        const float replayOffsetX = m_cache.ReplayOffsetX( cacheKey );
        const float replayOffsetY = m_cache.ReplayOffsetY( cacheKey );
        FlushUIDrawList( m_cache.DrawList(), screenW, screenH, replayOffsetX, replayOffsetY );
        m_cache.StoreFrame( cacheKey );
        return;
    }

    UIDrawList& drawList = m_cache.MutableDrawList();
    drawList.Clear();
    const UIDrawContext draw( screenW, screenH, &drawList );
    PROFILE_BEGIN( "Frame/UI/DrawBuild" );

    const UIRect blurBounds = { x, y, w, h };
    Text2d::FlushQuads();
    PROFILE_BEGIN( "Frame/UI/Blur" );
    m_backdropBlur.Draw( draw, blurBounds, screenW, screenH, data.currentFrame, data.now, m_blurPreviewEnabled );
    PROFILE_END( "Frame/UI/Blur" );

    Chrome::DrawWindowFrame( draw, windowBounds, titleH, tabH, m_blurPreviewEnabled, titleText );
    Chrome::DrawTitleButtons( draw, Chrome::GetTitleButtonRects( windowBounds ), m_window.isMaximized, m_mouseX, m_mouseY );

    static const char* kTabs[] = { "WHATS NEW", "Profile", "Scene", "Physics", "Options", "Controls", "Cine" };
    const int tabCount = static_cast<int>( InGameUITab::Count );
    const float tabPad = 14.0f;
    m_tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    m_tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( m_activeTab ) );

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedPanel( { contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f }, Style::Radii().window, palette.windowSubtle, palette.innerBorder );

    if ( m_activeTab == InGameUITab::WhatsNew )
    {
        SetWhatsNewControlBounds( m_whatsNewToggles, m_whatsNewSliders, contentX, scrolledY, contentW );
        DrawWhatsNewTab( m_whatsNewToggles,
                         m_whatsNewSliders,
                         draw,
                         contentX,
                         contentY,
                         contentW,
                         contentH,
                         scrolledY );
    }
    else if ( m_activeTab == InGameUITab::Profiler )
    {
        ProfilerTab::Draw( m_profilerTab, draw, contentX, contentY, contentW, contentH, m_scrollY );
    }
    else if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::Draw( m_sceneTab,
                        m_sceneCombo,
                        m_resetSceneButton,
                        m_resetDefaultsButton,
                        m_saveDefaultsButton,
                        draw,
                        data,
                        contentX,
                        contentY,
                        contentW,
                        contentH,
                        scrolledY,
                        m_mouseX,
                        m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Physics )
    {
        PhysicsTab::Draw( m_physicsTab,
                          draw,
                          data,
                          contentX,
                          contentY,
                          contentW,
                          contentH,
                          scrolledY,
                          m_activeSlider,
                          m_mouseX,
                          m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Options )
    {
        OptionsTab::Draw( m_optionsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY, m_activeSlider );
    }
    else if ( m_activeTab == InGameUITab::Cinematic )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        const char* cineSceneOptions[UI_CINE_SCENE_MAX_OPTIONS] = {};
        int cineSceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};
        const int cineSceneOptionCount = BuildCineSceneOptions( data.sceneOptions, data.sceneOptionCount, cineSceneOptions, cineSceneIndices );
        const int selectedCineSceneOption = SelectedCineSceneOption( cineSceneIndices, cineSceneOptionCount, data.selectedCineModeSceneOption );

        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Cine" );
        m_cineSceneCombo.SetBounds( contentX, scrolledY + UI_CINEMATIC_SCENE_Y, contentW, 24.0f );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_CINEMATIC_SCENE_Y, 24.0f ) )
        {
            m_cineSceneCombo.Draw( draw, "Mode", cineSceneOptions, cineSceneOptionCount, selectedCineSceneOption, m_mouseX, m_mouseY );
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
            DrawContentToggle( draw,
                               contentY,
                               contentH,
                               m_cinematicFeatureToggles[i],
                               tx,
                               toggleY,
                               colW,
                               kCinematicFeatureSpecs[i].label,
                               CinematicFeatureEnabled( data.cinematic, kCinematicFeatureSpecs[i].feature ) );
        }
        const float baseY = scrolledY + UI_CINEMATIC_START_Y;
        for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
        {
            const CinematicSliderSpec& spec = kCinematicSliderSpecs[i];
            const float sliderY = CinematicSliderY( i, baseY );
            if ( spec.section && IsRowVisible( contentY, contentH, sliderY - UI_CINEMATIC_SECTION_H + 4.0f, 18.0f ) )
            {
                DrawSectionTitle( draw, contentX, contentY, contentH, sliderY - UI_CINEMATIC_SECTION_H + 4.0f, 12.0f, spec.section );
            }
            const float value = std::clamp( CinematicValueForParam( data.cinematic, spec.param ), spec.minValue, spec.maxValue );
            snprintf( buf, sizeof( buf ), spec.valueFormat, value );
            m_cinematicSliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );
            if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
            {
                m_cinematicSliders[i].Draw( draw, spec.label, buf, value, spec.minValue, spec.maxValue );
            }
        }
    }
    else
    {
        ControlsTab::Draw( m_controlsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY );
    }

    m_scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    m_scrollBar.Draw( draw, static_cast<float>( ContentHeight() ), contentH, m_scrollY, m_scrollbarVisibleUntil, data.now );

    const float by = y + h - bottomH;
    draw.Rect( x + 16.0f, by, w - 32.0f, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.14f );
    const float footerPad = 18.0f;
    const float footerGap = 16.0f;
    const float footerX = x + footerPad;
    const float footerW = (std::max)( 120.0f, w - footerPad * 2.0f );
    const bool hasSeparateStats = footerW >= 560.0f;
    const float controlsW = hasSeparateStats ? 462.0f : footerW;
    draw.RoundedPanel( { footerX, by + 16.0f, controlsW, 56.0f }, Style::Radii().control, palette.windowSubtle, palette.innerBorder );

    const UIRect rendererComboBounds = FooterRendererComboBounds( x, by );
    const UIRect waterComboBounds = FooterWaterComboBounds( x, by );
    const UIRect blurFooterBounds = FooterBlurBounds( x, by );
    const UIRect vsyncFooterBounds = FooterVsyncBounds( x, by );
    const UIRect hitboxFooterBounds = FooterHitboxBounds( x, by );
    const UIRect timelineFooterBounds = FooterTimelineBounds( x, by );
    const UIRect perfFooterBounds = FooterPerfBounds( x, by );
    m_rendererCombo.SetBounds( rendererComboBounds.x, rendererComboBounds.y, rendererComboBounds.w, rendererComboBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurFooterBounds.x, blurFooterBounds.y, blurFooterBounds.w, blurFooterBounds.h );
    m_vsyncToggle.SetBounds( vsyncFooterBounds.x, vsyncFooterBounds.y, vsyncFooterBounds.w, vsyncFooterBounds.h );
    m_hitboxToggle.SetBounds( hitboxFooterBounds.x, hitboxFooterBounds.y, hitboxFooterBounds.w, hitboxFooterBounds.h );
    m_histogramToggle.SetBounds( perfFooterBounds.x, perfFooterBounds.y, perfFooterBounds.w, perfFooterBounds.h );
    m_timelineToggle.SetBounds( timelineFooterBounds.x, timelineFooterBounds.y, timelineFooterBounds.w, timelineFooterBounds.h );
    static const char* kRendererOptions[] = { "GL", "DX11", "DX12" };
    static const char* kReflectionOptions[] = { "FBO", "DXR", "None" };
    m_rendererCombo.Draw( draw, "Renderer", kRendererOptions, 3, currentRendererIndex, m_mouseX, m_mouseY );
    DrawFooterToggle( draw, blurFooterBounds, "Blur", m_blurPreviewEnabled );
    DrawFooterToggle( draw, vsyncFooterBounds, "VSync", data.vsyncEnabled );
    DrawFooterToggle( draw, hitboxFooterBounds, "Hitboxes", m_hitboxOverlayEnabled );
    DrawFooterToggle( draw, perfFooterBounds, "Perf", ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) );
    DrawFooterToggle( draw, timelineFooterBounds, "Timeline", ProfilerTab::TimelineEnabled( m_profilerTab ) );
    m_reflectionCombo.Draw( draw,
                            "Water",
                            kReflectionOptions,
                            3,
                            WaterReflectionModeFromData( data ),
                            m_mouseX,
                            m_mouseY,
                            ReflectionDisabledMask( currentRendererIndex ) );

    char status[128];
    const float frameDisplayMs = data.fps > 0.0f ? 1000.0f / data.fps : 0.0f;
    const int cpuPercent = static_cast<int>( std::clamp( ( data.renderMs + data.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int gpuPercent = static_cast<int>( std::clamp( data.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.drawCallsBeforeUI + data.UIDrawCalls;
    snprintf( status, sizeof( status ), "%.0f", data.fps );
    if ( hasSeparateStats )
    {
        const float statsX = footerX + controlsW + footerGap;
        const float statsW = (std::max)( 120.0f, x + w - footerPad - statsX );
        draw.RoundedPanel( { statsX, by + 16.0f, statsW, 56.0f }, Style::Radii().control, palette.windowSubtle, palette.innerBorder );

        if ( statsW < 350.0f )
        {
            char fpsText[32];
            char frameText[32];
            char drawText[32];
            snprintf( fpsText, sizeof( fpsText ), "%.0f", data.fps );
            snprintf( frameText, sizeof( frameText ), "%.2f ms", frameDisplayMs );
            snprintf( drawText, sizeof( drawText ), "%d/%d", drawCalls, data.UIDrawCalls );
            DrawCompactFooterStat( draw, statsX, by + 23.0f, "FPS", fpsText, palette.accent.r, palette.accent.g, palette.accent.b );
            DrawCompactFooterStat( draw, statsX, by + 41.0f, "Frame", frameText, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
            DrawCompactFooterStat( draw, statsX, by + 59.0f, "Draw/UI", drawText, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
        }
        else
        {
            DrawFooterStatCell( draw, statsX + 18.0f, by, "FPS", status, palette.accent.r, palette.accent.g, palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 78.0f, by );
            snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
            DrawFooterStatCell( draw, statsX + 100.0f, by, "Frame Time", status, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
            DrawFooterStatDivider( draw, statsX + 190.0f, by );
            snprintf( status, sizeof( status ), "%d%%", cpuPercent );
            DrawFooterStatCell( draw, statsX + 212.0f, by, "CPU", status, palette.accent.r, palette.accent.g, palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 266.0f, by );
            snprintf( status, sizeof( status ), "%d%%", gpuPercent );
            DrawFooterStatCell( draw, statsX + 288.0f, by, "GPU", status, palette.accent.r, palette.accent.g, palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 342.0f, by );
            snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.UIDrawCalls );
            DrawFooterStatCell( draw, statsX + statsW - 112.0f, by, "Draws / UI", status, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
        }
    }
    else
    {
        if ( titleStatW > 0.0f && titleStatX + titleStatW < x + w - 116.0f )
        {
            draw.Text( titleStatX, y + 17.0f, 10.5f, palette.accent.r, palette.accent.g, palette.accent.b, titleStat );
        }
    }

    if ( ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) )
    {
        ProfilerTab::DrawPerformanceHistogram( m_profilerTab, draw, data );
    }

    draw.Rect( x + w - 24.0f, y + h - 9.0f, 14.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.58f );
    draw.Rect( x + w - 18.0f, y + h - 15.0f, 8.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.46f );
    draw.Rect( x + w - 12.0f, y + h - 21.0f, 2.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.38f );

    DrawHitboxOverlay( draw, data, windowBounds, { contentX, contentY, contentW, contentH }, { footerX, by + 16.0f, controlsW, 56.0f } );

    PROFILE_END( "Frame/UI/DrawBuild" );
    m_cache.StoreFrame( cacheKey );
    FlushUIDrawList( drawList, screenW, screenH );
}
