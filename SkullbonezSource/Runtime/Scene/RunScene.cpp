/*
File: SkullbonezSource/Runtime/Scene/RunScene.cpp
Purpose:
  Loads, resets, and advances authored and generated scenes.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"
#include "SceneRuntimeLoad.h"
#include "SceneRuntimeReset.h"
#include "../Editor/EditorHullAssets.h"
#include "../../Physics/ObjectContactManifold.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/WorkerPool.h"

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
using Json = nlohmann::ordered_json;
constexpr float SCENE_EDITOR_TEXTURE_MODE_INVERTED = -2.0f;

void ApplySceneWorkerThreadSetting( int requestedWorkerThreads )
{
    const int clampedWorkerThreads =
        std::clamp( requestedWorkerThreads, -1, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
    SkullbonezCore::Threading::WorkerPool& workerPool = SkullbonezCore::Threading::WorkerPool::Instance();
    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    Cfg().workerThreads = clampedWorkerThreads;
    if ( workerPool.GetThreadCount() != resolvedWorkerThreads )
    {
        workerPool.Initialise( clampedWorkerThreads );
    }
}

Quaternion MakeSceneEulerQuaternion( float eulerXDeg, float eulerYDeg, float eulerZDeg )
{
    static constexpr float DEG2RAD = 3.14159265f / 180.0f;
    const float xHalf = eulerXDeg * DEG2RAD * 0.5f;
    const float yHalf = eulerYDeg * DEG2RAD * 0.5f;
    const float zHalf = eulerZDeg * DEG2RAD * 0.5f;

    const Quaternion xRotation( sinf( xHalf ), 0.0f, 0.0f, cosf( xHalf ) );
    const Quaternion yRotation( 0.0f, sinf( yHalf ), 0.0f, cosf( yHalf ) );
    const Quaternion zRotation( 0.0f, 0.0f, sinf( zHalf ), cosf( zHalf ) );

    Quaternion orientation;
    orientation *= xRotation * yRotation * zRotation;
    orientation.Normalise();
    return orientation;
}


bool SceneNameEndsWithPartSuffix( const char* name, const char* suffix )
{
    if ( !name || !suffix )
    {
        return false;
    }
    const size_t nameLength = strlen( name );
    const size_t suffixLength = strlen( suffix );
    if ( nameLength <= suffixLength || name[nameLength - suffixLength - 1] != '_' )
    {
        return false;
    }
    return strcmp( name + nameLength - suffixLength, suffix ) == 0;
}


bool IsSimpleRagdollPartName( const char* name )
{
    static const char* partSuffixes[] = {
        "torso",
        "head",
        "upper_arm_l",
        "lower_arm_l",
        "upper_arm_r",
        "lower_arm_r",
        "upper_leg_l",
        "lower_leg_l",
        "upper_leg_r",
        "lower_leg_r",
    };
    for ( const char* suffix : partSuffixes )
    {
        if ( SceneNameEndsWithPartSuffix( name, suffix ) )
        {
            return true;
        }
    }
    return false;
}


bool TryGetSimpleRagdollPartPrefixLength( const char* name, const char* suffix, size_t& outPrefixLength )
{
    outPrefixLength = 0;
    if ( !SceneNameEndsWithPartSuffix( name, suffix ) )
    {
        return false;
    }

    outPrefixLength = strlen( name ) - strlen( suffix );
    return true;
}


bool IsSimpleRagdollNeckJointName( const char* bodyA, const char* bodyB )
{
    size_t torsoPrefixLength = 0;
    size_t headPrefixLength = 0;
    return TryGetSimpleRagdollPartPrefixLength( bodyA, "torso", torsoPrefixLength ) &&
           TryGetSimpleRagdollPartPrefixLength( bodyB, "head", headPrefixLength ) &&
           torsoPrefixLength == headPrefixLength && strncmp( bodyA, bodyB, torsoPrefixLength ) == 0;
}


bool SceneMaterialTargetMatches( const SceneObjectMaterialOverride& material, const GameModel& model )
{
    if ( IsSimpleRagdollPartName( model.GetName() ) )
    {
        return false;
    }
    if ( strcmp( material.target, "all" ) == 0 )
    {
        return true;
    }
    if ( strcmp( material.target, "balls" ) == 0 )
    {
        return model.IsSphere();
    }
    if ( strcmp( material.target, "boxes" ) == 0 )
    {
        return model.IsBox();
    }
    if ( strcmp( material.target, "hulls" ) == 0 || strcmp( material.target, "convex_hulls" ) == 0 )
    {
        return model.IsConvexHull();
    }
    if ( strncmp( material.target, "prefix:", 7 ) == 0 )
    {
        return strncmp( model.GetName(), material.target + 7, strlen( material.target + 7 ) ) == 0;
    }
    return strcmp( material.target, model.GetName() ) == 0;
}

bool SceneNameStartsWith( const char* name, const char* prefix )
{
    return name && strncmp( name, prefix, strlen( prefix ) ) == 0;
}

bool IsEditorPlacedSphereName( const char* name )
{
    return SceneNameStartsWith( name, "static_ball_" ) || SceneNameStartsWith( name, "dynamic_ball_" ) ||
           SceneNameStartsWith( name, "sleeping_ball_" ) || SceneNameStartsWith( name, "static_sphere_" ) ||
           SceneNameStartsWith( name, "dynamic_sphere_" ) || SceneNameStartsWith( name, "sleeping_sphere_" );
}

void ApplyEditorPlacedSphereMaterial( GameModel& model )
{
    if ( IsEditorPlacedSphereName( model.GetName() ) )
    {
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, SCENE_EDITOR_TEXTURE_MODE_INVERTED );
    }
}

bool IsCineScenePath( const std::string& path )
{
    const char* name = FileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}

bool IsSceneJsonFile( const std::filesystem::path& path )
{
    const std::string name = path.filename().string();
    return name.size() > 11 && name.compare( name.size() - 11, 11, ".scene.json" ) == 0;
}

bool IsSceneNameChar( char value )
{
    return ( value >= 'a' && value <= 'z' ) || ( value >= 'A' && value <= 'Z' ) || ( value >= '0' && value <= '9' ) ||
           value == '-' || value == '_';
}

std::string SanitizeSceneFileName( const char* requestedName )
{
    std::string clean;
    if ( requestedName )
    {
        for ( const char* cursor = requestedName; *cursor != '\0' && clean.size() < 48; ++cursor )
        {
            const char value = *cursor;
            if ( IsSceneNameChar( value ) )
            {
                clean.push_back( value );
            }
            else if ( value == ' ' || value == '.' )
            {
                clean.push_back( '_' );
            }
        }
    }

    while ( !clean.empty() && clean.front() == '_' )
    {
        clean.erase( clean.begin() );
    }
    while ( !clean.empty() && clean.back() == '_' )
    {
        clean.pop_back();
    }
    return clean;
}

std::filesystem::path UniqueScenePath( const std::filesystem::path& sceneDir, const std::string& baseName )
{
    std::filesystem::path candidate = sceneDir / ( baseName + ".scene.json" );
    if ( !std::filesystem::exists( candidate ) )
    {
        return candidate;
    }

    for ( int suffix = 2; suffix < 1000; ++suffix )
    {
        char numberedName[80] = {};
        snprintf( numberedName, sizeof( numberedName ), "%s_%02d.scene.json", baseName.c_str(), suffix );
        candidate = sceneDir / numberedName;
        if ( !std::filesystem::exists( candidate ) )
        {
            return candidate;
        }
    }
    return std::filesystem::path();
}

bool WriteStarterSceneFile( const std::filesystem::path& path, const std::string& displayName )
{
    std::ofstream output( path, std::ios::trunc );
    if ( !output )
    {
        return false;
    }

    Json scene;
    scene["format"] = "skullbonez.scene.json";
    scene["version"] = 1;
    scene["name"] = displayName;
    scene["simulation"] = {
        { "physics", true },
        { "text", true },
        { "world",
          {
              { "gravity", -9.81f },
              { "fluidHeight", 0.0f },
              { "fluidDensity", 0.0f },
          } },
    };
    scene["editor"] = {
        { "editableScene", true },
    };
    scene["playback"] = {
        { "frames", "unlimited" },
        { "fixedStep", true },
    };
    scene["debug"] = {
        { "waterHidden", true },
    };
    scene["terrain"] = {
        { "flatSlope",
          {
              { "baseY", 30.0f },
              { "slopeX", 0.0f },
              { "slopeZ", 0.0f },
          } },
    };
    scene["cameras"] = Json::array( {
        {
            { "name", "main" },
            { "position", Json::array( { 500.0f, 120.0f, 760.0f } ) },
            { "view", Json::array( { 500.0f, 45.0f, 500.0f } ) },
            { "up", Json::array( { 0.0f, 1.0f, 0.0f } ) },
        },
    } );
    scene["objects"] = Json::array();
    output << scene.dump( 2 ) << '\n';
    return output.good();
}

Json& EnsureJsonObject( Json& parent, const char* key )
{
    Json& child = parent[key];
    if ( !child.is_object() )
    {
        child = Json::object();
    }
    return child;
}

void SetTouchedCinematicSceneProperties( Json& root, uint64_t touchedMask, const CinematicRenderConfig& c )
{
    // Concept: save only values the UI actually touched.
    //
    // Scene JSON can include reusable style files plus a few local overrides.
    // The touched mask prevents "Save Defaults" from expanding every engine.cfg
    // or style default into the scene file.
    if ( touchedMask == 0 )
    {
        return;
    }

    Json& cinematic = EnsureJsonObject( root, "cinematic" );
    const auto writeBool = [&]( uint64_t bit, const char* key, bool value )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            cinematic[key] = value;
        }
    };
    const auto writeFloat = [&]( uint64_t bit, const char* key, float value )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            cinematic[key] = value;
        }
    };
    const auto writeInt = [&]( uint64_t bit, const char* key, int value )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            cinematic[key] = value;
        }
    };

    writeBool( SCENE_CINE_RENDERING, "rendering", c.enabled );
    writeBool( SCENE_CINE_SKY_ATMOSPHERE, "skyAtmosphere", c.skyAtmosphereEnabled );
    writeBool( SCENE_CINE_CLOUDS, "clouds", c.cloudsEnabled );
    writeBool( SCENE_CINE_GOD_RAYS, "godRays", c.godRaysEnabled );
    writeBool( SCENE_CINE_VOLUMETRIC_LIGHTING, "volumetricLighting", c.volumetricLightingEnabled );
    writeBool( SCENE_CINE_BLOOM, "bloom", c.bloomEnabled );
    writeBool( SCENE_CINE_FOG, "fog", c.fogEnabled );
    writeBool( SCENE_CINE_TERRAIN_RELIEF_ENABLED, "terrainReliefEnabled", c.terrainReliefEnabled );

    writeFloat( SCENE_CINE_EXPOSURE, "exposure", c.exposure );
    writeFloat( SCENE_CINE_GAMMA, "gamma", c.gamma );
    writeFloat( SCENE_CINE_SUN_SCREEN_X, "sunScreenX", c.sunScreenX );
    writeFloat( SCENE_CINE_SUN_SCREEN_Y, "sunScreenY", c.sunScreenY );
    writeFloat( SCENE_CINE_SUN_COLOR_R, "sunColorR", c.sunColorR );
    writeFloat( SCENE_CINE_SUN_COLOR_G, "sunColorG", c.sunColorG );
    writeFloat( SCENE_CINE_SUN_COLOR_B, "sunColorB", c.sunColorB );
    writeFloat( SCENE_CINE_SUN_INTENSITY, "sunIntensity", c.sunIntensity );
    writeFloat( SCENE_CINE_SKY_HORIZON_R, "skyHorizonR", c.skyHorizonR );
    writeFloat( SCENE_CINE_SKY_HORIZON_G, "skyHorizonG", c.skyHorizonG );
    writeFloat( SCENE_CINE_SKY_HORIZON_B, "skyHorizonB", c.skyHorizonB );
    writeFloat( SCENE_CINE_SKY_ZENITH_R, "skyZenithR", c.skyZenithR );
    writeFloat( SCENE_CINE_SKY_ZENITH_G, "skyZenithG", c.skyZenithG );
    writeFloat( SCENE_CINE_SKY_ZENITH_B, "skyZenithB", c.skyZenithB );
    writeFloat( SCENE_CINE_SKY_GLOW_STRENGTH, "skyGlowStrength", c.skyGlowStrength );
    writeFloat( SCENE_CINE_CLOUD_COVERAGE, "cloudCoverage", c.cloudCoverage );
    writeFloat( SCENE_CINE_CLOUD_SOFTNESS, "cloudSoftness", c.cloudSoftness );
    writeFloat( SCENE_CINE_CLOUD_SCALE, "cloudScale", c.cloudScale );
    writeFloat( SCENE_CINE_CLOUD_INTENSITY, "cloudIntensity", c.cloudIntensity );
    writeFloat( SCENE_CINE_SUN_SHAFT_STRENGTH, "sunShaftStrength", c.sunShaftStrength );
    writeFloat( SCENE_CINE_SUN_SHAFT_FALLOFF, "sunShaftFalloff", c.sunShaftFalloff );
    writeFloat( SCENE_CINE_VOLUMETRIC_STRENGTH, "volumetricStrength", c.volumetricStrength );
    writeFloat( SCENE_CINE_VOLUMETRIC_DENSITY, "volumetricDensity", c.volumetricDensity );
    writeFloat( SCENE_CINE_VOLUMETRIC_DECAY, "volumetricDecay", c.volumetricDecay );
    writeFloat( SCENE_CINE_BLOOM_THRESHOLD, "bloomThreshold", c.bloomThreshold );
    writeFloat( SCENE_CINE_BLOOM_KNEE, "bloomKnee", c.bloomKnee );
    writeFloat( SCENE_CINE_BLOOM_STRENGTH, "bloomStrength", c.bloomStrength );
    writeFloat( SCENE_CINE_BLOOM_RADIUS, "bloomRadius", c.bloomRadius );
    writeFloat( SCENE_CINE_TERRAIN_RELIEF, "terrainRelief", c.terrainRelief );
    writeFloat( SCENE_CINE_BASIN_DEPTH, "basinDepth", c.basinDepth );
    writeFloat( SCENE_CINE_BASIN_RIM_LIFT, "basinRimLift", c.basinRimLift );
    writeBool( SCENE_CINE_SHADOWS, "shadows", c.shadowsEnabled );
    writeInt( SCENE_CINE_SHADOW_MAP_SIZE, "shadowMapSize", c.shadowMapSize );
    writeInt( SCENE_CINE_SHADOW_PCF_RADIUS, "shadowPcfRadius", c.shadowPcfRadius );
    writeFloat( SCENE_CINE_SHADOW_STRENGTH, "shadowStrength", c.shadowStrength );
    writeFloat( SCENE_CINE_SHADOW_SOFTNESS, "shadowSoftness", c.shadowSoftness );
    writeFloat( SCENE_CINE_SHADOW_DEPTH_BIAS, "shadowDepthBias", c.shadowDepthBias );
    writeFloat( SCENE_CINE_SHADOW_SLOPE_BIAS, "shadowSlopeBias", c.shadowSlopeBias );
    writeFloat( SCENE_CINE_SHADOW_MAX_DISTANCE, "shadowMaxDistance", c.shadowMaxDistance );
    writeFloat( SCENE_CINE_FOG_COLOR_R, "fogColorR", c.fogColorR );
    writeFloat( SCENE_CINE_FOG_COLOR_G, "fogColorG", c.fogColorG );
    writeFloat( SCENE_CINE_FOG_COLOR_B, "fogColorB", c.fogColorB );
    writeFloat( SCENE_CINE_FOG_START, "fogStart", c.fogStart );
    writeFloat( SCENE_CINE_FOG_END, "fogEnd", c.fogEnd );
    writeFloat( SCENE_CINE_FOG_DENSITY, "fogDensity", c.fogDensity );
    writeFloat( SCENE_CINE_FOG_MAX_OPACITY, "fogMaxOpacity", c.fogMaxOpacity );

    if ( ( touchedMask & SCENE_CINE_STYLE_MODES ) != 0 )
    {
        cinematic["styleModes"] = Json::array( { c.skyMode, c.terrainMode, c.objectStyle, c.waterMode } );
    }
    if ( ( touchedMask & SCENE_CINE_STYLE_GRADE ) != 0 )
    {
        cinematic["styleGrade"] = Json::array( { c.styleSaturation, c.styleContrast, c.styleVignette } );
    }
    if ( ( touchedMask & SCENE_CINE_TERRAIN_TINT ) != 0 )
    {
        cinematic["terrainTint"] = Json::array( { c.terrainTintR, c.terrainTintG, c.terrainTintB } );
    }
    if ( ( touchedMask & SCENE_CINE_TERRAIN_ACCENT ) != 0 )
    {
        cinematic["terrainAccent"] = Json::array( { c.terrainAccentR, c.terrainAccentG, c.terrainAccentB } );
    }
    if ( ( touchedMask & SCENE_CINE_TERRAIN_GRID ) != 0 )
    {
        cinematic["terrainGrid"] = Json::array( { c.terrainGridScale, c.terrainGridStrength } );
    }
    if ( ( touchedMask & SCENE_CINE_WATER_TINT ) != 0 )
    {
        cinematic["waterTint"] = Json::array( { c.waterTintR, c.waterTintG, c.waterTintB } );
    }
    if ( ( touchedMask & SCENE_CINE_WATER_PROFILE ) != 0 )
    {
        cinematic["waterProfile"] = Json::array( { c.waterAlpha, c.waterReflectionStrength, c.waterGlintStrength } );
    }
    if ( ( touchedMask & SCENE_CINE_BASIN_MASK ) != 0 )
    {
        cinematic["basinMask"] =
            Json::array( { c.basinCenterX, c.basinCenterZ, c.basinRadiusX, c.basinRadiusZ, c.basinFeather } );
    }
}

const char* WaterReflectionJsonValue( bool noReflect, bool rtReflect )
{
    if ( noReflect )
    {
        return "none";
    }
    return rtReflect ? "dxr" : "fbo";
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
                      lines[sectionBody].find(
                          "# ---------------------------------------------------------------------------" ) !=
                          std::string::npos ) )
            {
                ++sectionBody;
            }

            for ( size_t j = sectionBody; j < lines.size(); ++j )
            {
                if ( lines[j].find( "# ---------------------------------------------------------------------------" ) !=
                     std::string::npos )
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

size_t CinematicConfigInsertIndex( const std::vector<std::string>& lines )
{
    for ( size_t i = 0; i < lines.size(); ++i )
    {
        if ( lines[i].find( "Cinematic rendering" ) != std::string::npos )
        {
            size_t sectionBody = i + 1;
            while ( sectionBody < lines.size() &&
                    ( lines[sectionBody].empty() ||
                      lines[sectionBody].find(
                          "# ---------------------------------------------------------------------------" ) !=
                          std::string::npos ) )
            {
                ++sectionBody;
            }

            for ( size_t j = sectionBody; j < lines.size(); ++j )
            {
                if ( lines[j].find( "# ---------------------------------------------------------------------------" ) !=
                     std::string::npos )
                {
                    return j;
                }
            }
            return lines.size();
        }
    }

    for ( size_t i = 0; i < lines.size(); ++i )
    {
        if ( lines[i].find( "Physics" ) != std::string::npos )
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
    const bool hasOrdinarySection =
        std::any_of( lines.begin(),
                     lines.end(),
                     []( const std::string& line ) { return line.find( "Ordinary rendering" ) != std::string::npos; } );
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

void AppendMissingCinematicConfigLines( std::vector<std::string>& lines, std::vector<std::string>& missing )
{
    if ( missing.empty() )
    {
        return;
    }

    std::vector<std::string> insertLines;
    const bool hasCinematicSection = std::any_of( lines.begin(),
                                                  lines.end(),
                                                  []( const std::string& line )
                                                  { return line.find( "Cinematic rendering" ) != std::string::npos; } );
    if ( !hasCinematicSection )
    {
        insertLines.push_back( "# ---------------------------------------------------------------------------" );
        insertLines.push_back( "# Cinematic rendering" );
        insertLines.push_back( "# ---------------------------------------------------------------------------" );
    }
    insertLines.insert( insertLines.end(), missing.begin(), missing.end() );
    insertLines.push_back( "" );

    const size_t insertIndex = CinematicConfigInsertIndex( lines );
    lines.insert( lines.begin() + static_cast<std::ptrdiff_t>( insertIndex ), insertLines.begin(), insertLines.end() );
}
} // namespace

SceneAuthoredCameraContext Run::BuildSceneAuthoredCameraContext()
{
    return SceneAuthoredCameraContext{ m_systems.cameras, *m_systems.terrain };
}

SceneAuthoredModelContext Run::BuildSceneAuthoredModelContext()
{
    return SceneAuthoredModelContext{ SceneState(),
                                      m_cWorldEnvironment,
                                      m_systems.terrain.get(),
                                      m_cGameModelCollection,
                                      m_cGameModelCollection.GetPhysicsEngine(),
                                      m_requiredSceneContacts,
                                      m_requiredBroadphaseXCells };
}

SceneGeneratedCameraContext Run::BuildSceneGeneratedCameraContext()
{
    return SceneGeneratedCameraContext{ m_systems.cameras, *m_systems.terrain };
}

SceneGeneratedModelContext Run::BuildSceneGeneratedModelContext()
{
    return SceneGeneratedModelContext{ SceneState(),
                                       Cfg(),
                                       m_cWorldEnvironment,
                                       m_systems.terrain.get(),
                                       m_cGameModelCollection,
                                       m_cGameModelCollection.GetPhysicsEngine(),
                                       m_launchOptions.generatedObjectTypeOverride };
}

void Run::UpdateRequiredSceneContacts()
{
    if ( m_requiredSceneContacts.empty() )
    {
        return;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( RunRequiredContactState& required : m_requiredSceneContacts )
    {
        if ( required.touched || required.bodyA < 0 || required.bodyB < 0 )
        {
            continue;
        }

        ObjectContactManifold manifold;
        if ( BuildObjectContactManifold( models[static_cast<size_t>( required.bodyA )],
                                         models[static_cast<size_t>( required.bodyB )],
                                         required.bodyA,
                                         required.bodyB,
                                         Cfg().contactEpsilon + 0.25f,
                                         manifold ) )
        {
            required.touched = true;
        }
    }

    const std::vector<PhysicsDebugContact>& contacts = m_cGameModelCollection.GetPhysicsDebugContacts();
    for ( const PhysicsDebugContact& contact : contacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 )
        {
            continue;
        }
        for ( RunRequiredContactState& required : m_requiredSceneContacts )
        {
            if ( required.touched || required.bodyA < 0 || required.bodyB < 0 )
            {
                continue;
            }
            const bool sameOrder = contact.bodyA == required.bodyA && contact.bodyB == required.bodyB;
            const bool swappedOrder = contact.bodyA == required.bodyB && contact.bodyB == required.bodyA;
            if ( sameOrder || swappedOrder )
            {
                required.touched = true;
                break;
            }
        }
    }
}


bool Run::RequiredSceneContactsComplete() const
{
    for ( const RunRequiredContactState& contact : m_requiredSceneContacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
        {
            return false;
        }
    }
    return true;
}


void Run::UpdateRequiredSceneBroadphaseXCells( const SpatialGrid::ActiveCell* activeCells, int activeCellCount )
{
    if ( m_requiredBroadphaseXCells.empty() || !activeCells || activeCellCount <= 0 )
    {
        return;
    }

    for ( RunRequiredBroadphaseXCellsState& required : m_requiredBroadphaseXCells )
    {
        if ( required.activated )
        {
            continue;
        }

        required.lastActiveCellCount = activeCellCount;
        required.lastMissingCellX = -1;
        required.hasObservedXRange = false;
        for ( int i = 0; i < activeCellCount; ++i )
        {
            const SpatialGrid::ActiveCell& active = activeCells[i];
            if ( active.iy == required.cellY && active.iz == required.cellZ )
            {
                if ( !required.hasObservedXRange )
                {
                    required.lastObservedMinX = active.ix;
                    required.lastObservedMaxX = active.ix;
                    required.hasObservedXRange = true;
                }
                else
                {
                    required.lastObservedMinX = (std::min)( required.lastObservedMinX, static_cast<int>( active.ix ) );
                    required.lastObservedMaxX = (std::max)( required.lastObservedMaxX, static_cast<int>( active.ix ) );
                }
            }
        }

        bool allActive = true;
        for ( int x = required.minCellX; x <= required.maxCellX; ++x )
        {
            bool found = false;
            for ( int i = 0; i < activeCellCount; ++i )
            {
                const SpatialGrid::ActiveCell& active = activeCells[i];
                if ( active.ix == x && active.iy == required.cellY && active.iz == required.cellZ )
                {
                    found = true;
                    break;
                }
            }

            if ( !found )
            {
                allActive = false;
                required.lastMissingCellX = x;
                break;
            }
        }

        if ( allActive )
        {
            required.activated = true;
        }
    }
}


bool Run::RequiredSceneBroadphaseXCellsComplete() const
{
    for ( const RunRequiredBroadphaseXCellsState& required : m_requiredBroadphaseXCells )
    {
        if ( !required.activated )
        {
            return false;
        }
    }
    return true;
}


void Run::LoadScene( int index, bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
{
    SceneController& runtime = m_sceneController;
    SceneRuntimeResetContext resetContext{ m_runtimeSettings,
                                           m_debug,
                                           SceneState(),
                                           m_sceneUIOverrides,
                                           m_camera,
                                           m_cWorldEnvironment,
                                           m_physicsDebugVisualizer };
    SceneRuntimeLoadBeginContext loadBeginContext{ runtime,
                                                   resetContext,
                                                   m_sceneBrowser,
                                                   IsGfxReady() ? &Gfx() : nullptr,
                                                   m_launchOptions.interactiveSceneRun };
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "scene_reload" );
#endif
    const SceneRuntimeLoadBeginResult loadBegin =
        BeginSceneRuntimeLoad( loadBeginContext, index, suppressExitOnComplete, preserveRuntimeState );
    if ( !loadBegin.shouldLoad )
    {
        return;
    }

    const bool suppressAutomationExit = loadBegin.suppressAutomationExit;
    const bool shouldPreserveRuntimeState = loadBegin.shouldPreserveRuntimeState;
    const SceneRuntimeResetSnapshot& resetSnapshot = loadBegin.resetSnapshot;
    const std::string& scenePath = *loadBegin.scenePath;

    m_diagnosticsRuntime.ClosePerfLogWithMemoryCheckpoint( sPerfPass + 1, "end" );

    // Reset scene-local state; operator HUD preferences are restored below.
    SceneState().ResetForLoad( Cfg().cinematicRender );
    m_diagnosticsRuntime.PerfLog().isPerfTest = false;
    m_diagnosticsRuntime.PerfLog().perfHeaderWritten = false;
    m_simulation.Reset();
    m_diagnosticsRuntime.Capture().ResetScreenshot();
    m_diagnosticsRuntime.PerfLog().perfLogPath[0] = '\0';
    m_diagnosticsRuntime.PerfLog().isPerfLogFlushEnabled = false;
    m_diagnosticsRuntime.PerfLog().perfLogFlushInterval = 0;
    m_diagnosticsRuntime.PerfLog().perfLogWritesSinceFlush = 0;
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
    m_diagnosticsRuntime.UIStress() = DiagnosticsRuntime::UIStressState{};
    m_requiredSceneContacts.clear();

    m_systems.cameras->Reset();
    m_cGameModelCollection.Clear();

    CancelMousePickup();
    ResetAttachedCamera();
    {
        const RuntimeInteractionTransition transition = m_interaction.ResetForScene( InteractionExitReason::LoadScene );
        ClearRuntimeInteractionStateForTransition( transition );
        m_interaction.ResetForScene( InteractionExitReason::LoadScene );
    }
    SetCameraModeLabelAfterInteractionTransition( scenePath.empty() ? RunCameraMode::Demo : RunCameraMode::Scene );
    m_runtimeTools.ClearRayCastTestLines();
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
    m_debug.frozenWaterTime = 0.0f;
    m_camera.trackBallIndex = -1;
    m_camera.trackHeight = 300.0f;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
    m_camera.autoCycleShotsTaken = 0;
    m_camera.input = {};
    // overlayMode intentionally preserved — the user's HUD state persists across scene reloads.
    m_camera.selectedCamera = 0;

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
    bool hasSceneTornadoSystem = false;
    TornadoSystemConfig sceneTornadoSystem;

    // Branch on file-backed scene mode vs generated demo mode.
    if ( scenePath.empty() )
    {
        Cfg().gameModelCapacity = m_startup.gameModelCapacity;
        ApplySceneWorkerThreadSetting( m_startup.workerThreads );
        if ( m_launchOptions.seedOverride > 0 )
        {
            rngSeed = m_launchOptions.seedOverride;
        }
        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;
        UseDefaultTerrain();
        ApplyConfiguredWorldEnvironment();
        ApplyNoWaterOverride();
        if ( shouldPreserveRuntimeState )
        {
            // Restore setup-affecting live controls before the generated model pool is rebuilt.
            // Other visual/debug controls are restored later after scene JSON has loaded.
            ApplyUIWorldOverride( resetSnapshot.worldGravity,
                                  resetSnapshot.worldFluidHeight,
                                  resetSnapshot.worldFluidDensity );
        }

        SceneState().isSceneMode = false;
        SetUpCameras();
        if ( m_sceneUIOverrides.solverBallCountOverride >= 0 || m_sceneUIOverrides.solverBoxCountOverride >= 0 )
        {
            SceneGeneratedSetup::SetUpSolverObjects( BuildSceneGeneratedModelContext(),
                                                     (std::max)( 0, m_sceneUIOverrides.solverBallCountOverride ),
                                                     (std::max)( 0, m_sceneUIOverrides.solverBoxCountOverride ) );
        }
        else
        {
            SceneGeneratedSetup::SetUpGameModels( BuildSceneGeneratedModelContext(),
                                                  m_sceneUIOverrides.modelCountOverride >= 0
                                                      ? m_sceneUIOverrides.modelCountOverride
                                                      : DEFAULT_GAME_MODELS );
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
        hasSceneTornadoSystem = scene.HasTornadoSystem();
        if ( hasSceneTornadoSystem )
        {
            sceneTornadoSystem = scene.GetTornadoSystemConfig();
        }
        Cfg().gameModelCapacity =
            scene.HasModelCapacityOverride() ? scene.GetModelCapacity() : m_startup.gameModelCapacity;
        ApplySceneWorkerThreadSetting( scene.HasWorkerThreadOverride() ? scene.GetWorkerThreads()
                                                                       : m_startup.workerThreads );
        SceneState().isScenePhysics = scene.IsPhysicsEnabled();
        SceneState().isSceneText = scene.IsTextEnabled();
        m_diagnosticsRuntime.PerfLog().isPerfLogFlushEnabled = scene.IsPerfLogFlushEnabled();
        m_diagnosticsRuntime.PerfLog().perfLogFlushInterval = scene.GetPerfLogFlushInterval();
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
        SceneState().isEditableScene = scene.IsEditableScene();
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
        // that the .scene.json file explicitly authored.
        SceneState().hasCinematicRenderingOverride = scene.HasCinematicRenderingOverride();
        SceneState().isCinematicRenderingEnabled = scene.IsCinematicRenderingEnabled();
        SceneState().hasCinematicExposure = scene.HasCinematicExposure();
        SceneState().cinematicExposure = scene.GetCinematicExposure();
        SceneState().hasCinematicGamma = scene.HasCinematicGamma();
        SceneState().cinematicGamma = scene.GetCinematicGamma();
        SceneState().cinematicOverrideMask = scene.GetCinematicOverrideMask();
        SceneState().cinematicRender = Cfg().cinematicRender;
        ApplyCinematicSceneOverrides( SceneState().cinematicRender,
                                      SceneState().cinematicOverrideMask,
                                      scene.GetCinematicRenderConfig() );

        const SceneUIOptions& UIOptions = scene.GetUIOptions();
        const double UINow = m_timers.simulationTimer.GetTotalTime();
        bool isAutomationScene = scene.IsExitOnComplete() || scene.IsScreenshotAndExit() ||
                                 scene.GetScreenshotFrame() >= 0 || scene.GetScreenshotMs() >= 0 ||
                                 scene.GetScreenshotInterval() > 0 || scene.GetPerfLogPath()[0] != '\0';
#ifdef _DEBUG
        isAutomationScene = isAutomationScene || m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#endif
        if ( !preserveUIState )
        {
            if ( !UIOptions.hasVisible )
            {
                if ( isAutomationScene && !UIOptions.hasSettings )
                {
                    m_UI.SetVisible( false, UINow );
                }
                else if ( !UIOptions.hasSettings )
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
            m_diagnosticsRuntime.UIStress().enabled = UIOptions.stressEnabled;
        }
        if ( UIOptions.hasStressSeed )
        {
            m_diagnosticsRuntime.UIStress().randomState = UIOptions.stressSeed;
        }
        if ( UIOptions.hasStressActions )
        {
            m_diagnosticsRuntime.UIStress().actionsPerFrame = std::clamp( UIOptions.stressActionsPerFrame, 1, 32 );
        }
        SceneState().targetFrameCount = scene.GetFrameCount();
        SceneState().isExitOnComplete = suppressAutomationExit ? false : scene.IsExitOnComplete();
        m_diagnosticsRuntime.Capture().Screenshot().screenshotFrame = scene.GetScreenshotFrame();
        m_diagnosticsRuntime.Capture().Screenshot().screenshotMs = scene.GetScreenshotMs();
        m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit =
            suppressAutomationExit ? false : scene.IsScreenshotAndExit();

        if ( scene.GetScreenshotPath()[0] != '\0' )
        {
            strcpy_s( m_diagnosticsRuntime.Capture().Screenshot().screenshotPath,
                      sizeof( m_diagnosticsRuntime.Capture().Screenshot().screenshotPath ),
                      scene.GetScreenshotPath() );
        }
        // Interval capture: create output directory
        m_diagnosticsRuntime.Capture().Screenshot().screenshotInterval = scene.GetScreenshotInterval();
        if ( scene.GetScreenshotDir()[0] != '\0' )
        {
            strcpy_s( m_diagnosticsRuntime.Capture().Screenshot().screenshotDir,
                      sizeof( m_diagnosticsRuntime.Capture().Screenshot().screenshotDir ),
                      scene.GetScreenshotDir() );
            CreateDirectoryA( m_diagnosticsRuntime.Capture().Screenshot().screenshotDir, nullptr );
        }

        // Perf test: open CSV log file
        const char* pPerfPath = scene.GetPerfLogPath();
        if ( pPerfPath[0] != '\0' )
        {
            m_diagnosticsRuntime.PerfLog().isPerfTest = true;
            strcpy_s( m_diagnosticsRuntime.PerfLog().perfLogPath,
                      sizeof( m_diagnosticsRuntime.PerfLog().perfLogPath ),
                      pPerfPath );
            const char* mode = ( sPerfPass == 0 ) ? "w" : "a";
            fopen_s( &m_diagnosticsRuntime.PerfLog().perfLogFile, m_diagnosticsRuntime.PerfLog().perfLogPath, mode );
            if ( m_diagnosticsRuntime.PerfLog().perfLogFile )
            {
                m_diagnosticsRuntime.PerfLog().perfLogWritesSinceFlush = 0;
                m_diagnosticsRuntime.LogPerfMemory( sPerfPass + 1, "start" );
            }
        }

        // Override RNG seed for deterministic scenes. CLI --seed wins so a launcher snapshot can
        // replay an unseeded/random scene or deliberately override a scene file seed.
        if ( scene.GetSeed() > 0 )
        {
            rngSeed = scene.GetSeed();
        }
        if ( m_launchOptions.seedOverride > 0 )
        {
            rngSeed = m_launchOptions.seedOverride;
        }
        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;

        // Scene terrain is authoritative.  A flat-slope test scene must not leak
        // its analytic terrain into the next height-map scene.
        if ( scene.HasFlatSlope() )
        {
            SceneState().hasFlatSlope = true;
            SceneState().flatBaseY = scene.GetFlatBaseY();
            SceneState().flatSlopeX = scene.GetFlatSlopeX();
            SceneState().flatSlopeZ = scene.GetFlatSlopeZ();
            UseFlatSlopeTerrain( scene.GetFlatBaseY(), scene.GetFlatSlopeX(), scene.GetFlatSlopeZ() );
        }
        else
        {
            SceneState().hasFlatSlope = false;
            UseDefaultTerrain();
        }

        ApplyConfiguredWorldEnvironment();
        // Override world environment if scene specifies world values
        if ( scene.HasWorldOverride() )
        {
            m_cWorldEnvironment = WorldEnvironment( scene.GetWorldFluidHeight(),
                                                    scene.GetWorldFluidDensity(),
                                                    Cfg().gasDensity,
                                                    scene.GetWorldGravity() );
            UpdateWorldTerrainBounds();
        }
        ApplyNoWaterOverride();
        if ( shouldPreserveRuntimeState )
        {
            // World sliders/keyboard water edits are part of the live scene controls.
            // Restore them after terrain/world JSON and --no-water have resolved,
            // so a plain reset keeps the operator's current environment.
            ApplyUIWorldOverride( resetSnapshot.worldGravity,
                                  resetSnapshot.worldFluidHeight,
                                  resetSnapshot.worldFluidDensity );
        }

        SceneAuthoredSetup::SetUpCameras( BuildSceneAuthoredCameraContext(), scene );

        if ( m_sceneUIOverrides.solverBallCountOverride >= 0 || m_sceneUIOverrides.solverBoxCountOverride >= 0 )
        {
            SceneGeneratedSetup::SetUpSolverObjects( BuildSceneGeneratedModelContext(),
                                                     (std::max)( 0, m_sceneUIOverrides.solverBallCountOverride ),
                                                     (std::max)( 0, m_sceneUIOverrides.solverBoxCountOverride ) );
        }
        else if ( m_sceneUIOverrides.modelCountOverride >= 0 )
        {
            SceneGeneratedSetup::SetUpGameModels( BuildSceneGeneratedModelContext(),
                                                  m_sceneUIOverrides.modelCountOverride );
        }
        else if ( scene.GetSolverBallCount() > 0 || scene.GetSolverBoxCount() > 0 )
        {
            // Exact-count solver spawn — explicit ball/box split for benchmarks.
            SceneGeneratedSetup::SetUpSolverObjects( BuildSceneGeneratedModelContext(),
                                                     scene.GetSolverBallCount(),
                                                     scene.GetSolverBoxCount() );
        }
        else
        {
            SceneAuthoredSetup::SetUpGameModels( BuildSceneAuthoredModelContext(), scene );
        }

        // Physics regression log: current-solver per-frame CSV enabled only by command line.
#ifdef _DEBUG
        m_cGameModelCollection.SetPhysicsRegressionLogPath(
            m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride );
        m_cGameModelCollection.SetPhysicsCollisionTimeLogPath(
            m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride );
        if ( m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled )
        {
            m_cGameModelCollection.SetPhysicsDiagnosticsPath( m_diagnosticsRuntime.PhysicsDiagnostics().path );
        }
#endif

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

        // Snapshot scenes start paused in Inspect by default; authored live scenes
        // may opt out when body-state entries are just stable initial poses.
        const bool hasSnapshotState =
            scene.GetBallStateCount() > 0 || scene.GetBoxStateCount() > 0 || scene.GetConvexHullStateCount() > 0;
#ifdef _DEBUG
        const bool shouldPauseSnapshotState = hasSnapshotState && scene.ShouldPauseSnapshotState() &&
                                              !m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
        const bool shouldPauseSnapshotState = hasSnapshotState && scene.ShouldPauseSnapshotState();
#endif
        if ( shouldPauseSnapshotState )
        {
            const RuntimeInteractionTransition transition = m_interaction.EnterInspect();
            ApplyRuntimeInteractionTransitionCleanup( transition );
            SetCameraModeLabelAfterInteractionTransition( RunCameraMode::Inspect );
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
        RestoreSceneRuntimeResetSnapshot( resetContext, resetSnapshot, suppressExitOnComplete );
    }

    // CLI --time-scale and --fixed-step override anything the scene file sets.
    if ( m_launchOptions.timeScaleOverride > 0.0f )
    {
        SceneState().timeScale = m_launchOptions.timeScaleOverride;
    }
    if ( m_sceneUIOverrides.timeScaleOverride > 0.0f )
    {
        SceneState().timeScale = m_sceneUIOverrides.timeScaleOverride;
    }
    if ( m_launchOptions.fixedStep )
    {
        SceneState().isFixedStep = true;
    }
    if ( !shouldPreserveRuntimeState )
    {
        m_runtimeSettings.tornadoField = Physics::TornadoFieldConfig();
        m_runtimeSettings.tornadoSystem = Physics::TornadoSystemConfig();
        ApplyTornadoDefaultsForActiveScene();
        if ( hasSceneTornadoSystem )
        {
            m_runtimeSettings.tornadoSystem = sceneTornadoSystem;
            m_runtimeSettings.tornadoField.enabled = false;
            m_runtimeSettings.tornadoVisual.enabled = true;
        }
    }
    if ( m_launchOptions.hasTornadoOverride )
    {
        if ( m_runtimeSettings.tornadoSystem.enabled || !m_runtimeSettings.tornadoSystem.vortices.empty() )
        {
            m_runtimeSettings.tornadoSystem.enabled = m_launchOptions.tornadoEnabled;
            m_runtimeSettings.tornadoField.enabled = false;
        }
        else
        {
            m_runtimeSettings.tornadoField.enabled = m_launchOptions.tornadoEnabled;
        }
        if ( m_runtimeSettings.tornadoVisual.autoEnableWithTornado )
        {
            m_runtimeSettings.tornadoVisual.enabled = m_launchOptions.tornadoEnabled;
        }
    }
    if ( m_launchOptions.tornadoVectors )
    {
        m_runtimeSettings.tornadoField.visualizeVelocityField = true;
        m_runtimeSettings.tornadoSystem.visualizeVelocityField = true;
    }
    SyncTornadoFieldToPhysics();
    m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
    if ( m_launchOptions.frameCountOverride > 0 )
    {
        SceneState().targetFrameCount = m_launchOptions.frameCountOverride;
        SceneState().isExitOnComplete = true;
    }
    if ( m_launchOptions.uiStress )
    {
        m_diagnosticsRuntime.UIStress().enabled = true;
        m_diagnosticsRuntime.UIStress().randomState = m_launchOptions.uiStressSeed;
        m_diagnosticsRuntime.UIStress().actionsPerFrame = m_launchOptions.uiStressActions;
        m_UI.SetVisible( true, m_timers.simulationTimer.GetTotalTime() );
        m_UI.SetMinimized( false, m_timers.simulationTimer.GetTotalTime() );
    }
    if ( m_launchOptions.hasCinematicShadowsOverride )
    {
        ActiveCinematicConfig().shadowsEnabled = m_launchOptions.cinematicShadows;
        SceneState().cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    }
    if ( m_launchOptions.hasPhysicsDebugFlagsOverride )
    {
        m_debug.physicsDebugFlags = m_launchOptions.physicsDebugFlagsOverride;
    }
    if ( m_launchOptions.hasPhysicsDebugTransparentOverride )
    {
        m_debug.isPhysicsDebugTransparent = m_launchOptions.physicsDebugTransparentOverride;
    }
    if ( m_launchOptions.hasPhysicsDebugAlphaOverride )
    {
        m_debug.physicsDebugAlpha = m_launchOptions.physicsDebugAlphaOverride;
    }
    if ( m_launchOptions.hasPhysicsDebugContactLingerOverride )
    {
        m_debug.physicsDebugContactLinger = m_launchOptions.physicsDebugContactLingerOverride;
    }

#ifdef _DEBUG
    Log().WriteEventf( "scene_started index=%d load=%d path=\"%s\" renderer=\"%s\" target_frames=%d seed=%u "
                       "fixed_step=%d physics=%d text=%d models=%d",
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

    // Runtime swap policy is chosen after config/scene overrides are resolved.
    Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );

    // Restart timers
    m_timers.frameTimer.StartTimer();
    m_timers.workTimer.StartTimer();
    m_timers.updateTimer.StartTimer();
    m_timers.cameraTimer.StartTimer();
    m_timers.simulationTimer.StartTimer();
    ResetReplayTimelineForActiveScene();

    // Initialize DXR raytracing on first scene load (requires terrain + sphere meshes to exist)
    // Force sphere mesh creation (normally lazy-init on first render)
    const auto renderCapabilities = Gfx().GetCapabilities();
    if ( renderCapabilities.supportsDxrReflection && RenderHelper::GetSphereInstMeshHandle() == 0 )
    {
        RenderHelper::EnsureSphereMesh();
    }
    {
    }
    if ( renderCapabilities.supportsDxrReflection && m_systems.terrain && m_systems.terrain->GetMesh() )
    {
        IMesh* terrainMesh = m_systems.terrain->GetMesh();
        uint64_t terrainVBVA = terrainMesh->GetVertexBufferGPUVA();
        int terrainVertCount = terrainMesh->GetVertexCount();
        int terrainStride = terrainMesh->GetStride();

        uint32_t sphereHandle = RenderHelper::GetSphereInstMeshHandle();
        uint64_t sphereVBVA = Gfx().GetInstancedMeshStaticVBVA( sphereHandle );
        int sphereVertCount = RenderHelper::GetSphereVertexCount();
        int sphereStride = Gfx().GetInstancedMeshStaticStride( sphereHandle );

        {
        }

        if ( terrainVBVA != 0 && sphereVBVA != 0 )
        {
            Gfx().InitDXR( terrainVBVA,
                           terrainVertCount,
                           terrainStride,
                           sphereVBVA,
                           sphereVertCount,
                           sphereStride,
                           ActiveGameModelCapacity() );
        }
    }
}


bool Run::SaveCurrentEditableSceneSnapshot()
{
    const std::string* scenePath = m_sceneController.CurrentPath();
    if ( !SceneState().isSceneMode || !scenePath || scenePath->empty() )
    {
        return false;
    }

    return m_cGameModelCollection.SaveSceneSnapshot( scenePath->c_str(),
                                                     SceneState().isScenePhysics,
                                                     SceneState().isSceneText,
                                                     m_cWorldEnvironment,
                                                     m_systems.cameras->GetCameraTranslation(),
                                                     m_systems.cameras->GetCameraView(),
                                                     m_systems.cameras->GetCameraUp(),
                                                     true,
                                                     SceneState().isFixedStep,
                                                     m_debug.isWaterHidden,
                                                     m_debug.isTerrainHidden,
                                                     SceneState().hasFlatSlope,
                                                     SceneState().flatBaseY,
                                                     SceneState().flatSlopeX,
                                                     SceneState().flatSlopeZ );
}


bool Run::SaveCurrentSceneDefaults()
{
    const std::string* scenePath = m_sceneController.CurrentPath();
    if ( !SceneState().isSceneMode || !scenePath || scenePath->empty() )
    {
        return false;
    }
    if ( SceneState().isEditableScene )
    {
        return SaveCurrentEditableSceneSnapshot();
    }

    std::ifstream input( *scenePath );
    if ( !input )
    {
        return false;
    }

    Json root;
    try
    {
        input >> root;
    }
    catch ( const std::exception& )
    {
        return false;
    }

    if ( !root.is_object() )
    {
        return false;
    }

    root["format"] = "skullbonez.scene.json";
    root["version"] = 1;
    Json& simulation = EnsureJsonObject( root, "simulation" );
    Json& playback = EnsureJsonObject( root, "playback" );
    Json& runtime = EnsureJsonObject( root, "runtime" );
    Json& debug = EnsureJsonObject( root, "debug" );
    Json& physicsDebug = EnsureJsonObject( debug, "physics" );
    Json& world = EnsureJsonObject( simulation, "world" );

    simulation["physics"] = SceneState().isScenePhysics;
    simulation["text"] = SceneState().isSceneText;
    simulation["textOnly"] = m_debug.isTextOnly;
    runtime["vsync"] = m_runtimeSettings.isVsyncEnabled;
    runtime["pipelineSync"] = m_runtimeSettings.isPipelineSyncEnabled;
    playback["fixedStep"] = SceneState().isFixedStep;
    if ( SceneState().targetFrameCount > 0 )
    {
        playback["frames"] = SceneState().targetFrameCount;
    }
    else
    {
        playback["frames"] = "unlimited";
    }

    simulation["seed"] = (std::max)( 1u, SceneState().rngSeed );
    simulation["timeScale"] = SceneState().timeScale;
    playback["exitOnComplete"] = SceneState().isExitOnComplete;

    physicsDebug["axes"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0;
    physicsDebug["contacts"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0;
    physicsDebug["sleep"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0;
    physicsDebug["pipeline"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0;
    physicsDebug["terrainContact"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_TERRAIN_CONTACT ) != 0;
    physicsDebug["transparent"] = m_debug.isPhysicsDebugTransparent;
    physicsDebug["alpha"] = m_debug.physicsDebugAlpha;
    physicsDebug["contactLinger"] = m_debug.physicsDebugContactLinger;

    debug["collisionVisualizer"] = m_debug.isCollisionVisualizer;
    debug["broadphaseOverlay"] = m_debug.isBroadphaseOverlay;
    debug["waterFreeze"] = m_debug.isWaterFreezeDebug;
    debug["waterFlat"] = m_debug.isWaterFlatDebug;
    debug["waterHidden"] = m_debug.isWaterHidden;
    debug["terrainHidden"] = m_debug.isTerrainHidden;
    debug["waterReflection"] = WaterReflectionJsonValue( m_debug.isWaterNoReflect, m_debug.isWaterRTReflect );
    if ( m_camera.trackBallIndex >= 0 && m_camera.trackHeight > 0.0f )
    {
        playback["trackHeight"] = m_camera.trackHeight;
    }
    else
    {
        playback.erase( "trackHeight" );
    }
    if ( m_camera.autoCycleInterval > 0.0f )
    {
        playback["autoCycleInterval"] = m_camera.autoCycleInterval;
    }
    else
    {
        playback.erase( "autoCycleInterval" );
    }
    world["gravity"] = m_cWorldEnvironment.GetGravity();
    world["fluidHeight"] = m_cWorldEnvironment.GetFluidSurfaceHeight();
    world["fluidDensity"] = m_cWorldEnvironment.GetFluidDensity();
    SetTouchedCinematicSceneProperties( root, SceneState().uiCinematicOverrideMask, SceneState().cinematicRender );

    if ( m_sceneUIOverrides.modelCountOverride >= 0 )
    {
        simulation["solverBalls"] = m_sceneUIOverrides.modelCountOverride;
        simulation.erase( "solverBoxes" );
    }
    else if ( SceneState().solverBallCount > 0 || SceneState().solverBoxCount > 0 ||
              m_sceneUIOverrides.solverBallCountOverride >= 0 || m_sceneUIOverrides.solverBoxCountOverride >= 0 )
    {
        simulation["solverBalls"] = SceneState().solverBallCount;
        simulation["solverBoxes"] = SceneState().solverBoxCount;
    }

    std::ofstream output( *scenePath, std::ios::trunc );
    if ( !output )
    {
        return false;
    }

    output << root.dump( 2 ) << '\n';
    return output.good();
}


bool Run::SaveRenderDefaults()
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

bool Run::SaveSkyDefaults()
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

    const CinematicRenderConfig& cinematic = ActiveCinematicConfig();
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

    setBool( "cinematic_sky_atmosphere", cinematic.skyAtmosphereEnabled );
    setBool( "cinematic_clouds", cinematic.cloudsEnabled );
    setBool( "cinematic_god_rays", cinematic.godRaysEnabled );
    setBool( "cinematic_volumetric_lighting", cinematic.volumetricLightingEnabled );
    setFloat( "cinematic_exposure", cinematic.exposure, "%.2f" );
    setFloat( "cinematic_gamma", cinematic.gamma, "%.2f" );
    setFloat( "cinematic_sun_screen_x", cinematic.sunScreenX, "%.3f" );
    setFloat( "cinematic_sun_screen_y", cinematic.sunScreenY, "%.3f" );
    setFloat( "cinematic_sun_color_r", cinematic.sunColorR, "%.2f" );
    setFloat( "cinematic_sun_color_g", cinematic.sunColorG, "%.2f" );
    setFloat( "cinematic_sun_color_b", cinematic.sunColorB, "%.2f" );
    setFloat( "cinematic_sun_intensity", cinematic.sunIntensity, "%.2f" );
    setFloat( "cinematic_sky_horizon_r", cinematic.skyHorizonR, "%.2f" );
    setFloat( "cinematic_sky_horizon_g", cinematic.skyHorizonG, "%.2f" );
    setFloat( "cinematic_sky_horizon_b", cinematic.skyHorizonB, "%.2f" );
    setFloat( "cinematic_sky_zenith_r", cinematic.skyZenithR, "%.2f" );
    setFloat( "cinematic_sky_zenith_g", cinematic.skyZenithG, "%.2f" );
    setFloat( "cinematic_sky_zenith_b", cinematic.skyZenithB, "%.2f" );
    setFloat( "cinematic_sky_glow_strength", cinematic.skyGlowStrength, "%.2f" );
    setFloat( "cinematic_cloud_coverage", cinematic.cloudCoverage, "%.2f" );
    setFloat( "cinematic_cloud_softness", cinematic.cloudSoftness, "%.2f" );
    setFloat( "cinematic_cloud_scale", cinematic.cloudScale, "%.2f" );
    setFloat( "cinematic_cloud_intensity", cinematic.cloudIntensity, "%.2f" );
    setFloat( "cinematic_sun_shaft_strength", cinematic.sunShaftStrength, "%.2f" );
    setFloat( "cinematic_sun_shaft_falloff", cinematic.sunShaftFalloff, "%.2f" );
    setFloat( "cinematic_volumetric_strength", cinematic.volumetricStrength, "%.2f" );
    setFloat( "cinematic_volumetric_density", cinematic.volumetricDensity, "%.2f" );
    setFloat( "cinematic_volumetric_decay", cinematic.volumetricDecay, "%.3f" );
    setInt( "cinematic_sky_mode", cinematic.skyMode );
    setFloat( "cinematic_style_saturation", cinematic.styleSaturation, "%.2f" );
    setFloat( "cinematic_style_contrast", cinematic.styleContrast, "%.2f" );
    setFloat( "cinematic_style_vignette", cinematic.styleVignette, "%.2f" );

    AppendMissingCinematicConfigLines( lines, missing );

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


void Run::RefreshSceneBrowserList()
{
    m_sceneBrowser.paths.clear();
    m_sceneBrowser.names.clear();
    m_sceneBrowser.namePtrs.clear();

    const std::filesystem::path sceneDir = std::filesystem::path( DATA_ROOT ) / "scenes";
    try
    {
        if ( !std::filesystem::exists( sceneDir ) )
        {
            return;
        }

        for ( const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator( sceneDir ) )
        {
            if ( !entry.is_regular_file() || !IsSceneJsonFile( entry.path() ) )
            {
                continue;
            }
            m_sceneBrowser.paths.push_back( NormalizeScenePath( entry.path().generic_string() ) );
        }
    }
    catch ( const std::filesystem::filesystem_error& e )
    {
        Log().WriteEventf( "scene_browser_refresh_failed message=\"%s\"", e.what() );
        m_sceneBrowser.paths.clear();
    }

    std::sort( m_sceneBrowser.paths.begin(), m_sceneBrowser.paths.end() );
    m_sceneBrowser.paths.erase( std::unique( m_sceneBrowser.paths.begin(), m_sceneBrowser.paths.end() ),
                                m_sceneBrowser.paths.end() );
    m_sceneBrowser.names.reserve( m_sceneBrowser.paths.size() );
    m_sceneBrowser.namePtrs.reserve( m_sceneBrowser.paths.size() );
    for ( const std::string& path : m_sceneBrowser.paths )
    {
        m_sceneBrowser.names.emplace_back( FileNameFromPath( path.c_str() ) );
    }
    for ( const std::string& name : m_sceneBrowser.names )
    {
        m_sceneBrowser.namePtrs.push_back( name.c_str() );
    }
}


int Run::CurrentSceneBrowserIndex() const
{
    const std::string* currentScenePath = m_sceneController.CurrentPath();
    if ( !currentScenePath )
    {
        return -1;
    }

    const std::string currentPath = NormalizeScenePath( *currentScenePath );
    for ( int i = 0; i < static_cast<int>( m_sceneBrowser.paths.size() ); ++i )
    {
        if ( NormalizeScenePath( m_sceneBrowser.paths[i] ) == currentPath )
        {
            return i;
        }
    }
    return -1;
}


bool Run::CreateSceneFromUI( const char* requestedName )
{
    const std::string cleanName = SanitizeSceneFileName( requestedName );
    if ( cleanName.empty() )
    {
        return false;
    }

    const std::filesystem::path sceneDir = std::filesystem::path( DATA_ROOT ) / "scenes";
    std::error_code ec;
    std::filesystem::create_directories( sceneDir, ec );
    if ( ec )
    {
        Log().WriteEventf( "scene_create_failed name=\"%s\" reason=\"mkdir\" message=\"%s\"",
                           cleanName.c_str(),
                           ec.message().c_str() );
        return false;
    }

    const std::filesystem::path scenePath = UniqueScenePath( sceneDir, cleanName );
    if ( scenePath.empty() || !WriteStarterSceneFile( scenePath, cleanName ) )
    {
        Log().WriteEventf( "scene_create_failed name=\"%s\" reason=\"write\"", cleanName.c_str() );
        return false;
    }

    RefreshSceneBrowserList();
    const std::string normalizedPath = NormalizeScenePath( scenePath.generic_string() );
    for ( int i = 0; i < static_cast<int>( m_sceneBrowser.paths.size() ); ++i )
    {
        if ( NormalizeScenePath( m_sceneBrowser.paths[i] ) == normalizedPath )
        {
            LoadSceneFromBrowserIndex( i );
            return true;
        }
    }

    EnterInteractiveSceneRun();
    LoadScene( m_sceneController.Append( normalizedPath ), true, true );
    return true;
}


void Run::LoadSceneFromBrowserIndex( int index )
{
    m_sceneCoordinator.LoadSceneFromBrowserIndex( index, m_sceneBrowser.paths );
}


void Run::LoadDemoSceneFromUI()
{
    m_sceneCoordinator.LoadDemoSceneFromUI();
}


bool Run::ApplyCinematicModeFromBrowserIndex( int index )
{
    EnterInteractiveSceneRun();
    m_launchOptions.hasCinematicRenderingOverride = false;

    auto resetObjectMaterials = [&]()
    {
        for ( int modelIndex = 0; modelIndex < m_cGameModelCollection.GetModelCount(); ++modelIndex )
        {
            GameModel& model = m_cGameModelCollection.GetModelAtIndex( modelIndex );
            if ( !IsSimpleRagdollPartName( model.GetName() ) )
            {
                model.SetRenderTint( 1.0f, 1.0f, 1.0f, 0.0f );
            }
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
        m_sceneBrowser.selectedCineModeSceneIndex = -1;
        return true;
    }

    if ( index >= static_cast<int>( m_sceneBrowser.paths.size() ) || !IsCineScenePath( m_sceneBrowser.paths[index] ) )
    {
        return false;
    }

    TestScene lookScene = TestScene::LoadFromFile( m_sceneBrowser.paths[index].c_str() );
    cinematic = m_defaultCinematicRender;
    ApplyCinematicSceneOverrides( cinematic,
                                  lookScene.GetCinematicOverrideMask(),
                                  lookScene.GetCinematicRenderConfig() );
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
    m_sceneBrowser.selectedCineModeSceneIndex = index;
    return true;
}


void Run::ApplyLiveStyleScene( const TestScene& styleScene )
{
    m_launchOptions.hasCinematicRenderingOverride = false;

    for ( int modelIndex = 0; modelIndex < m_cGameModelCollection.GetModelCount(); ++modelIndex )
    {
        GameModel& model = m_cGameModelCollection.GetModelAtIndex( modelIndex );
        if ( !IsSimpleRagdollPartName( model.GetName() ) )
        {
            model.SetRenderTint( 1.0f, 1.0f, 1.0f, 0.0f );
        }
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
    ApplyCinematicSceneOverrides( cinematic,
                                  styleScene.GetCinematicOverrideMask(),
                                  styleScene.GetCinematicRenderConfig() );
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
    m_sceneBrowser.selectedCineModeSceneIndex = -1;
}


void Run::ApplyDemoHeroStyleOverride()
{
    if ( !m_launchOptions.demoHeroStyle || SceneState().isSceneMode )
    {
        return;
    }

    const std::string stylePath = std::string( DATA_ROOT ) + "styles/low_poly_art_style.style.json";
    const TestScene styleScene = TestScene::LoadStyleFromFile( stylePath.c_str() );
    ApplyLiveStyleScene( styleScene );
    printf( "[scene] Applied low-poly hero rendering mode to generated demo scene.\n" );
}


bool Run::ApplyAdjacentCinematicMode( int direction )
{
    return m_sceneCoordinator.ApplyAdjacentCinematicMode( direction,
                                                          m_sceneBrowser.paths,
                                                          m_sceneBrowser.selectedCineModeSceneIndex );
}


void Run::LoadAdjacentSceneFromBrowser( int direction )
{
    m_sceneCoordinator.LoadAdjacentSceneFromBrowser( direction, m_sceneBrowser.paths );
}


void Run::ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
{
    m_sceneCoordinator.ResetCurrentScene( preserveUIState, suppressExitOnComplete, preserveRuntimeState );
}


void Run::ApplyUIModelCountOverride( int count )
{
    m_sceneUIOverrides.modelCountOverride = std::clamp( count, 0, ActiveGameModelCapacity() );
    m_sceneUIOverrides.solverBallCountOverride = -1;
    m_sceneUIOverrides.solverBoxCountOverride = -1;
    if ( !m_sceneController.HasCurrentEntry() )
    {
        return;
    }

    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }
    m_cGameModelCollection.Clear();
    m_runtimeTools.ClearRayCastTestLines();
    m_simulation.Reset();
    SceneState().currentFrame = 0;
    SceneState().isTestComplete = false;
    if ( m_sceneUIOverrides.modelCountOverride <= 0 )
    {
        SceneState().modelCount = 0;
        m_camera.trackBallIndex = -1;
        ResetReplayTimelineForActiveScene();
        PROFILE_SCHEDULE_RESET();
        return;
    }

    const unsigned int seed = SceneState().rngSeed > 0 ? SceneState().rngSeed : 1u;
    SceneState().rngState = seed;
    SceneGeneratedSetup::SetUpGameModels( BuildSceneGeneratedModelContext(), m_sceneUIOverrides.modelCountOverride );
    if ( m_camera.trackBallIndex >= m_sceneUIOverrides.modelCountOverride )
    {
        m_camera.trackBallIndex = m_sceneUIOverrides.modelCountOverride - 1;
    }
    ResetReplayTimelineForActiveScene();
    PROFILE_SCHEDULE_RESET();
}


void Run::ApplyUISolverObjectCounts( int balls, int boxes )
{
    const int modelCapacity = ActiveGameModelCapacity();
    balls = std::clamp( balls, 0, modelCapacity );
    boxes = std::clamp( boxes, 0, modelCapacity );
    if ( balls + boxes > modelCapacity )
    {
        boxes = (std::max)( 0, modelCapacity - balls );
    }
    m_sceneUIOverrides.solverBallCountOverride = balls;
    m_sceneUIOverrides.solverBoxCountOverride = boxes;
    m_sceneUIOverrides.modelCountOverride = -1;
    if ( !m_sceneController.HasCurrentEntry() )
    {
        return;
    }

    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }
    m_cGameModelCollection.Clear();
    m_runtimeTools.ClearRayCastTestLines();
    m_simulation.Reset();
    SceneState().currentFrame = 0;
    SceneState().isTestComplete = false;

    const unsigned int seed = SceneState().rngSeed > 0 ? SceneState().rngSeed : 1u;
    SceneState().rngState = seed;
    SceneGeneratedSetup::SetUpSolverObjects( BuildSceneGeneratedModelContext(),
                                             m_sceneUIOverrides.solverBallCountOverride,
                                             m_sceneUIOverrides.solverBoxCountOverride );
    if ( SceneState().modelCount <= 0 )
    {
        m_camera.trackBallIndex = -1;
    }
    else if ( m_camera.trackBallIndex >= SceneState().modelCount )
    {
        m_camera.trackBallIndex = SceneState().modelCount - 1;
    }
    ResetReplayTimelineForActiveScene();
    PROFILE_SCHEDULE_RESET();
}


void Run::ApplyUIWorldOverride( float gravity, float fluidHeight, float fluidDensity )
{
    const float previousGravity = m_cWorldEnvironment.GetGravity();
    const float previousFluidHeight = m_cWorldEnvironment.GetFluidSurfaceHeight();
    const float previousFluidDensity = m_cWorldEnvironment.GetFluidDensity();
    m_cWorldEnvironment.SetGravity( gravity );
    m_cWorldEnvironment.SetFluidSurfaceHeight( fluidHeight );
    m_cWorldEnvironment.SetFluidDensity( fluidDensity );
    RecordReplayWorldOverrideEvent( previousGravity,
                                    previousFluidHeight,
                                    previousFluidDensity,
                                    gravity,
                                    fluidHeight,
                                    fluidDensity );
}


void Run::ApplyConfiguredWorldEnvironment()
{
    const EngineConfig& cfg = Cfg();
    m_cWorldEnvironment = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
    UpdateWorldTerrainBounds();
}


void Run::ApplyNoWaterOverride()
{
    if ( !m_launchOptions.noWater || !m_systems.terrain )
    {
        return;
    }

    m_cWorldEnvironment.SetFluidSurfaceHeight( m_systems.terrain->GetMinHeight() - NO_WATER_TERRAIN_CLEARANCE );
}


void Run::ApplyTornadoDefaultsForActiveScene()
{
    Physics::TornadoFieldConfig field = m_runtimeSettings.tornadoField;
    const CinematicRenderConfig& cinematic = ActiveCinematicConfig();
    const float basinRadius = (std::max)( cinematic.basinRadiusX, cinematic.basinRadiusZ );

    field.center =
        Vector3( cinematic.basinCenterX, m_cWorldEnvironment.GetFluidSurfaceHeight(), cinematic.basinCenterZ );
    field.radius = std::clamp( basinRadius * 1.28f, 180.0f, 340.0f );
    field.height = (std::max)( 130.0f, field.radius * 0.66f );
    field.inwardAcceleration = 150.0f;
    field.swirlAcceleration = 185.0f;
    field.liftAcceleration = 64.0f;
    m_runtimeSettings.tornadoField = field;
}


void Run::SyncTornadoFieldToPhysics()
{
    m_cGameModelCollection.SetTornadoFieldConfig( m_runtimeSettings.tornadoField );
    m_cGameModelCollection.SetTornadoSystemConfig( m_runtimeSettings.tornadoSystem );
}


void Run::UseDefaultTerrain()
{
    if ( !m_systems.terrain || m_systems.isFlatSlopeTerrain )
    {
        if ( IsGfxReady() )
        {
            Gfx().FlushGPU();
        }
        const std::string terrainRawPath =
            ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain, "terrain.raw", Cfg().terrainRaw );
        m_systems.terrain = std::make_unique<Terrain>( terrainRawPath.c_str(), 256, 8, 15 );
        m_systems.isFlatSlopeTerrain = false;
    }

    UpdateWorldTerrainBounds();
}


void Run::UseFlatSlopeTerrain( float baseY, float slopeX, float slopeZ )
{
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }
    m_systems.terrain = std::make_unique<Terrain>( baseY, slopeX, slopeZ );
    m_systems.isFlatSlopeTerrain = true;

    UpdateWorldTerrainBounds();
}


void Run::UpdateWorldTerrainBounds()
{
    if ( !m_systems.terrain )
    {
        return;
    }

    XZBounds tb = m_systems.terrain->GetXZBounds();
    m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
}


bool Run::AdvanceScene()
{
    const bool preserveInteractiveUI = SceneState().isInteractiveRun;
    return m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfLog().isPerfTest,
                                            sPerfPass,
                                            preserveInteractiveUI );
}
