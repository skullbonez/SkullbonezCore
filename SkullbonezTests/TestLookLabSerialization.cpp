/*
File: TestLookLabSerialization.cpp
Purpose:
  Verifies exact standalone style, bundle, receipt, and atomic-write contracts.

Summary:
  Tests serialize deterministic candidates, reload them through the production
  style parser, and serialize the parsed values again byte-for-byte. Filesystem
  cases cover parent creation, replacement, collisions, and bounded failures.

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
#include "../SkullbonezSource/Scene/AuthoredScene.h"
#include "../SkullbonezSource/Scene/StandaloneStyleWriter.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
using namespace SkullbonezCore;
constexpr const char* ROOT = "TestOutput/look_lab_serialization";

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

TEST_CASE( "Look Lab standalone style is complete stable and parser exact" )
{
    TemporaryLookLabOutput cleanup;
    Core::SbDiagnosticStore diagnostics;
    const Runtime::LookLabCandidate candidate = Runtime::GenerateLookLabCandidate( 0x0123456789abcdefull );
    const Scene::StandaloneStyleSnapshot snapshot = SnapshotFromCandidate( candidate );
    std::string first;
    std::string second;
    REQUIRE( Scene::StandaloneStyleWriter::Serialize( diagnostics, snapshot, first ).Ok() );
    REQUIRE( Scene::StandaloneStyleWriter::Serialize( diagnostics, snapshot, second ).Ok() );
    CHECK( first == second );
    CHECK( FingerprintText( first ) == 0xc31502d6333e241bull );
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
    constexpr uint64_t allResolvedMask = ( ( 1ull << 63 ) - 1ull ) & ~( 1ull << 55 );
    CHECK( parsed.GetCinematicOverrideMask() == allResolvedMask );
    REQUIRE( parsed.GetObjectMaterialOverrideCount() == static_cast<int>( snapshot.materialRules.size() ) );

    std::string reparsed;
    REQUIRE( Scene::StandaloneStyleWriter::Serialize( diagnostics, SnapshotFromParsedStyle( parsed ), reparsed ).Ok() );
    CHECK( reparsed == first );
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
