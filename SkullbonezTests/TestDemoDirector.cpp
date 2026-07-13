//
// File: SkullbonezTests/TestDemoDirector.cpp
// Purpose:
//   Cover the cold Demo Director shot-list load/save contract.
//
// Summary:
//   Demo Director playback will be render-only runtime state, but authoring
//   starts with a file boundary. These tests pin that boundary before camera
//   mode and automation code begins consuming it.
//
// Glossary:
//   Shot list: `.shot.json` document that stores ordered demo phases.
//   Round-trip: Load a hand-written document, save it, and load the saved copy
//     with the same fixed-capacity phase data.
//
// Invariants:
//   - Bad shot-list input returns false instead of throwing.
//   - Successful loads preserve every authored pose and timing field used by
//     later director phases.
//
// Related:
//   - SkullbonezSource/Runtime/DemoDirector.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/DemoDirector.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

using SkullbonezCore::Runtime::DemoPhase;
using SkullbonezCore::Runtime::DemoShotList;
using SkullbonezCore::Runtime::LoadDemoShotList;
using SkullbonezCore::Runtime::PhaseAdvance;
using SkullbonezCore::Runtime::SaveDemoShotList;

namespace
{
struct TemporaryShotListFiles
{
    const char* inputPath = "unit_demo_director_input.shot.json";
    const char* savedPath = "unit_demo_director_saved.shot.json";
    const char* malformedPath = "unit_demo_director_malformed.shot.json";

    ~TemporaryShotListFiles()
    {
        std::remove( inputPath );
        std::remove( savedPath );
        std::remove( malformedPath );
    }
};

void WriteTextFile( const char* path, const char* text )
{
    std::ofstream output( path );
    if ( !output )
    {
        throw std::runtime_error( std::string( "TestDemoDirector: failed to create " ) + path );
    }
    output << text;
}

void WriteRoundTripFixture( const char* path )
{
    WriteTextFile( path,
                   "{\n"
                   "  \"format\": \"skullbonez.shot.json\",\n"
                   "  \"version\": 1,\n"
                   "  \"loop\": true,\n"
                   "  \"phases\": [\n"
                   "    {\n"
                   "      \"name\": \"root-line\",\n"
                   "      \"camera\": {\n"
                   "        \"position\": [10.0, 20.0, 30.0],\n"
                   "        \"view\": [40.0, 50.0, 60.0],\n"
                   "        \"up\": [0.0, 1.0, 0.0]\n"
                   "      },\n"
                   "      \"stylePath\": \"SkullbonezData/styles/consequence.style.json\",\n"
                   "      \"advance\": \"RevealAtLeast\",\n"
                   "      \"timerSeconds\": 2.5,\n"
                   "      \"revealThreshold\": 0.6,\n"
                   "      \"blendInSeconds\": 0.75,\n"
                   "      \"revealRate\": 0.5\n"
                   "    },\n"
                   "    {\n"
                   "      \"name\": \"settle\",\n"
                   "      \"camera\": {\n"
                   "        \"position\": [-1.0, 3.0, 5.0],\n"
                   "        \"view\": [7.0, 11.0, 13.0],\n"
                   "        \"up\": [0.0, 0.9, 0.1]\n"
                   "      },\n"
                   "      \"stylePath\": \"SkullbonezData/styles/low_poly_art_style.style.json\",\n"
                   "      \"advance\": \"Timer\",\n"
                   "      \"timerSeconds\": 4.25,\n"
                   "      \"revealThreshold\": 1.0,\n"
                   "      \"blendInSeconds\": 1.5,\n"
                   "      \"revealRate\": 1.25\n"
                   "    }\n"
                   "  ]\n"
                   "}\n" );
}

void CheckPhaseMatches( const DemoPhase& phase,
                        const char* name,
                        PhaseAdvance advance,
                        float eyeX,
                        float viewY,
                        float upZ,
                        float timerSeconds,
                        float revealThreshold,
                        float blendInSeconds,
                        float revealRate )
{
    CHECK( std::string( phase.name ) == name );
    CHECK( phase.advance == advance );
    CHECK( phase.camera.eye.x == doctest::Approx( eyeX ) );
    CHECK( phase.camera.view.y == doctest::Approx( viewY ) );
    CHECK( phase.camera.up.z == doctest::Approx( upZ ) );
    CHECK( phase.timerSeconds == doctest::Approx( timerSeconds ) );
    CHECK( phase.revealThreshold == doctest::Approx( revealThreshold ) );
    CHECK( phase.blendInSeconds == doctest::Approx( blendInSeconds ) );
    CHECK( phase.revealRate == doctest::Approx( revealRate ) );
}
} // namespace


TEST_CASE( "DemoDirector: shot list load/save round-trips authored phases" )
{
    const TemporaryShotListFiles files;
    WriteRoundTripFixture( files.inputPath );

    DemoShotList loaded;
    REQUIRE( LoadDemoShotList( files.inputPath, loaded ) );
    CHECK( loaded.loop );
    REQUIRE( loaded.phaseCount == 2 );
    CheckPhaseMatches( loaded.phases[0],
                       "root-line",
                       PhaseAdvance::RevealAtLeast,
                       10.0f,
                       50.0f,
                       0.0f,
                       2.5f,
                       0.6f,
                       0.75f,
                       0.5f );
    CheckPhaseMatches( loaded.phases[1], "settle", PhaseAdvance::Timer, -1.0f, 11.0f, 0.1f, 4.25f, 1.0f, 1.5f, 1.25f );
    CHECK( std::string( loaded.phases[0].stylePath ) == "SkullbonezData/styles/consequence.style.json" );
    CHECK( std::string( loaded.phases[1].stylePath ) == "SkullbonezData/styles/low_poly_art_style.style.json" );

    REQUIRE( SaveDemoShotList( files.savedPath, loaded ) );

    DemoShotList saved;
    REQUIRE( LoadDemoShotList( files.savedPath, saved ) );
    CHECK( saved.loop == loaded.loop );
    REQUIRE( saved.phaseCount == loaded.phaseCount );
    CheckPhaseMatches( saved.phases[0],
                       "root-line",
                       PhaseAdvance::RevealAtLeast,
                       10.0f,
                       50.0f,
                       0.0f,
                       2.5f,
                       0.6f,
                       0.75f,
                       0.5f );
    CheckPhaseMatches( saved.phases[1], "settle", PhaseAdvance::Timer, -1.0f, 11.0f, 0.1f, 4.25f, 1.0f, 1.5f, 1.25f );
}


TEST_CASE( "DemoDirector: malformed shot list returns false" )
{
    const TemporaryShotListFiles files;
    WriteTextFile( files.malformedPath,
                   "{ \"format\": \"skullbonez.shot.json\", \"version\": 1, \"phases\": [ { \"name\": 7 } ] }" );

    DemoShotList loaded;
    CHECK_FALSE( LoadDemoShotList( files.malformedPath, loaded ) );
    CHECK( loaded.phaseCount == 0 );
}
