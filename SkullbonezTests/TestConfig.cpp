/*
File: SkullbonezTests/TestConfig.cpp
Purpose:
  Locks the public EngineConfig file-load and stable-dump compatibility contract.

Mental model:
  EngineConfig::Load treats engine.cfg as a tolerant cold data boundary: valid
  rows apply, while unknown or malformed rows warn and leave their destinations
  unchanged. Dump walks the same registry in one stable compatibility order.

Glossary:
  Registry row: One public key, value type/range, and EngineConfig destination.
  Stable key hash: Compact fingerprint of every dumped key in traversal order;
    it detects an omission, duplicate, rename, or reorder without copying the
    full 218-key registry into this test.

Invariants:
  - The dump contains one header plus exactly 218 unique setting rows.
  - Rejected rows do not block later valid rows in the same file.
  - Temporary fixtures are cold test artifacts and are removed after each run.

Related:
  - SkullbonezSource/Core/Config.h
  - SkullbonezSource/Core/Config.cpp
  - Agentic/Plans/TODO/engine-config-decomposition.md
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Config.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using SkullbonezCore::Basics::EngineConfig;

namespace
{
constexpr const char* kConfigInputPath = "unit_engine_config_input.cfg";
constexpr const char* kConfigDumpPath = "unit_engine_config_dump.cfg";
constexpr uint64_t kStableConfigKeyOrderHash = 0x2de8f34ff5b0a129ull;

struct TemporaryConfigFiles
{
    ~TemporaryConfigFiles()
    {
        std::remove( kConfigInputPath );
        std::remove( kConfigDumpPath );
    }
};

bool WriteTextFile( const char* path, const char* text )
{
    FILE* file = nullptr;
    if ( fopen_s( &file, path, "wb" ) != 0 || !file )
    {
        return false;
    }
    const size_t length = strlen( text );
    const bool wroteAll = fwrite( text, 1, length, file ) == length;
    return fclose( file ) == 0 && wroteAll;
}

bool DumpConfig( const EngineConfig& config )
{
    FILE* file = nullptr;
    if ( fopen_s( &file, kConfigDumpPath, "wb" ) != 0 || !file )
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

TEST_CASE( "EngineConfig: valid file produces the stable complete dump order" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath,
                            "screen_x = 2048\n"
                            "fullscreen = yes\n"
                            "gravity = -9.5\n"
                            "sky_front = unit_sky_front.jpg\n"
                            "contact_audio_debug_counters = on\n" ) );

    EngineConfig config;
    config.Load( kConfigInputPath );
    CHECK( config.window.screenX == 2048 );
    CHECK( config.window.fullscreen );
    CHECK( config.worldForces.gravity == doctest::Approx( -9.5f ) );
    CHECK( config.assetPaths.skyFront == "unit_sky_front.jpg" );
    CHECK( config.contactAudio.debugCounters );

    REQUIRE( DumpConfig( config ) );
    const std::vector<std::string> lines = ReadLines( kConfigDumpPath );
    REQUIRE( lines.size() == 219 );
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
    CHECK( uniqueKeys.size() == 218 );
    CHECK( keyOrderHash == kStableConfigKeyOrderHash );
    CHECK( lines[1] == "screen_x = 2048" );
    CHECK( lines.back() == "contact_audio_debug_counters = 1" );
}

TEST_CASE( "EngineConfig: unknown key is ignored and later valid rows still apply" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath,
                            "screen_x = 2222\n"
                            "unit_unknown_setting = 77\n"
                            "screen_y = 777\n" ) );

    EngineConfig config;
    config.Load( kConfigInputPath );
    CHECK( config.window.screenX == 2222 );
    CHECK( config.window.screenY == 777 );
    CHECK( config.window.refreshRate == 75 );
}

TEST_CASE( "EngineConfig: malformed and out-of-range values preserve defaults" )
{
    TemporaryConfigFiles files;
    REQUIRE( WriteTextFile( kConfigInputPath,
                            "screen_x = 0\n"
                            "gravity = not-a-number\n"
                            "physics_sleep_frames = 1000001\n"
                            "screen_y = 720\n" ) );

    EngineConfig config;
    config.Load( kConfigInputPath );
    CHECK( config.window.screenX == 1800 );
    CHECK( config.worldForces.gravity == doctest::Approx( -30.0f ) );
    CHECK( config.physicsSleep.frames == 30 );
    CHECK( config.window.screenY == 720 );
}
