/*
File: LookLabGenerator.cpp
Purpose:
  Generates and validates deterministic Look Lab presentation candidates.

Summary:
  A fixed recipe selects compatible render branches, then a private SplitMix64
  stream perturbs quantized Oklab palette roles and bounded scalar controls.
  Canonical encoding makes the reproducibility claim independent of C++ object
  padding and standard-library implementation details.

Glossary:
  SplitMix64: Fixed-width integer generator whose wraparound operations define
    the complete Look Lab version-1 random stream.
  Quantized Oklab: Perceptual lightness/opponent coordinates stored as Q12
    integers before deterministic conversion to linear RGB.

Invariants:
  - Draw order is recipe, palette, feature, scalar, then material variation.
  - All generated floats are multiples of 1/4096 and therefore exactly
    representable as IEEE-754 binary32 values.
  - Deep-space candidates disable atmosphere, clouds, shafts, and fog.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabGenerator.h
*/
#include "LookLabGenerator.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr uint64_t SPLITMIX_INCREMENT = 0x9E3779B97F4A7C15ull;
constexpr uint64_t SPLITMIX_MULTIPLIER_1 = 0xBF58476D1CE4E5B9ull;
constexpr uint64_t SPLITMIX_MULTIPLIER_2 = 0x94D049BB133111EBull;
constexpr int Q12_ONE = 4096;
constexpr int Q12_RGB_MAX = 9011; // 2.2, rounded down to the canonical grid.

struct SplitMix64
{
    uint64_t state;

    uint64_t Next()
    {
        state += SPLITMIX_INCREMENT;
        uint64_t z = state;
        z = ( z ^ ( z >> 30 ) ) * SPLITMIX_MULTIPLIER_1;
        z = ( z ^ ( z >> 27 ) ) * SPLITMIX_MULTIPLIER_2;
        return z ^ ( z >> 31 );
    }

    uint32_t NextQ12()
    {
        return static_cast<uint32_t>( Next() >> 52 );
    }
};

struct OklabQ12
{
    int l;
    int a;
    int b;
};

struct Color3
{
    float r;
    float g;
    float b;
};

struct RecipeSpec
{
    const char* name;
    OklabQ12 base;
    int skyPrimary;
    int skyAlternate;
    int terrainPrimary;
    int terrainAlternate;
    int objectStyle;
    int waterMode;
    Rendering::RenderMaterialKind primaryMaterial;
    bool highKey;
    bool emissive;
    bool atmospheric;
};

constexpr std::array<RecipeSpec, static_cast<size_t>( LookLabRecipeFamily::Count )> RECIPES = { {
    { "golden_realism",
      { 2867, 328, 492 },
      0,
      20,
      0,
      12,
      0,
      1,
      Rendering::RenderMaterialKind::Textured,
      false,
      false,
      true },
    { "low_poly_storybook",
      { 3031, 451, 246 },
      11,
      19,
      7,
      14,
      6,
      4,
      Rendering::RenderMaterialKind::Matte,
      true,
      false,
      false },
    { "painterly_poster",
      { 2744, 533, -164 },
      6,
      16,
      6,
      14,
      5,
      1,
      Rendering::RenderMaterialKind::Metal,
      false,
      false,
      true },
    { "neon_cyberpunk",
      { 1556, 574, -533 },
      3,
      7,
      3,
      10,
      3,
      3,
      Rendering::RenderMaterialKind::Emissive,
      false,
      true,
      false },
    { "tron_graphic", { 1311, -287, -615 }, 15, 18, 3, 13, 7, 0, Rendering::RenderMaterialKind::Glass, false, true, false },
    { "atmospheric_storm", { 1884, -82, -205 }, 13, 8, 1, 8, 2, 1, Rendering::RenderMaterialKind::Toon, false, false, true },
    { "studio_high_key", { 3440, 82, -41 }, 2, 10, 2, 15, 2, 3, Rendering::RenderMaterialKind::LowPoly, true, false, false },
    { "industrial_low_key",
      { 1475, 123, 82 },
      1,
      12,
      1,
      8,
      1,
      3,
      Rendering::RenderMaterialKind::Shadow,
      false,
      false,
      true },
    { "desert_warm", { 2908, 410, 656 }, 5, 13, 5, 12, 1, 0, Rendering::RenderMaterialKind::Foliage, false, false, true },
    { "nordic_cool", { 3318, -164, -205 }, 17, 20, 11, 9, 4, 0, Rendering::RenderMaterialKind::Bark, true, false, true },
    { "ocean_terrestrial",
      { 2458, -369, -369 },
      9,
      0,
      9,
      0,
      0,
      2,
      Rendering::RenderMaterialKind::Stone,
      false,
      false,
      true },
    { "alien_world", { 2212, 615, -492 }, 4, 16, 4, 13, 0, 1, Rendering::RenderMaterialKind::Ridge, false, true, true },
    { "deep_space_dreamscape",
      { 1229, 410, -697 },
      21,
      16,
      8,
      3,
      3,
      0,
      Rendering::RenderMaterialKind::Shore,
      false,
      true,
      false },
    { "abstract_chromatic",
      { 2335, 697, 369 },
      18,
      7,
      13,
      6,
      5,
      4,
      Rendering::RenderMaterialKind::Pine,
      false,
      true,
      false },
} };

