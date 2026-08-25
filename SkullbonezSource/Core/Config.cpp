/*
File: SkullbonezSource/Core/Config.cpp
Purpose:
  Loads, stores, and exposes engine configuration values from files and command-line overrides.

Summary:
  The parser registers every configuration key once, layers file values and
  command-line overrides into EngineConfig, validates ranges, and preserves
  versioned compatibility spellings. Optional absence keeps defaults, while
  other open/read failures leave the prior object intact and report startup
  failure.

Glossary:
  - ConfigSetting: One typed key-to-field registry row, including the accepted
    range and the destination inside EngineConfig.
  - Configuration registry: The single table that keeps file parsing,
    command-line overrides, validation, and compatibility spellings aligned.
  - Mutual-gravity worker toggle: Version-2 execution key that enables the
    deterministic pair-build worker stage without changing force semantics.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Domain tables own key/type/range/destination facts. kConfigSettingOrder is
    the single lookup and Dump order; neither path may invent a second sequence.
  - Version-1 config files omit the mutual-gravity worker key and therefore
    retain its enabled default; the migration tool materializes the same value.
  - Both file passes must finish before parsed settings replace caller state.

Related:
  - SkullbonezSource/Core/Config.h
  - Agentic/Reference/runtime-reference.md
*/
#include "Common.h"
#include "Config.h"
#include "SbDiagnosticStore.h"
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>


using namespace SkullbonezCore::Core;


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

using FileHandle = std::unique_ptr<FILE, decltype( &fclose )>;

#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
thread_local int s_settingsReadFailureAfterLineForTest = -1;
#endif

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
    fprintf( stderr, "[config] %s:%d ignored %s=%s (%s).\n", path ? path : "<config>", line, key ? key : "<unknown>",
             value ? value : "", reason ? reason : "invalid value" );
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

