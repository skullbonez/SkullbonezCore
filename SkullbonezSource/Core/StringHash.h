/*
File: SkullbonezSource/Core/StringHash.h
Purpose:
  Defines the deterministic compile-time string hash shared by engine registries
  and profiler marker identities.

Summary:
  String-authored systems keep human-readable names at their boundaries and use
  this compact value only for fixed lookup tables and marker matching. The hash
  is infrastructure rather than asset policy, so lower layers can consume it
  without depending on Assets.

Glossary:
  FNV-1a: Deterministic non-cryptographic hash that mixes each input byte into a
    32-bit value using a fixed offset and prime.
  Marker identity: Stable hash paired with a full profiler path for bounded
    lookup and collision validation.

Invariants:
  - HashStr remains constexpr and preserves the exact legacy FNV-1a values.
  - Callers retain the original string when collisions must be detected or
    authored identity must remain visible.

Related:
  - SkullbonezSource/Assets/AssetKeys.h defines asset-specific hashed names.
  - SkullbonezSource/Core/Profiler.h consumes marker hashes.
  - SkullbonezSource/Core/WorkerPool.h hashes its fallback marker path.
*/
#pragma once

#include <cstdint>

constexpr std::uint32_t HashStr( const char* value, std::uint32_t hash = 2166136261u )
{
    return ( *value == '\0' ) ? hash : HashStr( value + 1, ( hash ^ static_cast<std::uint32_t>( *value ) ) * 16777619u );
}
