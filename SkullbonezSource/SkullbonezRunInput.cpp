// --- Includes ---
#include "SkullbonezRunInternal.h"

// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
void ResetMouseLookInput( RunCameraState& camera )
{
    camera.input.xMove = 0;
    camera.input.yMove = 0;
    camera.hasMouseLookLastClient = false;
    camera.needsMouseLookReset = true;
    Input::ResetMouseLookDeltas();
}


void SetMouseLookDelta( RunCameraState& camera, long rawX, long rawY )
{
    const long absX = rawX < 0 ? -rawX : rawX;
    const long absY = rawY < 0 ? -rawY : rawY;

    if ( absX > CAMERA_MOUSE_SPIKE_DELTA_PIXELS || absY > CAMERA_MOUSE_SPIKE_DELTA_PIXELS )
    {
        camera.input.xMove = 0;
        camera.input.yMove = 0;
        return;
    }

    camera.input.xMove = std::clamp( rawX, -CAMERA_MOUSE_MAX_DELTA_PIXELS, CAMERA_MOUSE_MAX_DELTA_PIXELS );
    camera.input.yMove = std::clamp( rawY, -CAMERA_MOUSE_MAX_DELTA_PIXELS, CAMERA_MOUSE_MAX_DELTA_PIXELS );
}


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


void ApplyCinematicUIParam( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicParam param, float rawValue )
{
    // The UI sends "the user dragged this slider to this raw value." This helper
    // clamps the value into a safe range, writes it into the live cinematic
    // config, and marks the scene override bit so reloads keep the user's tweak.
    const auto clampValue = []( float value, float minValue, float maxValue ) -> float
    {
        return std::clamp( value, minValue, maxValue );
    };
    const auto clampIntValue = []( float value, int minValue, int maxValue ) -> int
    {
        return std::clamp( static_cast<int>( std::round( value ) ), minValue, maxValue );
    };

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
    cinematic.shadowsEnabled = enabled;
    scene.cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    scene.uiCinematicOverrideMask |= SCENE_CINE_SHADOWS;
    if ( enabled )
    {
        cinematic.enabled = true;
        scene.hasCinematicRenderingOverride = true;
        scene.isCinematicRenderingEnabled = true;
        scene.cinematicOverrideMask |= SCENE_CINE_RENDERING;
        scene.uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
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
} // namespace

void SkullbonezRun::StepPhysicsPipelineStage( int direction )
{
    const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    m_debug.physicsDebugFlags |= PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( m_debug.physicsDebugPipelineStageCursor + direction ) % stageCount;
    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }
    m_debug.physicsDebugPipelineStageCursor = nextStage;
}