bool ApplyConfigString( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line,
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

#define CONFIG_INT( KEY, FIELD, MIN_VALUE, MAX_VALUE )                                                                      \
    { KEY,                                                                                                                  \
      ConfigValueType::Int,                                                                                                 \
      true,                                                                                                                 \
      static_cast<double>( MIN_VALUE ),                                                                                     \
      static_cast<double>( MAX_VALUE ),                                                                                     \
      []( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line ) -> bool          \
      {                                                                                                                     \
          int parsed = 0;                                                                                                   \
          if ( !ParseConfigIntValue( value, setting, path, line, parsed ) )                                                 \
          {                                                                                                                 \
              return false;                                                                                                 \
          }                                                                                                                 \
          cfg.FIELD = parsed;                                                                                               \
          return true;                                                                                                      \
      },                                                                                                                    \
      []( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting )                                                \
      { fprintf( out, "%s = %d\n", setting.name, cfg.FIELD ); } }

#define CONFIG_FLOAT( KEY, FIELD, MIN_VALUE, MAX_VALUE )                                                                    \
    { KEY,                                                                                                                  \
      ConfigValueType::Float,                                                                                               \
      true,                                                                                                                 \
      static_cast<double>( MIN_VALUE ),                                                                                     \
      static_cast<double>( MAX_VALUE ),                                                                                     \
      []( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line ) -> bool          \
      {                                                                                                                     \
          float parsed = 0.0f;                                                                                              \
          if ( !ParseConfigFloatValue( value, setting, path, line, parsed ) )                                               \
          {                                                                                                                 \
              return false;                                                                                                 \
          }                                                                                                                 \
          cfg.FIELD = parsed;                                                                                               \
          return true;                                                                                                      \
      },                                                                                                                    \
      []( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting )                                                \
      { fprintf( out, "%s = %.9g\n", setting.name, static_cast<double>( cfg.FIELD ) ); } }

#define CONFIG_BOOL( KEY, FIELD )                                                                                           \
    { KEY,                                                                                                                  \
      ConfigValueType::Bool,                                                                                                \
      true,                                                                                                                 \
      0.0,                                                                                                                  \
      static_cast<double>( INT_MAX ),                                                                                       \
      []( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line ) -> bool          \
      {                                                                                                                     \
          bool parsed = false;                                                                                              \
          if ( !ParseConfigBoolValue( value, setting, path, line, parsed ) )                                                \
          {                                                                                                                 \
              return false;                                                                                                 \
          }                                                                                                                 \
          cfg.FIELD = parsed;                                                                                               \
          return true;                                                                                                      \
      },                                                                                                                    \
      []( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting )                                                \
      { fprintf( out, "%s = %d\n", setting.name, cfg.FIELD ? 1 : 0 ); } }

#define CONFIG_STRING( KEY, FIELD )                                                                                         \
    { KEY,                                                                                                                  \
      ConfigValueType::String,                                                                                              \
      false,                                                                                                                \
      0.0,                                                                                                                  \
      0.0,                                                                                                                  \
      []( EngineConfig& cfg, const char* value, const ConfigSetting& setting, const char* path, int line ) -> bool          \
      { return ApplyConfigString( cfg, value, setting, path, line, cfg.FIELD ); },                                          \
      []( const EngineConfig& cfg, FILE* out, const ConfigSetting& setting )                                                \
      { fprintf( out, "%s = %s\n", setting.name, cfg.FIELD.c_str() ); } }

template <typename T, size_t N> constexpr size_t ArrayCount( const T ( & )[N] )
{
    return N;
}

enum class ConfigSettingDomain
{
    Window,
    Camera,
    TerrainGeometry,
    Skybox,
    RuntimeCapacity,
    PhysicsExecution,
    RuntimeRender,
    ReplayPrediction,
    SceneLight,
    OrdinaryRender,
    CinematicRender,
    WorldForce,
    BodySimulation,
    PhysicsMaterial,
    Broadphase,
    PersistentContactSolver,
    TerrainContact,
    PhysicsSleep,
    BlobShadow,
    GeneratedScene,
    AssetPaths,
    WaterRenderStyle,
    Count
};

struct ConfigSettingRange
{
    ConfigSettingDomain domain;
    size_t first;
    size_t count;
};

struct ConfigSettingTable
{
    const ConfigSetting* settings;
    size_t count;
};

template <size_t N> constexpr ConfigSettingRange FullConfigRange( ConfigSettingDomain domain, const ConfigSetting ( & )[N] )
{
    return { domain, 0, N };
}

// Concept: each table owns one EngineConfig domain's key/type/range/destination
// facts. The separate order registry below composes those facts into the public
// compatibility sequence without creating a second copy of any binding.
static const ConfigSetting kWindowSettings[] = {
    CONFIG_INT( "screen_x", window.screenX, 1, 32768 ),
    CONFIG_INT( "screen_y", window.screenY, 1, 32768 ),
    CONFIG_BOOL( "fullscreen", window.fullscreen ),
    CONFIG_INT( "bits_per_pixel", window.bitsPerPixel, 1, 128 ),
    CONFIG_INT( "refresh_rate", window.refreshRate, 1, 1000 ),
};

static const ConfigSetting kCameraSettings[] = {
    CONFIG_FLOAT( "frustum_near", camera.frustumNear, 0.0001, 100000000.0 ),
    CONFIG_FLOAT( "frustum_far", camera.frustumFar, 0.0001, 100000000.0 ),
    CONFIG_FLOAT( "mouse_sensitivity", camera.mouseSensitivity, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "key_speed", camera.keySpeed, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "camera_tween_rate", camera.cameraTweenRate, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "camera_collision_threshold", camera.cameraCollisionThreshold, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "min_camera_height", camera.minCameraHeight, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "max_camera_height", camera.maxCameraHeight, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "min_view_mag", camera.minViewMag, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "max_view_mag", camera.maxViewMag, 0.0, 1000000.0 ),
};

static const ConfigSetting kTerrainGeometrySettings[] = {
    CONFIG_FLOAT( "terrain_scale", terrainGeometry.scale, 0.0001, 1000000.0 ),
    CONFIG_FLOAT( "terrain_height_scale", terrainGeometry.heightScale, -1000000.0, 1000000.0 ),
};

static const ConfigSetting kSkyboxSettings[] = {
    CONFIG_FLOAT( "skybox_render_height", skybox.renderHeight, -1000000.0, 1000000.0 ),
    CONFIG_INT( "skybox_overflow", skybox.overflow, -1000000, 1000000 ),
    CONFIG_FLOAT( "skybox_scale", skybox.scale, 0.0001, 1000000.0 ),
};

static const ConfigSetting kRuntimeCapacitySettings[] = {
    CONFIG_INT( "game_model_capacity", runtimeCapacity.sceneObjectCapacity, 1,
                SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ),
    CONFIG_INT( "worker_threads", runtimeCapacity.workerThreads, -1, 1024 ),
};

static const ConfigSetting kPhysicsExecutionSettings[] = {
    CONFIG_BOOL( "physics_parallel", physicsExecution.parallel ),
    CONFIG_BOOL( "physics_parallel_apply_forces", physicsExecution.parallelApplyForces ),
    CONFIG_BOOL( "physics_parallel_mutual_gravity", physicsExecution.parallelMutualGravity ),
    CONFIG_BOOL( "physics_parallel_tornado_field", physicsExecution.parallelExternalForceFields ),
    CONFIG_BOOL( "physics_parallel_narrowphase", physicsExecution.parallelNarrowphase ),
    CONFIG_BOOL( "physics_parallel_terrain_detect", physicsExecution.parallelTerrainDetect ),
    CONFIG_BOOL( "physics_parallel_integrate", physicsExecution.parallelIntegrate ),
};

static const ConfigSetting kRuntimeRenderSettings[] = {
    CONFIG_BOOL( "shadow_parallel_prep", runtimeRender.shadowParallelPrep ),
    CONFIG_BOOL( "vsync_enabled", runtimeRender.vsyncEnabled ),
    CONFIG_BOOL( "force_pipeline_sync", runtimeRender.forcePipelineSync ),
    CONFIG_BOOL( "render_collision_volumes", runtimeRender.renderCollisionVolumes ),
    CONFIG_BOOL( "presentation_interpolation", runtimeRender.presentationInterpolation ),
};

static const ConfigSetting kReplayPredictionSettings[] = {
    CONFIG_FLOAT( "replay_prediction_instant_budget_ms", replayPrediction.instantBudgetMs, 0.0, 10000.0 ),
    CONFIG_INT( "replay_prediction_probe_ticks", replayPrediction.probeTicks, 8, 2400 ),
};

static const ConfigSetting kSceneLightSettings[] = {
    CONFIG_FLOAT( "scene_light_color_r", sceneLight.colorR, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "scene_light_color_g", sceneLight.colorG, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "scene_light_color_b", sceneLight.colorB, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "scene_light_color_a", sceneLight.colorA, -1000000.0, 1000000.0 ),
};

static const ConfigSetting kOrdinaryRenderSettings[] = {
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
    CONFIG_BOOL( "ordinary_shadows", ordinaryRender.shadow.enabled ),
    CONFIG_BOOL( "ordinary_shadow_terrain_casts", ordinaryRender.shadow.terrainCasts ),
    CONFIG_BOOL( "ordinary_shadow_objects_cast", ordinaryRender.shadow.objectsCast ),
    CONFIG_BOOL( "ordinary_shadow_terrain_receives", ordinaryRender.shadow.terrainReceives ),
    CONFIG_BOOL( "ordinary_shadow_objects_receive", ordinaryRender.shadow.objectsReceive ),
    CONFIG_INT( "ordinary_shadow_map_size", ordinaryRender.shadow.mapSize, 256, 8192 ),
    CONFIG_INT( "ordinary_shadow_pcf_radius", ordinaryRender.shadow.pcfRadius, 0, 3 ),
    CONFIG_FLOAT( "ordinary_shadow_strength", ordinaryRender.shadow.strength, 0.0, 1.0 ),
    CONFIG_FLOAT( "ordinary_shadow_softness", ordinaryRender.shadow.softness, 0.25, 4.0 ),
    CONFIG_FLOAT( "ordinary_shadow_depth_bias", ordinaryRender.shadow.depthBias, 0.0, 0.05 ),
    CONFIG_FLOAT( "ordinary_shadow_slope_bias", ordinaryRender.shadow.slopeBias, 0.0, 0.05 ),
    CONFIG_FLOAT( "ordinary_shadow_max_distance", ordinaryRender.shadow.maxDistance, 128.0, 10000.0 ),
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
    CONFIG_FLOAT( "replay_trajectory_future_width", ordinaryRender.replayTrajectory.futureWidth, 1.0, 6.0 ),
    CONFIG_FLOAT( "replay_trajectory_future_alpha", ordinaryRender.replayTrajectory.futureAlpha, 0.05, 1.0 ),
    CONFIG_FLOAT( "replay_trajectory_future_edge_feather", ordinaryRender.replayTrajectory.futureEdgeFeather, 0.25, 1.25 ),
    CONFIG_FLOAT( "replay_trajectory_causal_width", ordinaryRender.replayTrajectory.causalWidth, 1.0, 6.0 ),
    CONFIG_FLOAT( "replay_trajectory_causal_alpha", ordinaryRender.replayTrajectory.causalAlpha, 0.05, 1.0 ),
    CONFIG_FLOAT( "replay_trajectory_causal_edge_feather", ordinaryRender.replayTrajectory.causalEdgeFeather, 0.25, 1.25 ),
    CONFIG_FLOAT( "replay_trajectory_baseline_width", ordinaryRender.replayTrajectory.baselineWidth, 1.0, 6.0 ),
    CONFIG_FLOAT( "replay_trajectory_baseline_alpha", ordinaryRender.replayTrajectory.baselineAlpha, 0.05, 1.0 ),
    CONFIG_FLOAT( "replay_trajectory_baseline_edge_feather", ordinaryRender.replayTrajectory.baselineEdgeFeather, 0.25,
                  1.25 ),
    CONFIG_FLOAT( "replay_trajectory_marker_width", ordinaryRender.replayTrajectory.markerWidth, 1.0, 6.0 ),
    CONFIG_FLOAT( "replay_trajectory_marker_alpha", ordinaryRender.replayTrajectory.markerAlpha, 0.05, 1.0 ),
    CONFIG_FLOAT( "replay_trajectory_marker_edge_feather", ordinaryRender.replayTrajectory.markerEdgeFeather, 0.25, 1.25 ),
    CONFIG_FLOAT( "replay_trajectory_selected_emphasis", ordinaryRender.replayTrajectory.selectedEmphasis, 0.0, 1.0 ),
};

static const ConfigSetting kCinematicRenderSettings[] = {

    // Compatibility: historical "screen" keys still target normalized world-sky angles.
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
    CONFIG_FLOAT( "cinematic_sun_screen_x", cinematicRender.sunAzimuth, 0.0, 1.0 ),
    CONFIG_FLOAT( "cinematic_sun_screen_y", cinematicRender.sunElevation, 0.0, 1.0 ),
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
    CONFIG_BOOL( "cinematic_shadows", cinematicRender.shadow.enabled ),
    CONFIG_BOOL( "cinematic_shadow_terrain_casts", cinematicRender.shadow.terrainCasts ),
    CONFIG_BOOL( "cinematic_shadow_objects_cast", cinematicRender.shadow.objectsCast ),
    CONFIG_BOOL( "cinematic_shadow_terrain_receives", cinematicRender.shadow.terrainReceives ),
    CONFIG_BOOL( "cinematic_shadow_objects_receive", cinematicRender.shadow.objectsReceive ),
    CONFIG_INT( "cinematic_shadow_map_size", cinematicRender.shadow.mapSize, 256, 8192 ),
    CONFIG_INT( "cinematic_shadow_pcf_radius", cinematicRender.shadow.pcfRadius, 0, 3 ),
    CONFIG_FLOAT( "cinematic_shadow_strength", cinematicRender.shadow.strength, 0.0, 1.0 ),
    CONFIG_FLOAT( "cinematic_shadow_softness", cinematicRender.shadow.softness, 0.25, 4.0 ),
    CONFIG_FLOAT( "cinematic_shadow_depth_bias", cinematicRender.shadow.depthBias, 0.0, 0.05 ),
    CONFIG_FLOAT( "cinematic_shadow_slope_bias", cinematicRender.shadow.slopeBias, 0.0, 0.05 ),
    CONFIG_FLOAT( "cinematic_shadow_max_distance", cinematicRender.shadow.maxDistance, 128.0, 10000.0 ),
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
};

static const ConfigSetting kWorldForceSettings[] = {
    CONFIG_FLOAT( "gravity", worldForces.gravity, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "fluid_height", worldForces.fluidHeight, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "fluid_density", worldForces.fluidDensity, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "gas_density", worldForces.gasDensity, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "fluid_angular_drag_multiplier", worldForces.fluidAngularDragMultiplier, 0.0, 1000000.0 ),
};

static const ConfigSetting kBodySimulationSettings[] = {
    CONFIG_FLOAT( "velocity_limit", bodySimulation.velocityLimit, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "contact_restitution_threshold", bodySimulation.contactRestitutionThreshold, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "contact_epsilon", bodySimulation.contactEpsilon, 0.0, 1000000.0 ),
};

static const ConfigSetting kPhysicsMaterialSettings[] = {
    CONFIG_FLOAT( "sphere_drag_coeff", physicsMaterial.sphereDragCoeff, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "friction_coeff", physicsMaterial.frictionCoeff, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "object_friction_coeff", physicsMaterial.objectFrictionCoeff, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "rolling_friction_coeff", physicsMaterial.rollingFrictionCoeff, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "spin_friction_coeff", physicsMaterial.spinFrictionCoeff, 0.0, 1000000.0 ),
};

static const ConfigSetting kBroadphaseSettings[] = {
    CONFIG_FLOAT( "broadphase_cell", broadphase.cellSize, 0.0001, 1000000.0 ),
};

static const ConfigSetting kPersistentContactSolverSettings[] = {
    CONFIG_FLOAT( "persistent_contact_slop", persistentContactSolver.slop, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "persistent_contact_baumgarte_beta", persistentContactSolver.baumgarteBeta, 0.0, 1.0 ),
    CONFIG_FLOAT( "persistent_contact_position_correction_percent", persistentContactSolver.positionCorrectionPercent, 0.0,
                  1.0 ),
    CONFIG_INT( "persistent_contact_solver_iterations", persistentContactSolver.iterations, 1, 1000000 ),
};

static const ConfigSetting kTerrainContactSettings[] = {
    CONFIG_FLOAT( "terrain_contact_threshold", terrainContact.threshold, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "terrain_contact_slop", terrainContact.slop, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "terrain_contact_baumgarte_beta", terrainContact.baumgarteBeta, 0.0, 1.0 ),
    CONFIG_FLOAT( "terrain_max_baumgarte_bias", terrainContact.maxBaumgarteBias, 0.0, 1000000.0 ),
};

static const ConfigSetting kPhysicsSleepSettings[] = {
    CONFIG_FLOAT( "physics_sleep_linear_speed", physicsSleep.linearSpeed, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "physics_sleep_angular_speed", physicsSleep.angularSpeed, 0.0, 1000000.0 ),
    CONFIG_INT( "physics_sleep_frames", physicsSleep.frames, 0, 1000000 ),
};

static const ConfigSetting kBlobShadowSettings[] = {
    CONFIG_FLOAT( "shadow_max_height", blobShadow.maxHeight, 0.0, 1000000.0 ),
    CONFIG_FLOAT( "shadow_max_alpha", blobShadow.maxAlpha, 0.0, 1.0 ),
    CONFIG_FLOAT( "shadow_offset", blobShadow.offset, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "shadow_scale", blobShadow.scale, 0.0, 1000000.0 ),
};

static const ConfigSetting kGeneratedSceneSettings[] = {
    CONFIG_FLOAT( "spawn_x_base", generatedScene.spawnXBase, -1000000.0, 1000000.0 ),
    CONFIG_INT( "spawn_x_range", generatedScene.spawnXRange, 0, 1000000 ),
    CONFIG_FLOAT( "spawn_y_base", generatedScene.spawnYBase, -1000000.0, 1000000.0 ),
    CONFIG_INT( "spawn_y_range", generatedScene.spawnYRange, 0, 1000000 ),
    CONFIG_FLOAT( "spawn_z_base", generatedScene.spawnZBase, -1000000.0, 1000000.0 ),
    CONFIG_INT( "spawn_z_range", generatedScene.spawnZRange, 0, 1000000 ),
    CONFIG_FLOAT( "ball_mass_min", generatedScene.ballMassMin, 0.0, 1000000.0 ),
    CONFIG_INT( "ball_mass_range", generatedScene.ballMassRange, 0, 1000000 ),
    CONFIG_FLOAT( "ball_moment_min", generatedScene.ballMomentMin, 0.0, 1000000.0 ),
    CONFIG_INT( "ball_moment_range", generatedScene.ballMomentRange, 0, 1000000 ),
    CONFIG_FLOAT( "ball_restitution_min", generatedScene.ballRestitutionMin, -1000000.0, 1000000.0 ),
    CONFIG_INT( "ball_restitution_range", generatedScene.ballRestitutionRange, 0, 1000000 ),
    CONFIG_INT( "ball_radius_range", generatedScene.ballRadiusRange, 0, 1000000 ),
    CONFIG_INT( "ball_force_range", generatedScene.ballForceRange, 0, 1000000 ),
};

static const ConfigSetting kAssetPathsSettings[] = {
    CONFIG_STRING( "sky_front", assetPaths.skyFront ),
    CONFIG_STRING( "sky_left", assetPaths.skyLeft ),
    CONFIG_STRING( "sky_back", assetPaths.skyBack ),
    CONFIG_STRING( "sky_right", assetPaths.skyRight ),
    CONFIG_STRING( "sky_up", assetPaths.skyUp ),
    CONFIG_STRING( "sky_down", assetPaths.skyDown ),
    CONFIG_STRING( "terrain_texture", assetPaths.terrainTexture ),
    CONFIG_STRING( "sphere_texture", assetPaths.sphereTexture ),
    CONFIG_STRING( "terrain_raw", assetPaths.terrainRaw ),
};

static const ConfigSetting kWaterRenderStyleSettings[] = {
    CONFIG_FLOAT( "ocean_wave_height", waterRenderStyle.oceanWaveHeight, -1000000.0, 1000000.0 ),
    CONFIG_FLOAT( "ocean_perturb_strength", waterRenderStyle.oceanPerturbStrength, -1000000.0, 1000000.0 ),
};

constexpr size_t kExpectedConfigSettingCount = 224;
static_assert( ArrayCount( kWindowSettings ) + ArrayCount( kCameraSettings ) + ArrayCount( kTerrainGeometrySettings ) +
                       ArrayCount( kSkyboxSettings ) + ArrayCount( kRuntimeCapacitySettings ) +
                       ArrayCount( kPhysicsExecutionSettings ) + ArrayCount( kRuntimeRenderSettings ) +
                       ArrayCount( kReplayPredictionSettings ) + ArrayCount( kSceneLightSettings ) +
                       ArrayCount( kOrdinaryRenderSettings ) + ArrayCount( kCinematicRenderSettings ) +
                       ArrayCount( kWorldForceSettings ) + ArrayCount( kBodySimulationSettings ) +
                       ArrayCount( kPhysicsMaterialSettings ) + ArrayCount( kBroadphaseSettings ) +
                       ArrayCount( kPersistentContactSolverSettings ) + ArrayCount( kTerrainContactSettings ) +
                       ArrayCount( kPhysicsSleepSettings ) + ArrayCount( kBlobShadowSettings ) +
                       ArrayCount( kGeneratedSceneSettings ) + ArrayCount( kAssetPathsSettings ) +
                       ArrayCount( kWaterRenderStyleSettings ) ==
                   kExpectedConfigSettingCount,
               "Every engine config key must belong to exactly one domain table." );

// The enum ordinal, table pointer, and table count have one shared definition.
// Traversal and its constexpr proof therefore cannot disagree about which
// physical array belongs to a domain.
static constexpr ConfigSettingTable kConfigSettingTables[] = {
    { kWindowSettings, ArrayCount( kWindowSettings ) },
    { kCameraSettings, ArrayCount( kCameraSettings ) },
    { kTerrainGeometrySettings, ArrayCount( kTerrainGeometrySettings ) },
    { kSkyboxSettings, ArrayCount( kSkyboxSettings ) },
    { kRuntimeCapacitySettings, ArrayCount( kRuntimeCapacitySettings ) },
    { kPhysicsExecutionSettings, ArrayCount( kPhysicsExecutionSettings ) },
    { kRuntimeRenderSettings, ArrayCount( kRuntimeRenderSettings ) },
    { kReplayPredictionSettings, ArrayCount( kReplayPredictionSettings ) },
    { kSceneLightSettings, ArrayCount( kSceneLightSettings ) },
    { kOrdinaryRenderSettings, ArrayCount( kOrdinaryRenderSettings ) },
    { kCinematicRenderSettings, ArrayCount( kCinematicRenderSettings ) },
    { kWorldForceSettings, ArrayCount( kWorldForceSettings ) },
    { kBodySimulationSettings, ArrayCount( kBodySimulationSettings ) },
    { kPhysicsMaterialSettings, ArrayCount( kPhysicsMaterialSettings ) },
    { kBroadphaseSettings, ArrayCount( kBroadphaseSettings ) },
    { kPersistentContactSolverSettings, ArrayCount( kPersistentContactSolverSettings ) },
    { kTerrainContactSettings, ArrayCount( kTerrainContactSettings ) },
    { kPhysicsSleepSettings, ArrayCount( kPhysicsSleepSettings ) },
    { kBlobShadowSettings, ArrayCount( kBlobShadowSettings ) },
    { kGeneratedSceneSettings, ArrayCount( kGeneratedSceneSettings ) },
    { kAssetPathsSettings, ArrayCount( kAssetPathsSettings ) },
    { kWaterRenderStyleSettings, ArrayCount( kWaterRenderStyleSettings ) },
};
static_assert( ArrayCount( kConfigSettingTables ) == static_cast<size_t>( ConfigSettingDomain::Count ),
               "Every config domain must have exactly one table descriptor." );

// Invariant: these ranges are the single compatibility order for lookup and
// Dump(). Slices preserve the few historical interleavings between domains.
// Hazard: reordering a slice changes dump output and duplicate-key precedence.
static constexpr ConfigSettingRange kConfigSettingOrder[] = {
    FullConfigRange( ConfigSettingDomain::Window, kWindowSettings ),
    FullConfigRange( ConfigSettingDomain::Camera, kCameraSettings ),
    FullConfigRange( ConfigSettingDomain::TerrainGeometry, kTerrainGeometrySettings ),
    FullConfigRange( ConfigSettingDomain::Skybox, kSkyboxSettings ),
    FullConfigRange( ConfigSettingDomain::RuntimeCapacity, kRuntimeCapacitySettings ),
    FullConfigRange( ConfigSettingDomain::PhysicsExecution, kPhysicsExecutionSettings ),
    { ConfigSettingDomain::RuntimeRender, 0, 1 },
    FullConfigRange( ConfigSettingDomain::ReplayPrediction, kReplayPredictionSettings ),
    FullConfigRange( ConfigSettingDomain::SceneLight, kSceneLightSettings ),
    FullConfigRange( ConfigSettingDomain::OrdinaryRender, kOrdinaryRenderSettings ),
    FullConfigRange( ConfigSettingDomain::CinematicRender, kCinematicRenderSettings ),
    { ConfigSettingDomain::WorldForce, 0, 4 },
    { ConfigSettingDomain::BodySimulation, 0, 1 },
    { ConfigSettingDomain::PhysicsMaterial, 0, 1 },
    { ConfigSettingDomain::WorldForce, 4, 1 },
    { ConfigSettingDomain::PhysicsMaterial, 1, 4 },
    { ConfigSettingDomain::BodySimulation, 1, 2 },
    FullConfigRange( ConfigSettingDomain::Broadphase, kBroadphaseSettings ),
    FullConfigRange( ConfigSettingDomain::PersistentContactSolver, kPersistentContactSolverSettings ),
    FullConfigRange( ConfigSettingDomain::TerrainContact, kTerrainContactSettings ),
    FullConfigRange( ConfigSettingDomain::PhysicsSleep, kPhysicsSleepSettings ),
    FullConfigRange( ConfigSettingDomain::BlobShadow, kBlobShadowSettings ),
    FullConfigRange( ConfigSettingDomain::GeneratedScene, kGeneratedSceneSettings ),
    FullConfigRange( ConfigSettingDomain::AssetPaths, kAssetPathsSettings ),
    FullConfigRange( ConfigSettingDomain::WaterRenderStyle, kWaterRenderStyleSettings ),
    { ConfigSettingDomain::RuntimeRender, 1, 4 },
};

// Invariant: every row of every domain appears in exactly one ordered slice.
// Full-domain additions update automatically; additions to an interleaved
// domain fail this assertion until their deliberate compatibility position is
// added to kConfigSettingOrder.
constexpr bool ConfigSettingOrderCoversEveryDomainRowExactlyOnce()
{
    for ( const ConfigSettingRange& range : kConfigSettingOrder )
    {
        if ( static_cast<size_t>( range.domain ) >= static_cast<size_t>( ConfigSettingDomain::Count ) )
        {
            return false;
        }
    }

    for ( size_t domainIndex = 0; domainIndex < static_cast<size_t>( ConfigSettingDomain::Count ); ++domainIndex )
    {
        const ConfigSettingDomain domain = static_cast<ConfigSettingDomain>( domainIndex );
        const size_t domainCount = kConfigSettingTables[domainIndex].count;

        for ( const ConfigSettingRange& range : kConfigSettingOrder )
        {
            if ( range.domain == domain && ( range.first > domainCount || range.count > domainCount - range.first ) )
            {
                return false;
            }
        }

        for ( size_t row = 0; row < domainCount; ++row )
        {
            size_t visits = 0;

            for ( const ConfigSettingRange& range : kConfigSettingOrder )
            {
                if ( range.domain == domain && row >= range.first && row - range.first < range.count )
                {
                    ++visits;
                }
            }

            if ( visits != 1 )
            {
                return false;
            }
        }
    }

    return true;
}

static_assert( ConfigSettingOrderCoversEveryDomainRowExactlyOnce(),
               "Config setting order must visit every row of every domain exactly once." );

template <typename Visitor> bool VisitConfigSettingsInOrder( Visitor&& visitor )
{
    for ( const ConfigSettingRange& range : kConfigSettingOrder )
    {
        const ConfigSettingTable& table = kConfigSettingTables[static_cast<size_t>( range.domain )];

        for ( size_t row = 0; row < range.count; ++row )
        {
            if ( !visitor( table.settings[range.first + row] ) )
            {
                return false;
            }
        }
    }

    return true;
}

const ConfigSetting* FindConfigSetting( const char* name )
{
    const ConfigSetting* found = nullptr;
    VisitConfigSettingsInOrder( [name, &found]( const ConfigSetting& setting )
                                {
                                    if ( strcmp( setting.name, name ) != 0 )
                                    {
                                        return true;
                                    }

                                    found = &setting;
                                    return false;
                                } );

    return found;
}

SbResult OpenOptionalConfigFile( SbDiagnosticStore& diagnostics, const char* path, FILE*& outFile, bool& outMissing )
{
    outFile = nullptr;
    outMissing = false;
    errno = 0;
    const errno_t openError = fopen_s( &outFile, path, "r" );

    if ( openError == 0 && outFile )
    {
        return SbResult::Success();
    }

    const int error = openError != 0 ? static_cast<int>( openError ) : errno;
    if ( error == ENOENT )
    {
        outMissing = true;
        return SbResult::Success();
    }

    return diagnostics.Failure( "Core/EngineConfig", "Unable to open engine config for reading: %s (error %d).", path,
                                error );
}

SbResult RequireCompleteConfigRead( SbDiagnosticStore& diagnostics, FILE* file, const char* path, const char* pass,
                                    bool injectedFailure = false )
{
    if ( !injectedFailure && !ferror( file ) )
    {
        return SbResult::Success();
    }

    const int error = errno;
    return diagnostics.Failure( "Core/EngineConfig", "Unable to complete engine config %s read: %s (error %d).", pass,
                                path, error );
}

// Invariant: version validation is a separate read pass. A future file must
// fail before even one otherwise-valid setting mutates the destination object.
SbResult ReadConfigFormatVersion( SbDiagnosticStore& diagnostics, FILE* file, const char* path,
                                  unsigned int& outVersion )
{
    outVersion = 0;

    bool sawVersion = false;
    char line[512];
    int lineNumber = 0;

    while ( fgets( line, sizeof( line ), file ) )
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
            continue;
        }

        *eq = '\0';
        char* key = TrimInPlace( trimmedLine );

        if ( strcmp( key, "format_version" ) != 0 )
        {
            continue;
        }

        if ( sawVersion )
        {
            return diagnostics.Failure( "Core/EngineConfig", "Duplicate engine config format_version at %s:%d.", path,
                                        lineNumber );
        }

        char* value = TrimInPlace( eq + 1 );
        char* hash = strchr( value, '#' );

        if ( hash )
        {
            *hash = '\0';
            value = TrimInPlace( value );
        }

        errno = 0;
        char* end = nullptr;
        const unsigned long parsed = strtoul( value, &end, 10 );

        if ( end == value || *TrimInPlace( end ) != '\0' || errno == ERANGE || parsed > UINT_MAX )
        {
            return diagnostics.Failure( "Core/EngineConfig", "Invalid engine config format_version at %s:%d.", path,
                                        lineNumber );
        }

        outVersion = static_cast<unsigned int>( parsed );
        sawVersion = true;
    }

    const SbResult readResult = RequireCompleteConfigRead( diagnostics, file, path, "format-version" );
    if ( !readResult.Ok() )
    {
        return readResult;
    }

    if ( outVersion > ENGINE_CONFIG_FORMAT_VERSION )
    {
        return diagnostics.Failure( "Core/EngineConfig",
                                    "Engine config format version %u is newer than current version %u: %s.", outVersion,
                                    ENGINE_CONFIG_FORMAT_VERSION, path );
    }

    // Versions 0-6 share the key/value grammar. Versioned execution rows are
    // optional, so absence selects built-in defaults; the cold migration tool
    // materializes the retained v2 row and removes retired ownerless rows.
    return SbResult::Success();
}
} // anonymous namespace

