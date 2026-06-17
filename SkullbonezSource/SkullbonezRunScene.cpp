/*
File: SkullbonezSource/SkullbonezRunScene.cpp
Purpose:
  Loads, resets, and advances authored and generated scenes.

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
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRunInternal.h"
#include "SkullbonezWorkerPool.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
int NextSceneRand( unsigned int& state )
{
    // Match the MSVC CRT sequence so seeded scene layouts stay stable while avoiding
    // global RNG state.
    state = state * 214013u + 2531011u;
    return static_cast<int>( ( state >> 16 ) & 0x7fffu );
}

void ApplySceneWorkerThreadSetting( int requestedWorkerThreads )
{
    const int clampedWorkerThreads = std::clamp( requestedWorkerThreads, -1, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
    SkullbonezCore::Threading::WorkerPool& workerPool = SkullbonezCore::Threading::WorkerPool::Instance();
    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    Cfg().workerThreads = clampedWorkerThreads;
    if ( workerPool.GetThreadCount() != resolvedWorkerThreads )
    {
        workerPool.Initialise( clampedWorkerThreads );
    }
}

bool SceneMaterialTargetMatches( const SceneObjectMaterialOverride& material, const GameModel& model )
{
    if ( strcmp( material.target, "all" ) == 0 )
    {
        return true;
    }
    if ( strcmp( material.target, "balls" ) == 0 )
    {
        return !model.IsBox();
    }
    if ( strcmp( material.target, "boxes" ) == 0 )
    {
        return model.IsBox();
    }
    if ( strncmp( material.target, "prefix:", 7 ) == 0 )
    {
        return strncmp( model.GetName(), material.target + 7, strlen( material.target + 7 ) ) == 0;
    }
    return strcmp( material.target, model.GetName() ) == 0;
}

bool IsCineScenePath( const std::string& path )
{
    const char* name = FileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 ||
           strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr ||
           strstr( name, "cine_" ) == name;
}

void SetTouchedCinematicSceneDirectives( std::vector<std::string>& lines, uint64_t touchedMask, const CinematicRenderConfig& c )
{
    // Concept: save only values the UI actually touched.
    //
    // Scene files can include reusable style files plus a few local overrides.
    // The touched mask prevents "Save Defaults" from expanding every engine.cfg
    // or style default into the scene file. That keeps authored intent readable:
    // only the controls changed in the UI are written back as scene directives.
    char buf[256] = {};
    const auto writeBool = [&]( uint64_t bit, const char* key, bool value )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            snprintf( buf, sizeof( buf ), "%s %s", key, OnOff( value ) );
            SetSceneDirective( lines, key, buf, true );
        }
    };
    const auto writeFloat = [&]( uint64_t bit, const char* key, float value, const char* format )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            snprintf( buf, sizeof( buf ), format, key, value );
            SetSceneDirective( lines, key, buf, true );
        }
    };
    const auto writeInt = [&]( uint64_t bit, const char* key, int value )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            snprintf( buf, sizeof( buf ), "%s %d", key, value );
            SetSceneDirective( lines, key, buf, true );
        }
    };

    writeBool( SCENE_CINE_RENDERING, "cinematic_rendering", c.enabled );
    writeBool( SCENE_CINE_SKY_ATMOSPHERE, "cinematic_sky_atmosphere", c.skyAtmosphereEnabled );
    writeBool( SCENE_CINE_CLOUDS, "cinematic_clouds", c.cloudsEnabled );
    writeBool( SCENE_CINE_GOD_RAYS, "cinematic_god_rays", c.godRaysEnabled );
    writeBool( SCENE_CINE_VOLUMETRIC_LIGHTING, "cinematic_volumetric_lighting", c.volumetricLightingEnabled );
    writeBool( SCENE_CINE_BLOOM, "cinematic_bloom", c.bloomEnabled );
    writeBool( SCENE_CINE_FOG, "cinematic_fog", c.fogEnabled );
    writeBool( SCENE_CINE_TERRAIN_RELIEF_ENABLED, "cinematic_terrain_relief_enabled", c.terrainReliefEnabled );

    writeFloat( SCENE_CINE_EXPOSURE, "cinematic_exposure", c.exposure, "%s %.2f" );
    writeFloat( SCENE_CINE_GAMMA, "cinematic_gamma", c.gamma, "%s %.2f" );
    writeFloat( SCENE_CINE_SUN_SCREEN_X, "cinematic_sun_screen_x", c.sunScreenX, "%s %.3f" );
    writeFloat( SCENE_CINE_SUN_SCREEN_Y, "cinematic_sun_screen_y", c.sunScreenY, "%s %.3f" );
    writeFloat( SCENE_CINE_SUN_COLOR_R, "cinematic_sun_color_r", c.sunColorR, "%s %.2f" );
    writeFloat( SCENE_CINE_SUN_COLOR_G, "cinematic_sun_color_g", c.sunColorG, "%s %.2f" );
    writeFloat( SCENE_CINE_SUN_COLOR_B, "cinematic_sun_color_b", c.sunColorB, "%s %.2f" );
    writeFloat( SCENE_CINE_SUN_INTENSITY, "cinematic_sun_intensity", c.sunIntensity, "%s %.2f" );
    writeFloat( SCENE_CINE_SKY_HORIZON_R, "cinematic_sky_horizon_r", c.skyHorizonR, "%s %.2f" );
    writeFloat( SCENE_CINE_SKY_HORIZON_G, "cinematic_sky_horizon_g", c.skyHorizonG, "%s %.2f" );
    writeFloat( SCENE_CINE_SKY_HORIZON_B, "cinematic_sky_horizon_b", c.skyHorizonB, "%s %.2f" );
    writeFloat( SCENE_CINE_SKY_ZENITH_R, "cinematic_sky_zenith_r", c.skyZenithR, "%s %.2f" );
    writeFloat( SCENE_CINE_SKY_ZENITH_G, "cinematic_sky_zenith_g", c.skyZenithG, "%s %.2f" );
    writeFloat( SCENE_CINE_SKY_ZENITH_B, "cinematic_sky_zenith_b", c.skyZenithB, "%s %.2f" );
    writeFloat( SCENE_CINE_SKY_GLOW_STRENGTH, "cinematic_sky_glow_strength", c.skyGlowStrength, "%s %.2f" );
    writeFloat( SCENE_CINE_CLOUD_COVERAGE, "cinematic_cloud_coverage", c.cloudCoverage, "%s %.2f" );
    writeFloat( SCENE_CINE_CLOUD_SOFTNESS, "cinematic_cloud_softness", c.cloudSoftness, "%s %.2f" );
    writeFloat( SCENE_CINE_CLOUD_SCALE, "cinematic_cloud_scale", c.cloudScale, "%s %.2f" );
    writeFloat( SCENE_CINE_CLOUD_INTENSITY, "cinematic_cloud_intensity", c.cloudIntensity, "%s %.2f" );
    writeFloat( SCENE_CINE_SUN_SHAFT_STRENGTH, "cinematic_sun_shaft_strength", c.sunShaftStrength, "%s %.2f" );
    writeFloat( SCENE_CINE_SUN_SHAFT_FALLOFF, "cinematic_sun_shaft_falloff", c.sunShaftFalloff, "%s %.2f" );
    writeFloat( SCENE_CINE_VOLUMETRIC_STRENGTH, "cinematic_volumetric_strength", c.volumetricStrength, "%s %.2f" );
    writeFloat( SCENE_CINE_VOLUMETRIC_DENSITY, "cinematic_volumetric_density", c.volumetricDensity, "%s %.2f" );
    writeFloat( SCENE_CINE_VOLUMETRIC_DECAY, "cinematic_volumetric_decay", c.volumetricDecay, "%s %.3f" );
    writeFloat( SCENE_CINE_BLOOM_THRESHOLD, "cinematic_bloom_threshold", c.bloomThreshold, "%s %.2f" );
    writeFloat( SCENE_CINE_BLOOM_KNEE, "cinematic_bloom_knee", c.bloomKnee, "%s %.2f" );
    writeFloat( SCENE_CINE_BLOOM_STRENGTH, "cinematic_bloom_strength", c.bloomStrength, "%s %.2f" );
    writeFloat( SCENE_CINE_BLOOM_RADIUS, "cinematic_bloom_radius", c.bloomRadius, "%s %.2f" );
    writeFloat( SCENE_CINE_TERRAIN_RELIEF, "cinematic_terrain_relief", c.terrainRelief, "%s %.2f" );
    writeFloat( SCENE_CINE_BASIN_DEPTH, "cinematic_basin_depth", c.basinDepth, "%s %.2f" );
    writeFloat( SCENE_CINE_BASIN_RIM_LIFT, "cinematic_basin_rim_lift", c.basinRimLift, "%s %.2f" );
    writeBool( SCENE_CINE_SHADOWS, "cinematic_shadows", c.shadowsEnabled );
    writeInt( SCENE_CINE_SHADOW_MAP_SIZE, "cinematic_shadow_map_size", c.shadowMapSize );
    writeInt( SCENE_CINE_SHADOW_PCF_RADIUS, "cinematic_shadow_pcf_radius", c.shadowPcfRadius );
    writeFloat( SCENE_CINE_SHADOW_STRENGTH, "cinematic_shadow_strength", c.shadowStrength, "%s %.3f" );
    writeFloat( SCENE_CINE_SHADOW_SOFTNESS, "cinematic_shadow_softness", c.shadowSoftness, "%s %.2f" );
    writeFloat( SCENE_CINE_SHADOW_DEPTH_BIAS, "cinematic_shadow_depth_bias", c.shadowDepthBias, "%s %.5f" );
    writeFloat( SCENE_CINE_SHADOW_SLOPE_BIAS, "cinematic_shadow_slope_bias", c.shadowSlopeBias, "%s %.5f" );
    writeFloat( SCENE_CINE_SHADOW_MAX_DISTANCE, "cinematic_shadow_max_distance", c.shadowMaxDistance, "%s %.2f" );
    writeFloat( SCENE_CINE_FOG_COLOR_R, "cinematic_fog_color_r", c.fogColorR, "%s %.2f" );
    writeFloat( SCENE_CINE_FOG_COLOR_G, "cinematic_fog_color_g", c.fogColorG, "%s %.2f" );
    writeFloat( SCENE_CINE_FOG_COLOR_B, "cinematic_fog_color_b", c.fogColorB, "%s %.2f" );
    writeFloat( SCENE_CINE_FOG_START, "cinematic_fog_start", c.fogStart, "%s %.2f" );
    writeFloat( SCENE_CINE_FOG_END, "cinematic_fog_end", c.fogEnd, "%s %.2f" );
    writeFloat( SCENE_CINE_FOG_DENSITY, "cinematic_fog_density", c.fogDensity, "%s %.5f" );
    writeFloat( SCENE_CINE_FOG_MAX_OPACITY, "cinematic_fog_max_opacity", c.fogMaxOpacity, "%s %.2f" );

    if ( ( touchedMask & SCENE_CINE_STYLE_MODES ) != 0 )
    {
        snprintf( buf, sizeof( buf ), "cinematic_style_modes %d %d %d %d", c.skyMode, c.terrainMode, c.objectStyle, c.waterMode );
        SetSceneDirective( lines, "cinematic_style_modes", buf, true );
    }
    if ( ( touchedMask & SCENE_CINE_STYLE_GRADE ) != 0 )
    {
        snprintf( buf, sizeof( buf ), "cinematic_style_grade %.2f %.2f %.2f", c.styleSaturation, c.styleContrast, c.styleVignette );
        SetSceneDirective( lines, "cinematic_style_grade", buf, true );
    }
    if ( ( touchedMask & SCENE_CINE_TERRAIN_TINT ) != 0 )
    {
        snprintf( buf, sizeof( buf ), "cinematic_terrain_tint %.2f %.2f %.2f", c.terrainTintR, c.terrainTintG, c.terrainTintB );
        SetSceneDirective( lines, "cinematic_terrain_tint", buf, true );
    }
    if ( ( touchedMask & SCENE_CINE_TERRAIN_ACCENT ) != 0 )
    {
        snprintf( buf, sizeof( buf ), "cinematic_terrain_accent %.2f %.2f %.2f", c.terrainAccentR, c.terrainAccentG, c.terrainAccentB );
        SetSceneDirective( lines, "cinematic_terrain_accent", buf, true );
    }
    if ( ( touchedMask & SCENE_CINE_TERRAIN_GRID ) != 0 )
    {
        snprintf( buf, sizeof( buf ), "cinematic_terrain_grid %.2f %.2f", c.terrainGridScale, c.terrainGridStrength );
        SetSceneDirective( lines, "cinematic_terrain_grid", buf, true );
    }
    if ( ( touchedMask & SCENE_CINE_WATER_TINT ) != 0 )
    {
        snprintf( buf, sizeof( buf ), "cinematic_water_tint %.2f %.2f %.2f", c.waterTintR, c.waterTintG, c.waterTintB );
        SetSceneDirective( lines, "cinematic_water_tint", buf, true );
    }
    if ( ( touchedMask & SCENE_CINE_WATER_PROFILE ) != 0 )
    {
        snprintf( buf, sizeof( buf ), "cinematic_water_profile %.2f %.2f %.2f", c.waterAlpha, c.waterReflectionStrength, c.waterGlintStrength );
        SetSceneDirective( lines, "cinematic_water_profile", buf, true );
    }
    if ( ( touchedMask & SCENE_CINE_BASIN_MASK ) != 0 )
    {
        snprintf( buf, sizeof( buf ), "cinematic_basin_mask %.2f %.2f %.2f %.2f %.2f", c.basinCenterX, c.basinCenterZ, c.basinRadiusX, c.basinRadiusZ, c.basinFeather );
        SetSceneDirective( lines, "cinematic_basin_mask", buf, true );
    }
}

bool ConfigLineMatchesKey( const std::string& line, const char* key )
{
    size_t start = line.find_first_not_of( " \t" );
    if ( start == std::string::npos || line[start] == '#' )
    {
        return false;
    }

    const size_t keyLen = strlen( key );
    if ( line.compare( start, keyLen, key ) != 0 )
    {
        return false;
    }

    size_t pos = start + keyLen;
    while ( pos < line.size() && ( line[pos] == ' ' || line[pos] == '\t' ) )
    {
        ++pos;
    }
    return pos < line.size() && line[pos] == '=';
}

bool ReplaceConfigLine( std::vector<std::string>& lines, const char* key, const std::string& value )
{
    const std::string lineText = std::string( key ) + " = " + value;
    bool replaced = false;
    for ( size_t i = 0; i < lines.size(); )
    {
        if ( ConfigLineMatchesKey( lines[i], key ) )
        {
            if ( !replaced )
            {
                lines[i] = lineText;
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
    return replaced;
}

size_t OrdinaryConfigInsertIndex( const std::vector<std::string>& lines )
{
    for ( size_t i = 0; i < lines.size(); ++i )
    {
        if ( lines[i].find( "Ordinary rendering" ) != std::string::npos )
        {
            size_t sectionBody = i + 1;
            while ( sectionBody < lines.size() &&
                    ( lines[sectionBody].empty() ||
                      lines[sectionBody].find( "# ---------------------------------------------------------------------------" ) != std::string::npos ) )
            {
                ++sectionBody;
            }

            for ( size_t j = sectionBody; j < lines.size(); ++j )
            {
                if ( lines[j].find( "# ---------------------------------------------------------------------------" ) != std::string::npos )
                {
                    return j;
                }
            }
            return lines.size();
        }
    }

    for ( size_t i = 0; i < lines.size(); ++i )
    {
        if ( lines[i].find( "Cinematic rendering" ) != std::string::npos )
        {
            return i > 1 ? i - 1 : i;
        }
    }
    return lines.size();
}

void AppendMissingOrdinaryConfigLines( std::vector<std::string>& lines, std::vector<std::string>& missing )
{
    if ( missing.empty() )
    {
        return;
    }

    std::vector<std::string> insertLines;
    const bool hasOrdinarySection = std::any_of( lines.begin(), lines.end(), []( const std::string& line )
                                                 { return line.find( "Ordinary rendering" ) != std::string::npos; } );
    if ( !hasOrdinarySection )
    {
        insertLines.push_back( "# ---------------------------------------------------------------------------" );
        insertLines.push_back( "# Ordinary rendering" );
        insertLines.push_back( "# ---------------------------------------------------------------------------" );
    }
    insertLines.insert( insertLines.end(), missing.begin(), missing.end() );
    insertLines.push_back( "" );

    const size_t insertIndex = OrdinaryConfigInsertIndex( lines );
    lines.insert( lines.begin() + static_cast<std::ptrdiff_t>( insertIndex ), insertLines.begin(), insertLines.end() );
}
} // namespace

void SkullbonezRun::SetUpGameModels( int count )
{
    SceneState().modelCount = count;
    SceneState().solverBallCount = 0;
    SceneState().solverBoxCount = 0;

    const SkullbonezConfig& cfg = Cfg();

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( NextSceneRand( SceneState().rngState ) % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( NextSceneRand( SceneState().rngState ) % range );
        return ( NextSceneRand( SceneState().rngState ) % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = [&]() -> float
    { return ( NextSceneRand( SceneState().rngState ) % 2 == 0 ) ? 1.0f : -1.0f; };

    for ( int x = 0; x < SceneState().modelCount; ++x )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( NextSceneRand( SceneState().rngState ) % cfg.ballRestitutionRange ) / 10.0f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        bool makeBox = false;
        if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
        {
            makeBox = true;
        }
        else if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
        {
            makeBox = false;
        }
        else
        {
            // ~30% of generated objects are boxes, giving the default demo a
            // mixed collision workload without requiring explicit scene bodies.
            makeBox = ( NextSceneRand( SceneState().rngState ) % 10 ) < 3;
        }

        if ( makeBox )
        {
            float halfExtent = ( 1.0f + static_cast<float>( NextSceneRand( SceneState().rngState ) % 3 ) ) * 0.6f;
            float hx = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( SceneState().rngState ) % 4 ) * 0.2f );
            float hy = halfExtent;
            float hz = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( SceneState().rngState ) % 4 ) * 0.2f );

            // Box inertia: I = m/3 * (hy^2 + hz^2) etc.
            float hx2 = hx * hx;
            float hy2 = hy * hy;
            float hz2 = hz * hz;
            float m3 = mass / 3.0f;
            Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

            GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), inertia, mass );
            gameModel.SetCoefficientRestitution( restitution );
            gameModel.SetTerrain( m_systems.terrain.get() );
            gameModel.AddBoundingBox( Vector3( hx, hy, hz ) );
            gameModel.SetImpulseForce( force, forcePos );

            m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
        }
        else
        {
            float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
            float radius = ( 1.0f + static_cast<float>( NextSceneRand( SceneState().rngState ) % cfg.ballRadiusRange ) ) * 0.5f;

            GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), Vector3( moment, moment, moment ), mass );
            gameModel.SetCoefficientRestitution( restitution );
            gameModel.SetTerrain( m_systems.terrain.get() );
            gameModel.AddBoundingSphere( radius );
            gameModel.SetImpulseForce( force, forcePos );

            m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
        }
    }
}


// Spawns exact sphere/box counts using the same random parameter ranges as the
// mixed generated scene path. Spheres are spawned first so fixed seeds remain
// deterministic for benchmark scenes.
void SkullbonezRun::SetUpSolverObjects( int balls, int boxes )
{
    balls = (std::max)( 0, balls );
    boxes = (std::max)( 0, boxes );
    const int totalObjects = balls + boxes;
    if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        balls = totalObjects;
        boxes = 0;
    }
    else if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        balls = 0;
        boxes = totalObjects;
    }

    SceneState().modelCount = balls + boxes;
    SceneState().solverBallCount = balls;
    SceneState().solverBoxCount = boxes;

    const SkullbonezConfig& cfg = Cfg();

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( NextSceneRand( SceneState().rngState ) % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( NextSceneRand( SceneState().rngState ) % range );
        return ( NextSceneRand( SceneState().rngState ) % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = [&]() -> float
    { return ( NextSceneRand( SceneState().rngState ) % 2 == 0 ) ? 1.0f : -1.0f; };

    // --- Sphere pass ---
    for ( int i = 0; i < balls; ++i )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( NextSceneRand( SceneState().rngState ) % cfg.ballRestitutionRange ) / 10.0f;
        float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
        float radius = ( 1.0f + static_cast<float>( NextSceneRand( SceneState().rngState ) % cfg.ballRadiusRange ) ) * 0.5f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), Vector3( moment, moment, moment ), mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.AddBoundingSphere( radius );
        gameModel.SetImpulseForce( force, forcePos );
        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // --- Box pass ---
    // Box inertia tensor (solid cuboid about centre of mass):
    //   Ix = m/12 * (hy^2 + hz^2),  Iy = m/12 * (hx^2 + hz^2),  Iz = m/12 * (hx^2 + hy^2)
    // where hx, hy, hz are the full extents (2 * half-extents).
    // The spawn code uses half-extents internally, so the factor is m/3 (= m/12 * 4).
    for ( int i = 0; i < boxes; ++i )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( NextSceneRand( SceneState().rngState ) % cfg.ballRestitutionRange ) / 10.0f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        float halfExtent = ( 1.0f + static_cast<float>( NextSceneRand( SceneState().rngState ) % 3 ) ) * 0.6f;
        float hx = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( SceneState().rngState ) % 4 ) * 0.2f );
        float hy = halfExtent;
        float hz = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( SceneState().rngState ) % 4 ) * 0.2f );

        float hx2 = hx * hx, hy2 = hy * hy, hz2 = hz * hz;
        float m3 = mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), inertia, mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.AddBoundingBox( Vector3( hx, hy, hz ) );
        gameModel.SetImpulseForce( force, forcePos );
        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    SceneState().modelCount = balls + boxes;
}


void SkullbonezRun::SetUpCamerasFromScene( const TestScene& scene )
{
    m_systems.cameras = CameraCollection::Instance();

    for ( int i = 0; i < scene.GetCameraCount(); ++i )
    {
        const SceneCamera& cam = scene.GetCamera( i );
        uint32_t hash = HashStr( cam.name );
        m_systems.cameras->AddCamera( cam.m_position, cam.view, cam.up, hash );
    }

    // set the camera m_boundaries
    m_systems.cameras->SetCameraXZBounds( m_systems.terrain->GetXZBounds() );

    // set the m_terrain
    m_systems.cameras->SetTerrain( m_systems.terrain.get() );

    // lock the m_cameras
    m_systems.cameras->SetLockedMode( false );
}


void SkullbonezRun::SetUpGameModelsFromScene( const TestScene& scene )
{
    SceneState().modelCount = scene.GetBallCount() + scene.GetBallStateCount() + scene.GetBoxCount();

    for ( int i = 0; i < scene.GetBallCount(); ++i )
    {
        const SceneBall& ball = scene.GetBall( i );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( ball.posX, ball.posY, ball.posZ ),
                             Vector3( ball.moment, ball.moment, ball.moment ),
                             ball.m_mass );

        gameModel.SetCoefficientRestitution( ball.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( ball.name );
        gameModel.AddBoundingSphere( ball.m_radius );
        gameModel.SetFixed( ball.isFixed );

        // apply initial orientation if specified (euler angles in degrees, XYZ order)
        if ( ball.hasInitOrient )
        {
            gameModel.SetInitialOrientation( ball.eulerX, ball.eulerY, ball.eulerZ );
        }

        // apply force if any is specified
        if ( !ball.isFixed && ( ball.forceX != 0.0f || ball.forceY != 0.0f || ball.forceZ != 0.0f ) )
        {
            gameModel.SetImpulseForce(
                Vector3( ball.forceX, ball.forceY, ball.forceZ ),
                Vector3( ball.forcePosX, ball.forcePosY, ball.forcePosZ ) );
        }

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // ball_state entries: full dynamic state from a snapshot
    for ( int i = 0; i < scene.GetBallStateCount(); ++i )
    {
        const SceneBallState& bs = scene.GetBallState( i );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( bs.posX, bs.posY, bs.posZ ),
                             Vector3( bs.inertiaX, bs.inertiaY, bs.inertiaZ ),
                             bs.mass );

        gameModel.SetCoefficientRestitution( bs.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( bs.name );
        gameModel.AddBoundingSphere( bs.radius );
        gameModel.SetLinearVelocity( Vector3( bs.velX, bs.velY, bs.velZ ) );
        gameModel.SetAngularVelocity( Vector3( bs.angVelX, bs.angVelY, bs.angVelZ ) );
        gameModel.SetOrientation( Quaternion( bs.orientX, bs.orientY, bs.orientZ, bs.orientW ) );

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // box entries: rigid box entities
    for ( int i = 0; i < scene.GetBoxCount(); ++i )
    {
        const SceneBox& box = scene.GetBox( i );

        // Box inertia: I = m/3 * (hy^2 + hz^2) etc. for half-extents
        float hx2 = box.halfX * box.halfX;
        float hy2 = box.halfY * box.halfY;
        float hz2 = box.halfZ * box.halfZ;
        float m3 = box.mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( box.posX, box.posY, box.posZ ),
                             inertia,
                             box.mass );

        gameModel.SetCoefficientRestitution( box.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( box.name );
        gameModel.AddBoundingBox( Vector3( box.halfX, box.halfY, box.halfZ ) );

        if ( box.hasInitOrient )
        {
            gameModel.SetInitialOrientation( box.eulerX, box.eulerY, box.eulerZ );
        }

        if ( box.hasInitVelocity )
        {
            gameModel.SetLinearVelocity( Vector3( box.velX, box.velY, box.velZ ) );
        }

        gameModel.SetFixed( box.isFixed );

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    for ( int materialIndex = 0; materialIndex < scene.GetObjectMaterialOverrideCount(); ++materialIndex )
    {
        const SceneObjectMaterialOverride& material = scene.GetObjectMaterialOverride( materialIndex );
        for ( int modelIndex = 0; modelIndex < m_cGameModelCollection.GetModelCount(); ++modelIndex )
        {
            GameModel& model = m_cGameModelCollection.GetModelAtIndex( modelIndex );
            if ( SceneMaterialTargetMatches( material, model ) )
            {
                model.SetRenderMaterial( material.material );
            }
        }
    }
}


bool SkullbonezRun::HasSceneQueueEntry( int index ) const
{
    return m_sceneRuntime.HasEntry( index );
}


bool SkullbonezRun::HasCurrentSceneQueueEntry() const
{
    return m_sceneRuntime.HasCurrentEntry();
}


const std::string* SkullbonezRun::CurrentSceneQueuePath() const
{
    return m_sceneRuntime.CurrentPath();
}


SceneRuntimeResetSnapshot SkullbonezRun::CaptureSceneRuntimeResetSnapshot()
{
    // Standard reset is a simulation rebuild, not a scene/config reload.  Capture
    // every operator-facing scene control before LoadScene reapplies file defaults.
    // Transient run artifacts such as frame counters, screenshot/perf files, timers,
    // contact caches, and generated object transforms intentionally reset below.
    SceneRuntimeResetSnapshot snapshot;
    snapshot.runtimeSettings = m_runtimeSettings;
    snapshot.debug = m_debug;
    snapshot.isScenePhysics = SceneState().isScenePhysics;
    snapshot.isSceneText = SceneState().isSceneText;
    snapshot.isFixedStep = SceneState().isFixedStep;
    snapshot.isExitOnComplete = SceneState().isExitOnComplete;
    snapshot.isInteractiveRun = SceneState().isInteractiveRun;
    snapshot.targetFrameCount = SceneState().targetFrameCount;
    snapshot.timeScale = SceneState().timeScale;
    snapshot.worldGravity = m_cWorldEnvironment.GetGravity();
    snapshot.worldFluidHeight = m_cWorldEnvironment.GetFluidSurfaceHeight();
    snapshot.worldFluidDensity = m_cWorldEnvironment.GetFluidDensity();
    // Preserve live Cine-tab edits across a scene reset/reload. Without this,
    // pressing reset would snap the visual look back to the file/defaults.
    snapshot.hasCinematicRenderingOverride = SceneState().hasCinematicRenderingOverride;
    snapshot.isCinematicRenderingEnabled = SceneState().isCinematicRenderingEnabled;
    snapshot.hasCinematicExposure = SceneState().hasCinematicExposure;
    snapshot.cinematicExposure = SceneState().cinematicExposure;
    snapshot.hasCinematicGamma = SceneState().hasCinematicGamma;
    snapshot.cinematicGamma = SceneState().cinematicGamma;
    snapshot.cinematicOverrideMask = SceneState().cinematicOverrideMask;
    snapshot.uiCinematicOverrideMask = SceneState().uiCinematicOverrideMask;
    snapshot.cinematicRender = SceneState().cinematicRender;
    snapshot.uiTimeScaleOverride = m_UITimeScaleOverride;
    snapshot.uiModelCountOverride = m_UIModelCountOverride;
    snapshot.uiSolverBallCountOverride = m_UISolverBallCountOverride;
    snapshot.uiSolverBoxCountOverride = m_UISolverBoxCountOverride;
    snapshot.trackBallIndex = m_camera.trackBallIndex;
    snapshot.trackHeight = m_camera.trackHeight;
    snapshot.autoCycleInterval = m_camera.autoCycleInterval;
    snapshot.autoCycleAccum = m_camera.autoCycleAccum;
    snapshot.autoCycleShotsTaken = m_camera.autoCycleShotsTaken;
    return snapshot;
}


void SkullbonezRun::RestoreSceneRuntimeResetSnapshot( const SceneRuntimeResetSnapshot& snapshot, bool suppressExitOnComplete )
{
    // Restore live run controls that do not affect object construction.  Timers,
    // frame counters, diagnostics/perf files, screenshots, input edge states, and
    // object transforms stay reset because they belong to the simulation run itself.
    m_runtimeSettings = snapshot.runtimeSettings;
    m_debug = snapshot.debug;
    SceneState().isScenePhysics = snapshot.isScenePhysics;
    SceneState().isSceneText = snapshot.isSceneText;
    SceneState().timeScale = snapshot.timeScale;
    SceneState().isFixedStep = snapshot.isFixedStep;
    SceneState().isInteractiveRun = snapshot.isInteractiveRun || suppressExitOnComplete;
    SceneState().isExitOnComplete = SceneState().isInteractiveRun ? false : snapshot.isExitOnComplete;
    SceneState().targetFrameCount = snapshot.targetFrameCount;
    // Re-apply preserved runtime/UI cinematic state after the scene rebuilds.
    SceneState().hasCinematicRenderingOverride = snapshot.hasCinematicRenderingOverride;
    SceneState().isCinematicRenderingEnabled = snapshot.isCinematicRenderingEnabled;
    SceneState().hasCinematicExposure = snapshot.hasCinematicExposure;
    SceneState().cinematicExposure = snapshot.cinematicExposure;
    SceneState().hasCinematicGamma = snapshot.hasCinematicGamma;
    SceneState().cinematicGamma = snapshot.cinematicGamma;
    SceneState().cinematicOverrideMask = snapshot.cinematicOverrideMask;
    SceneState().uiCinematicOverrideMask = snapshot.uiCinematicOverrideMask;
    SceneState().cinematicRender = snapshot.cinematicRender;
    m_UITimeScaleOverride = snapshot.uiTimeScaleOverride;
    m_UIModelCountOverride = snapshot.uiModelCountOverride;
    m_UISolverBallCountOverride = snapshot.uiSolverBallCountOverride;
    m_UISolverBoxCountOverride = snapshot.uiSolverBoxCountOverride;
    m_camera.trackHeight = snapshot.trackHeight;
    m_camera.trackBallIndex = ( snapshot.trackBallIndex >= 0 && snapshot.trackBallIndex < SceneState().modelCount )
                                  ? snapshot.trackBallIndex
                                  : -1;
    m_camera.autoCycleInterval = snapshot.autoCycleInterval;
    m_camera.autoCycleAccum = snapshot.autoCycleAccum;
    m_camera.autoCycleShotsTaken = snapshot.autoCycleShotsTaken;
    m_physicsDebugVisualizer.SetFlags( m_debug.physicsDebugFlags );
    m_physicsDebugVisualizer.SetContactLingerSeconds( m_debug.physicsDebugContactLinger );
}


void SkullbonezRun::ClearSceneRuntimeUIOverrides()
{
    // Scene changes and the explicit Reset Defaults command must make the scene
    // file/config authoritative again.  UI sliders are live overrides, so clearing
    // them here prevents stale counts or time scale from leaking into unrelated scenes.
    m_UITimeScaleOverride = 0.0f;
    m_UIModelCountOverride = -1;
    m_UISolverBallCountOverride = -1;
    m_UISolverBoxCountOverride = -1;
}


void SkullbonezRun::LoadScene( int index, bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
{
    SceneRuntime& runtime = m_sceneRuntime;
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "scene_reload" );
#endif
    if ( !runtime.HasEntry( index ) )
    {
        return;
    }

    if ( suppressExitOnComplete )
    {
        SceneState().isInteractiveRun = true;
    }
    if ( m_cmdInteractiveSceneRun )
    {
        SceneState().isInteractiveRun = true;
    }
    const bool suppressAutomationExit = SceneState().isInteractiveRun || suppressExitOnComplete;
    const bool shouldPreserveRuntimeState = preserveRuntimeState && runtime.HasCurrentEntry();
    SceneRuntimeResetSnapshot resetSnapshot;
    if ( shouldPreserveRuntimeState )
    {
        resetSnapshot = CaptureSceneRuntimeResetSnapshot();
    }
    else
    {
        ClearSceneRuntimeUIOverrides();
    }

    // Flush GPU before destroying scene resources to avoid use-after-free
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }

    runtime.BeginLoad( index );
    const std::string& scenePath = runtime.PathAt( index );
    if ( !shouldPreserveRuntimeState )
    {
        m_selectedCineModeSceneIndex = ( !scenePath.empty() && IsCineScenePath( scenePath ) ) ? CurrentSceneBrowserIndex() : -1;
    }

    // Close previous perf log if open
    if ( m_perfLogState.perfLogFile )
    {
        LogPerfMemory( "end" );
        if ( m_perfLogState.perfLogWritesSinceFlush > 0 )
        {
            fflush( m_perfLogState.perfLogFile );
            m_perfLogState.perfLogWritesSinceFlush = 0;
        }
        fclose( m_perfLogState.perfLogFile );
        m_perfLogState.perfLogFile = nullptr;
    }

    // Reset scene config to defaults
    SceneState().isScenePhysics = true;
    SceneState().isSceneText = true;
    m_perfLogState.isPerfTest = false;
    m_perfLogState.perfHeaderWritten = false;
    m_screenshot.isScreenshotSaved = false;
    m_screenshot.isScreenshotAndExit = false;
    SceneState().targetFrameCount = -1;
    SceneState().currentFrame = 0;
    SceneState().solverBallCount = 0;
    SceneState().solverBoxCount = 0;
    SceneState().hasCinematicRenderingOverride = false;
    SceneState().isCinematicRenderingEnabled = false;
    SceneState().hasCinematicExposure = false;
    SceneState().cinematicExposure = Cfg().cinematicRender.exposure;
    SceneState().hasCinematicGamma = false;
    SceneState().cinematicGamma = Cfg().cinematicRender.gamma;
    SceneState().cinematicOverrideMask = 0;
    SceneState().uiCinematicOverrideMask = 0;
    SceneState().cinematicRender = Cfg().cinematicRender;
    SceneState().isTestComplete = false;
    SceneState().isFinishLogged = false;
    m_simulation.Reset();
    m_screenshot.screenshotFrame = -1;
    m_screenshot.screenshotMs = -1;
    m_screenshot.screenshotPath[0] = '\0';
    m_screenshot.screenshotInterval = -1;
    m_screenshot.intervalCaptureCount = 0;
    m_screenshot.screenshotDir[0] = '\0';
    m_perfLogState.perfLogPath[0] = '\0';
    m_perfLogState.isPerfLogFlushEnabled = false;
    m_perfLogState.perfLogFlushInterval = 0;
    m_perfLogState.perfLogWritesSinceFlush = 0;
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
    m_uiStress = RunUIStressState{};

    // Reset cameras and game models
    m_systems.cameras->Reset();
    m_cGameModelCollection.Clear();

    // Reset input and debug state
    m_camera.isFlyMode = false;
    m_camera.isNudgeMode = false;
    ResetProjectilePool();
    m_debug.isWaterFreezeDebug = false;
    m_debug.isWaterNoReflect = false;
    m_debug.isWaterRTReflect = false;
    m_debug.isWaterFlatDebug = false;
    m_debug.isTerrainHidden = false;
    m_debug.isWaterHidden = false;
    m_debug.isTextOnly = false;
    m_debug.isUITestPattern = false;
    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
    m_debug.isPhysicsDebugTransparent = false;
    m_debug.physicsDebugAlpha = 0.28f;
    m_debug.physicsDebugContactLinger = 0.45f;
    m_debug.physicsDebugPipelineStageCursor = 0;
    m_physicsDebugVisualizer.SetFlags( PHYSICS_DEBUG_NONE );
#ifdef _DEBUG
    m_debug.reproSnapshotMessage[0] = '\0';
    m_debug.reproSnapshotMessageUntil = 0.0;
#endif
    SceneState().timeScale = 1.0f;
    SceneState().isFixedStep = false;
    SceneState().isExitOnComplete = false;
    m_debug.frozenWaterTime = 0.0f;
    m_camera.trackBallIndex = -1;
    m_camera.trackHeight = 300.0f;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
    m_camera.autoCycleShotsTaken = 0;
    m_camera.input = {};
    // overlayMode intentionally preserved — the user's HUD state persists across scene reloads.
    m_camera.selectedCamera = 0;

    // Reset timing
    m_timers.timeSinceLastRender = 0.0f;
    m_timers.renderTime = 0.0f;
    m_camera.cameraTime = 0.0f;
    m_timers.rollingRenderTime = 0.0f;
    m_timers.physicsTime = 0.0f;
    m_timers.rollingPhysicsTime = 0.0f;
    m_timers.rollingFpsTime = 0.0f;
    m_timers.rollingSceneEnergy = 0.0f;
    m_timers.cpuFrameWorkMs = 0.0f;
    m_timers.gpuFrameWorkMs = 0.0f;
    m_timers.sceneEnergyAccumulator = 0.0;
    m_timers.sceneEnergySampleCount = 0;
    m_timers.lastUIDrawCalls = 0;

    // Reseed RNG. Unseeded reruns mix in the load/reset counters so quick repeated
    // Q resets do not collapse to the same time(nullptr) seed. Scene files and CLI
    // overrides can still pin this exactly for repro.
    unsigned int rngSeed = static_cast<unsigned int>( time( nullptr ) );
    rngSeed ^= static_cast<unsigned int>( SceneState().loadCount ) * 2654435761u;
    rngSeed ^= static_cast<unsigned int>( SceneState().manualResetCount ) * 2246822519u;
    if ( rngSeed == 0 )
    {
        rngSeed = 1;
    }

    // Branch on file-backed scene mode vs generated demo mode.
    if ( scenePath.empty() )
    {
        Cfg().gameModelCapacity = m_startupGameModelCapacity;
        ApplySceneWorkerThreadSetting( m_startupWorkerThreads );
        if ( m_cmdSeedOverride > 0 )
        {
            rngSeed = m_cmdSeedOverride;
        }
        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;
        UseDefaultTerrain();
        ApplyNoWaterOverride();
        if ( shouldPreserveRuntimeState )
        {
            // Restore setup-affecting live controls before the generated model pool is rebuilt.
            // Other visual/debug controls are restored later after scene directives have loaded.
            ApplyUIWorldOverride( resetSnapshot.worldGravity, resetSnapshot.worldFluidHeight, resetSnapshot.worldFluidDensity );
        }

        SceneState().isSceneMode = false;
        SetUpCameras();
        if ( m_UISolverBallCountOverride >= 0 || m_UISolverBoxCountOverride >= 0 )
        {
            SetUpSolverObjects( (std::max)( 0, m_UISolverBallCountOverride ), (std::max)( 0, m_UISolverBoxCountOverride ) );
        }
        else
        {
            SetUpGameModels( m_UIModelCountOverride >= 0 ? m_UIModelCountOverride : DEFAULT_GAME_MODELS );
        }
        ApplyDemoHeroStyleOverride();
        const char* rendererName = Gfx().GetRendererName();
        char titleText[256];
        sprintf_s( titleText, "%s [%s]", TITLE_TEXT, rendererName );
        m_systems.window->SetTitleText( titleText );
    }
    else
    {
        SceneState().isSceneMode = true;
        TestScene scene = TestScene::LoadFromFile( scenePath.c_str() );
        Cfg().gameModelCapacity = scene.HasModelCapacityOverride() ? scene.GetModelCapacity() : m_startupGameModelCapacity;
        ApplySceneWorkerThreadSetting( scene.HasWorkerThreadOverride() ? scene.GetWorkerThreads() : m_startupWorkerThreads );
        SceneState().isScenePhysics = scene.IsPhysicsEnabled();
        SceneState().isSceneText = scene.IsTextEnabled();
        m_perfLogState.isPerfLogFlushEnabled = scene.IsPerfLogFlushEnabled();
        m_perfLogState.perfLogFlushInterval = scene.GetPerfLogFlushInterval();
        m_debug.physicsDebugFlags = scene.GetPhysicsDebugFlags();
        m_debug.isPhysicsDebugTransparent = scene.IsPhysicsDebugTransparent();
        m_debug.physicsDebugAlpha = scene.GetPhysicsDebugAlpha();
        m_debug.physicsDebugContactLinger = scene.GetPhysicsDebugContactLinger();
        if ( scene.HasVsyncOverride() )
        {
            m_runtimeSettings.isVsyncEnabled = scene.IsVsyncEnabled();
        }
        if ( scene.HasPipelineSyncOverride() )
        {
            m_runtimeSettings.isPipelineSyncEnabled = scene.IsPipelineSyncEnabled();
        }
        m_debug.isTextOnly = scene.IsTextOnly();
        m_debug.isWaterHidden = scene.IsWaterHidden();
        m_debug.isTerrainHidden = scene.IsTerrainHidden();
        m_debug.isCollisionVisualizer = scene.IsCollisionVisualizerEnabled();
        m_debug.isBroadphaseOverlay = scene.IsBroadphaseOverlayEnabled();
        m_debug.isWaterFreezeDebug = scene.IsWaterFreezeDebugEnabled();
        m_debug.isWaterFlatDebug = scene.IsWaterFlatDebugEnabled();
        const int waterReflectionMode = std::clamp( scene.GetWaterReflectionMode(), 0, 2 );
        m_debug.isWaterRTReflect = waterReflectionMode == 1;
        m_debug.isWaterNoReflect = waterReflectionMode == 2;
        if ( m_debug.isWaterFreezeDebug )
        {
            m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
        }
        SceneState().timeScale = scene.GetTimeScale();
        SceneState().isFixedStep = scene.IsFixedStep();
        // Start with engine.cfg defaults, then apply only the cinematic fields
        // that the .scene file explicitly authored.
        SceneState().hasCinematicRenderingOverride = scene.HasCinematicRenderingOverride();
        SceneState().isCinematicRenderingEnabled = scene.IsCinematicRenderingEnabled();
        SceneState().hasCinematicExposure = scene.HasCinematicExposure();
        SceneState().cinematicExposure = scene.GetCinematicExposure();
        SceneState().hasCinematicGamma = scene.HasCinematicGamma();
        SceneState().cinematicGamma = scene.GetCinematicGamma();
        SceneState().cinematicOverrideMask = scene.GetCinematicOverrideMask();
        SceneState().cinematicRender = Cfg().cinematicRender;
        ApplyCinematicSceneOverrides( SceneState().cinematicRender, SceneState().cinematicOverrideMask, scene.GetCinematicRenderConfig() );

        const SceneUIOptions& UIOptions = scene.GetUIOptions();
        const double UINow = m_timers.simulationTimer.GetTotalTime();
        bool isAutomationScene = scene.IsExitOnComplete() ||
                                 scene.IsScreenshotAndExit() ||
                                 scene.GetScreenshotFrame() >= 0 ||
                                 scene.GetScreenshotMs() >= 0 ||
                                 scene.GetScreenshotInterval() > 0 ||
                                 scene.GetPerfLogPath()[0] != '\0';
#ifdef _DEBUG
        isAutomationScene = isAutomationScene || m_physicsDiagnostics.isEnabled;
#endif
        if ( !preserveUIState )
        {
            if ( !UIOptions.hasVisible )
            {
                if ( isAutomationScene && !UIOptions.hasDirective )
                {
                    m_UI.SetVisible( false, UINow );
                }
                else if ( !UIOptions.hasDirective )
                {
                    if ( !m_UI.IsVisible() )
                    {
                        m_UI.SetVisible( true, UINow );
                    }
                    m_UI.SetMinimized( true, UINow );
                }
                else if ( !m_UI.IsVisible() )
                {
                    m_UI.SetVisible( true, UINow );
                }
            }
            if ( UIOptions.hasWindowRect )
            {
                m_UI.SetWindowBounds( UIOptions.windowX, UIOptions.windowY, UIOptions.windowW, UIOptions.windowH );
                if ( !UIOptions.hasMinimized )
                {
                    m_UI.SetMinimized( false, UINow );
                }
            }
            if ( UIOptions.hasActiveTab )
            {
                m_UI.SetActiveTab( static_cast<InGameUITab>( UIOptions.activeTab ) );
            }
            if ( UIOptions.hasBlur )
            {
                m_UI.SetBlurEnabled( UIOptions.blurEnabled );
            }
            if ( UIOptions.hasProfilerExpandAll )
            {
                m_UI.SetProfilerExpandAll( UIOptions.profilerExpandAll );
            }
            if ( UIOptions.hasProfilerTimeline )
            {
                m_UI.SetProfilerTimelineEnabled( UIOptions.profilerTimeline );
            }
            if ( UIOptions.hasPerformanceHistogram )
            {
                m_UI.SetPerformanceHistogramEnabled( UIOptions.performanceHistogram );
            }
            if ( UIOptions.hasHitboxOverlay )
            {
                m_UI.SetHitboxOverlayEnabled( UIOptions.hitboxOverlay );
            }
            if ( UIOptions.hasRendererComboOpen )
            {
                m_UI.SetRendererComboOpen( UIOptions.rendererComboOpen );
            }
            if ( UIOptions.hasWaterComboOpen )
            {
                m_UI.SetWaterComboOpen( UIOptions.waterComboOpen );
            }
            if ( UIOptions.hasSceneComboOpen )
            {
                m_UI.SetSceneComboOpen( UIOptions.sceneComboOpen );
            }
            if ( UIOptions.hasSceneFilter )
            {
                m_UI.SetSceneFilter( UIOptions.sceneFilter );
            }
            if ( UIOptions.hasScrollY )
            {
                m_UI.SetScrollY( UIOptions.scrollY );
            }
            m_UI.SetMouseOverride( UIOptions.hasMouseOverride, UIOptions.mouseX, UIOptions.mouseY );
            if ( UIOptions.hasVisible )
            {
                m_UI.SetVisible( UIOptions.isVisible, UINow );
            }
            if ( UIOptions.hasMinimized )
            {
                m_UI.SetMinimized( UIOptions.isMinimized, 0.0 );
            }
            if ( UIOptions.hasTestPattern )
            {
                m_debug.isUITestPattern = UIOptions.testPatternEnabled;
            }
        }
        if ( UIOptions.hasStress )
        {
            m_uiStress.enabled = UIOptions.stressEnabled;
        }
        if ( UIOptions.hasStressSeed )
        {
            m_uiStress.randomState = UIOptions.stressSeed;
        }
        if ( UIOptions.hasStressActions )
        {
            m_uiStress.actionsPerFrame = std::clamp( UIOptions.stressActionsPerFrame, 1, 32 );
        }
        SceneState().targetFrameCount = scene.GetFrameCount();
        SceneState().isExitOnComplete = suppressAutomationExit ? false : scene.IsExitOnComplete();
        m_screenshot.screenshotFrame = scene.GetScreenshotFrame();
        m_screenshot.screenshotMs = scene.GetScreenshotMs();
        m_screenshot.isScreenshotAndExit = suppressAutomationExit ? false : scene.IsScreenshotAndExit();

        if ( scene.GetScreenshotPath()[0] != '\0' )
        {
            strcpy_s( m_screenshot.screenshotPath, sizeof( m_screenshot.screenshotPath ), scene.GetScreenshotPath() );
        }
        // Interval capture: create output directory
        m_screenshot.screenshotInterval = scene.GetScreenshotInterval();
        if ( scene.GetScreenshotDir()[0] != '\0' )
        {
            strcpy_s( m_screenshot.screenshotDir, sizeof( m_screenshot.screenshotDir ), scene.GetScreenshotDir() );
            CreateDirectoryA( m_screenshot.screenshotDir, nullptr );
        }

        // Perf test: open CSV log file
        const char* pPerfPath = scene.GetPerfLogPath();
        if ( pPerfPath[0] != '\0' )
        {
            m_perfLogState.isPerfTest = true;
            strcpy_s( m_perfLogState.perfLogPath, sizeof( m_perfLogState.perfLogPath ), pPerfPath );
            const char* mode = ( sPerfPass == 0 ) ? "w" : "a";
            fopen_s( &m_perfLogState.perfLogFile, m_perfLogState.perfLogPath, mode );
            if ( m_perfLogState.perfLogFile )
            {
                m_perfLogState.perfLogWritesSinceFlush = 0;
                LogPerfMemory( "start" );
            }
        }

        // Physics regression log: current-solver per-frame CSV enabled only by command line.
#ifdef _DEBUG
        m_cGameModelCollection.SetPhysicsRegressionLogPath( m_perfLogState.physicsRegressionLogOverride );
        m_cGameModelCollection.SetPhysicsCollisionTimeLogPath( m_perfLogState.physicsCollisionTimeLogOverride );
#endif

        // Override RNG seed for deterministic scenes. CLI --seed wins so a nudge snapshot can
        // replay an unseeded/random scene or deliberately override a scene file seed.
        if ( scene.GetSeed() > 0 )
        {
            rngSeed = scene.GetSeed();
        }
        if ( m_cmdSeedOverride > 0 )
        {
            rngSeed = m_cmdSeedOverride;
        }
        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;

        // Scene terrain is authoritative.  A flat-slope test scene must not leak
        // its analytic terrain into the next height-map scene.
        if ( scene.HasFlatSlope() )
        {
            UseFlatSlopeTerrain( scene.GetFlatBaseY(), scene.GetFlatSlopeX(), scene.GetFlatSlopeZ() );
        }
        else
        {
            UseDefaultTerrain();
        }

        // Override world environment if scene specifies world values
        if ( scene.HasWorldOverride() )
        {
            m_cWorldEnvironment = WorldEnvironment( scene.GetWorldFluidHeight(), scene.GetWorldFluidDensity(), Cfg().gasDensity, scene.GetWorldGravity() );
            UpdateWorldTerrainBounds();
        }
        ApplyNoWaterOverride();
        if ( shouldPreserveRuntimeState )
        {
            // World sliders/keyboard water edits are part of the live scene controls.
            // Restore them after terrain/world directives and --no-water have resolved,
            // so a plain reset keeps the operator's current environment.
            ApplyUIWorldOverride( resetSnapshot.worldGravity, resetSnapshot.worldFluidHeight, resetSnapshot.worldFluidDensity );
        }

        SetUpCamerasFromScene( scene );

        if ( m_UISolverBallCountOverride >= 0 || m_UISolverBoxCountOverride >= 0 )
        {
            SetUpSolverObjects( (std::max)( 0, m_UISolverBallCountOverride ), (std::max)( 0, m_UISolverBoxCountOverride ) );
        }
        else if ( m_UIModelCountOverride >= 0 )
        {
            SetUpGameModels( m_UIModelCountOverride );
        }
        else if ( scene.GetSolverBallCount() > 0 || scene.GetSolverBoxCount() > 0 )
        {
            // Exact-count solver spawn — explicit ball/box split for benchmarks.
            SetUpSolverObjects( scene.GetSolverBallCount(), scene.GetSolverBoxCount() );
        }
        else
        {
            SetUpGameModelsFromScene( scene );
        }

        // Ball-tracking camera: enabled when scene specifies a positive track_height
        if ( scene.GetTrackHeight() > 0.0f )
        {
            m_camera.trackHeight = scene.GetTrackHeight();
            m_camera.trackBallIndex = 0;
            m_camera.autoCycleInterval = scene.GetAutoCycleInterval(); // -1 if not specified = disabled
        }

        const char* rendererName = Gfx().GetRendererName();
        char titleText[256];
        sprintf_s( titleText, "%s [SCENE MODE] [%s]", TITLE_TEXT, rendererName );
        m_systems.window->SetTitleText( titleText );

        // Snapshot scenes (ball_state) start paused in free camera mode ?
        // user presses F to resume simulation and attach to scene camera
        if ( scene.GetBallStateCount() > 0 )
        {
            m_camera.isFlyMode = true;
            m_camera.cameraTime = 0.0f;
            XZBounds unbounded;
            unbounded.m_xMin = -99999.9f;
            unbounded.m_xMax = 99999.9f;
            unbounded.m_zMin = -99999.9f;
            unbounded.m_zMax = 99999.9f;
            uint32_t activeCam = m_systems.cameras->GetSelectedCameraName();
            m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            Input::SetSystemCursorVisible( false );
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
            m_camera.hasMouseLookLastClient = false;
            m_camera.needsMouseLookReset = true;
            Input::ResetMouseLookDeltas();
        }
    }

    if ( shouldPreserveRuntimeState )
    {
        RestoreSceneRuntimeResetSnapshot( resetSnapshot, suppressExitOnComplete );
    }

    // CLI --time-scale and --fixed-step override anything the scene file sets.
    if ( m_cmdTimeScaleOverride > 0.0f )
    {
        SceneState().timeScale = m_cmdTimeScaleOverride;
    }
    if ( m_UITimeScaleOverride > 0.0f )
    {
        SceneState().timeScale = m_UITimeScaleOverride;
    }
    if ( m_cmdFixedStep )
    {
        SceneState().isFixedStep = true;
    }
    if ( !shouldPreserveRuntimeState )
    {
        m_runtimeSettings.tornadoField = Physics::TornadoFieldConfig();
        ApplyTornadoDefaultsForActiveScene();
    }
    if ( m_cmdHasTornadoOverride )
    {
        m_runtimeSettings.tornadoField.enabled = m_cmdTornadoEnabled;
    }
    if ( m_cmdTornadoVectors )
    {
        m_runtimeSettings.tornadoField.visualizeVelocityField = true;
    }
    SyncTornadoFieldToPhysics();
    m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
    if ( m_cmdFrameCountOverride > 0 )
    {
        SceneState().targetFrameCount = m_cmdFrameCountOverride;
        SceneState().isExitOnComplete = true;
    }
    if ( m_cmdUIStress )
    {
        m_uiStress.enabled = true;
        m_uiStress.randomState = m_cmdUIStressSeed;
        m_uiStress.actionsPerFrame = m_cmdUIStressActions;
        m_UI.SetVisible( true, m_timers.simulationTimer.GetTotalTime() );
        m_UI.SetMinimized( false, m_timers.simulationTimer.GetTotalTime() );
    }
    if ( m_cmdHasCinematicShadowsOverride )
    {
        ActiveCinematicConfig().shadowsEnabled = m_cmdCinematicShadows;
        SceneState().cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    }
    if ( m_cmdHasPhysicsDebugFlagsOverride )
    {
        m_debug.physicsDebugFlags = m_cmdPhysicsDebugFlagsOverride;
    }
    if ( m_cmdHasPhysicsDebugTransparentOverride )
    {
        m_debug.isPhysicsDebugTransparent = m_cmdPhysicsDebugTransparentOverride;
    }
    if ( m_cmdHasPhysicsDebugAlphaOverride )
    {
        m_debug.physicsDebugAlpha = m_cmdPhysicsDebugAlphaOverride;
    }
    if ( m_cmdHasPhysicsDebugContactLingerOverride )
    {
        m_debug.physicsDebugContactLinger = m_cmdPhysicsDebugContactLingerOverride;
    }

#ifdef _DEBUG
    Log().WriteEventf( "scene_started index=%d load=%d path=\"%s\" renderer=\"%s\" target_frames=%d seed=%u fixed_step=%d physics=%d text=%d models=%d",
                       SceneState().currentSceneIndex,
                       SceneState().loadCount,
                       scenePath.empty() ? "generated" : scenePath.c_str(),
                       IsGfxReady() ? Gfx().GetRendererName() : "unknown",
                       SceneState().targetFrameCount,
                       SceneState().rngSeed,
                       SceneState().isFixedStep ? 1 : 0,
                       SceneState().isScenePhysics ? 1 : 0,
                       SceneState().isSceneText ? 1 : 0,
                       SceneState().modelCount );
#endif

#ifdef _DEBUG
    BeginPhysicsDiagnosticsRun( scenePath.c_str() );
#endif

    // Apply runtime swap policy after config/scene overrides are resolved.
    Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );

    // Restart timers
    m_timers.frameTimer.StartTimer();
    m_timers.workTimer.StartTimer();
    m_timers.updateTimer.StartTimer();
    m_timers.cameraTimer.StartTimer();
    m_timers.simulationTimer.StartTimer();

    // Initialize DXR raytracing on first scene load (requires terrain + sphere meshes to exist)
    // Force sphere mesh creation (normally lazy-init on first render)
    const auto renderCapabilities = Gfx().GetCapabilities();
    if ( renderCapabilities.supportsDxrReflection && SkullbonezHelper::GetSphereInstMeshHandle() == 0 )
    {
        SkullbonezHelper::EnsureSphereMesh();
    }
    {
    }
    if ( renderCapabilities.supportsDxrReflection && m_systems.terrain && m_systems.terrain->GetMesh() )
    {
        IMesh* terrainMesh = m_systems.terrain->GetMesh();
        uint64_t terrainVBVA = terrainMesh->GetVertexBufferGPUVA();
        int terrainVertCount = terrainMesh->GetVertexCount();
        int terrainStride = terrainMesh->GetStride();

        uint32_t sphereHandle = SkullbonezHelper::GetSphereInstMeshHandle();
        uint64_t sphereVBVA = Gfx().GetInstancedMeshStaticVBVA( sphereHandle );
        int sphereVertCount = SkullbonezHelper::GetSphereVertexCount();
        int sphereStride = Gfx().GetInstancedMeshStaticStride( sphereHandle );

        {
        }

        if ( terrainVBVA != 0 && sphereVBVA != 0 )
        {
            Gfx().InitDXR( terrainVBVA, terrainVertCount, terrainStride, sphereVBVA, sphereVertCount, sphereStride, ActiveGameModelCapacity() );
        }
    }
}


bool SkullbonezRun::SaveCurrentSceneDefaults()
{
    const std::string* scenePath = CurrentSceneQueuePath();
    if ( !SceneState().isSceneMode || !scenePath || scenePath->empty() )
    {
        return false;
    }

    std::ifstream input( *scenePath );
    if ( !input )
    {
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while ( std::getline( input, line ) )
    {
        if ( !line.empty() && line.back() == '\r' )
        {
            line.pop_back();
        }
        lines.push_back( line );
    }

    char buf[128] = {};
    SetSceneDirective( lines, "physics", std::string( "physics " ) + OnOff( SceneState().isScenePhysics ), true );
    SetSceneDirective( lines, "text", std::string( "text " ) + OnOff( SceneState().isSceneText ), true );
    SetSceneDirective( lines, "text_only", std::string( "text_only " ) + OnOff( m_debug.isTextOnly ), true );
    SetSceneDirective( lines, "vsync", std::string( "vsync " ) + OnOff( m_runtimeSettings.isVsyncEnabled ), true );
    SetSceneDirective( lines, "pipeline_sync", std::string( "pipeline_sync " ) + OnOff( m_runtimeSettings.isPipelineSyncEnabled ), true );
    // Deprecated directives are intentionally removed on save.  Keeping this
    // cleanup here lets old local scene files self-heal without reintroducing
    // parser support for legacy physics, physics_mode, or roll_align.
    SetSceneDirective( lines, "legacy_balls", "", false );
    SetSceneDirective( lines, "physics_mode", "", false );
    SetSceneDirective( lines, "roll_align", "", false );
    SetSceneDirective( lines, "fixed_step", "fixed_step", SceneState().isFixedStep );
    if ( SceneState().targetFrameCount > 0 )
    {
        snprintf( buf, sizeof( buf ), "frames %d", SceneState().targetFrameCount );
    }
    else
    {
        strcpy_s( buf, sizeof( buf ), "frames unlimited" );
    }
    SetSceneDirective( lines, "frames", buf, true );
    snprintf( buf, sizeof( buf ), "seed %u", (std::max)( 1u, SceneState().rngSeed ) );
    SetSceneDirective( lines, "seed", buf, true );
    SetSceneDirective( lines, "exit_on_complete", "exit_on_complete", SceneState().isExitOnComplete );
    SetSceneDirective( lines, "physics_debug_axes", std::string( "physics_debug_axes " ) + OnOff( ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0 ), true );
    SetSceneDirective( lines, "physics_debug_contacts", std::string( "physics_debug_contacts " ) + OnOff( ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0 ), true );
    SetSceneDirective( lines, "physics_debug_sleep", std::string( "physics_debug_sleep " ) + OnOff( ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0 ), true );
    SetSceneDirective( lines, "physics_debug_pipeline", std::string( "physics_debug_pipeline " ) + OnOff( ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0 ), true );
    SetSceneDirective( lines, "physics_debug_terrain_contact", std::string( "physics_debug_terrain_contact " ) + OnOff( ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_TERRAIN_CONTACT ) != 0 ), true );
    SetSceneDirective( lines, "physics_debug_transparent", std::string( "physics_debug_transparent " ) + OnOff( m_debug.isPhysicsDebugTransparent ), true );
    snprintf( buf, sizeof( buf ), "physics_debug_alpha %.2f", m_debug.physicsDebugAlpha );
    SetSceneDirective( lines, "physics_debug_alpha", buf, true );
    snprintf( buf, sizeof( buf ), "physics_debug_contact_linger %.2f", m_debug.physicsDebugContactLinger );
    SetSceneDirective( lines, "physics_debug_contact_linger", buf, true );
    snprintf( buf, sizeof( buf ), "time_scale %.2f", SceneState().timeScale );
    SetSceneDirective( lines, "time_scale", buf, true );
    SetSceneDirective( lines, "collision_visualizer", std::string( "collision_visualizer " ) + OnOff( m_debug.isCollisionVisualizer ), true );
    SetSceneDirective( lines, "broadphase_overlay", std::string( "broadphase_overlay " ) + OnOff( m_debug.isBroadphaseOverlay ), true );
    SetSceneDirective( lines, "water_freeze", std::string( "water_freeze " ) + OnOff( m_debug.isWaterFreezeDebug ), true );
    SetSceneDirective( lines, "water_flat", std::string( "water_flat " ) + OnOff( m_debug.isWaterFlatDebug ), true );
    SetSceneDirective( lines, "water_hidden", std::string( "water_hidden " ) + OnOff( m_debug.isWaterHidden ), true );
    SetSceneDirective( lines, "terrain_hidden", std::string( "terrain_hidden " ) + OnOff( m_debug.isTerrainHidden ), true );
    SetSceneDirective( lines, "water_reflection", std::string( "water_reflection " ) + WaterReflectionDirectiveValue( m_debug.isWaterNoReflect, m_debug.isWaterRTReflect ), true );
    if ( m_camera.trackBallIndex >= 0 && m_camera.trackHeight > 0.0f )
    {
        snprintf( buf, sizeof( buf ), "track_height %.2f", m_camera.trackHeight );
        SetSceneDirective( lines, "track_height", buf, true );
    }
    else
    {
        SetSceneDirective( lines, "track_height", "", false );
    }
    if ( m_camera.autoCycleInterval > 0.0f )
    {
        snprintf( buf, sizeof( buf ), "auto_cycle_interval %.2f", m_camera.autoCycleInterval );
        SetSceneDirective( lines, "auto_cycle_interval", buf, true );
    }
    else
    {
        SetSceneDirective( lines, "auto_cycle_interval", "", false );
    }
    snprintf( buf, sizeof( buf ), "world %.2f %.2f %.2f", m_cWorldEnvironment.GetGravity(), m_cWorldEnvironment.GetFluidSurfaceHeight(), m_cWorldEnvironment.GetFluidDensity() );
    SetSceneDirective( lines, "world", buf, true );
    SetTouchedCinematicSceneDirectives( lines, SceneState().uiCinematicOverrideMask, SceneState().cinematicRender );

    if ( m_UIModelCountOverride >= 0 )
    {
        snprintf( buf, sizeof( buf ), "solver_balls %d", m_UIModelCountOverride );
        SetSceneDirective( lines, "solver_balls", buf, true );
        SetSceneDirective( lines, "solver_boxes", "", false );
    }
    else if ( SceneState().solverBallCount > 0 || SceneState().solverBoxCount > 0 || m_UISolverBallCountOverride >= 0 || m_UISolverBoxCountOverride >= 0 )
    {
        snprintf( buf, sizeof( buf ), "solver_balls %d", SceneState().solverBallCount );
        SetSceneDirective( lines, "solver_balls", buf, true );
        snprintf( buf, sizeof( buf ), "solver_boxes %d", SceneState().solverBoxCount );
        SetSceneDirective( lines, "solver_boxes", buf, true );
    }

    std::ofstream output( *scenePath, std::ios::trunc );
    if ( !output )
    {
        return false;
    }

    for ( const std::string& outLine : lines )
    {
        output << outLine << '\n';
    }
    return output.good();
}


bool SkullbonezRun::SaveRenderDefaults()
{
    const std::string configPath = std::string( DATA_ROOT ) + "engine.cfg";
    std::ifstream input( configPath );
    if ( !input )
    {
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while ( std::getline( input, line ) )
    {
        if ( !line.empty() && line.back() == '\r' )
        {
            line.pop_back();
        }
        lines.push_back( line );
    }

    const OrdinaryRenderConfig& ordinary = Cfg().ordinaryRender;
    std::vector<std::string> missing;
    char buf[128] = {};

    const auto setText = [&]( const char* key, const char* value )
    {
        const std::string lineText = std::string( key ) + " = " + value;
        if ( !ReplaceConfigLine( lines, key, value ) )
        {
            missing.push_back( lineText );
        }
    };
    const auto setBool = [&]( const char* key, bool value )
    {
        snprintf( buf, sizeof( buf ), "%d", value ? 1 : 0 );
        setText( key, buf );
    };
    const auto setInt = [&]( const char* key, int value )
    {
        snprintf( buf, sizeof( buf ), "%d", value );
        setText( key, buf );
    };
    const auto setFloat = [&]( const char* key, float value, const char* format )
    {
        snprintf( buf, sizeof( buf ), format, value );
        setText( key, buf );
    };

    setFloat( "ordinary_sun_intensity", ordinary.sunIntensity, "%.2f" );
    setFloat( "ordinary_sun_color_r", ordinary.sunColorR, "%.2f" );
    setFloat( "ordinary_sun_color_g", ordinary.sunColorG, "%.2f" );
    setFloat( "ordinary_sun_color_b", ordinary.sunColorB, "%.2f" );
    setFloat( "ordinary_ambient_strength", ordinary.ambientStrength, "%.2f" );
    setFloat( "ordinary_sky_ambient_r", ordinary.skyAmbientR, "%.2f" );
    setFloat( "ordinary_sky_ambient_g", ordinary.skyAmbientG, "%.2f" );
    setFloat( "ordinary_sky_ambient_b", ordinary.skyAmbientB, "%.2f" );
    setFloat( "ordinary_ground_ambient_r", ordinary.groundAmbientR, "%.2f" );
    setFloat( "ordinary_ground_ambient_g", ordinary.groundAmbientG, "%.2f" );
    setFloat( "ordinary_ground_ambient_b", ordinary.groundAmbientB, "%.2f" );
    setBool( "ordinary_shadows", ordinary.shadowsEnabled );
    setBool( "ordinary_shadow_terrain_casts", ordinary.shadowTerrainCasts );
    setBool( "ordinary_shadow_objects_cast", ordinary.shadowObjectsCast );
    setBool( "ordinary_shadow_terrain_receives", ordinary.shadowTerrainReceives );
    setBool( "ordinary_shadow_objects_receive", ordinary.shadowObjectsReceive );
    setInt( "ordinary_shadow_map_size", ordinary.shadowMapSize );
    setInt( "ordinary_shadow_pcf_radius", ordinary.shadowPcfRadius );
    setFloat( "ordinary_shadow_strength", ordinary.shadowStrength, "%.2f" );
    setFloat( "ordinary_shadow_softness", ordinary.shadowSoftness, "%.2f" );
    setFloat( "ordinary_shadow_depth_bias", ordinary.shadowDepthBias, "%.5f" );
    setFloat( "ordinary_shadow_slope_bias", ordinary.shadowSlopeBias, "%.5f" );
    setFloat( "ordinary_shadow_max_distance", ordinary.shadowMaxDistance, "%.1f" );
    setFloat( "ordinary_water_tint_r", ordinary.waterTintR, "%.3f" );
    setFloat( "ordinary_water_tint_g", ordinary.waterTintG, "%.3f" );
    setFloat( "ordinary_water_tint_b", ordinary.waterTintB, "%.3f" );
    setFloat( "ordinary_water_alpha", ordinary.waterAlpha, "%.2f" );
    setFloat( "ordinary_water_reflection_strength", ordinary.waterReflectionStrength, "%.2f" );
    setFloat( "ordinary_water_fresnel_f0", ordinary.waterFresnelF0, "%.3f" );
    setFloat( "ordinary_ball_roughness_scale", ordinary.ballRoughnessScale, "%.2f" );
    setFloat( "ordinary_ball_specular_scale", ordinary.ballSpecularScale, "%.2f" );
    setFloat( "ordinary_box_roughness_scale", ordinary.boxRoughnessScale, "%.2f" );
    setFloat( "ordinary_box_specular_scale", ordinary.boxSpecularScale, "%.2f" );

    AppendMissingOrdinaryConfigLines( lines, missing );

    std::ofstream output( configPath, std::ios::trunc );
    if ( !output )
    {
        return false;
    }
    for ( const std::string& outLine : lines )
    {
        output << outLine << '\n';
    }
    return output.good();
}


void SkullbonezRun::RefreshSceneBrowserList()
{
    m_sceneBrowserPaths.clear();
    m_sceneBrowserNames.clear();
    m_sceneBrowserNamePtrs.clear();

    const std::filesystem::path sceneDir = std::filesystem::path( DATA_ROOT ) / "scenes";
    try
    {
        if ( !std::filesystem::exists( sceneDir ) )
        {
            return;
        }

        for ( const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator( sceneDir ) )
        {
            if ( !entry.is_regular_file() || entry.path().extension() != ".scene" )
            {
                continue;
            }
            m_sceneBrowserPaths.push_back( NormalizeScenePath( entry.path().generic_string() ) );
        }
    }
    catch ( const std::filesystem::filesystem_error& e )
    {
        Log().WriteEventf( "scene_browser_refresh_failed message=\"%s\"", e.what() );
        m_sceneBrowserPaths.clear();
    }

    std::sort( m_sceneBrowserPaths.begin(), m_sceneBrowserPaths.end() );
    m_sceneBrowserPaths.erase( std::unique( m_sceneBrowserPaths.begin(), m_sceneBrowserPaths.end() ), m_sceneBrowserPaths.end() );
    m_sceneBrowserNames.reserve( m_sceneBrowserPaths.size() );
    m_sceneBrowserNamePtrs.reserve( m_sceneBrowserPaths.size() );
    for ( const std::string& path : m_sceneBrowserPaths )
    {
        m_sceneBrowserNames.emplace_back( FileNameFromPath( path.c_str() ) );
    }
    for ( const std::string& name : m_sceneBrowserNames )
    {
        m_sceneBrowserNamePtrs.push_back( name.c_str() );
    }
}


int SkullbonezRun::CurrentSceneBrowserIndex() const
{
    const std::string* currentScenePath = CurrentSceneQueuePath();
    if ( !currentScenePath )
    {
        return -1;
    }

    const std::string currentPath = NormalizeScenePath( *currentScenePath );
    for ( int i = 0; i < static_cast<int>( m_sceneBrowserPaths.size() ); ++i )
    {
        if ( NormalizeScenePath( m_sceneBrowserPaths[i] ) == currentPath )
        {
            return i;
        }
    }
    return -1;
}


void SkullbonezRun::LoadSceneFromBrowserIndex( int index )
{
    SceneRuntime& runtime = m_sceneRuntime;
    if ( index < 0 || index >= static_cast<int>( m_sceneBrowserPaths.size() ) )
    {
        return;
    }

    EnterInteractiveSceneRun();

    const std::string selectedPath = NormalizeScenePath( m_sceneBrowserPaths[index] );
    const int queuedIndex = runtime.FindNormalizedPath( selectedPath );
    if ( queuedIndex >= 0 )
    {
        if ( queuedIndex != runtime.CurrentIndex() )
        {
            LoadScene( queuedIndex, true, true );
        }
        else
        {
            SceneState().isExitOnComplete = false;
            m_screenshot.isScreenshotAndExit = false;
        }
        return;
    }

    LoadScene( runtime.Append( selectedPath ), true, true );
}


void SkullbonezRun::LoadDemoSceneFromUI()
{
    SceneRuntime& runtime = m_sceneRuntime;
    EnterInteractiveSceneRun();
    const int demoIndex = runtime.FindGeneratedDemo();
    if ( demoIndex >= 0 )
    {
        LoadScene( demoIndex, true, true );
        return;
    }

    LoadScene( runtime.Append( "" ), true, true );
}


bool SkullbonezRun::ApplyCinematicModeFromBrowserIndex( int index )
{
    EnterInteractiveSceneRun();
    m_cmdHasCinematicRenderingOverride = false;

    auto resetObjectMaterials = [&]()
    {
        for ( int modelIndex = 0; modelIndex < m_cGameModelCollection.GetModelCount(); ++modelIndex )
        {
            m_cGameModelCollection.GetModelAtIndex( modelIndex ).SetRenderTint( 1.0f, 1.0f, 1.0f, 0.0f );
        }
    };

    auto applyObjectMaterials = [&]( const TestScene& styleScene )
    {
        resetObjectMaterials();
        for ( int materialIndex = 0; materialIndex < styleScene.GetObjectMaterialOverrideCount(); ++materialIndex )
        {
            const SceneObjectMaterialOverride& material = styleScene.GetObjectMaterialOverride( materialIndex );
            for ( int modelIndex = 0; modelIndex < m_cGameModelCollection.GetModelCount(); ++modelIndex )
            {
                GameModel& model = m_cGameModelCollection.GetModelAtIndex( modelIndex );
                if ( SceneMaterialTargetMatches( material, model ) )
                {
                    model.SetRenderMaterial( material.material );
                }
            }
        }
    };

    CinematicRenderConfig& cinematic = ActiveCinematicConfig();
    if ( index < 0 )
    {
        cinematic = m_defaultCinematicRender;
        if ( SceneState().isSceneMode )
        {
            SceneState().hasCinematicRenderingOverride = false;
            SceneState().isCinematicRenderingEnabled = cinematic.enabled;
            SceneState().hasCinematicExposure = false;
            SceneState().cinematicExposure = cinematic.exposure;
            SceneState().hasCinematicGamma = false;
            SceneState().cinematicGamma = cinematic.gamma;
            SceneState().cinematicOverrideMask = 0;
            SceneState().uiCinematicOverrideMask = 0;
        }
        resetObjectMaterials();
        m_selectedCineModeSceneIndex = -1;
        return true;
    }

    if ( index >= static_cast<int>( m_sceneBrowserPaths.size() ) || !IsCineScenePath( m_sceneBrowserPaths[index] ) )
    {
        return false;
    }

    TestScene lookScene = TestScene::LoadFromFile( m_sceneBrowserPaths[index].c_str() );
    cinematic = m_defaultCinematicRender;
    ApplyCinematicSceneOverrides( cinematic, lookScene.GetCinematicOverrideMask(), lookScene.GetCinematicRenderConfig() );
    if ( SceneState().isSceneMode )
    {
        SceneState().hasCinematicRenderingOverride = lookScene.HasCinematicRenderingOverride();
        SceneState().isCinematicRenderingEnabled = lookScene.IsCinematicRenderingEnabled();
        SceneState().hasCinematicExposure = lookScene.HasCinematicExposure();
        SceneState().cinematicExposure = lookScene.GetCinematicExposure();
        SceneState().hasCinematicGamma = lookScene.HasCinematicGamma();
        SceneState().cinematicGamma = lookScene.GetCinematicGamma();
        SceneState().cinematicOverrideMask = lookScene.GetCinematicOverrideMask();
        SceneState().uiCinematicOverrideMask = 0;
    }
    applyObjectMaterials( lookScene );
    m_selectedCineModeSceneIndex = index;
    return true;
}


void SkullbonezRun::ApplyLiveStyleScene( const TestScene& styleScene )
{
    m_cmdHasCinematicRenderingOverride = false;

    for ( int modelIndex = 0; modelIndex < m_cGameModelCollection.GetModelCount(); ++modelIndex )
    {
        m_cGameModelCollection.GetModelAtIndex( modelIndex ).SetRenderTint( 1.0f, 1.0f, 1.0f, 0.0f );
    }

    for ( int materialIndex = 0; materialIndex < styleScene.GetObjectMaterialOverrideCount(); ++materialIndex )
    {
        const SceneObjectMaterialOverride& material = styleScene.GetObjectMaterialOverride( materialIndex );
        for ( int modelIndex = 0; modelIndex < m_cGameModelCollection.GetModelCount(); ++modelIndex )
        {
            GameModel& model = m_cGameModelCollection.GetModelAtIndex( modelIndex );
            if ( SceneMaterialTargetMatches( material, model ) )
            {
                model.SetRenderMaterial( material.material );
            }
        }
    }

    CinematicRenderConfig& cinematic = ActiveCinematicConfig();
    cinematic = m_defaultCinematicRender;
    ApplyCinematicSceneOverrides( cinematic, styleScene.GetCinematicOverrideMask(), styleScene.GetCinematicRenderConfig() );
    if ( SceneState().isSceneMode )
    {
        SceneState().hasCinematicRenderingOverride = styleScene.HasCinematicRenderingOverride();
        SceneState().isCinematicRenderingEnabled = styleScene.IsCinematicRenderingEnabled();
        SceneState().hasCinematicExposure = styleScene.HasCinematicExposure();
        SceneState().cinematicExposure = styleScene.GetCinematicExposure();
        SceneState().hasCinematicGamma = styleScene.HasCinematicGamma();
        SceneState().cinematicGamma = styleScene.GetCinematicGamma();
        SceneState().cinematicOverrideMask = styleScene.GetCinematicOverrideMask();
        SceneState().uiCinematicOverrideMask = 0;
    }
    m_selectedCineModeSceneIndex = -1;
}


void SkullbonezRun::ApplyDemoHeroStyleOverride()
{
    if ( !m_cmdDemoHeroStyle || SceneState().isSceneMode )
    {
        return;
    }

    const std::string stylePath = std::string( DATA_ROOT ) + "styles/low_poly_art_style.style";
    const TestScene styleScene = TestScene::LoadStyleFromFile( stylePath.c_str() );
    ApplyLiveStyleScene( styleScene );
    printf( "[scene] Applied low-poly hero rendering mode to generated demo scene.\n" );
}


bool SkullbonezRun::ApplyAdjacentCinematicMode( int direction )
{
    if ( direction == 0 )
    {
        return false;
    }

    std::vector<int> cineIndices;
    cineIndices.reserve( m_sceneBrowserPaths.size() );
    int currentPosition = -1;
    for ( int i = 0; i < static_cast<int>( m_sceneBrowserPaths.size() ); ++i )
    {
        if ( IsCineScenePath( m_sceneBrowserPaths[i] ) )
        {
            if ( i == m_selectedCineModeSceneIndex )
            {
                currentPosition = static_cast<int>( cineIndices.size() );
            }
            cineIndices.push_back( i );
        }
    }

    if ( cineIndices.empty() )
    {
        return false;
    }

    const int currentSceneIndex = CurrentSceneBrowserIndex();
    if ( currentPosition < 0 && currentSceneIndex >= 0 && IsCineScenePath( m_sceneBrowserPaths[currentSceneIndex] ) )
    {
        for ( int i = 0; i < static_cast<int>( cineIndices.size() ); ++i )
        {
            if ( cineIndices[i] == currentSceneIndex )
            {
                currentPosition = i;
                break;
            }
        }
    }

    const bool cineContext = currentPosition >= 0 || m_selectedCineModeSceneIndex >= 0 || m_UI.GetActiveTab() == InGameUITab::Cinematic;
    if ( !cineContext )
    {
        return false;
    }

    const int cineCount = static_cast<int>( cineIndices.size() );
    const int nextPosition = currentPosition < 0
                                 ? ( direction < 0 ? cineCount - 1 : 0 )
                                 : ( currentPosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;
    return ApplyCinematicModeFromBrowserIndex( cineIndices[nextPosition] );
}


void SkullbonezRun::LoadAdjacentSceneFromBrowser( int direction )
{
    SceneRuntime& runtime = m_sceneRuntime;
    if ( direction == 0 )
    {
        return;
    }

    if ( runtime.CurrentQueueIsCinematicDeck() )
    {
        LoadScene( runtime.AdjacentQueueIndex( direction ), true, true );
        return;
    }

    const int sceneCount = static_cast<int>( m_sceneBrowserPaths.size() );
    if ( sceneCount <= 0 )
    {
        return;
    }

    const int currentIndex = CurrentSceneBrowserIndex();
    if ( currentIndex >= 0 && IsCineScenePath( m_sceneBrowserPaths[currentIndex] ) )
    {
        std::vector<int> cineIndices;
        cineIndices.reserve( m_sceneBrowserPaths.size() );
        int currentCinePosition = -1;
        for ( int i = 0; i < sceneCount; ++i )
        {
            if ( IsCineScenePath( m_sceneBrowserPaths[i] ) )
            {
                if ( i == currentIndex )
                {
                    currentCinePosition = static_cast<int>( cineIndices.size() );
                }
                cineIndices.push_back( i );
            }
        }
        if ( !cineIndices.empty() && currentCinePosition >= 0 )
        {
            const int cineCount = static_cast<int>( cineIndices.size() );
            const int nextCinePosition = ( currentCinePosition + ( direction < 0 ? -1 : 1 ) + cineCount ) % cineCount;
            LoadSceneFromBrowserIndex( cineIndices[nextCinePosition] );
            return;
        }
    }

    int nextIndex = 0;
    if ( currentIndex < 0 )
    {
        nextIndex = direction < 0 ? sceneCount - 1 : 0;
    }
    else
    {
        nextIndex = ( currentIndex + ( direction < 0 ? -1 : 1 ) + sceneCount ) % sceneCount;
    }

    LoadSceneFromBrowserIndex( nextIndex );
}


void SkullbonezRun::ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
{
    SceneRuntime& runtime = m_sceneRuntime;
    if ( !runtime.HasCurrentEntry() )
    {
        return;
    }

    runtime.MarkManualReset();
    LoadScene( runtime.CurrentIndex(), preserveUIState, suppressExitOnComplete, preserveRuntimeState );
}


void SkullbonezRun::ApplyUIModelCountOverride( int count )
{
    m_UIModelCountOverride = std::clamp( count, 0, ActiveGameModelCapacity() );
    m_UISolverBallCountOverride = -1;
    m_UISolverBoxCountOverride = -1;
    if ( !HasCurrentSceneQueueEntry() )
    {
        return;
    }

    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }
    m_cGameModelCollection.Clear();
    ResetProjectilePool();
    m_simulation.Reset();
    SceneState().currentFrame = 0;
    SceneState().isTestComplete = false;
    if ( m_UIModelCountOverride <= 0 )
    {
        SceneState().modelCount = 0;
        m_camera.trackBallIndex = -1;
        PROFILE_SCHEDULE_RESET();
        return;
    }

    const unsigned int seed = SceneState().rngSeed > 0 ? SceneState().rngSeed : 1u;
    SceneState().rngState = seed;
    SetUpGameModels( m_UIModelCountOverride );
    if ( m_camera.trackBallIndex >= m_UIModelCountOverride )
    {
        m_camera.trackBallIndex = m_UIModelCountOverride - 1;
    }
    PROFILE_SCHEDULE_RESET();
}


void SkullbonezRun::ApplyUISolverObjectCounts( int balls, int boxes )
{
    const int modelCapacity = ActiveGameModelCapacity();
    balls = std::clamp( balls, 0, modelCapacity );
    boxes = std::clamp( boxes, 0, modelCapacity );
    if ( balls + boxes > modelCapacity )
    {
        boxes = (std::max)( 0, modelCapacity - balls );
    }
    m_UISolverBallCountOverride = balls;
    m_UISolverBoxCountOverride = boxes;
    m_UIModelCountOverride = -1;
    if ( !HasCurrentSceneQueueEntry() )
    {
        return;
    }

    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }
    m_cGameModelCollection.Clear();
    ResetProjectilePool();
    m_simulation.Reset();
    SceneState().currentFrame = 0;
    SceneState().isTestComplete = false;

    const unsigned int seed = SceneState().rngSeed > 0 ? SceneState().rngSeed : 1u;
    SceneState().rngState = seed;
    SetUpSolverObjects( m_UISolverBallCountOverride, m_UISolverBoxCountOverride );
    if ( SceneState().modelCount <= 0 )
    {
        m_camera.trackBallIndex = -1;
    }
    else if ( m_camera.trackBallIndex >= SceneState().modelCount )
    {
        m_camera.trackBallIndex = SceneState().modelCount - 1;
    }
    PROFILE_SCHEDULE_RESET();
}


void SkullbonezRun::ApplyUIWorldOverride( float gravity, float fluidHeight, float fluidDensity )
{
    m_cWorldEnvironment.SetGravity( gravity );
    m_cWorldEnvironment.SetFluidSurfaceHeight( fluidHeight );
    m_cWorldEnvironment.SetFluidDensity( fluidDensity );
}


void SkullbonezRun::ApplyNoWaterOverride()
{
    if ( !m_cmdNoWater || !m_systems.terrain )
    {
        return;
    }

    m_cWorldEnvironment.SetFluidSurfaceHeight( m_systems.terrain->GetMinHeight() - NO_WATER_TERRAIN_CLEARANCE );
}


void SkullbonezRun::ApplyTornadoDefaultsForActiveScene()
{
    Physics::TornadoFieldConfig field = m_runtimeSettings.tornadoField;
    const CinematicRenderConfig& cinematic = ActiveCinematicConfig();
    const float basinRadius = (std::max)( cinematic.basinRadiusX, cinematic.basinRadiusZ );

    field.center = Vector3( cinematic.basinCenterX,
                            m_cWorldEnvironment.GetFluidSurfaceHeight(),
                            cinematic.basinCenterZ );
    field.radius = std::clamp( basinRadius * 1.08f, 150.0f, 280.0f );
    field.height = (std::max)( 130.0f, field.radius * 0.66f );
    field.inwardAcceleration = 120.0f;
    field.swirlAcceleration = 170.0f;
    field.liftAcceleration = 78.0f;
    m_runtimeSettings.tornadoField = field;
}


void SkullbonezRun::SyncTornadoFieldToPhysics()
{
    m_cGameModelCollection.SetTornadoFieldConfig( m_runtimeSettings.tornadoField );
}


void SkullbonezRun::UseDefaultTerrain()
{
    if ( !m_systems.terrain || m_systems.isFlatSlopeTerrain )
    {
        if ( IsGfxReady() )
        {
            Gfx().FlushGPU();
        }
        const std::string terrainRawPath = ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain, "terrain.raw", Cfg().terrainRaw );
        m_systems.terrain = std::make_unique<Terrain>( terrainRawPath.c_str(), 256, 8, 15 );
        m_systems.isFlatSlopeTerrain = false;
    }

    UpdateWorldTerrainBounds();
}


void SkullbonezRun::UseFlatSlopeTerrain( float baseY, float slopeX, float slopeZ )
{
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }
    m_systems.terrain = std::make_unique<Terrain>( baseY, slopeX, slopeZ );
    m_systems.isFlatSlopeTerrain = true;

    UpdateWorldTerrainBounds();
}


void SkullbonezRun::UpdateWorldTerrainBounds()
{
    if ( !m_systems.terrain )
    {
        return;
    }

    XZBounds tb = m_systems.terrain->GetXZBounds();
    m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
}


bool SkullbonezRun::AdvanceScene()
{
    SceneRuntime& runtime = m_sceneRuntime;
    const bool preserveInteractiveUI = SceneState().isInteractiveRun;

    // For perf tests with 2 passes, the second pass re-runs the same scene
    if ( m_perfLogState.isPerfTest && sPerfPass == 0 )
    {
        sPerfPass = 1;
        LoadScene( runtime.CurrentIndex(), preserveInteractiveUI, preserveInteractiveUI, preserveInteractiveUI );
        return true;
    }

    // Reset perf pass counter for next scene
    sPerfPass = 0;

    const int nextIndex = runtime.NextIndex();
    if ( !runtime.HasEntry( nextIndex ) )
    {
        return false;
    }

    LoadScene( nextIndex, preserveInteractiveUI, preserveInteractiveUI );
    return true;
}