int Jitter( int center, int radius, SplitMix64& rng )
{
    const int width = radius * 2 + 1;
    return center - radius + static_cast<int>( ( static_cast<uint64_t>( width ) * rng.NextQ12() ) >> 12 );
}

int CubeQ12( int value )
{
    const int64_t cube = static_cast<int64_t>( value ) * value * value;
    return static_cast<int>( cube / ( static_cast<int64_t>( Q12_ONE ) * Q12_ONE ) );
}

Color3 OklabToLinearRgb( OklabQ12 color )
{
    // Concept: these are the published Oklab inverse matrices quantized to Q12.
    // Integer intermediates keep palette construction exact across compiler
    // optimization modes; only the final power-of-two division becomes float.
    const int l = color.l + ( 1623 * color.a ) / Q12_ONE + ( 884 * color.b ) / Q12_ONE;
    const int m = color.l - ( 432 * color.a ) / Q12_ONE - ( 261 * color.b ) / Q12_ONE;
    const int s = color.l - ( 367 * color.a ) / Q12_ONE - ( 5290 * color.b ) / Q12_ONE;
    const int l3 = CubeQ12( l );
    const int m3 = CubeQ12( m );
    const int s3 = CubeQ12( s );
    const int red = std::clamp( ( 16609 * l3 - 13548 * m3 + 1035 * s3 ) / Q12_ONE, 0, Q12_RGB_MAX );
    const int green = std::clamp( ( -5204 * l3 + 10677 * m3 - 1377 * s3 ) / Q12_ONE, 0, Q12_RGB_MAX );
    const int blue = std::clamp( ( -17 * l3 - 2882 * m3 + 6994 * s3 ) / Q12_ONE, 0, Q12_RGB_MAX );
    return { static_cast<float>( red ) / Q12_ONE, static_cast<float>( green ) / Q12_ONE,
             static_cast<float>( blue ) / Q12_ONE };
}

int Q12RangeValue( int minimum, int maximum, SplitMix64& rng )
{
    return minimum + static_cast<int>( ( static_cast<int64_t>( maximum - minimum ) * rng.NextQ12() ) >> 12 );
}

float Q12ToFloat( int value )
{
    return static_cast<float>( value ) / Q12_ONE;
}

float Q12Range( int minimum, int maximum, SplitMix64& rng )
{
    return Q12ToFloat( Q12RangeValue( minimum, maximum, rng ) );
}

int MinimumFogSpanQ12( int fogStartQ12 )
{
    // Why: integer ceiling preserves the 15-percent publication boundary on
    // the Q12 grid without compiler-dependent float multiply/rounding.
    constexpr int PERCENT_DENOMINATOR = 100;
    constexpr int MINIMUM_PERCENT = 15;
    const int percentage = static_cast<int>( ( static_cast<int64_t>( fogStartQ12 ) * MINIMUM_PERCENT + PERCENT_DENOMINATOR - 1 ) / PERCENT_DENOMINATOR );
    return std::max( 32 * Q12_ONE, percentage );
}

float Luminance( float r, float g, float b )
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

void AssignColor( float& r, float& g, float& b, Color3 color )
{
    r = color.r;
    g = color.g;
    b = color.b;
}

void CopyText( char* destination, size_t capacity, const char* source )
{
    std::memset( destination, 0, capacity );
    const size_t copyLength = std::min( std::strlen( source ), capacity - 1 );
    std::memcpy( destination, source, copyLength );
}

void BuildMaterialRule( LookLabMaterialRule& rule, const char* target, Rendering::RenderMaterialKind kind, const char* name,
                        Color3 base, Color3 emissive, SplitMix64& rng )
{
    CopyText( rule.target.data(), rule.target.size(), target );
    CopyText( rule.material.name, sizeof( rule.material.name ), name );
    rule.material.kind = kind;
    rule.material.baseColor[0] = base.r;
    rule.material.baseColor[1] = base.g;
    rule.material.baseColor[2] = base.b;
    rule.material.baseColor[3] = Q12Range( 2867, 4096, rng );
    rule.material.emissiveColor[0] = emissive.r;
    rule.material.emissiveColor[1] = emissive.g;
    rule.material.emissiveColor[2] = emissive.b;
    rule.material.emissiveStrength = kind == Rendering::RenderMaterialKind::Emissive ? Q12Range( 4096, 24576, rng ) : 0.0f;
    rule.material.roughness = Q12Range( 614, 3686, rng );
    rule.material.metallic = kind == Rendering::RenderMaterialKind::Metal ? Q12Range( 2867, 4096, rng )
                                                                          : Q12Range( 0, 1024, rng );

    rule.material.specular = Q12Range( 819, 3686, rng );
    rule.material.transmission = kind == Rendering::RenderMaterialKind::Glass ? Q12Range( 2458, 4096, rng ) : 0.0f;
    rule.material.stylization = Q12Range( 0, 4096, rng );

    // Invariant: the style parser's canonical "textured" spelling resolves to
    // the legacy -1 sentinel. Store that same bridge value in the detached
    // candidate so serialize/parse equality also holds for textured recipes.
    rule.material.textureMode = Rendering::RenderMaterialKindLegacyMode( kind );
    rule.material.contactFlashAlpha = 0.0f;
    rule.material.flags = 0;
}

