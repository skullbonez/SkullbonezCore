/*
File: SkullbonezSource/Runtime/RunInternal.h
Purpose:
  Shares private run-loop data structures between split runtime implementation files.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.
  Scrubber: Replay timeline control that lets the operator inspect stored
  solver or presentation samples.
  Hot zone: Screen-space rectangle that wakes hidden UI controls when hovered.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "Run.h"
#include "../Rendering/Helper.h"
#include "../Physics/BoundingSphere.h"
#include "../GameObjects/GameModel.h"
#include "../Core/Profiler.h"
#include "../Rendering/IRenderBackend.h"
#include "../World/TerrainSupportClassifier.h"
#include "../UI/UIDraw.h"

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
using SkullbonezCore::UI::UIRenderParam;

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
inline constexpr double PERF_TEST_PASS_SECONDS = 2.0;      // Duration of each perf pass before averaging/reporting.
inline constexpr float WATER_HEIGHT_CONTROL_SPEED = 20.0f; // UI water-height slider speed in world meters per second.
inline constexpr float NO_WATER_TERRAIN_CLEARANCE = 100.0f;
inline constexpr float CAMERA_MOUSE_REFERENCE_DT = 1.0f / 60.0f;
inline constexpr long CAMERA_MOUSE_MAX_DELTA_PIXELS = 96;
inline constexpr long CAMERA_MOUSE_SPIKE_DELTA_PIXELS = 320;
inline constexpr double REPLAY_PREDICTION_REFRESH_SECONDS = 0.35;
inline constexpr double REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_MAX = 140.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_ANGULAR_MAX = 5.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_EXTRA = 36.0f;
#ifdef _DEBUG
inline constexpr const char* LAUNCHER_REPRO_SNAPSHOT_PATH = "Debug/launcher_repro_snapshots.txt";
inline constexpr double LAUNCHER_REPRO_MESSAGE_SECONDS = 3.0;
#endif

inline bool ReplayModelIsRagdollPart( const GameModel& model )
{
    return model.GetRuntimeCollectionKind() == SkullbonezCore::GameObjects::GameModelCollectionKind::SimpleRagdoll;
}

inline bool ReplayModelIsRagdollTorso( const GameModel& model )
{
    return ReplayModelIsRagdollPart( model ) && model.GetRuntimeCollectionPartIndex() == 0;
}

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

inline void
ApplyCinematicSceneOverrides( CinematicRenderConfig& target, uint64_t mask, const CinematicRenderConfig& source )
{
    // Scene files do not have to specify every cinematic value. The mask says
    // which fields were actually present, so loading a scene only replaces those
    // values and keeps all other defaults from engine.cfg.
#define APPLY_CINEMATIC_OVERRIDE( bit, field )                                                                         \
    if ( ( mask & ( bit ) ) != 0 )                                                                                     \
    {                                                                                                                  \
        target.field = source.field;                                                                                   \
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
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
