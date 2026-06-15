/*
File: SkullbonezSource/SkullbonezRunInternal.h
Purpose:
  Shares private run-loop data structures between split runtime implementation files.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "SkullbonezRun.h"
#include "SkullbonezHelper.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezTerrainSupportClassifier.h"
#include "UI/UIDraw.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <psapi.h>
#include <string>
#include <time.h>
#include <vector>

using SkullbonezCore::Basics::CinematicRenderConfig;
using SkullbonezCore::Environment::Camera;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::WorldEnvironment;
using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Geometry::SkyBox;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::Hardware::Input;
using SkullbonezCore::Hardware::InputState;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PhysicsPipelineStage;
using SkullbonezCore::Physics::PhysicsPipelineStageName;
using SkullbonezCore::Rendering::DestroyGfxBackend;
using SkullbonezCore::Rendering::Gfx;
using SkullbonezCore::Rendering::IMesh;
using SkullbonezCore::Rendering::IRenderBackend;
using SkullbonezCore::Rendering::IsGfxReady;
using SkullbonezCore::Text::Text2d;
using SkullbonezCore::Textures::TextureCollection;
using SkullbonezCore::UI::InGameUICommands;
using SkullbonezCore::UI::InGameUIFrameData;
using SkullbonezCore::UI::InGameUIInputResult;
using SkullbonezCore::UI::InGameUITab;
using SkullbonezCore::UI::UICinematicFeature;
using SkullbonezCore::UI::UICinematicParam;

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
inline constexpr double PERF_TEST_PASS_SECONDS = 2.0;
inline constexpr float WATER_HEIGHT_CONTROL_SPEED = 20.0f;
inline constexpr float NO_WATER_TERRAIN_CLEARANCE = 100.0f;
inline constexpr float CAMERA_MOUSE_REFERENCE_DT = 1.0f / 60.0f;
inline constexpr long CAMERA_MOUSE_MAX_DELTA_PIXELS = 96;
inline constexpr long CAMERA_MOUSE_SPIKE_DELTA_PIXELS = 320;
inline constexpr float CAMERA_PROJECTILE_SPEED = 12000.0f;
inline constexpr float CAMERA_PROJECTILE_SHIFT_MULTIPLIER = 3.0f;
inline constexpr float CAMERA_PROJECTILE_RADIUS = 0.25f;
inline constexpr float CAMERA_PROJECTILE_MASS = 0.25f;
inline constexpr float CAMERA_PROJECTILE_RESTITUTION = 0.2f;
inline constexpr float CAMERA_PROJECTILE_MOMENT = 0.025f;
inline constexpr float CAMERA_PROJECTILE_SPAWN_CLEARANCE = 12.0f;
inline constexpr float CAMERA_PROJECTILE_PARK_BASE = -5000.0f;
inline constexpr float CAMERA_PROJECTILE_SILVER_R = 1.0f;
inline constexpr float CAMERA_PROJECTILE_SILVER_G = 1.0f;
inline constexpr float CAMERA_PROJECTILE_SILVER_B = 1.0f;
inline constexpr int FIXED_STEP_TIME_SCALE_MAX_TICKS_PER_FRAME = 32;

#ifdef _DEBUG
inline constexpr const char* NUDGE_REPRO_SNAPSHOT_PATH = "Debug/nudge_repro_snapshots.txt";
inline constexpr double NUDGE_REPRO_MESSAGE_SECONDS = 3.0;

inline std::string JsonEscape( const char* value )
{
    std::string escaped;
    if ( !value )
    {
        return escaped;
    }

    for ( const char* p = value; *p != '\0'; ++p )
    {
        switch ( *p )
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += *p;
            break;
        }
    }
    return escaped;
}
#endif

inline void DrawUITestPattern( int screenW, int screenH )
{
    const UI::UIDrawContext draw( screenW, screenH );
    draw.Rect( 0.0f, 0.0f, static_cast<float>( screenW ), static_cast<float>( screenH ), 0.20f, 0.31f, 0.36f, 1.0f );

    constexpr float tile = 88.0f;
    for ( float y = 0.0f; y < static_cast<float>( screenH ); y += tile )
    {
        for ( float x = 0.0f; x < static_cast<float>( screenW ); x += tile )
        {
            const int ix = static_cast<int>( x / tile );
            const int iy = static_cast<int>( y / tile );
            const bool alternate = ( ( ix + iy ) & 1 ) != 0;
            if ( alternate )
            {
                draw.Rect( x, y, tile, tile, 0.10f, 0.78f, 0.96f, 0.96f );
            }
            else
            {
                draw.Rect( x, y, tile, tile, 1.0f, 0.72f, 0.18f, 0.94f );
            }
            draw.Rect( x + 12.0f, y + 12.0f, tile - 24.0f, 5.0f, 0.96f, 0.98f, 1.0f, 0.74f );
            draw.Rect( x + tile - 18.0f, y + 18.0f, 5.0f, tile - 32.0f, 0.12f, 0.20f, 0.24f, 0.54f );
        }
    }

    draw.Rect( 44.0f, 46.0f, 780.0f, 560.0f, 1.0f, 1.0f, 1.0f, 0.18f );
    draw.Rect( 76.0f, 116.0f, 720.0f, 8.0f, 0.98f, 0.12f, 0.46f, 0.82f );
    draw.Rect( 76.0f, 300.0f, 720.0f, 8.0f, 0.30f, 1.0f, 0.56f, 0.78f );
    draw.Rect( 76.0f, 484.0f, 720.0f, 8.0f, 0.38f, 0.54f, 1.0f, 0.82f );
    Text::Text2d::FlushQuads();
}

inline bool SceneDirectiveMatches( const std::string& line, const char* key )
{
    // Scene-default writes work on line-oriented directives, not a full parsed
    // syntax tree.  Match only a complete directive key so editing "text" never
    // accidentally catches "text_only", and so tabs remain valid separators for
    // hand-authored scene files.
    const size_t keyLen = strlen( key );
    if ( line.compare( 0, keyLen, key ) != 0 )
    {
        return false;
    }
    return line.size() == keyLen || line[keyLen] == ' ' || line[keyLen] == '\t';
}

inline bool IsSceneBodyDirective( const std::string& line )
{
    return SceneDirectiveMatches( line, "camera" ) ||
           SceneDirectiveMatches( line, "ball" ) ||
           SceneDirectiveMatches( line, "box" ) ||
           SceneDirectiveMatches( line, "floating_box" ) ||
           SceneDirectiveMatches( line, "ball_state" );
}

inline size_t SceneDefaultInsertIndex( const std::vector<std::string>& lines )
{
    for ( size_t i = 0; i < lines.size(); ++i )
    {
        if ( IsSceneBodyDirective( lines[i] ) )
        {
            return i;
        }
    }
    return lines.size();
}

inline void SetSceneDirective( std::vector<std::string>& lines, const char* key, const std::string& value, bool includeDirective )
{
    // Save Defaults is deliberately surgical: preserve body/camera ordering and
    // comments, replace the first matching singleton directive, and delete any
    // duplicate stale copies.  When includeDirective is false this same helper is
    // used as a migration broom for directives that no longer exist.
    bool replaced = false;
    for ( size_t i = 0; i < lines.size(); )
    {
        if ( SceneDirectiveMatches( lines[i], key ) )
        {
            if ( includeDirective && !replaced )
            {
                lines[i] = value;
                replaced = true;
                ++i;
            }
            else
            {
                lines.erase( lines.begin() + static_cast<std::ptrdiff_t>( i ) );
            }
            continue;
        }
        ++i;
    }

    if ( includeDirective && !replaced )
    {
        lines.insert( lines.begin() + static_cast<std::ptrdiff_t>( SceneDefaultInsertIndex( lines ) ), value );
    }
}

inline const char* OnOff( bool value )
{
    return value ? "on" : "off";
}

inline const char* WaterReflectionDirectiveValue( bool noReflect, bool rtReflect )
{
    if ( noReflect )
    {
        return "none";
    }
    return rtReflect ? "dxr" : "fbo";
}

inline void ApplyCinematicSceneOverrides( CinematicRenderConfig& target, uint64_t mask, const CinematicRenderConfig& source )
{
    // Scene files do not have to specify every cinematic value. The mask says
    // which fields were actually present, so loading a scene only replaces those
    // values and keeps all other defaults from engine.cfg.
#define APPLY_CINEMATIC_OVERRIDE( bit, field ) \
    if ( ( mask & ( bit ) ) != 0 )             \
    {                                          \
        target.field = source.field;           \
    }

    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_RENDERING, enabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_ATMOSPHERE, skyAtmosphereEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUDS, cloudsEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_GOD_RAYS, godRaysEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_VOLUMETRIC_LIGHTING, volumetricLightingEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM, bloomEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG, fogEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_RELIEF_ENABLED, terrainReliefEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_EXPOSURE, exposure )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_GAMMA, gamma )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_SCREEN_X, sunScreenX )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_SCREEN_Y, sunScreenY )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_COLOR_R, sunColorR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_COLOR_G, sunColorG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_COLOR_B, sunColorB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_INTENSITY, sunIntensity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_HORIZON_R, skyHorizonR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_HORIZON_G, skyHorizonG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_HORIZON_B, skyHorizonB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_ZENITH_R, skyZenithR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_ZENITH_G, skyZenithG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_ZENITH_B, skyZenithB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SKY_GLOW_STRENGTH, skyGlowStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUD_COVERAGE, cloudCoverage )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUD_SOFTNESS, cloudSoftness )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUD_SCALE, cloudScale )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_CLOUD_INTENSITY, cloudIntensity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_SHAFT_STRENGTH, sunShaftStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SUN_SHAFT_FALLOFF, sunShaftFalloff )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_VOLUMETRIC_STRENGTH, volumetricStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_VOLUMETRIC_DENSITY, volumetricDensity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_VOLUMETRIC_DECAY, volumetricDecay )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM_THRESHOLD, bloomThreshold )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM_KNEE, bloomKnee )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM_STRENGTH, bloomStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BLOOM_RADIUS, bloomRadius )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_RELIEF, terrainRelief )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_DEPTH, basinDepth )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_RIM_LIFT, basinRimLift )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOWS, shadowsEnabled )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_MAP_SIZE, shadowMapSize )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_PCF_RADIUS, shadowPcfRadius )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_STRENGTH, shadowStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_SOFTNESS, shadowSoftness )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_DEPTH_BIAS, shadowDepthBias )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_SLOPE_BIAS, shadowSlopeBias )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_SHADOW_MAX_DISTANCE, shadowMaxDistance )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_COLOR_R, fogColorR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_COLOR_G, fogColorG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_COLOR_B, fogColorB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_START, fogStart )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_END, fogEnd )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_DENSITY, fogDensity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_FOG_MAX_OPACITY, fogMaxOpacity )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_MODES, skyMode )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_MODES, terrainMode )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_MODES, objectStyle )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_MODES, waterMode )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_GRADE, styleSaturation )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_GRADE, styleContrast )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_STYLE_GRADE, styleVignette )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_TINT, terrainTintR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_TINT, terrainTintG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_TINT, terrainTintB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_ACCENT, terrainAccentR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_ACCENT, terrainAccentG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_ACCENT, terrainAccentB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_GRID, terrainGridScale )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_TERRAIN_GRID, terrainGridStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_TINT, waterTintR )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_TINT, waterTintG )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_TINT, waterTintB )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_PROFILE, waterAlpha )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_PROFILE, waterReflectionStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_WATER_PROFILE, waterGlintStrength )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinCenterX )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinCenterZ )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinRadiusX )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinRadiusZ )
    APPLY_CINEMATIC_OVERRIDE( SCENE_CINE_BASIN_MASK, basinFeather )

#undef APPLY_CINEMATIC_OVERRIDE
}

inline const char* FileNameFromPath( const char* path )
{
    if ( !path )
    {
        return "";
    }

    const char* slash = strrchr( path, '/' );
    const char* backslash = strrchr( path, '\\' );
    const char* separator = slash;
    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}

inline std::string NormalizeScenePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    return normalized;
}

// Captures the part of a live run that belongs to the operator's current scene
// configuration rather than the simulation instance.  A normal Reset button
// should rebuild bodies, timers, contact caches, screenshots, and diagnostics;
// it should not silently undo debug overlays, physics debug settings, time scale,
// fixed-step mode, world sliders, or generated-count overrides.  Scene changes
// and Reset To Defaults skip this snapshot so the file/config becomes authority.
struct SceneRuntimeResetSnapshot
{
    RunRuntimeSettings runtimeSettings; // Live runtime toggles changed while operating the current scene
    RunDebugState debug;                // Debug overlays/visualizers, including the C-key physics debug mode and associated alpha/linger knobs
    bool isScenePhysics = true;         // Live scene simulation toggle; reset should rebuild the run, not silently re-enable physics
    bool isSceneText = true;            // Live text/HUD toggle from the scene controls
    bool isFixedStep = false;           // Live stepping mode; resetting the simulation should not change how it advances
    bool isExitOnComplete = false;      // Interactive reset preserves the user's automation/hold choice
    bool isInteractiveRun = false;      // Once a user owns the scene, a reset should not go back to CLI auto-quit behavior
    int targetFrameCount = -1;          // Live frame-count control from the UI
    float timeScale = 1.0f;             // Live time-scale control from the UI/scene controls
    float worldGravity = 0.0f;          // Live world/environment sliders
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
    bool hasCinematicRenderingOverride = false;
    bool isCinematicRenderingEnabled = false;
    bool hasCinematicExposure = false;
    float cinematicExposure = 1.0f;
    bool hasCinematicGamma = false;
    float cinematicGamma = 2.2f;
    uint64_t cinematicOverrideMask = 0;
    uint64_t uiCinematicOverrideMask = 0;
    CinematicRenderConfig cinematicRender;
    float uiTimeScaleOverride = 0.0f; // UI overrides feed object setup during reload, so they must survive before the scene rebuilds
    int uiModelCountOverride = -1;
    int uiSolverBallCountOverride = -1;
    int uiSolverBoxCountOverride = -1;
    int trackBallIndex = -1; // Scene-tab camera tracking controls
    float trackHeight = 300.0f;
    float autoCycleInterval = -1.0f;
    float autoCycleAccum = 0.0f;
    int autoCycleShotsTaken = 0;
};
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