bool IsSupportedSkyMode( int mode )
{
    return ( mode >= 0 && mode <= 13 ) || ( mode >= 15 && mode <= 21 );
}

bool IsFinite( float value )
{
    return std::isfinite( value );
}

template <typename T> void AppendInteger( std::vector<uint8_t>& bytes, T value )
{
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = static_cast<Unsigned>( value );

    for ( size_t i = 0; i < sizeof( T ); ++i )
    {
        bytes.push_back( static_cast<uint8_t>( bits & 0xffu ) );
        bits >>= 8;
    }
}

void AppendFloat( std::vector<uint8_t>& bytes, float value )
{
    AppendInteger( bytes, std::bit_cast<uint32_t>( value ) );
}

void AppendCinematic( std::vector<uint8_t>& bytes, const Core::CinematicRenderConfig& c )
{
    // Invariant: prerelease generator version 1 consumes exactly 84 atoms in
    // this field order; it is not an in-memory dump. Insertions require a new
    // generator version because changing this order invalidates
    // fingerprints and later reproducibility receipts.
    const bool toggles[] = { c.enabled,        c.skyAtmosphereEnabled,      c.cloudsEnabled,
                             c.godRaysEnabled, c.volumetricLightingEnabled, c.bloomEnabled,
                             c.fogEnabled,     c.terrainReliefEnabled };

    for ( bool value : toggles )
    {
        bytes.push_back( value ? 1u : 0u );
    }

    const float beforeShadow[] = { c.exposure,          c.gamma,
                                   c.sunAzimuth,        c.sunElevation,
                                   c.sunColorR,         c.sunColorG,
                                   c.sunColorB,         c.sunIntensity,
                                   c.skyHorizonR,       c.skyHorizonG,
                                   c.skyHorizonB,       c.skyZenithR,
                                   c.skyZenithG,        c.skyZenithB,
                                   c.skyGlowStrength,   c.cloudCoverage,
                                   c.cloudSoftness,     c.cloudScale,
                                   c.cloudIntensity,    c.sunShaftStrength,
                                   c.sunShaftFalloff,   c.volumetricStrength,
                                   c.volumetricDensity, c.volumetricDecay,
                                   c.bloomThreshold,    c.bloomKnee,
                                   c.bloomStrength,     c.bloomRadius,
                                   c.terrainRelief,     c.basinDepth,
                                   c.basinRimLift };

    for ( float value : beforeShadow )
    {
        AppendFloat( bytes, value );
    }

    bytes.push_back( c.shadow.enabled ? 1u : 0u );
    bytes.push_back( c.shadow.terrainCasts ? 1u : 0u );
    bytes.push_back( c.shadow.objectsCast ? 1u : 0u );
    bytes.push_back( c.shadow.terrainReceives ? 1u : 0u );
    bytes.push_back( c.shadow.objectsReceive ? 1u : 0u );
    AppendInteger( bytes, c.shadow.mapSize );
    AppendInteger( bytes, c.shadow.pcfRadius );
    AppendFloat( bytes, c.shadow.strength );
    AppendFloat( bytes, c.shadow.softness );
    AppendFloat( bytes, c.shadow.depthBias );
    AppendFloat( bytes, c.shadow.slopeBias );
    AppendFloat( bytes, c.shadow.maxDistance );

    const float afterShadow[] = { c.fogColorR, c.fogColorG,  c.fogColorB,    c.fogStart,
                                  c.fogEnd,    c.fogDensity, c.fogMaxOpacity };

    for ( float value : afterShadow )
    {
        AppendFloat( bytes, value );
    }

    AppendInteger( bytes, c.skyMode );
    AppendInteger( bytes, c.terrainMode );
    AppendInteger( bytes, c.objectStyle );
    AppendInteger( bytes, c.waterMode );

    const float styleValues[] = { c.styleSaturation,    c.styleContrast,    c.styleVignette,       c.terrainTintR,
                                  c.terrainTintG,       c.terrainTintB,     c.terrainAccentR,      c.terrainAccentG,
                                  c.terrainAccentB,     c.terrainGridScale, c.terrainGridStrength, c.waterTintR,
                                  c.waterTintG,         c.waterTintB,       c.waterAlpha,          c.waterReflectionStrength,
                                  c.waterGlintStrength, c.basinCenterX,     c.basinCenterZ,        c.basinRadiusX,
                                  c.basinRadiusZ,       c.basinFeather };

    for ( float value : styleValues )
    {
        AppendFloat( bytes, value );
    }
}
} // namespace

