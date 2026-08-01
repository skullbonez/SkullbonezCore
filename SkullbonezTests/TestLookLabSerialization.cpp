/*
File: TestLookLabSerialization.cpp
Purpose:
  Verifies exact standalone style, bundle, receipt, and atomic-write contracts.

Summary:
  Tests serialize deterministic candidates, reload them through the production
  style parser, and serialize the parsed values again byte-for-byte, including
  a two-process producer/consumer mode. Filesystem cases cover parent creation,
  replacement, collisions, curated compatibility, and bounded failures.

Glossary:
  Reconstructed snapshot: Detached style rebuilt exclusively from parser
    outputs after the saved JSON has been loaded.
  Honest receipt: Text whose independent style and screenshot states match the
    supplied transaction facts, including partial failure.

Invariants:
  - Equality is proved through canonical serialization, never struct memcmp.
  - Temporary test directories are removed only beneath TestOutput.
  - Every filesystem failure is inspected while its SbResult lease is live.

Related:
  - SkullbonezSource/Scene/StandaloneStyleWriter.cpp
  - SkullbonezSource/Runtime/Direction/LookLabBundleWriter.cpp
  - SkullbonezSource/Core/AtomicTextFileWriter.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Runtime/Direction/LookLabBundleWriter.h"
#include "../SkullbonezSource/Runtime/Direction/LookLabGenerator.h"
#include "../SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h"
#include "../SkullbonezSource/Scene/AuthoredScene.h"
#include "../SkullbonezSource/Scene/StandaloneStyleWriter.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
using namespace SkullbonezCore;
constexpr const char* ROOT = "TestOutput/look_lab_serialization";
constexpr const char* FRESH_PROCESS_ROOT = "TestOutput/look_lab_fresh_process";

struct TemporaryLookLabOutput
{
    TemporaryLookLabOutput()
    {
        std::error_code ignored;
        std::filesystem::remove_all( ROOT, ignored );
    }

    ~TemporaryLookLabOutput()
    {
        std::error_code ignored;
        std::filesystem::remove_all( ROOT, ignored );
    }
};

std::string ReadText( const char* path )
{
    std::ifstream input( path, std::ios::binary );
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

uint64_t FingerprintText( const std::string& text )
{
    uint64_t hash = 14695981039346656037ull;

    for ( unsigned char byte : text )
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }

    return hash;
}

Scene::StandaloneStyleSnapshot SnapshotFromCandidate( const Runtime::LookLabCandidate& candidate )
{
    Scene::StandaloneStyleSnapshot snapshot;
    snapshot.cinematic = candidate.cinematic;

    for ( const Runtime::LookLabMaterialRule& candidateRule : candidate.materialRules )
    {
        Scene::StandaloneStyleMaterialRule rule;
        strncpy_s( rule.target.data(), rule.target.size(), candidateRule.target.data(), _TRUNCATE );
        rule.material = candidateRule.material;
        snapshot.materialRules.push_back( rule );
    }

    return snapshot;
}

Scene::StandaloneStyleSnapshot SnapshotFromParsedStyle( const Runtime::AuthoredScene& parsed )
{
    Scene::StandaloneStyleSnapshot snapshot;
    snapshot.cinematic = parsed.GetCinematicRenderConfig();

    for ( int index = 0; index < parsed.GetObjectMaterialOverrideCount(); ++index )
    {
        const Runtime::SceneObjectMaterialOverride& parsedRule = parsed.GetObjectMaterialOverride( index );
        Scene::StandaloneStyleMaterialRule rule;
        strncpy_s( rule.target.data(), rule.target.size(), parsedRule.target, _TRUNCATE );
        rule.material = parsedRule.material;
        snapshot.materialRules.push_back( rule );
    }

    return snapshot;
}

template <size_t Capacity> void CopyText( std::array<char, Capacity>& output, const char* text )
{
    strncpy_s( output.data(), output.size(), text, _TRUNCATE );
}

template <typename Value> Value DifferentValue( Value value )
{
    if constexpr ( std::is_same_v<Value, bool> )
    {
        return !value;
    }
    else if constexpr ( std::is_integral_v<Value> )
    {
        return value + 1;
    }
    else
    {
        return value + 0.123046875f;
    }
}

void CheckCinematicApplication( const Runtime::AuthoredScene& parsed )
{
    const Core::CinematicRenderConfig& source = parsed.GetCinematicRenderConfig();
    const uint64_t mask = parsed.GetCinematicOverrideMask();
    Core::CinematicRenderConfig applied = source;

    // Test sensitivity: every field begins different from the parsed source.
    // A set bit must restore its source value; an unset bit must preserve the
    // perturbed inherited value. This mirrors the complete production surface.
#define PERTURB( field ) applied.field = DifferentValue( source.field );
    PERTURB( enabled )
    PERTURB( skyAtmosphereEnabled )
    PERTURB( cloudsEnabled )
    PERTURB( godRaysEnabled )
    PERTURB( volumetricLightingEnabled )
    PERTURB( bloomEnabled )
    PERTURB( fogEnabled )
    PERTURB( terrainReliefEnabled )
    PERTURB( exposure )
    PERTURB( gamma )
    PERTURB( sunAzimuth )
    PERTURB( sunElevation )
    PERTURB( sunColorR )
    PERTURB( sunColorG )
    PERTURB( sunColorB )
    PERTURB( sunIntensity )
    PERTURB( skyHorizonR )
    PERTURB( skyHorizonG )
    PERTURB( skyHorizonB )
    PERTURB( skyZenithR )
    PERTURB( skyZenithG )
    PERTURB( skyZenithB )
    PERTURB( skyGlowStrength )
    PERTURB( cloudCoverage )
    PERTURB( cloudSoftness )
    PERTURB( cloudScale )
    PERTURB( cloudIntensity )
    PERTURB( sunShaftStrength )
    PERTURB( sunShaftFalloff )
    PERTURB( volumetricStrength )
    PERTURB( volumetricDensity )
    PERTURB( volumetricDecay )
    PERTURB( bloomThreshold )
    PERTURB( bloomKnee )
    PERTURB( bloomStrength )
    PERTURB( bloomRadius )
    PERTURB( terrainRelief )
    PERTURB( basinDepth )
    PERTURB( basinRimLift )
    PERTURB( shadow.enabled )
    PERTURB( shadow.terrainCasts )
    PERTURB( shadow.objectsCast )
    PERTURB( shadow.terrainReceives )
    PERTURB( shadow.objectsReceive )
    PERTURB( shadow.mapSize )
    PERTURB( shadow.pcfRadius )
    PERTURB( shadow.strength )
    PERTURB( shadow.softness )
    PERTURB( shadow.depthBias )
    PERTURB( shadow.slopeBias )
    PERTURB( shadow.maxDistance )
    PERTURB( fogColorR )
    PERTURB( fogColorG )
    PERTURB( fogColorB )
    PERTURB( fogStart )
    PERTURB( fogEnd )
    PERTURB( fogDensity )
    PERTURB( fogMaxOpacity )
    PERTURB( skyMode )
    PERTURB( terrainMode )
    PERTURB( objectStyle )
    PERTURB( waterMode )
    PERTURB( styleSaturation )
    PERTURB( styleContrast )
    PERTURB( styleVignette )
    PERTURB( terrainTintR )
    PERTURB( terrainTintG )
    PERTURB( terrainTintB )
    PERTURB( terrainAccentR )
    PERTURB( terrainAccentG )
    PERTURB( terrainAccentB )
    PERTURB( terrainGridScale )
    PERTURB( terrainGridStrength )
    PERTURB( waterTintR )
    PERTURB( waterTintG )
    PERTURB( waterTintB )
    PERTURB( waterAlpha )
    PERTURB( waterReflectionStrength )
    PERTURB( waterGlintStrength )
    PERTURB( basinCenterX )
    PERTURB( basinCenterZ )
    PERTURB( basinRadiusX )
    PERTURB( basinRadiusZ )
    PERTURB( basinFeather )
#undef PERTURB

    const Core::CinematicRenderConfig inherited = applied;
    Runtime::ApplyCinematicSceneOverrides( applied, mask, source );

#define CHECK_OVERRIDE( bit, field )                                                                                       \
    CHECK( applied.field == ( ( mask & ( bit ) ) != 0 ? source.field : inherited.field ) );
    CHECK_OVERRIDE( Runtime::SCENE_CINE_RENDERING, enabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SKY_ATMOSPHERE, skyAtmosphereEnabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_CLOUDS, cloudsEnabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_GOD_RAYS, godRaysEnabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_VOLUMETRIC_LIGHTING, volumetricLightingEnabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BLOOM, bloomEnabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_FOG, fogEnabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_RELIEF_ENABLED, terrainReliefEnabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_EXPOSURE, exposure )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_GAMMA, gamma )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SUN_AZIMUTH, sunAzimuth )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SUN_ELEVATION, sunElevation )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SUN_COLOR_R, sunColorR )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SUN_COLOR_G, sunColorG )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SUN_COLOR_B, sunColorB )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SUN_INTENSITY, sunIntensity )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SKY_HORIZON_R, skyHorizonR )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SKY_HORIZON_G, skyHorizonG )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SKY_HORIZON_B, skyHorizonB )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SKY_ZENITH_R, skyZenithR )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SKY_ZENITH_G, skyZenithG )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SKY_ZENITH_B, skyZenithB )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SKY_GLOW_STRENGTH, skyGlowStrength )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_CLOUD_COVERAGE, cloudCoverage )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_CLOUD_SOFTNESS, cloudSoftness )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_CLOUD_SCALE, cloudScale )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_CLOUD_INTENSITY, cloudIntensity )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SUN_SHAFT_STRENGTH, sunShaftStrength )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SUN_SHAFT_FALLOFF, sunShaftFalloff )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_VOLUMETRIC_STRENGTH, volumetricStrength )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_VOLUMETRIC_DENSITY, volumetricDensity )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_VOLUMETRIC_DECAY, volumetricDecay )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BLOOM_THRESHOLD, bloomThreshold )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BLOOM_KNEE, bloomKnee )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BLOOM_STRENGTH, bloomStrength )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BLOOM_RADIUS, bloomRadius )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_RELIEF, terrainRelief )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BASIN_DEPTH, basinDepth )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BASIN_RIM_LIFT, basinRimLift )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOWS, shadow.enabled )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_PARTICIPATION, shadow.terrainCasts )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_PARTICIPATION, shadow.objectsCast )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_PARTICIPATION, shadow.terrainReceives )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_PARTICIPATION, shadow.objectsReceive )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_MAP_SIZE, shadow.mapSize )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_PCF_RADIUS, shadow.pcfRadius )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_STRENGTH, shadow.strength )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_SOFTNESS, shadow.softness )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_DEPTH_BIAS, shadow.depthBias )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_SLOPE_BIAS, shadow.slopeBias )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_SHADOW_MAX_DISTANCE, shadow.maxDistance )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_FOG_COLOR_R, fogColorR )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_FOG_COLOR_G, fogColorG )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_FOG_COLOR_B, fogColorB )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_FOG_START, fogStart )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_FOG_END, fogEnd )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_FOG_DENSITY, fogDensity )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_FOG_MAX_OPACITY, fogMaxOpacity )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_STYLE_MODES, skyMode )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_STYLE_MODES, terrainMode )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_STYLE_MODES, objectStyle )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_STYLE_MODES, waterMode )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_STYLE_GRADE, styleSaturation )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_STYLE_GRADE, styleContrast )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_STYLE_GRADE, styleVignette )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_TINT, terrainTintR )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_TINT, terrainTintG )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_TINT, terrainTintB )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_ACCENT, terrainAccentR )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_ACCENT, terrainAccentG )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_ACCENT, terrainAccentB )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_GRID, terrainGridScale )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_TERRAIN_GRID, terrainGridStrength )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_WATER_TINT, waterTintR )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_WATER_TINT, waterTintG )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_WATER_TINT, waterTintB )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_WATER_PROFILE, waterAlpha )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_WATER_PROFILE, waterReflectionStrength )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_WATER_PROFILE, waterGlintStrength )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BASIN_MASK, basinCenterX )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BASIN_MASK, basinCenterZ )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BASIN_MASK, basinRadiusX )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BASIN_MASK, basinRadiusZ )
    CHECK_OVERRIDE( Runtime::SCENE_CINE_BASIN_MASK, basinFeather )
#undef CHECK_OVERRIDE
}

TEST_CASE( "Look Lab standalone style is complete stable and parser exact" )
{
    TemporaryLookLabOutput cleanup;
    Core::SbDiagnosticStore diagnostics;
    const Runtime::LookLabCandidate candidate = Runtime::GenerateLookLabCandidate( 0x0123456789abcdefull );
    Scene::StandaloneStyleSnapshot snapshot = SnapshotFromCandidate( candidate );
    snapshot.cinematic.shadow.terrainCasts = false;
    snapshot.cinematic.shadow.objectsCast = true;
    snapshot.cinematic.shadow.terrainReceives = false;
    snapshot.cinematic.shadow.objectsReceive = true;
    std::string first;
    std::string second;
    REQUIRE( Scene::StandaloneStyleWriter::Serialize( diagnostics, snapshot, first ).Ok() );
    REQUIRE( Scene::StandaloneStyleWriter::Serialize( diagnostics, snapshot, second ).Ok() );
    CHECK( first == second );
    CHECK( FingerprintText( first ) == 0xa79343801bda3d50ull );
    CHECK( first.back() == '\n' );

    const size_t format = first.find( "\"format\"" );
    const size_t version = first.find( "\"version\"" );
    const size_t cinematic = first.find( "\"cinematic\"" );
    const size_t modes = first.find( "\"styleModes\"" );
    const size_t shadows = first.find( "\"shadows\"" );
    const size_t materials = first.find( "\"objectMaterials\"" );
    REQUIRE( format < version );
    REQUIRE( version < cinematic );
    REQUIRE( cinematic < modes );
    REQUIRE( modes < shadows );
    REQUIRE( shadows < materials );
    CHECK( first.find( "\"include\"" ) == std::string::npos );
    CHECK( first.find( "\"mode\": \"textured\"" ) != std::string::npos );

    const std::string stylePath = std::string( ROOT ) + "/nested/look.style.json";
    REQUIRE( Scene::StandaloneStyleWriter::SaveAtomic( diagnostics, snapshot, stylePath.c_str() ).Ok() );
    CHECK( ReadText( stylePath.c_str() ) == first );

    Runtime::AuthoredScene parsed;
    REQUIRE( Runtime::AuthoredScene::TryLoadStyleFromFile( diagnostics, stylePath.c_str(), parsed ).Ok() );
    constexpr uint64_t allResolvedMask = ( 1ull << 63 ) - 1ull;
    CHECK( parsed.GetCinematicOverrideMask() == allResolvedMask );
    REQUIRE( parsed.GetObjectMaterialOverrideCount() == static_cast<int>( snapshot.materialRules.size() ) );

    const Core::ShadowQualityConfig& parsedShadow = parsed.GetCinematicRenderConfig().shadow;
    CHECK_FALSE( parsedShadow.terrainCasts );
    CHECK( parsedShadow.objectsCast );
    CHECK_FALSE( parsedShadow.terrainReceives );
    CHECK( parsedShadow.objectsReceive );

    std::string reparsed;
    REQUIRE( Scene::StandaloneStyleWriter::Serialize( diagnostics, SnapshotFromParsedStyle( parsed ), reparsed ).Ok() );
    CHECK( reparsed == first );
}

TEST_CASE( "Look Lab fresh-process output reloads without generator or catalog input" )
{
    std::array<char, 16> phase = {};
    std::size_t phaseLength = 0;
    getenv_s( &phaseLength, phase.data(), phase.size(), "SKULLBONEZ_LOOK_LAB_FRESH_PROCESS" );

    if ( phaseLength == 0 )
    {
        return;
    }

    Core::SbDiagnosticStore diagnostics;
    const std::string stylePath = std::string( FRESH_PROCESS_ROOT ) + "/look.style.json";

    if ( std::strcmp( phase.data(), "produce" ) == 0 )
    {
        Scene::StandaloneStyleSnapshot snapshot =
            SnapshotFromCandidate( Runtime::GenerateLookLabCandidate( 0x5eedf11a11c0ffeeull ) );
        snapshot.cinematic.shadow.terrainCasts = false;
        snapshot.cinematic.shadow.objectsReceive = false;
        REQUIRE( Scene::StandaloneStyleWriter::SaveAtomic( diagnostics, snapshot, stylePath.c_str() ).Ok() );
        REQUIRE( std::filesystem::exists( stylePath ) );
        return;
    }

    REQUIRE( std::strcmp( phase.data(), "consume" ) == 0 );
    REQUIRE( std::filesystem::exists( stylePath ) );
    Runtime::AuthoredScene parsed;
    REQUIRE( Runtime::AuthoredScene::TryLoadStyleFromFile( diagnostics, stylePath.c_str(), parsed ).Ok() );
    constexpr uint64_t allResolvedMask = ( 1ull << 63 ) - 1ull;
    CHECK( parsed.GetCinematicOverrideMask() == allResolvedMask );
    CHECK_FALSE( parsed.GetCinematicRenderConfig().shadow.terrainCasts );
    CHECK_FALSE( parsed.GetCinematicRenderConfig().shadow.objectsReceive );
    REQUIRE( parsed.GetObjectMaterialOverrideCount() == static_cast<int>( Runtime::LOOK_LAB_MATERIAL_RULE_COUNT ) );
    std::string reserialized;
    REQUIRE( Scene::StandaloneStyleWriter::Serialize( diagnostics, SnapshotFromParsedStyle( parsed ), reserialized ).Ok() );
    CHECK( reserialized == ReadText( stylePath.c_str() ) );
}

TEST_CASE( "Look Lab keeps every tracked curated style parser-compatible" )
{
    namespace fs = std::filesystem;
    Core::SbDiagnosticStore diagnostics;
    std::vector<fs::path> styles;

    for ( const fs::directory_entry& entry : fs::directory_iterator( "SkullbonezData/styles" ) )
    {
        if ( entry.is_regular_file() && entry.path().filename().generic_string().ends_with( ".style.json" ) )
        {
            styles.push_back( entry.path() );
        }
    }

    std::sort( styles.begin(), styles.end() );
    REQUIRE( styles.size() == 23 );

    for ( const fs::path& style : styles )
    {
        INFO( "style=" << style.generic_string() );
        Runtime::AuthoredScene parsed;
        REQUIRE( Runtime::AuthoredScene::TryLoadStyleFromFile( diagnostics, style.generic_string().c_str(), parsed ).Ok() );
        CHECK( parsed.GetSchemaVersion() == 1 );
        CheckCinematicApplication( parsed );
    }
}

TEST_CASE( "Look Lab style publication replaces atomically and fails boundedly" )
{
    TemporaryLookLabOutput cleanup;
    Core::SbDiagnosticStore diagnostics;
    Scene::StandaloneStyleSnapshot snapshot = SnapshotFromCandidate( Runtime::GenerateLookLabCandidate( 31 ) );
    const std::string stylePath = std::string( ROOT ) + "/replace/look.style.json";
    std::filesystem::create_directories( std::filesystem::path( stylePath ).parent_path() );
    {
        std::ofstream stale( stylePath, std::ios::binary );
        stale << "stale";
    }

    std::string expected;
    REQUIRE( Scene::StandaloneStyleWriter::Serialize( diagnostics, snapshot, expected ).Ok() );
    REQUIRE( Scene::StandaloneStyleWriter::SaveAtomic( diagnostics, snapshot, stylePath.c_str() ).Ok() );
    CHECK( ReadText( stylePath.c_str() ) == expected );

    for ( const std::filesystem::directory_entry& entry :
          std::filesystem::directory_iterator( std::filesystem::path( stylePath ).parent_path() ) )
    {
        CHECK( entry.path().filename().generic_string().find( ".tmp." ) == std::string::npos );
    }

    snapshot.cinematic.exposure = std::numeric_limits<float>::quiet_NaN();
    Core::SbResult invalidValue = Scene::StandaloneStyleWriter::SaveAtomic( diagnostics, snapshot, stylePath.c_str() );
    REQUIRE_FALSE( invalidValue.Ok() );
    CHECK( std::strlen( invalidValue.ErrorMessage() ) < Core::SbDiagnosticStore::MESSAGE_CAPACITY );
    CHECK( ReadText( stylePath.c_str() ) == expected );

    snapshot = SnapshotFromCandidate( Runtime::GenerateLookLabCandidate( 31 ) );
    const std::string directoryTarget = std::string( ROOT ) + "/directory_target";
    std::filesystem::create_directories( directoryTarget );
    Core::SbResult invalidPath = Scene::StandaloneStyleWriter::SaveAtomic( diagnostics, snapshot, directoryTarget.c_str() );
    REQUIRE_FALSE( invalidPath.Ok() );
    CHECK( std::strlen( invalidPath.ErrorMessage() ) < Core::SbDiagnosticStore::MESSAGE_CAPACITY );
    CHECK( std::filesystem::is_directory( directoryTarget ) );
}

TEST_CASE( "Look Lab bundle naming and receipt facts are exact and collision safe" )
{
    TemporaryLookLabOutput cleanup;
    Core::SbDiagnosticStore diagnostics;
    constexpr uint64_t seed = 0x00000000000000abull;
    Runtime::LookLabBundlePaths paths;
    const std::string bundleRoot = std::string( ROOT ) + "/LookLab";
    REQUIRE( Runtime::LookLabBundleWriter::CreateBundleDirectory( diagnostics, bundleRoot.c_str(), "2026-08-01_17-23-45",
                                                                  seed, paths )
                 .Ok() );
    CHECK( std::string( paths.directory.data() ).ends_with( "2026-08-01_17-23-45_seed_00000000000000ab" ) );
    CHECK( std::filesystem::is_directory( paths.directory.data() ) );

    Runtime::LookLabBundlePaths collisionPaths;
    Core::SbResult collision = Runtime::LookLabBundleWriter::CreateBundleDirectory( diagnostics, bundleRoot.c_str(),
                                                                                    "2026-08-01_17-23-45", seed,
                                                                                    collisionPaths );
    REQUIRE_FALSE( collision.Ok() );
    CHECK( std::strlen( collision.ErrorMessage() ) < Core::SbDiagnosticStore::MESSAGE_CAPACITY );

    const Runtime::LookLabCandidate candidate = Runtime::GenerateLookLabCandidate( seed );
    const Scene::StandaloneStyleSnapshot snapshot = SnapshotFromCandidate( candidate );
    Runtime::LookLabReceiptFacts facts;
    CopyText( facts.localTimestamp, "2026-08-01_17-23-45" );
    facts.utcOffsetMinutes = 10 * 60;
    facts.seed = seed;
    facts.generatorVersion = candidate.generatorVersion;
    facts.recipe = candidate.recipe;
    CopyText( facts.sourceScenePath, "SkullbonezData/scenes/concept.scene.json" );
    CopyText( facts.sourceSceneDisplayName, "Concept Basin" );
    facts.styleStatus = Runtime::LookLabArtifactStatus::Saved;
    facts.screenshotStatus = Runtime::LookLabArtifactStatus::Pending;
    REQUIRE( Scene::StandaloneStyleWriter::SaveAtomic( diagnostics, snapshot, paths.style.data() ).Ok() );
    REQUIRE( Runtime::LookLabBundleWriter::SaveReceiptAtomic( diagnostics, facts, snapshot, paths ).Ok() );
    const std::string pending = ReadText( paths.receipt.data() );
    CHECK( pending.find( "local_timestamp=2026-08-01_17-23-45\n" ) != std::string::npos );
    CHECK( pending.find( "utc_offset=+10:00\n" ) != std::string::npos );
    CHECK( pending.find( "seed=00000000000000ab\n" ) != std::string::npos );
    CHECK( pending.find( "style_status=saved\n" ) != std::string::npos );
    CHECK( pending.find( "screenshot_status=pending\n" ) != std::string::npos );
    CHECK( pending.find( "cinematic.exposure=" ) != std::string::npos );
    CHECK( pending.find( "objectMaterials[0].target=\"balls\"" ) != std::string::npos );

    facts.screenshotStatus = Runtime::LookLabArtifactStatus::Failed;
    CopyText( facts.screenshotDiagnostic, "capture rejected" );
    REQUIRE( Runtime::LookLabBundleWriter::SaveReceiptAtomic( diagnostics, facts, snapshot, paths ).Ok() );
    const std::string failed = ReadText( paths.receipt.data() );
    CHECK( failed != pending );
    CHECK( failed.find( "screenshot_status=failed\n" ) != std::string::npos );
    CHECK( failed.find( "screenshot_diagnostic=capture rejected\n" ) != std::string::npos );

    Runtime::LookLabBundlePaths invalidPaths;
    CHECK_FALSE(
        Runtime::LookLabBundleWriter::CreateBundleDirectory( diagnostics, bundleRoot.c_str(), "bad", seed, invalidPaths )
            .Ok() );
    CopyText( facts.sourceSceneDisplayName, "bad\nmetadata" );
    std::string rejectedReceipt;
    CHECK_FALSE( Runtime::LookLabBundleWriter::BuildReceipt( diagnostics, facts, snapshot, paths, rejectedReceipt ).Ok() );
    facts.localTimestamp.fill( '7' );
    CHECK_FALSE( Runtime::LookLabBundleWriter::BuildReceipt( diagnostics, facts, snapshot, paths, rejectedReceipt ).Ok() );
}
} // namespace
