/*
File: TestLookLabGenerator.cpp
Purpose:
  Locks Look Lab generator determinism, breadth, validity, and RNG isolation.

Summary:
  A fixed 4096-seed census proves every version-1 recipe and supported render
  branch is reachable while every produced candidate passes the same validator
  used by runtime authoring.

Invariants:
  - Same seed/version comparisons use canonical bytes, never struct memcmp.
  - Negative controls each plant one named defect in an otherwise valid value.
  - The C library RNG check detects accidental use of shared process randomness.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabGenerator.cpp
  - Agentic/Reports/2026-08-01/look-lab-random-style-authoring-ll0-census.md
*/
#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Runtime/Direction/LookLabGenerator.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <set>

namespace
{
using namespace SkullbonezCore;

TEST_CASE( "Look Lab generator is byte-exact for a seed and version" )
{
    constexpr uint64_t seed = 0x0123456789abcdefull;
    const Runtime::LookLabCandidate first = Runtime::GenerateLookLabCandidate( seed );
    const Runtime::LookLabCandidate second = Runtime::GenerateLookLabCandidate( seed );
    CHECK( Runtime::ValidateLookLabCandidate( first ) == Runtime::LookLabCandidateIssue::None );
    CHECK( Runtime::EncodeLookLabCandidateCanonical( first ) == Runtime::EncodeLookLabCandidateCanonical( second ) );
    CHECK( Runtime::FingerprintLookLabCandidate( first ) == Runtime::FingerprintLookLabCandidate( second ) );
    CHECK( Runtime::FingerprintLookLabCandidate( first ) == 0xc3b6fad6b7b4defaull );
}

TEST_CASE( "Look Lab fixed seed matrix covers every supported branch" )
{
    std::array<bool, static_cast<size_t>( Runtime::LookLabRecipeFamily::Count )> recipes = {};
    std::set<int> skyModes;
    std::set<int> terrainModes;
    std::set<int> objectModes;
    std::set<int> waterModes;
    std::set<int> materialKinds;
    std::set<uint64_t> fingerprints;

    for ( uint64_t seed = 0; seed < 4096; ++seed )
    {
        INFO( "seed=" << seed );
        const Runtime::LookLabCandidate candidate = Runtime::GenerateLookLabCandidate( seed );
        INFO( "terrain=" << candidate.cinematic.terrainTintR << "," << candidate.cinematic.terrainTintG << ","
                         << candidate.cinematic.terrainTintB << " accent=" << candidate.cinematic.terrainAccentR << ","
                         << candidate.cinematic.terrainAccentG << "," << candidate.cinematic.terrainAccentB
                         << " water=" << candidate.cinematic.waterTintR << "," << candidate.cinematic.waterTintG << ","
                         << candidate.cinematic.waterTintB );
        REQUIRE( Runtime::ValidateLookLabCandidate( candidate ) == Runtime::LookLabCandidateIssue::None );
        recipes[static_cast<size_t>( candidate.recipe )] = true;
        skyModes.insert( candidate.cinematic.skyMode );
        terrainModes.insert( candidate.cinematic.terrainMode );
        objectModes.insert( candidate.cinematic.objectStyle );
        waterModes.insert( candidate.cinematic.waterMode );
        for ( const Runtime::LookLabMaterialRule& rule : candidate.materialRules )
        {
            materialKinds.insert( static_cast<int>( rule.material.kind ) );
        }
        fingerprints.insert( Runtime::FingerprintLookLabCandidate( candidate ) );
    }

    CHECK( std::all_of( recipes.begin(), recipes.end(), []( bool covered ) { return covered; } ) );
    CHECK( skyModes.size() == 21 );
    CHECK( terrainModes.size() == 16 );
    CHECK( objectModes.size() == 8 );
    CHECK( waterModes.size() == 5 );
    CHECK( materialKinds.size() == 14 );
    CHECK( fingerprints.size() == 4096 );
}

TEST_CASE( "Look Lab validator rejects planted defects" )
{
    const Runtime::LookLabCandidate valid = Runtime::GenerateLookLabCandidate( 77 );
    REQUIRE( Runtime::ValidateLookLabCandidate( valid ) == Runtime::LookLabCandidateIssue::None );

    Runtime::LookLabCandidate invalidRange = valid;
    invalidRange.cinematic.exposure = 99.0f;
    CHECK( Runtime::ValidateLookLabCandidate( invalidRange ) == Runtime::LookLabCandidateIssue::ValueOutOfRange );

    Runtime::LookLabCandidate unsupportedVersion = valid;
    unsupportedVersion.generatorVersion = Runtime::LOOK_LAB_GENERATOR_VERSION + 1;
    CHECK( Runtime::ValidateLookLabCandidate( unsupportedVersion ) == Runtime::LookLabCandidateIssue::UnsupportedVersion );

    Runtime::LookLabCandidate incompatible = valid;
    incompatible.cinematic.skyMode = Core::CinematicStyleMode::Sky::DeepSpace;
    incompatible.cinematic.cloudsEnabled = true;
    CHECK( Runtime::ValidateLookLabCandidate( incompatible ) == Runtime::LookLabCandidateIssue::IncompatibleFeatures );

    Runtime::LookLabCandidate nonFinite = valid;
    nonFinite.cinematic.gamma = std::numeric_limits<float>::quiet_NaN();
    CHECK( Runtime::ValidateLookLabCandidate( nonFinite ) == Runtime::LookLabCandidateIssue::NonFiniteValue );

    Runtime::LookLabCandidate blackFrame = valid;
    blackFrame.cinematic.skyHorizonR = blackFrame.cinematic.skyHorizonG = blackFrame.cinematic.skyHorizonB = 0.0f;
    blackFrame.cinematic.skyZenithR = blackFrame.cinematic.skyZenithG = blackFrame.cinematic.skyZenithB = 0.0f;
    blackFrame.cinematic.terrainTintR = blackFrame.cinematic.terrainTintG = blackFrame.cinematic.terrainTintB = 0.02f;
    blackFrame.cinematic.terrainAccentR = blackFrame.cinematic.terrainAccentG = blackFrame.cinematic.terrainAccentB = 0.0f;
    blackFrame.cinematic.waterMode = Core::CinematicStyleMode::Water::Off;
    for ( Runtime::LookLabMaterialRule& rule : blackFrame.materialRules )
    {
        rule.material.baseColor[0] = rule.material.baseColor[1] = rule.material.baseColor[2] = 0.0f;
        rule.material.emissiveStrength = 0.0f;
    }
    CHECK( Runtime::ValidateLookLabCandidate( blackFrame ) == Runtime::LookLabCandidateIssue::BlackFrame );
}

TEST_CASE( "Look Lab generator does not consume the process C RNG" )
{
    std::srand( 2401 );
    const int expectedFirst = std::rand();
    const int expectedSecond = std::rand();

    std::srand( 2401 );
    CHECK( std::rand() == expectedFirst );
    static_cast<void>( Runtime::GenerateLookLabCandidate( 0xfedcba9876543210ull ) );
    CHECK( std::rand() == expectedSecond );
}
} // namespace
