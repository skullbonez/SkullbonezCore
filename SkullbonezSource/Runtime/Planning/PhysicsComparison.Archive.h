#pragma once

#include <filesystem>
#include <span>
#include <string>

namespace SkullbonezCore::Runtime
{
struct ComparisonLoadProgress;

// Cold loading only: reconstruct immutable diagnostic bytes into a temporary
// file. The comparison loader verifies the full recorded SHA-256 before use.
bool RestoreComparisonDiagnostics( const std::filesystem::path& directory, std::span<const std::string> parts,
                                   const std::filesystem::path& output, ComparisonLoadProgress& progress );
} // namespace SkullbonezCore::Runtime
