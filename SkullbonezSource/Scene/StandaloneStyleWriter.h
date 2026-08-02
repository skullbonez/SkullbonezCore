/*
File: StandaloneStyleWriter.h
Purpose:
  Declares the detached schema-v1 style snapshot and exact writer boundary.

Summary:
  Scene owns the complete standalone style document because it owns the matching
  parser and schema vocabulary. Callers provide resolved cinematic values and
  ordered material rules without lending Runtime state or renderer stores.

Glossary:
  Standalone style snapshot: Fully resolved presentation value that needs no
    include, generator catalog, or inherited default when loaded later.
  Flattened listing: Human-readable ordered key/value projection derived from
    the authoritative JSON document.

Invariants:
  - The emitted root is skullbonez.style.json schema version 1 with no includes.
  - Every cinematic atom and complete material payload is always present.
  - Serialization and flattened listing use one ordered document construction.

Related:
  - SkullbonezSource/Scene/StandaloneStyleWriter.cpp
  - SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp
  - Agentic/Reports/2026-08-01/look-lab-random-style-authoring-ll0-census.md
*/
#pragma once

#include "../Core/Config.h"
#include "../Core/SbResult.h"
#include "../Rendering/RenderMaterial.h"

#include <array>
#include <string>
#include <vector>

namespace SkullbonezCore::Core
{
class SbDiagnosticStore;
}

namespace SkullbonezCore::Scene
{
struct StandaloneStyleMaterialRule
{
    std::array<char, 64> target = {};
    Rendering::RenderMaterial material;
};

// Invariant: this is one immutable-on-publication style value, not a
// multi-owner context. The schema consumes every member together, and
// TestLookLabSerialization.cpp proves complete production-parser round-trip.
struct StandaloneStyleSnapshot
{
    Core::CinematicRenderConfig cinematic;
    std::vector<StandaloneStyleMaterialRule> materialRules;
};

class StandaloneStyleWriter
{
  public:

    // Returns stable UTF-8 JSON with a final newline after validating the whole
    // detached value; failure leaves output publication to the caller.
    [[nodiscard]] static Core::SbResult Serialize( Core::SbDiagnosticStore& diagnostics,
                                                   const StandaloneStyleSnapshot& snapshot, std::string& output );

    // Derives an ordered key/value listing from the same schema document used
    // by Serialize; callers must not treat this text as a reload format.
    [[nodiscard]] static Core::SbResult BuildFlattenedListing( Core::SbDiagnosticStore& diagnostics,
                                                               const StandaloneStyleSnapshot& snapshot,
                                                               std::string& output );

    // Creates missing parent directories and atomically replaces path only
    // after complete serialization succeeds.
    [[nodiscard]] static Core::SbResult SaveAtomic( Core::SbDiagnosticStore& diagnostics,
                                                    const StandaloneStyleSnapshot& snapshot, const char* path );
};
} // namespace SkullbonezCore::Scene