const char* LookLabRecipeFamilyName( LookLabRecipeFamily family )
{
    const size_t index = static_cast<size_t>( family );
    return index < RECIPES.size() ? RECIPES[index].name : "unknown";
}

LookLabCandidate GenerateLookLabCandidate( uint64_t seed, uint32_t generatorVersion )
{
    LookLabCandidate candidate;
    candidate.seed = seed;
    candidate.generatorVersion = generatorVersion;

    if ( generatorVersion != LOOK_LAB_GENERATOR_VERSION )
    {
        return candidate;
    }

    SplitMix64 rng { seed };
    const size_t recipeIndex = static_cast<size_t>( rng.Next() % RECIPES.size() );
    const RecipeSpec& recipe = RECIPES[recipeIndex];
    candidate.recipe = static_cast<LookLabRecipeFamily>( recipeIndex );
    Core::CinematicRenderConfig& c = candidate.cinematic;

    // Invariant: recipe and palette consume a fixed draw count before any
    // feature switch. Adding a version-1 draw would change every later field.
    OklabQ12 base = { Jitter( recipe.base.l, 164, rng ), Jitter( recipe.base.a, 123, rng ),
                      Jitter( recipe.base.b, 123, rng ) };

    const Color3 horizon = OklabToLinearRgb( { std::clamp( base.l + 451, 819, 3891 ), base.a, base.b } );
    const Color3 zenith = OklabToLinearRgb( { std::clamp( base.l - 287, 614, 3482 ), base.a - 164, base.b - 246 } );
    const Color3 sun = OklabToLinearRgb( { std::clamp( base.l + 778, 2458, 4096 ), base.a + 205, base.b + 287 } );
    const Color3 terrain = OklabToLinearRgb( { std::clamp( base.l - 410, 737, 3441 ), base.a + 82, base.b } );
    const Color3 accent = OklabToLinearRgb( { std::clamp( base.l + 492, 819, 3686 ), -base.a / 2, -base.b / 2 } );
    const Color3 water = OklabToLinearRgb( { std::clamp( base.l - 164, 737, 3482 ), base.a - 246, base.b - 328 } );
    const Color3 fog = OklabToLinearRgb( { std::clamp( base.l + 205, 1024, 3686 ), base.a / 2, base.b / 2 } );

    c.enabled = true;
    c.skyMode = rng.NextQ12() < 2048 ? recipe.skyPrimary : recipe.skyAlternate;
    c.terrainMode = rng.NextQ12() < 2048 ? recipe.terrainPrimary : recipe.terrainAlternate;
    c.objectStyle = recipe.objectStyle;
    c.waterMode = recipe.waterMode;
    const bool deepSpace = c.skyMode == Core::CinematicStyleMode::Sky::DeepSpace;
    c.skyAtmosphereEnabled = !deepSpace;
    c.cloudsEnabled = !deepSpace && recipe.atmospheric && rng.NextQ12() < 3072;
    c.volumetricLightingEnabled = !deepSpace && recipe.atmospheric && rng.NextQ12() < 2867;
    c.godRaysEnabled = c.volumetricLightingEnabled && rng.NextQ12() < 2662;
    c.bloomEnabled = recipe.emissive || rng.NextQ12() < 3277;
    c.fogEnabled = !deepSpace && recipe.atmospheric && rng.NextQ12() < 3277;
    c.terrainReliefEnabled = rng.NextQ12() < ( recipe.highKey ? 1229u : 2458u );
    c.shadow.enabled = !recipe.emissive || rng.NextQ12() < 2458;

    c.exposure = Q12Range( recipe.highKey ? 4915 : 2867, recipe.emissive ? 6554 : 8192, rng );
    c.gamma = Q12Range( 5530, 9626, rng );
    c.sunAzimuth = Q12Range( 0, 4096, rng );
    c.sunElevation = Q12Range( 492, 3604, rng );
    AssignColor( c.sunColorR, c.sunColorG, c.sunColorB, sun );
    c.sunIntensity = deepSpace ? Q12Range( 1639, 8192, rng ) : Q12Range( 16384, 114688, rng );
    AssignColor( c.skyHorizonR, c.skyHorizonG, c.skyHorizonB, horizon );
    AssignColor( c.skyZenithR, c.skyZenithG, c.skyZenithB, zenith );
    c.skyGlowStrength = deepSpace ? Q12Range( 0, 2048, rng ) : Q12Range( 1024, 12288, rng );
    c.cloudCoverage = Q12Range( 205, 3604, rng );
    c.cloudSoftness = Q12Range( 164, 2253, rng );
    c.cloudScale = Q12Range( 4916, 73728, rng );
    c.cloudIntensity = c.cloudsEnabled ? Q12Range( 614, 6144, rng ) : 0.0f;
    c.sunShaftStrength = c.godRaysEnabled ? Q12Range( 410, 6554, rng ) : 0.0f;
    c.sunShaftFalloff = Q12Range( 2868, 20480, rng );
    c.volumetricStrength = c.volumetricLightingEnabled ? Q12Range( 205, 6144, rng ) : 0.0f;
    c.volumetricDensity = c.volumetricLightingEnabled ? Q12Range( 205, 6554, rng ) : 0.0f;
    c.volumetricDecay = Q12Range( 3523, 4035, rng );
    c.bloomThreshold = Q12Range( recipe.emissive ? 1434 : 2867, 9830, rng );
    c.bloomKnee = Q12Range( 328, 4915, rng );
    c.bloomStrength = c.bloomEnabled ? Q12Range( recipe.emissive ? 2048 : 410, 5120, rng ) : 0.0f;
    c.bloomRadius = Q12Range( 3277, 32768, rng );
    c.terrainRelief = c.terrainReliefEnabled ? Q12Range( 410, 5120, rng ) : 0.0f;
    c.basinDepth = c.terrainReliefEnabled ? Q12Range( 32768, 393216, rng ) : 0.0f;
    c.basinRimLift = c.terrainReliefEnabled ? Q12Range( 16384, 393216, rng ) : 0.0f;
    c.shadow.strength = c.shadow.enabled ? Q12Range( 820, 4096, rng ) : 0.0f;
    c.shadow.softness = Q12Range( 2048, 10240, rng );
    AssignColor( c.fogColorR, c.fogColorG, c.fogColorB, fog );
    const int fogStartQ12 = Q12RangeValue( 983040, 3686400, rng );
    const int fogSpanQ12 = Q12RangeValue( 524288, 4915200, rng );
    c.fogStart = Q12ToFloat( fogStartQ12 );
    c.fogEnd = Q12ToFloat( fogStartQ12 + std::max( fogSpanQ12, MinimumFogSpanQ12( fogStartQ12 ) ) );

    // Invariant: a generated candidate must pass the same fog-separation rule
    // used at publication. Preserve the version-1 draw schedule and every
    // already-valid byte; only previously rejected high-start/short-span seeds
    // are lifted to the exact validator boundary.
    c.fogDensity = c.fogEnabled ? Q12Range( 1, 49, rng ) : 0.0f;
    c.fogMaxOpacity = c.fogEnabled ? Q12Range( 410, 3359, rng ) : 0.0f;
    c.styleSaturation = Q12Range( recipe.highKey ? 2867 : 2253, 7578, rng );
    c.styleContrast = Q12Range( recipe.highKey ? 2868 : 3277, 7168, rng );
    c.styleVignette = Q12Range( 0, recipe.highKey ? 901 : 2540, rng );
    AssignColor( c.terrainTintR, c.terrainTintG, c.terrainTintB, terrain );
    AssignColor( c.terrainAccentR, c.terrainAccentG, c.terrainAccentB, accent );
    c.terrainGridScale = Q12Range( 32768, 393216, rng );
    c.terrainGridStrength = ( c.terrainMode == 3 || c.terrainMode == 10 || c.terrainMode == 13 )
                                ? Q12Range( 1024, 8192, rng )
                                : 0.0f;

    AssignColor( c.waterTintR, c.waterTintG, c.waterTintB, water );
    c.waterAlpha = Q12Range( 1024, 4096, rng );
    c.waterReflectionStrength = Q12Range( 0, 4096, rng );
    c.waterGlintStrength = Q12Range( 0, 10240, rng );

    const auto kindAt = []( int index ) { return static_cast<Rendering::RenderMaterialKind>( index % 14 ); };
    BuildMaterialRule( candidate.materialRules[0], "balls", recipe.primaryMaterial, recipe.name, accent, sun, rng );
    BuildMaterialRule( candidate.materialRules[1], "boxes", kindAt( static_cast<int>( recipeIndex ) + 5 ), "look_lab_boxes",
                       terrain, accent, rng );

    BuildMaterialRule( candidate.materialRules[2], "hulls", kindAt( static_cast<int>( recipeIndex ) + 9 ), "look_lab_hulls",
                       water, sun, rng );

    return candidate;
}

