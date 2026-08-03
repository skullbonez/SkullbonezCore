/*
File: LookLabGenerator.h
Purpose:
  Defines the detached Look Lab candidate and pure versioned generator API.

Summary:
  Runtime Direction can explore presentation looks without borrowing a scene,
  renderer, filesystem, or gameplay random stream. Candidates carry visual
  intent only; later owners resolve retained scene and quality values.

Glossary:
  Candidate: Detached presentation value produced solely from a seed and
    generator version.
  Canonical bytes: Explicit little-endian field encoding that excludes C++
    padding and container layout from the reproducibility contract.

Invariants:
  - Generator version 1 uses only its private SplitMix64 stream.
  - Identical seed and version inputs produce identical canonical bytes.
  - Candidates never authorize resource sizes, shader compilation, or scene
    mutation.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabGenerator.cpp
*/
#pragma once

#include "../../Core/Config.h"
#include "../../Rendering/RenderMaterial.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
inline constexpr uint32_t LOOK_LAB_GENERATOR_VERSION = 1;
inline constexpr std::size_t LOOK_LAB_MATERIAL_RULE_COUNT = 3;

enum class LookLabRecipeFamily : uint8_t
{
    GoldenRealism = 0,
    LowPolyStorybook,
    PainterlyPoster,
    NeonCyberpunk,
    TronGraphic,
    AtmosphericStorm,
    StudioHighKey,
    IndustrialLowKey,
    DesertWarm,
    NordicCool,
    OceanTerrestrial,
    AlienWorld,
    DeepSpaceDreamscape,
    AbstractChromatic,
    Count
};

enum class LookLabCandidateIssue : uint8_t
{
    None = 0,
    UnsupportedVersion,
    NonFiniteValue,
    ValueOutOfRange,
    UnsupportedMode,
    IncompatibleFeatures,
    InvisiblePalette,
    BlackFrame
};

struct LookLabMaterialRule
{
    std::array<char, 16> target = {};
    Rendering::RenderMaterial material;
};

struct LookLabCandidate
{
    uint64_t seed = 0;
    uint32_t generatorVersion = LOOK_LAB_GENERATOR_VERSION;
    LookLabRecipeFamily recipe = LookLabRecipeFamily::GoldenRealism;
    Core::CinematicRenderConfig cinematic;
    std::array<LookLabMaterialRule, LOOK_LAB_MATERIAL_RULE_COUNT> materialRules = {};
};

const char* LookLabRecipeFamilyName( LookLabRecipeFamily family );
LookLabCandidate GenerateLookLabCandidate( uint64_t seed, uint32_t generatorVersion = LOOK_LAB_GENERATOR_VERSION );
LookLabCandidateIssue ValidateLookLabCandidate( const LookLabCandidate& candidate );
std::vector<uint8_t> EncodeLookLabCandidateCanonical( const LookLabCandidate& candidate );
uint64_t FingerprintLookLabCandidate( const LookLabCandidate& candidate );
} // namespace SkullbonezCore::Runtime
