/*
File: SkullbonezSource/Core/Config.cpp
Purpose:
  Loads, stores, and exposes engine configuration values from files and command-line overrides.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.

Related:
  - SkullbonezSource/Core/Config.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Common.h"
#include "Config.h"
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>


using namespace SkullbonezCore::Basics;


namespace
{
enum class ConfigValueType
{
    Int,
    Float,
    Bool,
    String
};

struct ConfigSetting
{
    const char* name;
    ConfigValueType type;
    bool hasRange;
    double minValue;
    double maxValue;
    bool ( *apply )( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line );
    void ( *dump )( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting );
};

struct FileCloser
{
    void operator()( FILE* file ) const
    {
        if ( file )
        {
            fclose( file );
        }
    }
};

using FileHandle = std::unique_ptr<FILE, FileCloser>;

bool IsSpaceOrTab( char c )
{
    return c == ' ' || c == '\t';
}

char* TrimInPlace( char* text )
{
    while ( IsSpaceOrTab( *text ) )
    {
        ++text;
    }

    size_t len = strlen( text );
    while ( len > 0 && IsSpaceOrTab( text[len - 1] ) )
    {
        text[--len] = '\0';
    }
    return text;
}

void WarnConfigLine( const char* path, int line, const char* key, const char* value, const char* reason )
{
    fprintf( stderr,
             "[config] %s:%d ignored %s=%s (%s).\n",
             path ? path : "<config>",
             line,
             key ? key : "<unknown>",
             value ? value : "",
             reason ? reason : "invalid value" );
}

bool IsRangeValid( double value, const ConfigSetting& setting )
{
    return !setting.hasRange || ( value >= setting.minValue && value <= setting.maxValue );
}

bool ParseConfigIntValue( const char* value, const ConfigSetting& setting, const char* path, int line, int& out )
{
    if ( !value || *value == '\0' )
    {
        WarnConfigLine( path, line, setting.name, value, "expected integer" );
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = strtol( value, &end, 10 );
    if ( end == value || *end != '\0' || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX )
    {
        WarnConfigLine( path, line, setting.name, value, "expected integer" );
        return false;
    }

    if ( !IsRangeValid( static_cast<double>( parsed ), setting ) )
    {
        WarnConfigLine( path, line, setting.name, value, "outside allowed range" );
        return false;
    }

    out = static_cast<int>( parsed );
    return true;
}

bool ParseConfigFloatValue( const char* value, const ConfigSetting& setting, const char* path, int line, float& out )
{
    if ( !value || *value == '\0' )
    {
        WarnConfigLine( path, line, setting.name, value, "expected float" );
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const double parsed = strtod( value, &end );
    if ( end == value || *end != '\0' || errno == ERANGE )
    {
        WarnConfigLine( path, line, setting.name, value, "expected float" );
        return false;
    }

    if ( !IsRangeValid( parsed, setting ) )
    {
        WarnConfigLine( path, line, setting.name, value, "outside allowed range" );
        return false;
    }

    out = static_cast<float>( parsed );
    return true;
}

bool ParseConfigBoolValue( const char* value, const ConfigSetting& setting, const char* path, int line, bool& out )
{
    if ( !value || *value == '\0' )
    {
        WarnConfigLine( path, line, setting.name, value, "expected boolean" );
        return false;
    }

    if ( _stricmp( value, "true" ) == 0 || _stricmp( value, "on" ) == 0 || _stricmp( value, "yes" ) == 0 )
    {
        out = true;
        return true;
    }
    if ( _stricmp( value, "false" ) == 0 || _stricmp( value, "off" ) == 0 || _stricmp( value, "no" ) == 0 )
    {
        out = false;
        return true;
    }

    int parsed = 0;
    if ( ParseConfigIntValue( value, setting, path, line, parsed ) )
    {
        out = parsed != 0;
        return true;
    }

    WarnConfigLine( path, line, setting.name, value, "expected boolean" );
    return false;
}

bool ApplyConfigString( EngineConfig& cfg,
                        const char* value,
                        const ConfigSetting& setting,
                        const char* path,
                        int line,
                        std::string& out )
{
    static_cast<void>( cfg );
    if ( !value || *value == '\0' )
    {
        WarnConfigLine( path, line, setting.name, value, "expected non-empty string" );
        return false;
    }
    out = value;
    return true;
}

#define CONFIG_INT( KEY, FIELD, MIN_VALUE, MAX_VALUE )                                                                 \
    { KEY,                                                                                                             \
      ConfigValueType::Int,                                                                                            \
      true,                                                                                                            \
      static_cast<double>( MIN_VALUE ),                                                                                \
      static_cast<double>( MAX_VALUE ),                                                                                \
      []( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line ) -> bool     \
      {                                                                                                                \
          int parsed = 0;                                                                                              \
          if ( !ParseConfigIntValue( value, setting, path, line, parsed ) )                                            \
          {                                                                                                            \
              return false;                                                                                            \
          }                                                                                                            \
          cfg.FIELD = parsed;                                                                                          \
          return true;                                                                                                 \
      },                                                                                                               \
      []( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting )                                           \
      { fprintf( out, "%s = %d\n", setting.name, cfg.FIELD ); } }

#define CONFIG_FLOAT( KEY, FIELD, MIN_VALUE, MAX_VALUE )                                                               \
    { KEY,                                                                                                             \
      ConfigValueType::Float,                                                                                          \
      true,                                                                                                            \
      static_cast<double>( MIN_VALUE ),                                                                                \
      static_cast<double>( MAX_VALUE ),                                                                                \
      []( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line ) -> bool     \
      {                                                                                                                \
          float parsed = 0.0f;                                                                                         \
          if ( !ParseConfigFloatValue( value, setting, path, line, parsed ) )                                          \
          {                                                                                                            \
              return false;                                                                                            \
          }                                                                                                            \
          cfg.FIELD = parsed;                                                                                          \
          return true;                                                                                                 \
      },                                                                                                               \
      []( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting )                                           \
      { fprintf( out, "%s = %.9g\n", setting.name, static_cast<double>( cfg.FIELD ) ); } }

#define CONFIG_BOOL( KEY, FIELD )                                                                                      \
    { KEY,                                                                                                             \
      ConfigValueType::Bool,                                                                                           \
      true,                                                                                                            \
      0.0,                                                                                                             \
      static_cast<double>( INT_MAX ),                                                                                  \
      []( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line ) -> bool     \
      {                                                                                                                \
          bool parsed = false;                                                                                         \
          if ( !ParseConfigBoolValue( value, setting, path, line, parsed ) )                                           \
          {                                                                                                            \
              return false;                                                                                            \
          }                                                                                                            \
          cfg.FIELD = parsed;                                                                                          \
          return true;                                                                                                 \
      },                                                                                                               \
      []( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting )                                           \
      { fprintf( out, "%s = %d\n", setting.name, cfg.FIELD ? 1 : 0 ); } }

#define CONFIG_STRING( KEY, FIELD )                                                                                    \
    { KEY,                                                                                                             \
      ConfigValueType::String,                                                                                         \
      false,                                                                                                           \
      0.0,                                                                                                             \
      0.0,                                                                                                             \
      []( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line ) -> bool     \
      { return ApplyConfigString( cfg, value, setting, path, line, cfg.FIELD ); },                                     \
      []( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting )                                           \
      { fprintf( out, "%s = %s\n", setting.name, cfg.FIELD.c_str() ); } }

const ConfigSetting* ConfigSettings( size_t& outCount )
{
    static const ConfigSetting kSettings[] = {
        CONFIG_INT( "screen_x", window.screenX, 1, 32768 ),
        CONFIG_INT( "screen_y", window.screenY, 1, 32768 ),
        CONFIG_BOOL( "fullscreen", window.fullscreen ),
        CONFIG_INT( "bits_per_pixel", window.bitsPerPixel, 1, 128 ),
        CONFIG_INT( "refresh_rate", window.refreshRate, 1, 1000 ),

        CONFIG_FLOAT( "frustum_near", frustumNear, 0.0001, 100000000.0 ),
        CONFIG_FLOAT( "frustum_far", frustumFar, 0.0001, 100000000.0 ),

        CONFIG_FLOAT( "mouse_sensitivity", mouseSensitivity, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "key_speed", keySpeed, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "camera_tween_rate", cameraTweenRate, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "camera_collision_threshold", cameraCollisionThreshold, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "min_camera_height", minCameraHeight, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "max_camera_height", maxCameraHeight, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "min_view_mag", minViewMag, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "max_view_mag", maxViewMag, 0.0, 1000000.0 ),

        CONFIG_FLOAT( "terrain_scale", terrainScale, 0.0001, 1000000.0 ),
        CONFIG_FLOAT( "terrain_height_scale", terrainHeightScale, -1000000.0, 1000000.0 ),
        CONFIG_INT( "terrain_render_step_size", terrainRenderStepSize, 1, 1024 ),

        CONFIG_FLOAT( "skybox_render_height", skyboxRenderHeight, -1000000.0, 1000000.0 ),
        CONFIG_INT( "skybox_overflow", skyboxOverflow, -1000000, 1000000 ),
        CONFIG_FLOAT( "skybox_scale", skyboxScale, 0.0001, 1000000.0 ),

        CONFIG_INT( "game_model_capacity", gameModelCapacity, 1, MAX_GAME_MODELS ),
        CONFIG_INT( "worker_threads", workerThreads, -1, 1024 ),
        CONFIG_BOOL( "physics_parallel", physicsParallel ),
        CONFIG_BOOL( "physics_parallel_apply_forces", physicsParallelApplyForces ),
        CONFIG_BOOL( "physics_parallel_tornado_field", physicsParallelTornadoField ),
        CONFIG_BOOL( "physics_parallel_narrowphase", physicsParallelNarrowphase ),
        CONFIG_BOOL( "physics_parallel_terrain_detect", physicsParallelTerrainDetect ),
        CONFIG_BOOL( "physics_parallel_integrate", physicsParallelIntegrate ),
        CONFIG_BOOL( "shadow_parallel_prep", shadowParallelPrep ),

        CONFIG_FLOAT( "scene_light_color_r", sceneLight.colorR, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "scene_light_color_g", sceneLight.colorG, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "scene_light_color_b", sceneLight.colorB, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "scene_light_color_a", sceneLight.colorA, -1000000.0, 1000000.0 ),

        CONFIG_FLOAT( "ordinary_sun_intensity", ordinaryRender.sunIntensity, 0.0, 8.0 ),
        CONFIG_FLOAT( "ordinary_sun_color_r", ordinaryRender.sunColorR, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_sun_color_g", ordinaryRender.sunColorG, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_sun_color_b", ordinaryRender.sunColorB, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_ambient_strength", ordinaryRender.ambientStrength, 0.0, 2.0 ),
        CONFIG_FLOAT( "ordinary_sky_ambient_r", ordinaryRender.skyAmbientR, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_sky_ambient_g", ordinaryRender.skyAmbientG, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_sky_ambient_b", ordinaryRender.skyAmbientB, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_ground_ambient_r", ordinaryRender.groundAmbientR, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_ground_ambient_g", ordinaryRender.groundAmbientG, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_ground_ambient_b", ordinaryRender.groundAmbientB, 0.0, 4.0 ),
        CONFIG_BOOL( "ordinary_shadows", ordinaryRender.shadowsEnabled ),
        CONFIG_BOOL( "ordinary_shadow_terrain_casts", ordinaryRender.shadowTerrainCasts ),
        CONFIG_BOOL( "ordinary_shadow_objects_cast", ordinaryRender.shadowObjectsCast ),
        CONFIG_BOOL( "ordinary_shadow_terrain_receives", ordinaryRender.shadowTerrainReceives ),
        CONFIG_BOOL( "ordinary_shadow_objects_receive", ordinaryRender.shadowObjectsReceive ),
        CONFIG_INT( "ordinary_shadow_map_size", ordinaryRender.shadowMapSize, 256, 8192 ),
        CONFIG_INT( "ordinary_shadow_pcf_radius", ordinaryRender.shadowPcfRadius, 0, 3 ),
        CONFIG_FLOAT( "ordinary_shadow_strength", ordinaryRender.shadowStrength, 0.0, 1.0 ),
        CONFIG_FLOAT( "ordinary_shadow_softness", ordinaryRender.shadowSoftness, 0.25, 4.0 ),
        CONFIG_FLOAT( "ordinary_shadow_depth_bias", ordinaryRender.shadowDepthBias, 0.0, 0.05 ),
        CONFIG_FLOAT( "ordinary_shadow_slope_bias", ordinaryRender.shadowSlopeBias, 0.0, 0.05 ),
        CONFIG_FLOAT( "ordinary_shadow_max_distance", ordinaryRender.shadowMaxDistance, 128.0, 10000.0 ),
        CONFIG_FLOAT( "ordinary_water_tint_r", ordinaryRender.waterTintR, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_water_tint_g", ordinaryRender.waterTintG, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_water_tint_b", ordinaryRender.waterTintB, 0.0, 4.0 ),
        CONFIG_FLOAT( "ordinary_water_alpha", ordinaryRender.waterAlpha, 0.0, 1.0 ),
        CONFIG_FLOAT( "ordinary_water_reflection_strength", ordinaryRender.waterReflectionStrength, 0.0, 1.0 ),
        CONFIG_FLOAT( "ordinary_water_fresnel_f0", ordinaryRender.waterFresnelF0, 0.0, 0.25 ),
        CONFIG_FLOAT( "ordinary_ball_roughness_scale", ordinaryRender.ballRoughnessScale, 0.25, 2.0 ),
        CONFIG_FLOAT( "ordinary_ball_specular_scale", ordinaryRender.ballSpecularScale, 0.0, 2.0 ),
        CONFIG_FLOAT( "ordinary_box_roughness_scale", ordinaryRender.boxRoughnessScale, 0.25, 2.0 ),
        CONFIG_FLOAT( "ordinary_box_specular_scale", ordinaryRender.boxSpecularScale, 0.0, 2.0 ),

        CONFIG_BOOL( "cinematic_rendering", cinematicRender.enabled ),
        CONFIG_BOOL( "cinematic_sky_atmosphere", cinematicRender.skyAtmosphereEnabled ),
        CONFIG_BOOL( "cinematic_clouds", cinematicRender.cloudsEnabled ),
        CONFIG_BOOL( "cinematic_god_rays", cinematicRender.godRaysEnabled ),
        CONFIG_BOOL( "cinematic_volumetric_lighting", cinematicRender.volumetricLightingEnabled ),
        CONFIG_BOOL( "cinematic_bloom", cinematicRender.bloomEnabled ),
        CONFIG_BOOL( "cinematic_fog", cinematicRender.fogEnabled ),
        CONFIG_BOOL( "cinematic_terrain_relief_enabled", cinematicRender.terrainReliefEnabled ),
        CONFIG_FLOAT( "cinematic_exposure", cinematicRender.exposure, 0.0, 16.0 ),
        CONFIG_FLOAT( "cinematic_gamma", cinematicRender.gamma, 0.1, 8.0 ),
        CONFIG_FLOAT( "cinematic_sun_screen_x", cinematicRender.sunScreenX, 0.0, 1.0 ),
        CONFIG_FLOAT( "cinematic_sun_screen_y", cinematicRender.sunScreenY, 0.0, 1.0 ),
        CONFIG_FLOAT( "cinematic_sun_color_r", cinematicRender.sunColorR, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sun_color_g", cinematicRender.sunColorG, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sun_color_b", cinematicRender.sunColorB, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sun_intensity", cinematicRender.sunIntensity, 0.0, 80.0 ),
        CONFIG_FLOAT( "cinematic_sky_horizon_r", cinematicRender.skyHorizonR, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sky_horizon_g", cinematicRender.skyHorizonG, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sky_horizon_b", cinematicRender.skyHorizonB, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sky_zenith_r", cinematicRender.skyZenithR, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sky_zenith_g", cinematicRender.skyZenithG, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sky_zenith_b", cinematicRender.skyZenithB, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sky_glow_strength", cinematicRender.skyGlowStrength, 0.0, 16.0 ),
        CONFIG_FLOAT( "cinematic_cloud_coverage", cinematicRender.cloudCoverage, 0.0, 1.0 ),
        CONFIG_FLOAT( "cinematic_cloud_softness", cinematicRender.cloudSoftness, 0.001, 1.0 ),
        CONFIG_FLOAT( "cinematic_cloud_scale", cinematicRender.cloudScale, 0.1, 64.0 ),
        CONFIG_FLOAT( "cinematic_cloud_intensity", cinematicRender.cloudIntensity, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_sun_shaft_strength", cinematicRender.sunShaftStrength, 0.0, 8.0 ),
        CONFIG_FLOAT( "cinematic_sun_shaft_falloff", cinematicRender.sunShaftFalloff, 0.1, 10.0 ),
        CONFIG_FLOAT( "cinematic_volumetric_strength", cinematicRender.volumetricStrength, 0.0, 8.0 ),
        CONFIG_FLOAT( "cinematic_volumetric_density", cinematicRender.volumetricDensity, 0.0, 8.0 ),
        CONFIG_FLOAT( "cinematic_volumetric_decay", cinematicRender.volumetricDecay, 0.0, 1.0 ),
        CONFIG_FLOAT( "cinematic_bloom_threshold", cinematicRender.bloomThreshold, 0.0, 16.0 ),
        CONFIG_FLOAT( "cinematic_bloom_knee", cinematicRender.bloomKnee, 0.001, 8.0 ),
        CONFIG_FLOAT( "cinematic_bloom_strength", cinematicRender.bloomStrength, 0.0, 8.0 ),
        CONFIG_FLOAT( "cinematic_bloom_radius", cinematicRender.bloomRadius, 0.1, 32.0 ),
        CONFIG_FLOAT( "cinematic_terrain_relief", cinematicRender.terrainRelief, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_basin_depth", cinematicRender.basinDepth, 0.0, 256.0 ),
        CONFIG_FLOAT( "cinematic_basin_rim_lift", cinematicRender.basinRimLift, 0.0, 256.0 ),
        CONFIG_BOOL( "cinematic_shadows", cinematicRender.shadowsEnabled ),
        CONFIG_BOOL( "cinematic_shadow_terrain_casts", cinematicRender.shadowTerrainCasts ),
        CONFIG_BOOL( "cinematic_shadow_objects_cast", cinematicRender.shadowObjectsCast ),
        CONFIG_BOOL( "cinematic_shadow_terrain_receives", cinematicRender.shadowTerrainReceives ),
        CONFIG_BOOL( "cinematic_shadow_objects_receive", cinematicRender.shadowObjectsReceive ),
        CONFIG_INT( "cinematic_shadow_map_size", cinematicRender.shadowMapSize, 256, 8192 ),
        CONFIG_INT( "cinematic_shadow_pcf_radius", cinematicRender.shadowPcfRadius, 0, 3 ),
        CONFIG_FLOAT( "cinematic_shadow_strength", cinematicRender.shadowStrength, 0.0, 1.0 ),
        CONFIG_FLOAT( "cinematic_shadow_softness", cinematicRender.shadowSoftness, 0.25, 4.0 ),
        CONFIG_FLOAT( "cinematic_shadow_depth_bias", cinematicRender.shadowDepthBias, 0.0, 0.05 ),
        CONFIG_FLOAT( "cinematic_shadow_slope_bias", cinematicRender.shadowSlopeBias, 0.0, 0.05 ),
        CONFIG_FLOAT( "cinematic_shadow_max_distance", cinematicRender.shadowMaxDistance, 128.0, 10000.0 ),
        CONFIG_FLOAT( "cinematic_fog_color_r", cinematicRender.fogColorR, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_fog_color_g", cinematicRender.fogColorG, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_fog_color_b", cinematicRender.fogColorB, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_fog_start", cinematicRender.fogStart, 0.0, 10000.0 ),
        CONFIG_FLOAT( "cinematic_fog_end", cinematicRender.fogEnd, 0.0, 20000.0 ),
        CONFIG_FLOAT( "cinematic_fog_density", cinematicRender.fogDensity, 0.0, 0.1 ),
        CONFIG_FLOAT( "cinematic_fog_max_opacity", cinematicRender.fogMaxOpacity, 0.0, 1.0 ),
        CONFIG_INT( "cinematic_sky_mode", cinematicRender.skyMode, 0, 32 ),
        CONFIG_INT( "cinematic_terrain_mode", cinematicRender.terrainMode, 0, 32 ),
        CONFIG_INT( "cinematic_object_style", cinematicRender.objectStyle, 0, 32 ),
        CONFIG_INT( "cinematic_water_mode", cinematicRender.waterMode, 0, 4 ),
        CONFIG_FLOAT( "cinematic_style_saturation", cinematicRender.styleSaturation, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_style_contrast", cinematicRender.styleContrast, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_style_vignette", cinematicRender.styleVignette, 0.0, 1.0 ),
        CONFIG_FLOAT( "cinematic_terrain_tint_r", cinematicRender.terrainTintR, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_terrain_tint_g", cinematicRender.terrainTintG, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_terrain_tint_b", cinematicRender.terrainTintB, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_terrain_accent_r", cinematicRender.terrainAccentR, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_terrain_accent_g", cinematicRender.terrainAccentG, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_terrain_accent_b", cinematicRender.terrainAccentB, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_terrain_grid_scale", cinematicRender.terrainGridScale, 0.1, 1000.0 ),
        CONFIG_FLOAT( "cinematic_terrain_grid_strength", cinematicRender.terrainGridStrength, 0.0, 16.0 ),
        CONFIG_FLOAT( "cinematic_water_tint_r", cinematicRender.waterTintR, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_water_tint_g", cinematicRender.waterTintG, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_water_tint_b", cinematicRender.waterTintB, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_water_alpha", cinematicRender.waterAlpha, 0.0, 1.0 ),
        CONFIG_FLOAT( "cinematic_water_reflection_strength", cinematicRender.waterReflectionStrength, 0.0, 1.0 ),
        CONFIG_FLOAT( "cinematic_water_glint_strength", cinematicRender.waterGlintStrength, 0.0, 4.0 ),
        CONFIG_FLOAT( "cinematic_basin_center_x", cinematicRender.basinCenterX, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "cinematic_basin_center_z", cinematicRender.basinCenterZ, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "cinematic_basin_radius_x", cinematicRender.basinRadiusX, 1.0, 1000000.0 ),
        CONFIG_FLOAT( "cinematic_basin_radius_z", cinematicRender.basinRadiusZ, 1.0, 1000000.0 ),
        CONFIG_FLOAT( "cinematic_basin_feather", cinematicRender.basinFeather, 0.0, 1.0 ),

        CONFIG_FLOAT( "gravity", gravity, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "fluid_height", fluidHeight, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "fluid_density", fluidDensity, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "gas_density", gasDensity, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "velocity_limit", velocityLimit, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "sphere_drag_coeff", sphereDragCoeff, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "fluid_angular_drag_multiplier", fluidAngularDragMultiplier, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "friction_coeff", frictionCoeff, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "object_friction_coeff", objectFrictionCoeff, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "rolling_friction_coeff", rollingFrictionCoeff, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "spin_friction_coeff", spinFrictionCoeff, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "contact_restitution_threshold", contactRestitutionThreshold, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "contact_epsilon", contactEpsilon, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "broadphase_cell", broadphaseCell, 0.0001, 1000000.0 ),
        CONFIG_FLOAT( "persistent_contact_slop", persistentContactSlop, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "persistent_contact_baumgarte_beta", persistentContactBaumgarteBeta, 0.0, 1.0 ),
        CONFIG_FLOAT( "persistent_contact_position_correction_percent",
                      persistentContactPositionCorrectionPercent,
                      0.0,
                      1.0 ),
        CONFIG_INT( "persistent_contact_solver_iterations", persistentContactSolverIterations, 1, 1000000 ),
        CONFIG_FLOAT( "terrain_contact_threshold", terrainContactThreshold, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "terrain_contact_slop", terrainContactSlop, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "terrain_contact_baumgarte_beta", terrainContactBaumgarteBeta, 0.0, 1.0 ),
        CONFIG_FLOAT( "terrain_max_baumgarte_bias", terrainMaxBaumgarteBias, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "physics_sleep_linear_speed", physicsSleepLinearSpeed, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "physics_sleep_angular_speed", physicsSleepAngularSpeed, 0.0, 1000000.0 ),
        CONFIG_INT( "physics_sleep_frames", physicsSleepFrames, 0, 1000000 ),

        CONFIG_FLOAT( "shadow_max_height", shadowMaxHeight, 0.0, 1000000.0 ),
        CONFIG_FLOAT( "shadow_max_alpha", shadowMaxAlpha, 0.0, 1.0 ),
        CONFIG_FLOAT( "shadow_offset", shadowOffset, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "shadow_scale", shadowScale, 0.0, 1000000.0 ),

        CONFIG_FLOAT( "spawn_x_base", spawnXBase, -1000000.0, 1000000.0 ),
        CONFIG_INT( "spawn_x_range", spawnXRange, 0, 1000000 ),
        CONFIG_FLOAT( "spawn_y_base", spawnYBase, -1000000.0, 1000000.0 ),
        CONFIG_INT( "spawn_y_range", spawnYRange, 0, 1000000 ),
        CONFIG_FLOAT( "spawn_z_base", spawnZBase, -1000000.0, 1000000.0 ),
        CONFIG_INT( "spawn_z_range", spawnZRange, 0, 1000000 ),
        CONFIG_FLOAT( "ball_mass_min", ballMassMin, 0.0, 1000000.0 ),
        CONFIG_INT( "ball_mass_range", ballMassRange, 0, 1000000 ),
        CONFIG_FLOAT( "ball_moment_min", ballMomentMin, 0.0, 1000000.0 ),
        CONFIG_INT( "ball_moment_range", ballMomentRange, 0, 1000000 ),
        CONFIG_FLOAT( "ball_restitution_min", ballRestitutionMin, -1000000.0, 1000000.0 ),
        CONFIG_INT( "ball_restitution_range", ballRestitutionRange, 0, 1000000 ),
        CONFIG_INT( "ball_radius_range", ballRadiusRange, 0, 1000000 ),
        CONFIG_INT( "ball_force_range", ballForceRange, 0, 1000000 ),

        CONFIG_STRING( "sky_front", skyFront ),
        CONFIG_STRING( "sky_left", skyLeft ),
        CONFIG_STRING( "sky_back", skyBack ),
        CONFIG_STRING( "sky_right", skyRight ),
        CONFIG_STRING( "sky_up", skyUp ),
        CONFIG_STRING( "sky_down", skyDown ),
        CONFIG_STRING( "terrain_texture", terrainTexture ),
        CONFIG_STRING( "sphere_texture", sphereTexture ),
        CONFIG_STRING( "terrain_raw", terrainRaw ),

        CONFIG_FLOAT( "ocean_wave_height", oceanWaveHeight, -1000000.0, 1000000.0 ),
        CONFIG_FLOAT( "ocean_perturb_strength", oceanPerturbStrength, -1000000.0, 1000000.0 ),

        CONFIG_BOOL( "vsync_enabled", runtimeRender.vsyncEnabled ),
        CONFIG_BOOL( "force_pipeline_sync", runtimeRender.forcePipelineSync ),
        CONFIG_BOOL( "render_collision_volumes", runtimeRender.renderCollisionVolumes ),
        CONFIG_BOOL( "contact_audio_enabled", contactAudio.enabled ),
        CONFIG_FLOAT( "contact_audio_master_gain", contactAudio.masterGain, 0.0, 4.0 ),
        CONFIG_FLOAT( "contact_audio_max_distance_scale", contactAudio.maxDistanceScale, 0.01, 16.0 ),
        CONFIG_FLOAT( "contact_audio_rolling_level_db", contactAudio.rollingLevelDb, -60.0, 0.0 ),
        CONFIG_FLOAT( "contact_audio_rolling_max_distance", contactAudio.rollingMaxDistance, 1.0, 200.0 ),
        CONFIG_FLOAT( "contact_audio_rolling_min_slip_speed", contactAudio.rollingMinSlipSpeed, 0.1, 20.0 ),
        CONFIG_INT( "contact_audio_rolling_voices_per_window", contactAudio.rollingVoicesPerWindow, 0, 12 ),
        CONFIG_BOOL( "contact_audio_debug_counters", contactAudio.debugCounters ),
    };
    outCount = sizeof( kSettings ) / sizeof( kSettings[0] );
    return kSettings;
}

const ConfigSetting* FindConfigSetting( const char* name )
{
    size_t count = 0;
    const ConfigSetting* settings = ConfigSettings( count );
    for ( size_t i = 0; i < count; ++i )
    {
        if ( strcmp( settings[i].name, name ) == 0 )
        {
            return &settings[i];
        }
    }
    return nullptr;
}
} // anonymous namespace

/* ---------------------------------------------------------------------------------*/
void EngineConfig::Load( const char* path )
{
    // engine.cfg is an optional developer/runtime defaults file. Unknown or
    // malformed lines are skipped with a warning so older configs do not block
    // startup after a setting is removed.
    FILE* rawFile = nullptr;
    if ( fopen_s( &rawFile, path, "r" ) != 0 || !rawFile )
    {
        return;
    }
    FileHandle file( rawFile );

    char line[512];
    int lineNumber = 0;
    while ( fgets( line, sizeof( line ), file.get() ) )
    {
        ++lineNumber;
        size_t len = strlen( line );
        while ( len > 0 && ( line[len - 1] == '\r' || line[len - 1] == '\n' ) )
        {
            line[--len] = '\0';
        }

        char* trimmedLine = TrimInPlace( line );
        if ( *trimmedLine == '\0' || *trimmedLine == '#' )
        {
            continue;
        }

        char* eq = strchr( trimmedLine, '=' );
        if ( !eq )
        {
            WarnConfigLine( path, lineNumber, trimmedLine, "", "expected key=value" );
            continue;
        }

        *eq = '\0';
        char* key = TrimInPlace( trimmedLine );
        char* value = TrimInPlace( eq + 1 );

        char* hash = strchr( value, '#' );
        if ( hash )
        {
            *hash = '\0';
            value = TrimInPlace( value );
        }

        if ( *key == '\0' || *value == '\0' )
        {
            WarnConfigLine( path, lineNumber, key, value, "expected non-empty key and value" );
            continue;
        }

        const ConfigSetting* setting = FindConfigSetting( key );
        if ( !setting )
        {
            WarnConfigLine( path, lineNumber, key, value, "unknown setting" );
            continue;
        }

        setting->apply( *this, value, *setting, path, lineNumber );
    }
}


/* ---------------------------------------------------------------------------------*/
void EngineConfig::Dump( FILE* out ) const
{
    if ( !out )
    {
        return;
    }

    fprintf( out, "[config]\n" );
    size_t count = 0;
    const ConfigSetting* settings = ConfigSettings( count );
    for ( size_t i = 0; i < count; ++i )
    {
        settings[i].dump( *this, out, settings[i] );
    }
}