LookLabCandidateIssue ValidateLookLabCandidate( const LookLabCandidate& candidate )
{
    if ( candidate.generatorVersion != LOOK_LAB_GENERATOR_VERSION )
    {
        return LookLabCandidateIssue::UnsupportedVersion;
    }

    if ( static_cast<size_t>( candidate.recipe ) >= RECIPES.size() )
    {
        return LookLabCandidateIssue::UnsupportedMode;
    }

    const Core::CinematicRenderConfig& c = candidate.cinematic;
    const float values[] = { c.exposure,
                             c.gamma,
                             c.sunAzimuth,
                             c.sunElevation,
                             c.sunColorR,
                             c.sunColorG,
                             c.sunColorB,
                             c.sunIntensity,
                             c.skyHorizonR,
                             c.skyHorizonG,
                             c.skyHorizonB,
                             c.skyZenithR,
                             c.skyZenithG,
                             c.skyZenithB,
                             c.skyGlowStrength,
                             c.cloudCoverage,
                             c.cloudSoftness,
                             c.cloudScale,
                             c.cloudIntensity,
                             c.sunShaftStrength,
                             c.sunShaftFalloff,
                             c.volumetricStrength,
                             c.volumetricDensity,
                             c.volumetricDecay,
                             c.bloomThreshold,
                             c.bloomKnee,
                             c.bloomStrength,
                             c.bloomRadius,
                             c.terrainRelief,
                             c.basinDepth,
                             c.basinRimLift,
                             c.shadow.strength,
                             c.shadow.softness,
                             c.fogColorR,
                             c.fogColorG,
                             c.fogColorB,
                             c.fogStart,
                             c.fogEnd,
                             c.fogDensity,
                             c.fogMaxOpacity,
                             c.styleSaturation,
                             c.styleContrast,
                             c.styleVignette,
                             c.terrainTintR,
                             c.terrainTintG,
                             c.terrainTintB,
                             c.terrainAccentR,
                             c.terrainAccentG,
                             c.terrainAccentB,
                             c.terrainGridScale,
                             c.terrainGridStrength,
                             c.waterTintR,
                             c.waterTintG,
                             c.waterTintB,
                             c.waterAlpha,
                             c.waterReflectionStrength,
                             c.waterGlintStrength };

    for ( float value : values )
    {
        if ( !IsFinite( value ) )
        {
            return LookLabCandidateIssue::NonFiniteValue;
        }
    }

    const auto inRange = []( float value, float low, float high ) { return value >= low && value <= high; };
    const auto colorInRange = [&inRange]( float r, float g, float b, float high )
    { return inRange( r, 0.0f, high ) && inRange( g, 0.0f, high ) && inRange( b, 0.0f, high ); };

    if ( !c.enabled || !inRange( c.exposure, 0.55f, 2.25f ) || !inRange( c.gamma, 1.35f, 2.35f ) ||
         !inRange( c.sunAzimuth, 0.0f, 1.0f ) || !inRange( c.sunElevation, 0.12f, 0.88f ) ||
         !colorInRange( c.sunColorR, c.sunColorG, c.sunColorB, 3.2f ) || !inRange( c.sunIntensity, 0.4f, 28.0f ) ||
         !colorInRange( c.skyHorizonR, c.skyHorizonG, c.skyHorizonB, 2.2f ) ||
         !colorInRange( c.skyZenithR, c.skyZenithG, c.skyZenithB, 2.2f ) || !inRange( c.skyGlowStrength, 0.0f, 3.0f ) ||
         !inRange( c.cloudCoverage, 0.05f, 0.88f ) || !inRange( c.cloudSoftness, 0.04f, 0.55f ) ||
         !inRange( c.cloudScale, 1.2f, 18.0f ) || !inRange( c.cloudIntensity, 0.0f, 1.5f ) ||
         !inRange( c.sunShaftStrength, 0.0f, 1.6f ) || !inRange( c.sunShaftFalloff, 0.7f, 5.0f ) ||
         !inRange( c.volumetricStrength, 0.0f, 1.5f ) || !inRange( c.volumetricDensity, 0.0f, 1.6f ) ||
         !inRange( c.volumetricDecay, 0.86f, 0.985f ) || !inRange( c.bloomThreshold, 0.35f, 2.4f ) ||
         !inRange( c.bloomKnee, 0.08f, 1.2f ) || !inRange( c.bloomStrength, 0.0f, 1.25f ) ||
         !inRange( c.bloomRadius, 0.8f, 8.0f ) || !inRange( c.terrainRelief, 0.0f, 1.25f ) ||
         !inRange( c.basinDepth, 0.0f, 96.0f ) || !inRange( c.basinRimLift, 0.0f, 96.0f ) ||
         !inRange( c.shadow.strength, 0.0f, 1.0f ) || !inRange( c.shadow.softness, 0.5f, 2.5f ) ||
         !colorInRange( c.fogColorR, c.fogColorG, c.fogColorB, 2.2f ) || !inRange( c.fogDensity, 0.0f, 0.012f ) ||
         !inRange( c.fogStart, 0.0f, 10000.0f ) || !inRange( c.fogEnd, 0.0f, 20000.0f ) ||
         !inRange( c.fogMaxOpacity, 0.0f, 0.82f ) ||
         c.fogEnd < c.fogStart + Q12ToFloat( MinimumFogSpanQ12( static_cast<int>( c.fogStart * Q12_ONE ) ) ) ||
         !inRange( c.styleSaturation, 0.55f, 1.85f ) || !inRange( c.styleContrast, 0.70f, 1.75f ) ||
         !inRange( c.styleVignette, 0.0f, 0.62f ) || !colorInRange( c.terrainTintR, c.terrainTintG, c.terrainTintB, 2.2f ) ||
         !colorInRange( c.terrainAccentR, c.terrainAccentG, c.terrainAccentB, 2.2f ) ||
         !inRange( c.terrainGridScale, 8.0f, 96.0f ) || !inRange( c.terrainGridStrength, 0.0f, 2.0f ) ||
         !colorInRange( c.waterTintR, c.waterTintG, c.waterTintB, 2.2f ) || !inRange( c.waterAlpha, 0.25f, 1.0f ) ||
         !inRange( c.waterReflectionStrength, 0.0f, 1.0f ) || !inRange( c.waterGlintStrength, 0.0f, 2.5f ) )
    {
        return LookLabCandidateIssue::ValueOutOfRange;
    }

    const Core::CinematicRenderConfig retainedDefaults;

    if ( c.shadow.mapSize != retainedDefaults.shadow.mapSize || c.shadow.pcfRadius != retainedDefaults.shadow.pcfRadius ||
         c.shadow.depthBias != retainedDefaults.shadow.depthBias ||
         c.shadow.slopeBias != retainedDefaults.shadow.slopeBias ||
         c.shadow.maxDistance != retainedDefaults.shadow.maxDistance ||
         c.shadow.terrainCasts != retainedDefaults.shadow.terrainCasts ||
         c.shadow.objectsCast != retainedDefaults.shadow.objectsCast ||
         c.shadow.terrainReceives != retainedDefaults.shadow.terrainReceives ||
         c.shadow.objectsReceive != retainedDefaults.shadow.objectsReceive ||
         c.basinCenterX != retainedDefaults.basinCenterX || c.basinCenterZ != retainedDefaults.basinCenterZ ||
         c.basinRadiusX != retainedDefaults.basinRadiusX || c.basinRadiusZ != retainedDefaults.basinRadiusZ ||
         c.basinFeather != retainedDefaults.basinFeather )
    {
        return LookLabCandidateIssue::ValueOutOfRange;
    }

    if ( !IsSupportedSkyMode( c.skyMode ) || c.terrainMode < 0 || c.terrainMode > 15 || c.objectStyle < 0 ||
         c.objectStyle > 7 || c.waterMode < 0 || c.waterMode > 4 )
    {
        return LookLabCandidateIssue::UnsupportedMode;
    }

    if ( c.godRaysEnabled && !c.volumetricLightingEnabled )
    {
        return LookLabCandidateIssue::IncompatibleFeatures;
    }

    if ( ( !c.cloudsEnabled && c.cloudIntensity != 0.0f ) ||
         ( !c.volumetricLightingEnabled && ( c.volumetricStrength != 0.0f || c.volumetricDensity != 0.0f ) ) ||
         ( !c.bloomEnabled && c.bloomStrength != 0.0f ) ||
         ( !c.fogEnabled && ( c.fogDensity != 0.0f || c.fogMaxOpacity != 0.0f ) ) ||
         ( !c.terrainReliefEnabled && ( c.terrainRelief != 0.0f || c.basinDepth != 0.0f || c.basinRimLift != 0.0f ) ) ||
         ( !c.shadow.enabled && c.shadow.strength != 0.0f ) )
    {
        return LookLabCandidateIssue::IncompatibleFeatures;
    }

    if ( c.skyMode == Core::CinematicStyleMode::Sky::DeepSpace &&
         ( c.skyAtmosphereEnabled || c.cloudsEnabled || c.godRaysEnabled || c.fogEnabled ) )
    {
        return LookLabCandidateIssue::IncompatibleFeatures;
    }

    const float terrainLuminance = Luminance( c.terrainTintR, c.terrainTintG, c.terrainTintB );
    const float terrainPeak = std::max( { c.terrainTintR, c.terrainTintG, c.terrainTintB } );
    const float waterPeak = std::max( { c.waterTintR, c.waterTintG, c.waterTintB } );
    const float accentSeparation = std::max( { std::abs( c.terrainTintR - c.terrainAccentR ),
                                               std::abs( c.terrainTintG - c.terrainAccentG ),
                                               std::abs( c.terrainTintB - c.terrainAccentB ) } );

    if ( terrainPeak < 0.01f || ( c.waterMode != 0 && waterPeak < 0.01f ) || accentSeparation < 0.02f )
    {
        return LookLabCandidateIssue::InvisiblePalette;
    }

    float brightest = std::max( Luminance( c.skyHorizonR, c.skyHorizonG, c.skyHorizonB ),
                                Luminance( c.skyZenithR, c.skyZenithG, c.skyZenithB ) );

    brightest = std::max( brightest, terrainLuminance );
    constexpr const char* EXPECTED_TARGETS[LOOK_LAB_MATERIAL_RULE_COUNT] = { "balls", "boxes", "hulls" };

    for ( size_t ruleIndex = 0; ruleIndex < candidate.materialRules.size(); ++ruleIndex )
    {
        const LookLabMaterialRule& rule = candidate.materialRules[ruleIndex];
        const Rendering::RenderMaterial& material = rule.material;

        if ( std::memchr( rule.target.data(), '\0', rule.target.size() ) == nullptr ||
             std::strcmp( rule.target.data(), EXPECTED_TARGETS[ruleIndex] ) != 0 ||
             std::memchr( material.name, '\0', sizeof( material.name ) ) == nullptr )
        {
            return LookLabCandidateIssue::IncompatibleFeatures;
        }

        const float materialValues[] = { material.baseColor[0],     material.baseColor[1],     material.baseColor[2],
                                         material.baseColor[3],     material.emissiveColor[0], material.emissiveColor[1],
                                         material.emissiveColor[2], material.emissiveStrength, material.roughness,
                                         material.metallic,         material.specular,         material.transmission,
                                         material.stylization };

        for ( float value : materialValues )
        {
            if ( !IsFinite( value ) )
            {
                return LookLabCandidateIssue::NonFiniteValue;
            }
        }

        const int kind = static_cast<int>( material.kind );

        if ( kind < 0 || kind > 13 )
        {
            return LookLabCandidateIssue::UnsupportedMode;
        }

        if ( !inRange( material.baseColor[3], 0.0f, 1.0f ) || !inRange( material.roughness, 0.0f, 1.0f ) ||
             !inRange( material.metallic, 0.0f, 1.0f ) || !inRange( material.specular, 0.0f, 1.0f ) ||
             !inRange( material.transmission, 0.0f, 1.0f ) || !inRange( material.stylization, 0.0f, 1.0f ) ||
             !colorInRange( material.baseColor[0], material.baseColor[1], material.baseColor[2], 2.2f ) ||
             !colorInRange( material.emissiveColor[0], material.emissiveColor[1], material.emissiveColor[2], 4.0f ) ||
             !inRange( material.emissiveStrength, 0.0f, 8.0f ) || material.flags != 0 ||
             material.contactFlashAlpha != 0.0f ||
             material.textureMode != Rendering::RenderMaterialKindLegacyMode( material.kind ) )
        {
            return LookLabCandidateIssue::ValueOutOfRange;
        }

        brightest = std::max( brightest, Luminance( material.baseColor[0], material.baseColor[1], material.baseColor[2] ) );
    }

    return brightest <= 0.02f ? LookLabCandidateIssue::BlackFrame : LookLabCandidateIssue::None;
}

