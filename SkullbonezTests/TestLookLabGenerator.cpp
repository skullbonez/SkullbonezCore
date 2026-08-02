/*
File: TestLookLabGenerator.cpp
Purpose:
  Locks Look Lab generator determinism, breadth, validity, and RNG isolation.

Summary:
  A fixed 4096-seed branch census and a 65536-seed distribution census prove
  version-1 breadth, deterministic aggregate identity, and bounded palette
  envelopes while every candidate passes the runtime validator.

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
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>

namespace
{
using namespace SkullbonezCore;

constexpr std::size_t LARGE_CENSUS_SEED_COUNT = 65536;
constexpr std::size_t LOOK_LAB_FEATURE_COUNT = 8;

struct LookLabLargeCensus
{
    // Invariant: every member is one deterministic projection of the same
    // contiguous seed domain; callers compare complete census identities.
    std::array<uint32_t, static_cast<std::size_t>( Runtime::LookLabRecipeFamily::Count )> recipes = {};
    std::array<uint32_t, 22> skyModes = {};
    std::array<uint32_t, 16> terrainModes = {};
    std::array<uint32_t, 8> objectModes = {};
    std::array<uint32_t, 5> waterModes = {};
    std::array<uint32_t, 14> materialKinds = {};
    std::array<uint32_t, LOOK_LAB_FEATURE_COUNT> features = {};
    uint64_t streamHash = 14695981039346656037ull;
    std::size_t uniqueFingerprints = 0;
    uint32_t invalidCount = 0;
    uint32_t offQ12GridCount = 0;
    int paletteMinimumQ1e6 = INT_MAX;
    int paletteMaximumQ1e6 = INT_MIN;
    int luminanceMinimumQ1e6 = INT_MAX;
    int luminanceMaximumQ1e6 = INT_MIN;
    int contrastMinimumQ1e6 = INT_MAX;
    int contrastMaximumQ1e6 = INT_MIN;
};

int QuantizeMillion( float value )
{
    return static_cast<int>( std::lround( static_cast<double>( value ) * 1000000.0 ) );
}

int QuantizedLuminance( const std::array<float, 3>& color )
{
    return QuantizeMillion( 0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2] );
}

void MixFingerprint( uint64_t& hash, uint64_t fingerprint )
{
    for ( int byte = 0; byte < 8; ++byte )
    {
        hash ^= static_cast<uint8_t>( fingerprint & 0xffu );
        hash *= 1099511628211ull;
        fingerprint >>= 8;
    }
}

bool IsCanonicalQ12( float value )
{
    return std::isfinite( value ) && value * 4096.0f == std::trunc( value * 4096.0f );
}

bool GeneratedFloatsUseCanonicalQ12( const Runtime::LookLabCandidate& candidate )
{
    const Core::CinematicRenderConfig& c = candidate.cinematic;
    const float cinematicValues[] = {
        c.exposure,                c.gamma,                   c.sunAzimuth,
        c.sunElevation,            c.sunColorR,               c.sunColorG,
        c.sunColorB,               c.sunIntensity,            c.skyHorizonR,
        c.skyHorizonG,             c.skyHorizonB,             c.skyZenithR,
        c.skyZenithG,              c.skyZenithB,              c.skyGlowStrength,
        c.cloudCoverage,           c.cloudSoftness,           c.cloudScale,
        c.cloudIntensity,          c.sunShaftStrength,        c.sunShaftFalloff,
        c.volumetricStrength,      c.volumetricDensity,       c.volumetricDecay,
        c.bloomThreshold,          c.bloomKnee,               c.bloomStrength,
        c.bloomRadius,             c.terrainRelief,           c.basinDepth,
        c.basinRimLift,            c.shadow.strength,         c.shadow.softness,
        c.fogColorR,               c.fogColorG,               c.fogColorB,
        c.fogStart,                c.fogEnd,                  c.fogDensity,
        c.fogMaxOpacity,           c.styleSaturation,         c.styleContrast,
        c.styleVignette,           c.terrainTintR,            c.terrainTintG,
        c.terrainTintB,            c.terrainAccentR,          c.terrainAccentG,
        c.terrainAccentB,          c.terrainGridScale,        c.terrainGridStrength,
        c.waterTintR,              c.waterTintG,              c.waterTintB,
        c.waterAlpha,              c.waterReflectionStrength, c.waterGlintStrength,
    };

    if ( !std::all_of( std::begin( cinematicValues ), std::end( cinematicValues ), IsCanonicalQ12 ) )
    {
        return false;
    }

    for ( const Runtime::LookLabMaterialRule& rule : candidate.materialRules )
    {
        const Rendering::RenderMaterial& material = rule.material;
        const float materialValues[] = { material.baseColor[0],     material.baseColor[1],
                                         material.baseColor[2],     material.baseColor[3],
                                         material.emissiveColor[0], material.emissiveColor[1],
                                         material.emissiveColor[2], material.emissiveStrength,
                                         material.roughness,        material.metallic,
                                         material.specular,         material.transmission,
                                         material.stylization,      material.textureMode,
                                         material.contactFlashAlpha };

        if ( !std::all_of( std::begin( materialValues ), std::end( materialValues ), IsCanonicalQ12 ) )
        {
            return false;
        }
    }

    return true;
}

LookLabLargeCensus BuildLookLabLargeCensus()
{
    LookLabLargeCensus census;
    std::set<uint64_t> fingerprints;

    for ( uint64_t seed = 0; seed < LARGE_CENSUS_SEED_COUNT; ++seed )
    {
        const Runtime::LookLabCandidate candidate = Runtime::GenerateLookLabCandidate( seed );
        const Runtime::LookLabCandidateIssue issue = Runtime::ValidateLookLabCandidate( candidate );
        census.invalidCount += issue == Runtime::LookLabCandidateIssue::None ? 0u : 1u;
        census.offQ12GridCount += GeneratedFloatsUseCanonicalQ12( candidate ) ? 0u : 1u;

        if ( issue != Runtime::LookLabCandidateIssue::None )
        {
            std::printf( "[look-lab-census-invalid] seed=%llu issue=%u\n", static_cast<unsigned long long>( seed ),
                         static_cast<unsigned int>( issue ) );
        }

        ++census.recipes[static_cast<std::size_t>( candidate.recipe )];
        ++census.skyModes[static_cast<std::size_t>( candidate.cinematic.skyMode )];
        ++census.terrainModes[static_cast<std::size_t>( candidate.cinematic.terrainMode )];
        ++census.objectModes[static_cast<std::size_t>( candidate.cinematic.objectStyle )];
        ++census.waterModes[static_cast<std::size_t>( candidate.cinematic.waterMode )];
        census.features[0] += candidate.cinematic.cloudsEnabled ? 1u : 0u;
        census.features[1] += candidate.cinematic.volumetricLightingEnabled ? 1u : 0u;
        census.features[2] += candidate.cinematic.godRaysEnabled ? 1u : 0u;
        census.features[3] += candidate.cinematic.bloomEnabled ? 1u : 0u;
        census.features[4] += candidate.cinematic.fogEnabled ? 1u : 0u;
        census.features[5] += candidate.cinematic.terrainReliefEnabled ? 1u : 0u;
        census.features[6] += candidate.cinematic.waterMode != Core::CinematicStyleMode::Water::Off ? 1u : 0u;

        std::array<std::array<float, 3>, 8> palette = {
            std::array<float, 3> { candidate.cinematic.skyHorizonR, candidate.cinematic.skyHorizonG,
                                   candidate.cinematic.skyHorizonB },
            std::array<float, 3> { candidate.cinematic.skyZenithR, candidate.cinematic.skyZenithG,
                                   candidate.cinematic.skyZenithB },
            std::array<float, 3> { candidate.cinematic.terrainTintR, candidate.cinematic.terrainTintG,
                                   candidate.cinematic.terrainTintB },
            std::array<float, 3> { candidate.cinematic.terrainAccentR, candidate.cinematic.terrainAccentG,
                                   candidate.cinematic.terrainAccentB },
            std::array<float, 3> { candidate.cinematic.waterTintR, candidate.cinematic.waterTintG,
                                   candidate.cinematic.waterTintB },
            {},
            {},
            {},
        };

        for ( std::size_t ruleIndex = 0; ruleIndex < candidate.materialRules.size(); ++ruleIndex )
        {
            const Rendering::RenderMaterial& material = candidate.materialRules[ruleIndex].material;
            ++census.materialKinds[static_cast<std::size_t>( material.kind )];
            census.features[7] += material.emissiveStrength > 0.0f ? 1u : 0u;
            palette[5 + ruleIndex] = { material.baseColor[0], material.baseColor[1], material.baseColor[2] };
        }

        int candidateLuminanceMinimum = INT_MAX;
        int candidateLuminanceMaximum = INT_MIN;

        for ( const std::array<float, 3>& color : palette )
        {
            const int luminance = QuantizedLuminance( color );
            candidateLuminanceMinimum = std::min( candidateLuminanceMinimum, luminance );
            candidateLuminanceMaximum = std::max( candidateLuminanceMaximum, luminance );
            census.luminanceMinimumQ1e6 = std::min( census.luminanceMinimumQ1e6, luminance );
            census.luminanceMaximumQ1e6 = std::max( census.luminanceMaximumQ1e6, luminance );

            for ( float channel : color )
            {
                const int quantized = QuantizeMillion( channel );
                census.paletteMinimumQ1e6 = std::min( census.paletteMinimumQ1e6, quantized );
                census.paletteMaximumQ1e6 = std::max( census.paletteMaximumQ1e6, quantized );
            }
        }

        const int contrast = candidateLuminanceMaximum - candidateLuminanceMinimum;
        census.contrastMinimumQ1e6 = std::min( census.contrastMinimumQ1e6, contrast );
        census.contrastMaximumQ1e6 = std::max( census.contrastMaximumQ1e6, contrast );
        const uint64_t fingerprint = Runtime::FingerprintLookLabCandidate( candidate );
        fingerprints.insert( fingerprint );
        MixFingerprint( census.streamHash, fingerprint );
    }

    census.uniqueFingerprints = fingerprints.size();
    return census;
}

TEST_CASE( "Look Lab generator is byte-exact for a seed and version" )
{
    constexpr uint64_t seed = 0x0123456789abcdefull;
    const Runtime::LookLabCandidate first = Runtime::GenerateLookLabCandidate( seed );
    const Runtime::LookLabCandidate second = Runtime::GenerateLookLabCandidate( seed );
    CHECK( Runtime::ValidateLookLabCandidate( first ) == Runtime::LookLabCandidateIssue::None );
    CHECK( Runtime::EncodeLookLabCandidateCanonical( first ) == Runtime::EncodeLookLabCandidateCanonical( second ) );
    CHECK( Runtime::FingerprintLookLabCandidate( first ) == Runtime::FingerprintLookLabCandidate( second ) );
    CHECK( Runtime::FingerprintLookLabCandidate( first ) == 0x709160cd850d1846ull );
    CHECK( GeneratedFloatsUseCanonicalQ12( first ) );
}

TEST_CASE( "Look Lab Q12 census detector catches an off-grid control" )
{
    Runtime::LookLabCandidate candidate = Runtime::GenerateLookLabCandidate( 17 );
    candidate.cinematic.fogEnd += 0.0001f;
    CHECK_FALSE( GeneratedFloatsUseCanonicalQ12( candidate ) );
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

TEST_CASE( "Look Lab large deterministic census has useful bounded breadth" )
{
    const LookLabLargeCensus first = BuildLookLabLargeCensus();
    const LookLabLargeCensus repeated = BuildLookLabLargeCensus();

    CHECK( first.invalidCount == 0 );
    CHECK( first.uniqueFingerprints == LARGE_CENSUS_SEED_COUNT );
    CHECK( first.streamHash == repeated.streamHash );
    CHECK( first.streamHash == 0x3f5d4c4608cca5a2ull );
    CHECK( first.offQ12GridCount == 0 );
    CHECK( first.recipes == repeated.recipes );
    CHECK( first.features == repeated.features );
    CHECK( std::all_of( first.recipes.begin(), first.recipes.end(), []( uint32_t count ) { return count > 4000; } ) );
    CHECK( first.paletteMinimumQ1e6 >= 0 );
    CHECK( first.paletteMaximumQ1e6 <= 8000000 );
    CHECK( first.luminanceMinimumQ1e6 >= 0 );
    CHECK( first.contrastMinimumQ1e6 > 20000 );

    std::printf( "[look-lab-census] seeds=%zu invalid=%u unique=%zu hash=%016llx palette_q1e6=%d..%d "
                 "luminance_q1e6=%d..%d contrast_q1e6=%d..%d\n",
                 LARGE_CENSUS_SEED_COUNT, first.invalidCount, first.uniqueFingerprints,
                 static_cast<unsigned long long>( first.streamHash ), first.paletteMinimumQ1e6,
                 first.paletteMaximumQ1e6, first.luminanceMinimumQ1e6, first.luminanceMaximumQ1e6,
                 first.contrastMinimumQ1e6, first.contrastMaximumQ1e6 );
    std::printf( "[look-lab-census] recipes=" );

    for ( uint32_t count : first.recipes )
    {
        std::printf( "%u,", count );
    }

    std::printf( " features=" );

    for ( uint32_t count : first.features )
    {
        std::printf( "%u,", count );
    }

    std::printf( " sky=" );

    for ( uint32_t count : first.skyModes )
    {
        std::printf( "%u,", count );
    }

    std::printf( " terrain=" );

    for ( uint32_t count : first.terrainModes )
    {
        std::printf( "%u,", count );
    }

    std::printf( " object=" );

    for ( uint32_t count : first.objectModes )
    {
        std::printf( "%u,", count );
    }

    std::printf( " water=" );

    for ( uint32_t count : first.waterModes )
    {
        std::printf( "%u,", count );
    }

    std::printf( " material=" );

    for ( uint32_t count : first.materialKinds )
    {
        std::printf( "%u,", count );
    }

    std::printf( "\n" );
}

TEST_CASE( "Look Lab validator rejects planted defects" )
{
    const Runtime::LookLabCandidate valid = Runtime::GenerateLookLabCandidate( 77 );
    REQUIRE( Runtime::ValidateLookLabCandidate( valid ) == Runtime::LookLabCandidateIssue::None );

    Runtime::LookLabCandidate invalidRange = valid;
    invalidRange.cinematic.exposure = 99.0f;
    CHECK( Runtime::ValidateLookLabCandidate( invalidRange ) == Runtime::LookLabCandidateIssue::ValueOutOfRange );

    Runtime::LookLabCandidate hugeFiniteFog = valid;
    hugeFiniteFog.cinematic.fogStart = 1.0e30f;
    hugeFiniteFog.cinematic.fogEnd = 1.0e30f;
    CHECK( Runtime::ValidateLookLabCandidate( hugeFiniteFog ) == Runtime::LookLabCandidateIssue::ValueOutOfRange );

    Runtime::LookLabCandidate mutatedRetainedShadowPolicy = valid;
    mutatedRetainedShadowPolicy.cinematic.shadow.terrainCasts =
        !mutatedRetainedShadowPolicy.cinematic.shadow.terrainCasts;
    CHECK( Runtime::ValidateLookLabCandidate( mutatedRetainedShadowPolicy ) ==
           Runtime::LookLabCandidateIssue::ValueOutOfRange );

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
