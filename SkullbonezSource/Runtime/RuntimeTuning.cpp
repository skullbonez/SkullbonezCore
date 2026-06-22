/*
File: SkullbonezSource/Runtime/RuntimeTuning.cpp
Purpose:
  Owns UI-driven runtime tuning for cinematic rendering, ordinary rendering,
  and worker-thread overrides.

Mental model:
  Runtime input decides when a UI command is accepted. This file decides how
  accepted values clamp, mutate config, and persist as scene overrides.

Related:
  - SkullbonezSource/Runtime/RuntimeTuning.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "RuntimeTuning.h"

#include "../Core/WorkerPool.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param )
{
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return SCENE_CINE_EXPOSURE;
    case UICinematicParam::Gamma:
        return SCENE_CINE_GAMMA;
    case UICinematicParam::SkyMode:
    case UICinematicParam::TerrainMode:
    case UICinematicParam::ObjectStyle:
    case UICinematicParam::WaterMode:
        return SCENE_CINE_STYLE_MODES;
    case UICinematicParam::StyleSaturation:
    case UICinematicParam::StyleContrast:
    case UICinematicParam::StyleVignette:
        return SCENE_CINE_STYLE_GRADE;
    case UICinematicParam::SunX:
        return SCENE_CINE_SUN_SCREEN_X;
    case UICinematicParam::SunY:
        return SCENE_CINE_SUN_SCREEN_Y;
    case UICinematicParam::SunBrightness:
        return SCENE_CINE_SUN_INTENSITY;
    case UICinematicParam::SunRed:
        return SCENE_CINE_SUN_COLOR_R;
    case UICinematicParam::SunGreen:
        return SCENE_CINE_SUN_COLOR_G;
    case UICinematicParam::SunBlue:
        return SCENE_CINE_SUN_COLOR_B;
    case UICinematicParam::SkyGlow:
        return SCENE_CINE_SKY_GLOW_STRENGTH;
    case UICinematicParam::HorizonRed:
        return SCENE_CINE_SKY_HORIZON_R;
    case UICinematicParam::HorizonGreen:
        return SCENE_CINE_SKY_HORIZON_G;
    case UICinematicParam::HorizonBlue:
        return SCENE_CINE_SKY_HORIZON_B;
    case UICinematicParam::ZenithRed:
        return SCENE_CINE_SKY_ZENITH_R;
    case UICinematicParam::ZenithGreen:
        return SCENE_CINE_SKY_ZENITH_G;
    case UICinematicParam::ZenithBlue:
        return SCENE_CINE_SKY_ZENITH_B;
    case UICinematicParam::CloudCoverage:
        return SCENE_CINE_CLOUD_COVERAGE;
    case UICinematicParam::CloudSoftness:
        return SCENE_CINE_CLOUD_SOFTNESS;
    case UICinematicParam::CloudScale:
        return SCENE_CINE_CLOUD_SCALE;
    case UICinematicParam::CloudIntensity:
        return SCENE_CINE_CLOUD_INTENSITY;
    case UICinematicParam::ShaftStrength:
        return SCENE_CINE_SUN_SHAFT_STRENGTH;
    case UICinematicParam::ShaftFalloff:
        return SCENE_CINE_SUN_SHAFT_FALLOFF;
    case UICinematicParam::VolumetricStrength:
        return SCENE_CINE_VOLUMETRIC_STRENGTH;
    case UICinematicParam::VolumetricDensity:
        return SCENE_CINE_VOLUMETRIC_DENSITY;
    case UICinematicParam::VolumetricDecay:
        return SCENE_CINE_VOLUMETRIC_DECAY;
    case UICinematicParam::BloomThreshold:
        return SCENE_CINE_BLOOM_THRESHOLD;
    case UICinematicParam::BloomKnee:
        return SCENE_CINE_BLOOM_KNEE;
    case UICinematicParam::BloomStrength:
        return SCENE_CINE_BLOOM_STRENGTH;
    case UICinematicParam::BloomRadius:
        return SCENE_CINE_BLOOM_RADIUS;
    case UICinematicParam::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF;
    case UICinematicParam::TerrainTintRed:
    case UICinematicParam::TerrainTintGreen:
    case UICinematicParam::TerrainTintBlue:
        return SCENE_CINE_TERRAIN_TINT;
    case UICinematicParam::TerrainAccentRed:
    case UICinematicParam::TerrainAccentGreen:
    case UICinematicParam::TerrainAccentBlue:
        return SCENE_CINE_TERRAIN_ACCENT;
    case UICinematicParam::TerrainGridScale:
    case UICinematicParam::TerrainGridStrength:
        return SCENE_CINE_TERRAIN_GRID;
    case UICinematicParam::WaterTintRed:
    case UICinematicParam::WaterTintGreen:
    case UICinematicParam::WaterTintBlue:
        return SCENE_CINE_WATER_TINT;
    case UICinematicParam::WaterAlpha:
    case UICinematicParam::WaterReflection:
    case UICinematicParam::WaterGlint:
        return SCENE_CINE_WATER_PROFILE;
    case UICinematicParam::BasinCenterX:
    case UICinematicParam::BasinCenterZ:
    case UICinematicParam::BasinRadiusX:
    case UICinematicParam::BasinRadiusZ:
    case UICinematicParam::BasinFeather:
        return SCENE_CINE_BASIN_MASK;
    case UICinematicParam::BasinDepth:
        return SCENE_CINE_BASIN_DEPTH;
    case UICinematicParam::BasinRimLift:
        return SCENE_CINE_BASIN_RIM_LIFT;
    case UICinematicParam::FogDensity:
        return SCENE_CINE_FOG_DENSITY;
    case UICinematicParam::FogOpacity:
        return SCENE_CINE_FOG_MAX_OPACITY;
    case UICinematicParam::FogStart:
        return SCENE_CINE_FOG_START;
    case UICinematicParam::FogEnd:
        return SCENE_CINE_FOG_END;
    case UICinematicParam::FogRed:
        return SCENE_CINE_FOG_COLOR_R;
    case UICinematicParam::FogGreen:
        return SCENE_CINE_FOG_COLOR_G;
    case UICinematicParam::FogBlue:
        return SCENE_CINE_FOG_COLOR_B;
    default:
        return 0;
    }
}

uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature )
{
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        return SCENE_CINE_SKY_ATMOSPHERE;
    case UICinematicFeature::Clouds:
        return SCENE_CINE_CLOUDS;
    case UICinematicFeature::GodRays:
        return SCENE_CINE_GOD_RAYS;
    case UICinematicFeature::VolumetricLight:
        return SCENE_CINE_VOLUMETRIC_LIGHTING;
    case UICinematicFeature::Bloom:
        return SCENE_CINE_BLOOM;
    case UICinematicFeature::Fog:
        return SCENE_CINE_FOG;
    case UICinematicFeature::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF_ENABLED;
    case UICinematicFeature::Shadows:
        return SCENE_CINE_SHADOWS;
    default:
        return 0;
    }
}

void ApplyWorkerThreadCountOverride( int requestedWorkerThreads )
{
    const int clampedWorkerThreads =
        requestedWorkerThreads < 0
            ? -1
            : std::clamp( requestedWorkerThreads, 0, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
    SkullbonezCore::Threading::WorkerPool& workerPool = SkullbonezCore::Threading::WorkerPool::Instance();
    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    Cfg().workerThreads = clampedWorkerThreads;
    if ( workerPool.GetThreadCount() != resolvedWorkerThreads )
    {
        workerPool.Initialise( clampedWorkerThreads );
    }
}

void ApplyCinematicUIParam( CinematicRenderConfig& cinematic,
                            RunSceneState& scene,
                            UICinematicParam param,
                            float rawValue )
{
    // The UI sends "the user dragged this slider to this raw value." This helper
    // clamps the value into a safe range, writes it into the live cinematic
    // config, and marks the scene override bit so reloads keep the user's tweak.
    const auto clampValue = []( float value, float minValue, float maxValue ) -> float
    { return std::clamp( value, minValue, maxValue ); };
    const auto clampIntValue = []( float value, int minValue, int maxValue ) -> int
    { return std::clamp( static_cast<int>( std::round( value ) ), minValue, maxValue ); };

    switch ( param )
    {
    case UICinematicParam::Exposure:
        cinematic.exposure = clampValue( rawValue, 0.05f, 3.00f );
        scene.hasCinematicExposure = true;
        scene.cinematicExposure = cinematic.exposure;
        scene.cinematicOverrideMask |= SCENE_CINE_EXPOSURE;
        break;
    case UICinematicParam::Gamma:
        cinematic.gamma = clampValue( rawValue, 1.00f, 3.00f );
        scene.hasCinematicGamma = true;
        scene.cinematicGamma = cinematic.gamma;
        scene.cinematicOverrideMask |= SCENE_CINE_GAMMA;
        break;
    case UICinematicParam::SkyMode:
        cinematic.skyMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::TerrainMode:
        cinematic.terrainMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::ObjectStyle:
        cinematic.objectStyle = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::WaterMode:
        cinematic.waterMode = clampIntValue( rawValue, 0, 4 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::StyleSaturation:
        cinematic.styleSaturation = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleContrast:
        cinematic.styleContrast = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleVignette:
        cinematic.styleVignette = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::SunX:
        cinematic.sunScreenX = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_X;
        break;
    case UICinematicParam::SunY:
        cinematic.sunScreenY = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_Y;
        break;
    case UICinematicParam::SunBrightness:
        cinematic.sunIntensity = clampValue( rawValue, 0.00f, 40.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_INTENSITY;
        break;
    case UICinematicParam::SunRed:
        cinematic.sunColorR = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_R;
        break;
    case UICinematicParam::SunGreen:
        cinematic.sunColorG = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_G;
        break;
    case UICinematicParam::SunBlue:
        cinematic.sunColorB = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_B;
        break;
    case UICinematicParam::SkyGlow:
        cinematic.skyGlowStrength = clampValue( rawValue, 0.00f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_GLOW_STRENGTH;
        break;
    case UICinematicParam::HorizonRed:
        cinematic.skyHorizonR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_R;
        break;
    case UICinematicParam::HorizonGreen:
        cinematic.skyHorizonG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_G;
        break;
    case UICinematicParam::HorizonBlue:
        cinematic.skyHorizonB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_B;
        break;
    case UICinematicParam::ZenithRed:
        cinematic.skyZenithR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_R;
        break;
    case UICinematicParam::ZenithGreen:
        cinematic.skyZenithG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_G;
        break;
    case UICinematicParam::ZenithBlue:
        cinematic.skyZenithB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_B;
        break;
    case UICinematicParam::CloudCoverage:
        cinematic.cloudCoverage = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_COVERAGE;
        break;
    case UICinematicParam::CloudSoftness:
        cinematic.cloudSoftness = clampValue( rawValue, 0.01f, 0.65f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SOFTNESS;
        break;
    case UICinematicParam::CloudScale:
        cinematic.cloudScale = clampValue( rawValue, 0.50f, 12.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SCALE;
        break;
    case UICinematicParam::CloudIntensity:
        cinematic.cloudIntensity = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_INTENSITY;
        break;
    case UICinematicParam::ShaftStrength:
        cinematic.sunShaftStrength = clampValue( rawValue, 0.00f, 3.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_STRENGTH;
        break;
    case UICinematicParam::ShaftFalloff:
        cinematic.sunShaftFalloff = clampValue( rawValue, 0.25f, 5.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_FALLOFF;
        break;
    case UICinematicParam::VolumetricStrength:
        cinematic.volumetricStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_STRENGTH;
        break;
    case UICinematicParam::VolumetricDensity:
        cinematic.volumetricDensity = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DENSITY;
        break;
    case UICinematicParam::VolumetricDecay:
        cinematic.volumetricDecay = clampValue( rawValue, 0.800f, 0.995f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DECAY;
        break;
    case UICinematicParam::BloomThreshold:
        cinematic.bloomThreshold = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_THRESHOLD;
        break;
    case UICinematicParam::BloomKnee:
        cinematic.bloomKnee = clampValue( rawValue, 0.01f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_KNEE;
        break;
    case UICinematicParam::BloomStrength:
        cinematic.bloomStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_STRENGTH;
        break;
    case UICinematicParam::BloomRadius:
        cinematic.bloomRadius = clampValue( rawValue, 0.25f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_RADIUS;
        break;
    case UICinematicParam::TerrainRelief:
        cinematic.terrainRelief = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF;
        break;
    case UICinematicParam::TerrainTintRed:
        cinematic.terrainTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintGreen:
        cinematic.terrainTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintBlue:
        cinematic.terrainTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainAccentRed:
        cinematic.terrainAccentR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentGreen:
        cinematic.terrainAccentG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentBlue:
        cinematic.terrainAccentB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainGridScale:
        cinematic.terrainGridScale = clampValue( rawValue, 0.10f, 120.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::TerrainGridStrength:
        cinematic.terrainGridStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::WaterTintRed:
        cinematic.waterTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintGreen:
        cinematic.waterTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintBlue:
        cinematic.waterTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterAlpha:
        cinematic.waterAlpha = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterReflection:
        cinematic.waterReflectionStrength = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterGlint:
        cinematic.waterGlintStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::BasinCenterX:
        cinematic.basinCenterX = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinCenterZ:
        cinematic.basinCenterZ = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusX:
        cinematic.basinRadiusX = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusZ:
        cinematic.basinRadiusZ = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinFeather:
        cinematic.basinFeather = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinDepth:
        cinematic.basinDepth = clampValue( rawValue, 0.00f, 80.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_DEPTH;
        break;
    case UICinematicParam::BasinRimLift:
        cinematic.basinRimLift = clampValue( rawValue, 0.00f, 60.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_RIM_LIFT;
        break;
    case UICinematicParam::FogDensity:
        cinematic.fogDensity = clampValue( rawValue, 0.00000f, 0.00600f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_DENSITY;
        break;
    case UICinematicParam::FogOpacity:
        cinematic.fogMaxOpacity = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_MAX_OPACITY;
        break;
    case UICinematicParam::FogStart:
        cinematic.fogStart = clampValue( rawValue, 0.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_START;
        break;
    case UICinematicParam::FogEnd:
        cinematic.fogEnd = clampValue( rawValue, 100.00f, 4000.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_END;
        break;
    case UICinematicParam::FogRed:
        cinematic.fogColorR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_R;
        break;
    case UICinematicParam::FogGreen:
        cinematic.fogColorG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_G;
        break;
    case UICinematicParam::FogBlue:
        cinematic.fogColorB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_B;
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIParam( param );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}

void SetCinematicShadowsEnabledFromUI( CinematicRenderConfig& cinematic, RunSceneState& scene, bool enabled )
{
    // Shadow maps are configured next to the cinematic controls because the
    // original implementation grew from that renderer work, but the depth pass
    // now feeds normal rendering too. Toggling shadows from either the Options
    // tab or the Cine tab must therefore only touch the shadow flag and scene
    // override bits; it must not silently enable the HDR/post-processing stack.
    cinematic.shadowsEnabled = enabled;
    scene.cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    scene.uiCinematicOverrideMask |= SCENE_CINE_SHADOWS;
}

void ApplyOrdinaryRenderUIParam( OrdinaryRenderConfig& ordinary, UIRenderParam param, float rawValue )
{
    switch ( param )
    {
    case UIRenderParam::SunIntensity:
        ordinary.sunIntensity = std::clamp( rawValue, 0.0f, 4.0f );
        break;
    case UIRenderParam::SunRed:
        ordinary.sunColorR = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunGreen:
        ordinary.sunColorG = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunBlue:
        ordinary.sunColorB = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::AmbientStrength:
        ordinary.ambientStrength = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyRed:
        ordinary.skyAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyGreen:
        ordinary.skyAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyBlue:
        ordinary.skyAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundRed:
        ordinary.groundAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundGreen:
        ordinary.groundAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundBlue:
        ordinary.groundAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::ShadowStrength:
        ordinary.shadowStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::ShadowSoftness:
        ordinary.shadowSoftness = std::clamp( rawValue, 0.25f, 4.0f );
        break;
    case UIRenderParam::ShadowDepthBias:
        ordinary.shadowDepthBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::ShadowSlopeBias:
        ordinary.shadowSlopeBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::WaterRed:
        ordinary.waterTintR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterGreen:
        ordinary.waterTintG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterBlue:
        ordinary.waterTintB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterAlpha:
        ordinary.waterAlpha = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterReflection:
        ordinary.waterReflectionStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterFresnel:
        ordinary.waterFresnelF0 = std::clamp( rawValue, 0.0f, 0.12f );
        break;
    case UIRenderParam::BallRoughness:
        ordinary.ballRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BallSpecular:
        ordinary.ballSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::BoxRoughness:
        ordinary.boxRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BoxSpecular:
        ordinary.boxSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    default:
        break;
    }
}

void ToggleCinematicUIFeature( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicFeature feature )
{
    // Feature toggles are boolean pass switches: sky on/off, bloom on/off, etc.
    // Each toggle also marks the matching override bit for scene persistence.
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        cinematic.skyAtmosphereEnabled = !cinematic.skyAtmosphereEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ATMOSPHERE;
        break;
    case UICinematicFeature::Clouds:
        cinematic.cloudsEnabled = !cinematic.cloudsEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUDS;
        break;
    case UICinematicFeature::GodRays:
        cinematic.godRaysEnabled = !cinematic.godRaysEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_GOD_RAYS;
        break;
    case UICinematicFeature::VolumetricLight:
        cinematic.volumetricLightingEnabled = !cinematic.volumetricLightingEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_LIGHTING;
        break;
    case UICinematicFeature::Bloom:
        cinematic.bloomEnabled = !cinematic.bloomEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM;
        break;
    case UICinematicFeature::Fog:
        cinematic.fogEnabled = !cinematic.fogEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_FOG;
        break;
    case UICinematicFeature::TerrainRelief:
        cinematic.terrainReliefEnabled = !cinematic.terrainReliefEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF_ENABLED;
        break;
    case UICinematicFeature::Shadows:
        SetCinematicShadowsEnabledFromUI( cinematic, scene, !cinematic.shadowsEnabled );
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIFeature( feature );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