std::vector<uint8_t> EncodeLookLabCandidateCanonical( const LookLabCandidate& candidate )
{
    std::vector<uint8_t> bytes;
    bytes.reserve( 512 );
    AppendInteger( bytes, candidate.seed );
    AppendInteger( bytes, candidate.generatorVersion );
    bytes.push_back( static_cast<uint8_t>( candidate.recipe ) );
    AppendCinematic( bytes, candidate.cinematic );

    for ( const LookLabMaterialRule& rule : candidate.materialRules )
    {
        bytes.insert( bytes.end(), rule.target.begin(), rule.target.end() );
        bytes.insert( bytes.end(), std::begin( rule.material.name ), std::end( rule.material.name ) );
        bytes.push_back( static_cast<uint8_t>( rule.material.kind ) );

        for ( float value : rule.material.baseColor )
        {
            AppendFloat( bytes, value );
        }

        for ( float value : rule.material.emissiveColor )
        {
            AppendFloat( bytes, value );
        }

        AppendFloat( bytes, rule.material.emissiveStrength );
        AppendFloat( bytes, rule.material.roughness );
        AppendFloat( bytes, rule.material.metallic );
        AppendFloat( bytes, rule.material.specular );
        AppendFloat( bytes, rule.material.transmission );
        AppendFloat( bytes, rule.material.stylization );
        AppendFloat( bytes, rule.material.textureMode );
        AppendFloat( bytes, rule.material.contactFlashAlpha );
        AppendInteger( bytes, rule.material.flags );
    }

    return bytes;
}

uint64_t FingerprintLookLabCandidate( const LookLabCandidate& candidate )
{
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
    constexpr uint64_t FNV_PRIME = 1099511628211ull;
    uint64_t hash = FNV_OFFSET;

    for ( uint8_t byte : EncodeLookLabCandidateCanonical( candidate ) )
    {
        hash ^= byte;
        hash *= FNV_PRIME;
    }

    return hash;
}
} // namespace SkullbonezCore::Runtime