void SkullbonezRun::TakeInput()
{
    if ( !Input::IsAppFocused() )
    {
        Input::SetSystemCursorVisible( true );
        m_camera.input = {};
        m_camera.hasMouseLookLastClient = false;
        m_camera.needsMouseLookReset = true;
        Input::ResetMouseLookDeltas();
        m_leftSceneCycleWasDown = false;
        m_rightSceneCycleWasDown = false;
        Input::ConsumeMouseWheelDelta();
        m_UI.CancelInputCapture();
        RunUIStressActions();
        return;
    }

    const auto UIWantsReleasedMouse = [&]() -> bool
    {
        return m_camera.isFlyMode && m_UI.WantsNativeMouseCursor();
    };
    const auto ApplyCursorOwnership = [&]() -> void
    {
        Input::SetSystemCursorVisible( UIWantsReleasedMouse() );
    };
    const auto ReleaseMouseToUI = [&]() -> void
    {
        if ( UIWantsReleasedMouse() )
        {
            ReleaseCapture();
            ResetMouseLookInput( m_camera );
        }
    };

    ApplyCursorOwnership();

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    if ( !UIBlocksKeyboardBeforeInput )
    {
        // Toggle fly mode with F (edge-detected so snapshot-loaded fly mode survives the next frame)
        bool prevFlyMode = m_camera.isFlyMode;
        bool fNow = Input::IsKeyDown( 'F' );
        if ( fNow && !m_camera.input.Get( InputState::FWasDown ) )
        {
            m_camera.isFlyMode = !m_camera.isFlyMode;
            m_camera.isNudgeMode = false; // F-key fly never implies nudge
        }
        m_camera.input.Set( InputState::FWasDown, fNow );

        // N key: toggle nudge mode — free camera with live simulation (edge-detected).
        // Nudge entering also enters fly mode; nudge exiting also exits fly mode.
        {
            bool nNow = Input::IsKeyDown( 'N' );
            if ( nNow && !m_camera.input.Get( InputState::NWasDown ) )
            {
                m_camera.isNudgeMode = !m_camera.isNudgeMode;
                m_camera.isFlyMode = m_camera.isNudgeMode;
            }
            m_camera.input.Set( InputState::NWasDown, nNow );
        }

#ifdef _DEBUG
        {
            bool enterNow = Input::IsKeyDown( VK_RETURN );
            if ( enterNow && !m_camera.input.Get( InputState::EnterWasDown ) && m_camera.isNudgeMode )
            {
                WriteNudgeReproSnapshot();
            }
            m_camera.input.Set( InputState::EnterWasDown, enterNow );
        }
#endif

        if ( m_camera.isFlyMode != prevFlyMode )
        {
            if ( m_camera.isFlyMode )
            {
                // Entering fly mode: generated demo mode snaps to free camera; scene mode stays
                // on the current camera so fly controls work without requiring CAMERA_FREE
                if ( !m_scene.isSceneMode )
                {
                    m_systems.cameras->SelectCamera( CAMERA_FREE, false );
                }
                m_camera.cameraTime = 0.0f;
                XZBounds unbounded;
                unbounded.m_xMin = -99999.9f;
                unbounded.m_xMax = 99999.9f;
                unbounded.m_zMin = -99999.9f;
                unbounded.m_zMax = 99999.9f;
                uint32_t activeCam = m_scene.isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
                m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
                if ( UIWantsReleasedMouse() )
                {
                    ReleaseMouseToUI();
                    Input::SetSystemCursorVisible( true );
                }
                else
                {
                    Input::SetSystemCursorVisible( false );
                }
                ResetMouseLookInput( m_camera );
            }
            else
            {
                // Exiting fly mode restores terrain bounds and the camera-cycle clock.  The
                // Windows cursor stays hidden because the diagnostics UI now draws the styled
                // cursor itself; restoring IDC_ARROW here creates a mismatched second cursor.
                uint32_t activeCam = m_scene.isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
                m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
                Input::SetSystemCursorVisible( false );
                m_camera.cameraTime = 0.0f;
                // Exiting fly mode also exits nudge mode
                m_camera.isNudgeMode = false;
                ResetMouseLookInput( m_camera );
            }
        }

        // Water m_shader debug toggles
        bool key1Now = Input::IsKeyDown( '1' );
        if ( key1Now && !m_camera.input.Get( InputState::Key1WasDown ) )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        m_camera.input.Set( InputState::Key1WasDown, key1Now );
        // Key '2' cycles reflection mode: FBO (default) → DXR ray-traced (if supported) → none → FBO
        {
            static bool s_key2WasDown = false;
            bool s_key2Now = ( Input::IsKeyDown( '2' ) != 0 );
            if ( s_key2Now && !s_key2WasDown )
            {
                if ( !m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect )
                {
                    if ( Gfx().GetCapabilities().supportsDxrReflection )
                    {
                        m_debug.isWaterRTReflect = true; // FBO → DXR
                    }
                    else
                    {
                        m_debug.isWaterNoReflect = true; // DXR not available, skip to none
                    }
                }
                else if ( m_debug.isWaterRTReflect )
                {
                    m_debug.isWaterRTReflect = false;
                    m_debug.isWaterNoReflect = true; // DXR → none
                }
                else
                {
                    m_debug.isWaterNoReflect = false; // none → FBO
                }
            }
            s_key2WasDown = s_key2Now;
        }
        {
            bool key3Now = Input::IsKeyDown( '3' );
            if ( key3Now && !m_camera.input.Get( InputState::Key3WasDown ) )
            {
                m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            }
            m_camera.input.Set( InputState::Key3WasDown, key3Now );
        }
        {
            bool key4Now = Input::IsKeyDown( '4' );
            if ( key4Now && !m_camera.input.Get( InputState::Key4WasDown ) )
            {
                m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            }
            m_camera.input.Set( InputState::Key4WasDown, key4Now );
        }
        {
            bool key5Now = Input::IsKeyDown( '5' );
            if ( key5Now && !m_camera.input.Get( InputState::Key5WasDown ) )
            {
                m_debug.isWaterHidden = !m_debug.isWaterHidden;
            }
            m_camera.input.Set( InputState::Key5WasDown, key5Now );
        }
        // V key: collision visualizer. Renders balls and boxes as solid debug colours.
        {
            bool vNow = Input::IsKeyDown( 'V' );
            if ( vNow && !m_camera.input.Get( InputState::VWasDown ) )
            {
                m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            }
            m_camera.input.Set( InputState::VWasDown, vNow );
        }

        // C key: cycle physics debug overlay - None -> Axes -> Contacts -> Sleep -> All -> None.
        {
            bool cNow = Input::IsKeyDown( 'C' );
            if ( cNow && !m_camera.input.Get( InputState::CKeyWasDown ) )
            {
                switch ( m_debug.physicsDebugFlags )
                {
                case PHYSICS_DEBUG_NONE:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_AXES;
                    break;
                case PHYSICS_DEBUG_AXES:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
                    break;
                case PHYSICS_DEBUG_CONTACTS:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_SLEEP;
                    break;
                case PHYSICS_DEBUG_SLEEP:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_ALL;
                    break;
                default:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
                    break;
                }
            }
            m_camera.input.Set( InputState::CKeyWasDown, cNow );
        }

        // F7/F8: step the physics pipeline visualizer through the bounded Catto
        // stage trace from the most recent physics tick. The simulation can be
        // paused with fly mode and advanced separately with Space.
        {
            static bool s_pipelinePrevWasDown = false;
            static bool s_pipelineNextWasDown = false;
            const bool prevNow = Input::IsKeyDown( VK_F7 );
            const bool nextNow = Input::IsKeyDown( VK_F8 );
            if ( prevNow && !s_pipelinePrevWasDown )
            {
                StepPhysicsPipelineStage( -1 );
            }
            if ( nextNow && !s_pipelineNextWasDown )
            {
                StepPhysicsPipelineStage( 1 );
            }
            s_pipelinePrevWasDown = prevNow;
            s_pipelineNextWasDown = nextNow;
        }

        // 6 key: translucent debug collision volumes for inspecting axes/contact rows inside bodies.
        {
            bool key6Now = Input::IsKeyDown( '6' );
            if ( key6Now && !m_camera.input.Get( InputState::Key6WasDown ) )
            {
                m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            }
            m_camera.input.Set( InputState::Key6WasDown, key6Now );
        }

        // Q key: cycle render backend at runtime while preserving current simulation state (GL → DX11 → DX12 → GL).
        {
            bool isQNow = Input::IsKeyDown( 'Q' );
            if ( isQNow && !m_camera.input.Get( InputState::QKeyWasDown ) )
            {
                SwitchRenderer( GetNextRendererType( GetCurrentRendererType() ) );
            }
            m_camera.input.Set( InputState::QKeyWasDown, isQNow );
        }

        // G key: toggle broadphase overlay, or cycle tracked ball if overlay is off.
        bool isGNow = Input::IsKeyDown( 'G' );
        if ( isGNow && !m_camera.input.Get( InputState::GKeyWasDown ) )
        {
            if ( m_scene.isSceneMode && m_camera.trackBallIndex >= 0 && !m_debug.isBroadphaseOverlay )
            {
                int count = m_cGameModelCollection.GetModelCount();
                if ( count > 0 )
                {
                    m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
                }
            }
            else
            {
                m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            }
        }
        m_camera.input.Set( InputState::GKeyWasDown, isGNow );

        // 0 key: toggle the in-game diagnostics window. Tabs replace the old overlay cycle.
        // Edge-detected in both scene and generated demo modes; one toggle per keypress.
        {
            bool key0Now = Input::IsKeyDown( '0' );
            if ( key0Now && !m_camera.input.Get( InputState::Key0WasDown ) )
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( m_timers.simulationTimer.GetTotalTime() );
                m_debug.overlayMode = OverlayMode::None;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
            m_camera.input.Set( InputState::Key0WasDown, key0Now );
        }

        const bool leftSceneNow = Input::IsKeyDown( VK_LEFT );
        const bool rightSceneNow = Input::IsKeyDown( VK_RIGHT );
        if ( leftSceneNow && !m_leftSceneCycleWasDown )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( -1 ) )
            {
                LoadAdjacentSceneFromBrowser( -1 );
            }
        }
        if ( rightSceneNow && !m_rightSceneCycleWasDown )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( 1 ) )
            {
                LoadAdjacentSceneFromBrowser( 1 );
            }
        }
        m_leftSceneCycleWasDown = leftSceneNow;
        m_rightSceneCycleWasDown = rightSceneNow;
    }
    else
    {
        m_leftSceneCycleWasDown = Input::IsKeyDown( VK_LEFT );
        m_rightSceneCycleWasDown = Input::IsKeyDown( VK_RIGHT );
    }

    bool suppressNudgeFireThisFrame = UIBlocksKeyboardBeforeInput;
    if ( m_systems.window )
    {
        const int selectedSceneBrowserIndex = CurrentSceneBrowserIndex();
        InGameUIInputResult UIResult = m_UI.UpdateInput( m_systems.window->m_sWindow,
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.x ),
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.y ),
                                                         m_timers.simulationTimer.GetTotalTime(),
                                                         m_sceneBrowserNamePtrs.empty() ? nullptr : m_sceneBrowserNamePtrs.data(),
                                                         static_cast<int>( m_sceneBrowserNamePtrs.size() ),
                                                         selectedSceneBrowserIndex );
        const InGameUICommands& uiCommands = UIResult.commands;
        if ( uiCommands.ui.userInteracted )
        {
            EnterInteractiveSceneRun();
        }
        suppressNudgeFireThisFrame = suppressNudgeFireThisFrame || uiCommands.ui.userInteracted || m_UI.BlocksCameraMouse();

        // ESC flicks the diagnostics window between minimized and expanded, with
        // a very fast double-tap escape hatch for quitting interactive runs.
        // Run it after UI input processing so focused controls keep their local ESC
        // behavior first, such as closing the scene filter combo without also
        // hiding the whole diagnostics surface on the same frame.
        const bool escapeNow = Input::IsKeyDown( VK_ESCAPE );
        if ( escapeNow && !m_camera.input.Get( InputState::EscapeWasDown ) && !uiCommands.ui.userInteracted )
        {
            constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
            const double UINow = m_timers.simulationTimer.GetTotalTime();
            if ( UINow - m_lastEscapeTapTime <= ESC_QUICK_EXIT_SECONDS )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( UINow );
                m_debug.overlayMode = OverlayMode::None;
                m_lastEscapeTapTime = UINow;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
        }
        m_camera.input.Set( InputState::EscapeWasDown, escapeNow );

        if ( uiCommands.renderer.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
        }
        if ( uiCommands.physics.toggleCollisionVisualizer )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
        }
        if ( uiCommands.physics.togglePhysicsSleepPolicy )
        {
            m_runtimeSettings.isPhysicsSleepEnabled = !m_runtimeSettings.isPhysicsSleepEnabled;
            m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
        }
        if ( uiCommands.physics.togglePhysicsDebugFlags != 0 )
        {
            m_debug.physicsDebugFlags ^= ( uiCommands.physics.togglePhysicsDebugFlags & PHYSICS_DEBUG_ALL );
        }
        if ( uiCommands.physics.stepPhysicsPipelinePrevious )
        {
            StepPhysicsPipelineStage( -1 );
        }
        if ( uiCommands.physics.stepPhysicsPipelineNext )
        {
            StepPhysicsPipelineStage( 1 );
        }
        if ( uiCommands.physics.togglePhysicsDebugTransparent )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
        }
        if ( uiCommands.physics.toggleBroadphaseOverlay )
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
        }
        if ( uiCommands.sceneOptions.toggleTextOnly )
        {
            m_debug.isTextOnly = !m_debug.isTextOnly;
        }
        if ( uiCommands.sceneOptions.toggleFixedStep )
        {
            m_scene.isFixedStep = !m_scene.isFixedStep;
            m_timers.physicsAccumulator = 0.0f;
            m_timers.fixedStepTickAccumulator = 0.0f;
        }
        if ( uiCommands.sceneOptions.toggleTerrainHidden )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
        }
        if ( uiCommands.sceneOptions.toggleWaterHidden )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
        }
        if ( uiCommands.sceneOptions.toggleWaterFreeze )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        if ( uiCommands.sceneOptions.toggleWaterFlat )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
        }
        if ( uiCommands.sceneOptions.toggleShadows )
        {
            const bool shadowsActive = IsCinematicRenderingEnabled() && ActiveCinematicConfig().shadowsEnabled;
            m_cmdHasCinematicShadowsOverride = false;
            if ( !shadowsActive )
            {
                m_cmdHasCinematicRenderingOverride = false;
            }
            SetCinematicShadowsEnabledFromUI( ActiveCinematicConfig(), m_scene, !shadowsActive );
        }
        if ( uiCommands.water.toggleWaterReflection )
        {
            if ( m_debug.isWaterNoReflect )
            {
                m_debug.isWaterNoReflect = false;
            }
            else
            {
                m_debug.isWaterNoReflect = true;
                m_debug.isWaterRTReflect = false;
            }
        }
        if ( uiCommands.water.requestedWaterReflectionMode >= 0 )
        {
            const int mode = std::clamp( uiCommands.water.requestedWaterReflectionMode, 0, 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
        }
        if ( uiCommands.sceneOptions.requestedTimeScale > 0.0f )
        {
            m_UITimeScaleOverride = std::clamp( uiCommands.sceneOptions.requestedTimeScale, 0.10f, 10.00f );
            m_scene.timeScale = m_UITimeScaleOverride;
            m_timers.physicsAccumulator = 0.0f;
            m_timers.fixedStepTickAccumulator = 0.0f;
        }
        if ( uiCommands.run.requestedSeed > 0 )
        {
            m_scene.rngSeed = static_cast<unsigned int>( std::clamp( uiCommands.run.requestedSeed, 1, 999999 ) );
            srand( m_scene.rngSeed );
        }
        if ( uiCommands.physics.requestedPhysicsDebugAlpha >= 0.0f )
        {
            m_debug.physicsDebugAlpha = std::clamp( uiCommands.physics.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
        }
        if ( uiCommands.physics.requestedPhysicsDebugContactLinger >= 0.0f )
        {
            m_debug.physicsDebugContactLinger = std::clamp( uiCommands.physics.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
        }
        if ( uiCommands.sceneOptions.requestedModelCount >= 0 )
        {
            ApplyUIModelCountOverride( uiCommands.sceneOptions.requestedModelCount );
        }
        if ( uiCommands.run.requestedSolverBallCount >= 0 )
        {
            const int boxes = m_UISolverBoxCountOverride >= 0 ? m_UISolverBoxCountOverride : m_scene.solverBoxCount;
            ApplyUISolverObjectCounts( std::clamp( uiCommands.run.requestedSolverBallCount, 0, (std::max)( 0, 1000 - boxes ) ), boxes );
        }
        if ( uiCommands.run.requestedSolverBoxCount >= 0 )
        {
            const int balls = m_UISolverBallCountOverride >= 0 ? m_UISolverBallCountOverride : m_scene.solverBallCount;
            ApplyUISolverObjectCounts( balls, std::clamp( uiCommands.run.requestedSolverBoxCount, 0, (std::max)( 0, 1000 - balls ) ) );
        }
        if ( uiCommands.water.requestWorldGravity || uiCommands.water.requestWorldFluidHeight || uiCommands.water.requestWorldFluidDensity )
        {
            const float gravity = uiCommands.water.requestWorldGravity ? uiCommands.water.requestedWorldGravity : m_cWorldEnvironment.GetGravity();
            const float fluidHeight = uiCommands.water.requestWorldFluidHeight ? uiCommands.water.requestedWorldFluidHeight : m_cWorldEnvironment.GetFluidSurfaceHeight();
            const float fluidDensity = uiCommands.water.requestWorldFluidDensity ? uiCommands.water.requestedWorldFluidDensity : m_cWorldEnvironment.GetFluidDensity();
            ApplyUIWorldOverride( std::clamp( gravity, -100.0f, 0.0f ),
                                  std::clamp( fluidHeight, -100.0f, 200.0f ),
                                  std::clamp( fluidDensity, 0.0f, 5.0f ) );
        }
        if ( uiCommands.cinematic.toggleRendering )
        {
            // Master Cine switch. Clearing m_cmdHasCinematicRenderingOverride lets
            // the runtime toggle become the new source of truth after launch.
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            const bool currentlyEnabled = m_cmdHasCinematicRenderingOverride ? m_cmdCinematicRendering : cinematic.enabled;
            cinematic.enabled = !currentlyEnabled;
            m_cmdHasCinematicRenderingOverride = false;
            if ( m_scene.isSceneMode )
            {
                m_scene.hasCinematicRenderingOverride = true;
                m_scene.isCinematicRenderingEnabled = cinematic.enabled;
                m_scene.cinematicOverrideMask |= SCENE_CINE_RENDERING;
                m_scene.uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
            }
        }
        if ( uiCommands.cinematic.requestedModeSceneIndex >= -1 )
        {
            ApplyCinematicModeFromBrowserIndex( uiCommands.cinematic.requestedModeSceneIndex );
        }
        if ( uiCommands.cinematic.requestedFeature != UICinematicFeature::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            if ( uiCommands.cinematic.requestedFeature == UICinematicFeature::Shadows )
            {
                m_cmdHasCinematicShadowsOverride = false;
                if ( !cinematic.shadowsEnabled )
                {
                    m_cmdHasCinematicRenderingOverride = false;
                }
            }
            ToggleCinematicUIFeature( cinematic, m_scene, uiCommands.cinematic.requestedFeature );
        }
        if ( uiCommands.cinematic.requestedParam != UICinematicParam::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            ApplyCinematicUIParam( cinematic, m_scene, uiCommands.cinematic.requestedParam, uiCommands.cinematic.requestedValue );
        }
        if ( uiCommands.scene.resetScene )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
        if ( uiCommands.scene.resetSceneDefaults )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( false, true, false );
        }
        if ( uiCommands.scene.requestDemoScene )
        {
            LoadDemoSceneFromUI();
        }
        if ( uiCommands.scene.saveSceneDefaults )
        {
            SaveCurrentSceneDefaults();
        }
        if ( uiCommands.renderer.requestedRendererIndex >= 0 )
        {
            RuntimeRendererType requestedRenderer = RuntimeRendererType::OpenGL;
            if ( uiCommands.renderer.requestedRendererIndex == 1 )
            {
                requestedRenderer = RuntimeRendererType::DX11;
            }
            else if ( uiCommands.renderer.requestedRendererIndex == 2 )
            {
                requestedRenderer = RuntimeRendererType::DX12;
            }
            SwitchRenderer( requestedRenderer );
        }
        if ( uiCommands.scene.requestedSceneIndex >= 0 )
        {
            LoadSceneFromBrowserIndex( uiCommands.scene.requestedSceneIndex );
        }

        RunUIStressActions();
    }

    // Nudge mode owns left click for firing the pooled silver bullets.  Keyboard
    // shortcuts are intentionally avoided so aiming and firing live on the mouse.
    {
        const bool leftMouseNow = Input::IsLeftMouseDown();
        if ( m_camera.isNudgeMode &&
             leftMouseNow &&
             !m_camera.input.Get( InputState::LeftMouseWasDown ) &&
             !suppressNudgeFireThisFrame )
        {
            FireProjectile();
        }
        m_camera.input.Set( InputState::LeftMouseWasDown, leftMouseNow );
    }

    if ( m_UI.BlocksKeyboard() )
    {
        ResetMouseLookInput( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        ApplyCursorOwnership();
        return;
    }

    // F2: Save scene snapshot to Scenes/
    {
        bool f2Now = Input::IsKeyDown( VK_F2 );
        if ( f2Now && !m_camera.input.Get( InputState::F2WasDown ) )
        {
            CreateDirectoryA( "Scenes", nullptr );
            static int sSnapshotSeq = 0;
            bool saved = false;
            for ( int tries = 0; tries < 100 && !saved; ++tries )
            {
                char path[256];
                sprintf_s( path, sizeof( path ), "Scenes\\snapshot_%04d.scene", sSnapshotSeq++ );
                saved = m_cGameModelCollection.SaveSceneSnapshot(
                    path,
                    m_scene.isScenePhysics,
                    m_scene.isSceneText,
                    m_cWorldEnvironment,
                    m_systems.cameras->GetCameraTranslation(),
                    m_systems.cameras->GetCameraView(),
                    m_systems.cameras->GetCameraUp() );
            }
        }
        m_camera.input.Set( InputState::F2WasDown, f2Now );
    }

    // F3: Save screenshot to Screenshots/
    {
        bool f3Now = Input::IsKeyDown( VK_F3 );
        if ( f3Now && !m_camera.input.Get( InputState::F3WasDown ) )
        {
            CreateDirectoryA( "Screenshots", nullptr );
            static int sScreenshotSeq = 0;
            bool saved = false;
            for ( int tries = 0; tries < 100 && !saved; ++tries )
            {
                char path[256];
                sprintf_s( path, sizeof( path ), "Screenshots\\screenshot_%04d.bmp", sScreenshotSeq++ );
                if ( GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES )
                {
                    SaveScreenshot( path );
                    saved = true;
                }
            }
        }
        m_camera.input.Set( InputState::F3WasDown, f3Now );
    }

    // R: reset/reload the current scene from scratch. Backspace remains as a scene-mode alias.
    {
        bool rNow = Input::IsKeyDown( 'R' );
        if ( rNow && !m_camera.input.Get( InputState::RKeyWasDown ) )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
        m_camera.input.Set( InputState::RKeyWasDown, rNow );
    }
    if ( m_scene.isSceneMode )
    {
        bool bsNow = Input::IsKeyDown( VK_BACK );
        if ( bsNow && !m_camera.input.Get( InputState::BackspaceWasDown ) )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
        m_camera.input.Set( InputState::BackspaceWasDown, bsNow );
    }

    if ( m_camera.isFlyMode )
    {
        // Expanded diagnostics UI owns the native cursor in fly/nudge mode.
        // Otherwise mouse-look consumes raw Win32 deltas, with cursor-position deltas
        // as a remote-desktop friendly fallback when raw input is unavailable.
        if ( !Input::IsAppFocused() )
        {
            ResetMouseLookInput( m_camera );
        }
        else if ( UIWantsReleasedMouse() )
        {
            Input::SetSystemCursorVisible( true );
            ResetMouseLookInput( m_camera );
        }
        else if ( m_UI.BlocksCameraMouse() )
        {
            Input::SetSystemCursorVisible( false );
            ResetMouseLookInput( m_camera );
        }
        else
        {
            Input::SetSystemCursorVisible( false );
            long rawX = 0;
            long rawY = 0;
            const bool hasRawDelta = Input::ConsumeRawMouseDelta( rawX, rawY );
            POINT currentClient = Input::GetClientMouseCoordinates();

            if ( m_camera.needsMouseLookReset )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
                m_camera.needsMouseLookReset = false;
            }
            else if ( hasRawDelta )
            {
                SetMouseLookDelta( m_camera, rawX, rawY );
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else if ( !m_camera.hasMouseLookLastClient )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else
            {
                SetMouseLookDelta( m_camera,
                                   currentClient.x - m_camera.mouseLookLastClient.x,
                                   currentClient.y - m_camera.mouseLookLastClient.y );
                m_camera.mouseLookLastClient = currentClient;
            }
        }

        // WASD movement
        m_camera.input.Set( InputState::Up, Input::IsKeyDown( 'W' ) );
        m_camera.input.Set( InputState::Left, Input::IsKeyDown( 'A' ) );
        m_camera.input.Set( InputState::Down, Input::IsKeyDown( 'S' ) );
        m_camera.input.Set( InputState::Right, Input::IsKeyDown( 'D' ) );
    }
    else
    {
        ResetMouseLookInput( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
    }
}


void SkullbonezRun::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    if ( m_camera.isFlyMode )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 )
        {
            m_systems.cameras->RotatePrimary( m_camera.input.xMove * mouseMovementQty,
                                              m_camera.input.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_camera.input.Get( InputState::Up ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Left ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Down ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Right ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        m_systems.cameras->ApplyPrimaryMovementBuffer();
    }

    // Clamp camera Y between m_terrain surface and Cfg().maxCameraHeight (not in fly mode, not in scene mode)
    if ( !m_camera.isFlyMode && !m_scene.isSceneMode )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation();
        float minY = m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}


void SkullbonezRun::ResetProjectilePool()
{
    m_fire.bulletIndices.fill( -1 );
    m_fire.bulletNext = 0;
    m_fire.bulletPoolReady = false;
}


bool SkullbonezRun::EnsureProjectilePool()
{
    if ( m_fire.bulletPoolReady )
    {
        return true;
    }

    if ( m_cGameModelCollection.GetModelCount() > MAX_GAME_MODELS - RUNTIME_PROJECTILE_POOL_SIZE )
    {
        return false;
    }

    m_fire.bulletIndices.fill( -1 );
    for ( int i = 0; i < RUNTIME_PROJECTILE_POOL_SIZE; ++i )
    {
        const float parkOffset = static_cast<float>( i ) * ( CAMERA_PROJECTILE_RADIUS * 4.0f );
        GameModel bullet( &m_cWorldEnvironment,
                          Vector3( CAMERA_PROJECTILE_PARK_BASE, CAMERA_PROJECTILE_PARK_BASE - parkOffset, CAMERA_PROJECTILE_PARK_BASE ),
                          Vector3( CAMERA_PROJECTILE_MOMENT, CAMERA_PROJECTILE_MOMENT, CAMERA_PROJECTILE_MOMENT ),
                          CAMERA_PROJECTILE_MASS );
        bullet.SetTerrain( m_systems.terrain.get() );
        bullet.SetCoefficientRestitution( CAMERA_PROJECTILE_RESTITUTION );
        bullet.AddBoundingSphere( CAMERA_PROJECTILE_RADIUS );
        bullet.SetRenderTint( CAMERA_PROJECTILE_SILVER_R, CAMERA_PROJECTILE_SILVER_G, CAMERA_PROJECTILE_SILVER_B, 1.0f );
        bullet.SetFixed( true );

        char name[64];
        sprintf_s( name, sizeof( name ), "silver_bullet_%02d", i );
        bullet.SetName( name );

        const int bulletIndex = m_cGameModelCollection.GetModelCount();
        m_cGameModelCollection.AddGameModel( std::move( bullet ) );
        m_fire.bulletIndices[i] = bulletIndex;
    }

    m_fire.bulletNext = 0;
    m_fire.bulletPoolReady = true;
    return true;
}


void SkullbonezRun::FireProjectile()
{
    if ( !EnsureProjectilePool() )
    {
        return;
    }

    const int slot = m_fire.bulletNext;
    m_fire.bulletNext = ( m_fire.bulletNext + 1 ) % RUNTIME_PROJECTILE_POOL_SIZE;

    const int found = m_fire.bulletIndices[slot];
    if ( found < 0 || found >= m_cGameModelCollection.GetModelCount() )
    {
        ResetProjectilePool();
        return;
    }

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( found );

    const Vector3& camPos = m_systems.cameras->GetCameraTranslation();
    const Vector3& camView = m_systems.cameras->GetCameraView();
    Vector3 forward = camView - camPos;
    const float lenSq = forward * forward;
    if ( lenSq < 1e-8f )
    {
        return;
    }
    forward = forward * ( 1.0f / sqrtf( lenSq ) );

    const Vector3 spawnPos = camPos + forward * CAMERA_PROJECTILE_SPAWN_CLEARANCE;

    const float speedMult = Input::IsKeyDown( VK_SHIFT ) ? CAMERA_PROJECTILE_SHIFT_MULTIPLIER : 1.0f;
    const float fireSpeed = CAMERA_PROJECTILE_SPEED * speedMult;

    model.SetFixed( false );
    m_cGameModelCollection.WakeModel( found );
    model.SetPosition( spawnPos );
    model.SetLinearVelocity( forward * fireSpeed );
    model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
    model.SetRenderTint( CAMERA_PROJECTILE_SILVER_R, CAMERA_PROJECTILE_SILVER_G, CAMERA_PROJECTILE_SILVER_B, 1.0f );
}
