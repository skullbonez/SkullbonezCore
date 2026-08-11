/*
File: LookLabBundleWriter.h
Purpose:
  Declares Look Lab bundle naming and human-readable receipt publication.

Summary:
  Runtime Direction supplies detached generation/source/status facts while the
  Scene writer supplies the authoritative flattened style projection. The
  helper exclusively creates one timestamp/seed directory and atomically
  publishes its derived receipt.

Glossary:
  Bundle: One directory containing look.style.json, look.txt, and look.png.
  Receipt status: Honest per-artifact state that may remain partial when image
    capture fails after the style has succeeded.

Invariants:
  - Directory names use a validated local timestamp and 16 lowercase seed hex.
  - An existing bundle directory is a collision failure and is never reused.
  - Receipt settings are derived from the Scene snapshot, not reconstructed.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabBundleWriter.cpp
  - SkullbonezSource/Scene/StandaloneStyleWriter.h
*/
#pragma once

#include "LookLabGenerator.h"
#include "../../Core/SbResult.h"
#include "../../Scene/StandaloneStyleWriter.h"

#include <array>
#include <cstdint>
#include <string>

namespace SkullbonezCore::Core
{
class SbDiagnosticStore;
}

namespace SkullbonezCore::Runtime
{
enum class LookLabArtifactStatus : uint8_t
{
    Pending = 0,
    Saved,
    Failed,
    Cancelled
};

struct LookLabBundlePaths
{
    // Invariant: CreateBundleDirectory constructs all four paths from the same
    // exclusively reserved directory; TestLookLabSerialization.cpp pins their
    // relationship and collision behavior.
    std::array<char, 512> directory = {};
    std::array<char, 512> style = {};
    std::array<char, 512> receipt = {};
    std::array<char, 512> screenshot = {};
};

struct LookLabReceiptFacts
{
    // Invariant: one receipt revision consumes this complete fact set together.
    // Artifact diagnostics describe the status beside them and the focused
    // serialization test proves pending-to-failed atomic revision.
    std::array<char, 20> localTimestamp = {};
    int utcOffsetMinutes = 0;
    uint64_t seed = 0;
    uint32_t generatorVersion = LOOK_LAB_GENERATOR_VERSION;
    LookLabRecipeFamily recipe = LookLabRecipeFamily::GoldenRealism;
    std::array<char, 512> sourceScenePath = {};
    std::array<char, 128> sourceSceneDisplayName = {};
    LookLabArtifactStatus styleStatus = LookLabArtifactStatus::Pending;
    LookLabArtifactStatus screenshotStatus = LookLabArtifactStatus::Pending;
    std::array<char, 256> styleDiagnostic = {};
    std::array<char, 256> screenshotDiagnostic = {};
};

class LookLabBundleWriter
{
  public:

    // Reserves exactly <timestamp>_seed_<16hex> beneath lookLabRoot. Existing
    // final directories are collisions and are never adopted or overwritten.
    [[nodiscard]] static Core::SbResult CreateBundleDirectory( Core::SbDiagnosticStore& diagnostics, const char* lookLabRoot,
                                                               const char* localTimestamp, uint64_t seed,
                                                               LookLabBundlePaths& output );

    // Builds derived inspection text from exact transaction facts plus Scene's
    // authoritative flattened snapshot; it performs no filesystem mutation.
    [[nodiscard]] static Core::SbResult BuildReceipt( Core::SbDiagnosticStore& diagnostics, const LookLabReceiptFacts& facts,
                                                      const Scene::StandaloneStyleSnapshot& snapshot,
                                                      const LookLabBundlePaths& paths, std::string& output );

    // Atomically publishes look.txt at paths.receipt after complete validation
    // and receipt construction.
    [[nodiscard]] static Core::SbResult SaveReceiptAtomic( Core::SbDiagnosticStore& diagnostics,
                                                           const LookLabReceiptFacts& facts,
                                                           const Scene::StandaloneStyleSnapshot& snapshot,
                                                           const LookLabBundlePaths& paths );
};
} // namespace SkullbonezCore::Runtime
