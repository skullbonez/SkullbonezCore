/*
File: SkullbonezTests/TestConfig.cpp
Purpose:
  Locks the public SkullbonezCore::Core::EngineConfig file-load and stable-dump compatibility contract.

Summary:
  SkullbonezCore::Core::EngineConfig::Load treats engine.cfg as a tolerant cold data boundary: valid
  rows apply, while unknown or malformed rows warn and leave their destinations
  unchanged. Missing optional input keeps defaults, but other I/O failures are
  reported. Dump walks the same registry in one stable compatibility order.

Glossary:
  Registry row: One public key, value type/range, and SkullbonezCore::Core::EngineConfig destination.
  Stable key hash: Compact fingerprint of every dumped key in traversal order;
    it detects an omission, duplicate, rename, or reorder without copying the
    full 224-key registry into this test.

Invariants:
  - The dump contains one header plus exactly 224 unique setting rows.
  - Rejected rows do not block later valid rows in the same file.
  - Unsupported format versions fail before any setting mutates the config.
  - Only ENOENT is accepted as an absent optional file.
  - A row that crosses the fixed parser buffer fails without publishing its prefix.
  - Temporary fixtures are cold test artifacts and are removed after each run.

Related:
  - SkullbonezSource/Core/Config.h
  - SkullbonezSource/Core/Config.cpp
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/StdioFile.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

using SkullbonezCore::Core::EngineConfig;

namespace
{
constexpr const char* kConfigInputPath = "unit_engine_config_input.cfg";
constexpr const char* kConfigDumpPath = "unit_engine_config_dump.cfg";
constexpr const char* kConfigDirectoryPath = "unit_engine_config_directory";
constexpr uint64_t kStableConfigKeyOrderHash = 0x70d0f1072c9936caull;

struct TemporaryConfigFiles
{
    ~TemporaryConfigFiles()
    {
        std::remove( kConfigInputPath );
        std::remove( kConfigDumpPath );
        std::error_code error;
        std::filesystem::remove_all( kConfigDirectoryPath, error );
    }
};

bool WriteTextFile( const char* path, const char* text )
{
    FILE* file = nullptr;

    if ( SkullbonezCore::Core::OpenStdioFile( file, path, "wb" ) != 0 || !file )
    {
        return false;
    }

    const size_t length = strlen( text );
    const bool wroteAll = fwrite( text, 1, length, file ) == length;
    return fclose( file ) == 0 && wroteAll;
}

bool WriteBytes( const char* path, const std::string& bytes )
{
    FILE* file = nullptr;
    if ( SkullbonezCore::Core::OpenStdioFile( file, path, "wb" ) != 0 || !file )
    {
        return false;
    }
    const bool wroteAll = fwrite( bytes.data(), 1u, bytes.size(), file ) == bytes.size();
    return fclose( file ) == 0 && wroteAll;
}

bool DumpConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    FILE* file = nullptr;

    if ( SkullbonezCore::Core::OpenStdioFile( file, kConfigDumpPath, "wb" ) != 0 || !file )
    {
        return false;
    }

    config.Dump( file );
    return fclose( file ) == 0;
}

std::vector<std::string> ReadLines( const char* path )
{
    std::ifstream input( path );
    std::vector<std::string> lines;
    std::string line;

    while ( std::getline( input, line ) )
    {
        lines.push_back( line );
    }

    return lines;
}

uint64_t AppendStableHash( uint64_t hash, const std::string& text )
{
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    for ( unsigned char byte : text )
    {
        hash ^= byte;
        hash *= kFnvPrime;
    }

    hash ^= static_cast<unsigned char>( '\n' );
    hash *= kFnvPrime;
    return hash;
}
} // namespace

TEST_CASE( "SkullbonezCore::Core::EngineConfig: valid file produces the stable complete dump order" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath, "screen_x = 2048\n"
                                              "fullscreen = yes\n"
                                              "gravity = -9.5\n"
                                              "sky_front = unit_sky_front.jpg\n"
                                              "physics_parallel_mutual_gravity = off\n"
                                              "replay_trajectory_future_width = 1.35\n"
                                              "replay_trajectory_future_edge_feather = 3.0\n"
                                              "replay_trajectory_selected_emphasis = 0.60\n" ) );

    SkullbonezCore::Core::EngineConfig config;
    REQUIRE( config.Load( diagnostics, kConfigInputPath ).Ok() );
    CHECK( config.window.screenX == 2048 );
    CHECK( config.window.fullscreen );
    CHECK( config.worldForces.gravity == doctest::Approx( -9.5f ) );
    CHECK( config.assetPaths.skyFront == "unit_sky_front.jpg" );
    CHECK_FALSE( config.physicsExecution.parallelMutualGravity );
    CHECK( config.ordinaryRender.replayTrajectory.futureWidth == doctest::Approx( 1.35f ) );
    CHECK( config.ordinaryRender.replayTrajectory.futureEdgeFeather == doctest::Approx( 1.0f ) );
    CHECK( config.ordinaryRender.replayTrajectory.selectedEmphasis == doctest::Approx( 0.60f ) );

    REQUIRE( DumpConfig( config ) );
    const std::vector<std::string> lines = ReadLines( kConfigDumpPath );
    REQUIRE( lines.size() == 225 );
    CHECK( lines.front() == "[config]" );

    uint64_t keyOrderHash = 14695981039346656037ull;
    std::set<std::string> uniqueKeys;

    for ( size_t lineIndex = 1; lineIndex < lines.size(); ++lineIndex )
    {
        const size_t separator = lines[lineIndex].find( " = " );
        REQUIRE_MESSAGE( separator != std::string::npos, "Every dump row must retain key = value syntax" );
        const std::string key = lines[lineIndex].substr( 0, separator );
        CHECK_MESSAGE( uniqueKeys.insert( key ).second, "Every config key must be dumped exactly once" );
        keyOrderHash = AppendStableHash( keyOrderHash, key );
    }

    CHECK( uniqueKeys.size() == 224 );
    CHECK( keyOrderHash == kStableConfigKeyOrderHash );
    CHECK( lines[1] == "screen_x = 2048" );
    CHECK( lines.back() == "presentation_interpolation = 1" );
}

TEST_CASE( "SkullbonezCore::Core::EngineConfig: unknown key is ignored and later valid rows still apply" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath, "screen_x = 2222\n"
                                              "unit_unknown_setting = 77\n"
                                              "screen_y = 777\n" ) );

    SkullbonezCore::Core::EngineConfig config;
    REQUIRE( config.Load( diagnostics, kConfigInputPath ).Ok() );
    CHECK( config.window.screenX == 2222 );
    CHECK( config.window.screenY == 777 );
    CHECK( config.window.refreshRate == 75 );
}

TEST_CASE( "SkullbonezCore::Core::EngineConfig: malformed and out-of-range values preserve defaults" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath, "screen_x = 0\n"
                                              "gravity = not-a-number\n"
                                              "physics_sleep_frames = 1000001\n"
                                              "screen_y = 720\n" ) );

    SkullbonezCore::Core::EngineConfig config;
    REQUIRE( config.Load( diagnostics, kConfigInputPath ).Ok() );
    CHECK( config.window.screenX == 1800 );
    CHECK( config.worldForces.gravity == doctest::Approx( -30.0f ) );
    CHECK( config.physicsSleep.frames == 30 );
    CHECK( config.window.screenY == 720 );
}

TEST_CASE( "SkullbonezCore::Core::EngineConfig: only an absent optional file is ignored" )
{
    TemporaryConfigFiles files;
    std::remove( kConfigInputPath );
    EngineConfig missing;
    CHECK( missing.Load( diagnostics, kConfigInputPath ).Ok() );

    std::error_code directoryError;
    std::filesystem::remove_all( kConfigDirectoryPath, directoryError );
    REQUIRE_FALSE( directoryError );
    REQUIRE( std::filesystem::create_directory( kConfigDirectoryPath, directoryError ) );
    REQUIRE_FALSE( directoryError );
    EngineConfig inaccessible;
    inaccessible.window.screenX = 1234;
    const auto result = inaccessible.Load( diagnostics, kConfigDirectoryPath );
    CHECK_FALSE( result.Ok() );
    CHECK( std::string( result.ErrorOwner() ) == "Core/EngineConfig" );
    CHECK( std::string( result.ErrorMessage() ).find( kConfigDirectoryPath ) != std::string::npos );
    CHECK( inaccessible.window.screenX == 1234 );
}

TEST_CASE( "SkullbonezCore::Core::EngineConfig: settings read failure publishes no valid prefix" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath, "screen_x = 2222\nscreen_y = 777\n" ) );
    EngineConfig config;
    config.window.screenX = 1234;
    config.window.screenY = 567;

    SkullbonezCore::Core::SetEngineConfigSettingsReadFailureAfterLineForTest( 1 );
    const auto result = config.Load( diagnostics, kConfigInputPath );
    SkullbonezCore::Core::SetEngineConfigSettingsReadFailureAfterLineForTest( -1 );

    CHECK_FALSE( result.Ok() );
    CHECK( std::string( result.ErrorOwner() ) == "Core/EngineConfig" );
    CHECK( std::string( result.ErrorMessage() ).find( "settings read" ) != std::string::npos );
    CHECK( config.window.screenX == 1234 );
    CHECK( config.window.screenY == 567 );
}

TEST_CASE( "SkullbonezCore::Core::EngineConfig: overlong rows fail before a truncated prefix can publish" )
{
    TemporaryConfigFiles files;
    const std::string oversizedValue( 600u, 'a' );
    const std::string contents = "screen_x = 2222\nsky_front = " + oversizedValue + "\nscreen_y = 777\n";
    REQUIRE( WriteTextFile( kConfigInputPath, contents.c_str() ) );
    EngineConfig config;
    config.window.screenX = 1234;
    config.window.screenY = 567;
    config.assetPaths.skyFront = "prior_sky.jpg";

    SkullbonezCore::Core::SetEngineConfigFormatLineLimitBypassForTest( true );
    const auto result = config.Load( diagnostics, kConfigInputPath );
    SkullbonezCore::Core::SetEngineConfigFormatLineLimitBypassForTest( false );

    CHECK_FALSE( result.Ok() );
    CHECK( std::string( result.ErrorOwner() ) == "Core/EngineConfig" );
    CHECK( std::string( result.ErrorMessage() ).find( "does not fit" ) != std::string::npos );
    CHECK( std::string( result.ErrorMessage() ).find( ":2" ) != std::string::npos );
    CHECK( config.window.screenX == 1234 );
    CHECK( config.window.screenY == 567 );
    CHECK( config.assetPaths.skyFront == "prior_sky.jpg" );
}

TEST_CASE( "SkullbonezCore::Core::EngineConfig: line buffer boundaries distinguish terminators from continuation" )
{
    TemporaryConfigFiles files;
    const std::string prefix = "sky_front = ";
    const std::string newlineValue( 498u, 'b' );
    const std::string eofValue( 499u, 'c' );
    REQUIRE( prefix.size() + newlineValue.size() == 510u );
    REQUIRE( prefix.size() + eofValue.size() == 511u );

    const std::string newlineTerminated = prefix + newlineValue + "\nscreen_x = 2222\n";
    REQUIRE( WriteTextFile( kConfigInputPath, newlineTerminated.c_str() ) );
    EngineConfig newlineConfig;
    REQUIRE( newlineConfig.Load( diagnostics, kConfigInputPath ).Ok() );
    CHECK( newlineConfig.assetPaths.skyFront == newlineValue );
    CHECK( newlineConfig.window.screenX == 2222 );

    const std::string eofTerminated = prefix + eofValue;
    REQUIRE( WriteTextFile( kConfigInputPath, eofTerminated.c_str() ) );
    EngineConfig eofConfig;
    REQUIRE( eofConfig.Load( diagnostics, kConfigInputPath ).Ok() );
    CHECK( eofConfig.assetPaths.skyFront == eofValue );

    const std::string newlineOverflow = prefix + eofValue + "\n";
    REQUIRE( WriteTextFile( kConfigInputPath, newlineOverflow.c_str() ) );
    EngineConfig newlineOverflowConfig;
    CHECK_FALSE( newlineOverflowConfig.Load( diagnostics, kConfigInputPath ).Ok() );

    const std::string eofOverflow = prefix + std::string( 500u, 'd' );
    REQUIRE( WriteTextFile( kConfigInputPath, eofOverflow.c_str() ) );
    EngineConfig eofOverflowConfig;
    CHECK_FALSE( eofOverflowConfig.Load( diagnostics, kConfigInputPath ).Ok() );

    std::string ctrlZContinuation = prefix + eofValue;
    ctrlZContinuation.push_back( static_cast<char>( 0x1a ) );
    ctrlZContinuation += "screen_x = 2222\n";
    REQUIRE( WriteTextFile( kConfigInputPath, ctrlZContinuation.c_str() ) );
    EngineConfig ctrlZConfig;
    CHECK_FALSE( ctrlZConfig.Load( diagnostics, kConfigInputPath ).Ok() );

    std::string nulContinuation = prefix + eofValue;
    nulContinuation.push_back( '\0' );
    nulContinuation += "screen_x = 2222\n";
    REQUIRE( WriteBytes( kConfigInputPath, nulContinuation ) );
    EngineConfig nulConfig;
    CHECK_FALSE( nulConfig.Load( diagnostics, kConfigInputPath ).Ok() );
}

TEST_CASE( "SkullbonezCore::Core::EngineConfig: current version loads and future version fails before mutation" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath, "format_version = 6\nscreen_x = 2048\n" ) );

    SkullbonezCore::Core::EngineConfig current;
    REQUIRE( current.Load( diagnostics, kConfigInputPath ).Ok() );
    CHECK( current.window.screenX == 2048 );

    REQUIRE( WriteTextFile( kConfigInputPath, "format_version = 5\nterrain_render_step_size = 1\nscreen_x = 1600\n" ) );
    SkullbonezCore::Core::EngineConfig previous;
    REQUIRE( previous.Load( diagnostics, kConfigInputPath ).Ok() );
    CHECK( previous.window.screenX == 1600 );
    CHECK( previous.physicsExecution.parallelMutualGravity );

    REQUIRE( WriteTextFile( kConfigInputPath, "screen_x = 1234\nformat_version = 7\n" ) );
    SkullbonezCore::Core::EngineConfig future;
    const auto result = future.Load( diagnostics, kConfigInputPath );
    CHECK_FALSE( result.Ok() );
    CHECK( std::string( result.ErrorOwner() ) == "Core/EngineConfig" );
    CHECK( std::string( result.ErrorMessage() ).find( "version 7" ) != std::string::npos );
    CHECK( std::string( result.ErrorMessage() ).find( "current version 6" ) != std::string::npos );
    CHECK( future.window.screenX == 1800 );
}


TEST_CASE( "SkullbonezCore::Core::EngineConfig: v1 physics migration is deterministic and rejects invalid rows" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath, "format_version = 1\n"
                                              "gravity = -12.5\n"
                                              "physics_sleep_frames = 1000001\n"
                                              "persistent_contact_solver_iterations = 1000001\n"
                                              "physics_parallel_mutual_gravity = off\n"
                                              "physics_v1_unknown = 77\n" ) );

    EngineConfig first;
    REQUIRE( first.Load( diagnostics, kConfigInputPath ).Ok() );
    CHECK( first.worldForces.gravity == doctest::Approx( -12.5f ) );
    CHECK( first.physicsSleep.frames == 30 );
    CHECK( first.persistentContactSolver.iterations == 12 );
    CHECK_FALSE( first.physicsExecution.parallelMutualGravity );
    REQUIRE( DumpConfig( first ) );
    const std::vector<std::string> firstDump = ReadLines( kConfigDumpPath );

    EngineConfig second;
    REQUIRE( second.Load( diagnostics, kConfigInputPath ).Ok() );
    REQUIRE( DumpConfig( second ) );
    CHECK( ReadLines( kConfigDumpPath ) == firstDump );
}