#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
void SkullbonezCore::Core::SetEngineConfigSettingsReadFailureAfterLineForTest( int completedLines ) noexcept
{
    s_settingsReadFailureAfterLineForTest = completedLines;
}
#endif


SbResult EngineConfig::Load( SbDiagnosticStore& diagnostics, const char* path )
{
    // Concept: engine.cfg is an optional developer/runtime defaults file. Unknown or
    // malformed lines are skipped with a warning so older configs do not block
    // startup after a setting is removed.
    FILE* rawFile = nullptr;
    bool missing = false;
    const SbResult openResult = OpenOptionalConfigFile( diagnostics, path, rawFile, missing );

    if ( !openResult.Ok() )
    {
        return openResult;
    }

    if ( missing )
    {
        return SbResult::Success();
    }

    FileHandle file( rawFile, &fclose );
    unsigned int formatVersion = 0;
    const SbResult versionResult = ReadConfigFormatVersion( diagnostics, file.get(), path, formatVersion );
    if ( !versionResult.Ok() )
    {
        return versionResult;
    }

    clearerr( file.get() );
    errno = 0;
    if ( fseek( file.get(), 0, SEEK_SET ) != 0 )
    {
        return diagnostics.Failure( "Core/EngineConfig", "Unable to restart engine config settings read: %s (error %d).",
                                    path, errno );
    }

    EngineConfig candidate = *this;

    char line[512];
    int lineNumber = 0;
    bool injectedReadFailure = false;

    for ( ;; )
    {
#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
        if ( s_settingsReadFailureAfterLineForTest >= 0 && lineNumber >= s_settingsReadFailureAfterLineForTest )
        {
            // Test probe: leave the public object untouched after at least one
            // candidate row, exactly as a real ferror-terminated read must.
            errno = EIO;
            injectedReadFailure = true;
            break;
        }
#endif
        if ( !fgets( line, sizeof( line ), file.get() ) )
        {
            break;
        }

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

        if ( strcmp( key, "format_version" ) == 0 )
        {
            continue;
        }

        const ConfigSetting* setting = FindConfigSetting( key );

        if ( !setting )
        {
            WarnConfigLine( path, lineNumber, key, value, "unknown setting" );
            continue;
        }

        setting->apply( candidate, value, *setting, path, lineNumber );
    }

    const SbResult readResult =
        RequireCompleteConfigRead( diagnostics, file.get(), path, "settings", injectedReadFailure );
    if ( !readResult.Ok() )
    {
        // Invariant: an I/O failure cannot publish the valid prefix. Keep the
        // caller's entire prior config until both read passes finish cleanly.
        return readResult;
    }

    *this = candidate;
    return SbResult::Success();
}


void EngineConfig::Dump( FILE* out ) const
{
    if ( !out )
    {
        return;
    }

    fprintf( out, "[config]\n" );
    VisitConfigSettingsInOrder( [this, out]( const ConfigSetting& setting )
                                {
                                    setting.dump( *this, out, setting );

                                    return true;
                                } );
}
