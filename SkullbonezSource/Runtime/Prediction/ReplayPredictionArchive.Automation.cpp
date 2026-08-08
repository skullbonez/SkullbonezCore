/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp
Purpose:
  Verifies the RVPD prediction archive by restoring and rebuilding its bytes in
  the Automation configuration.

Summary:
  The sole replay-fidelity process captures one completed prediction. This
  non-presenting verifier restores that payload into temporary domain values
  and demands exact re-serialization before the process exits. It also emits
  authentic schema-v2 quaternion bytes and rejects a future schema.

Glossary:
  RVPD (Replay Visual Prediction Data): Bounded little-endian payload containing
    the completed prediction values required by durable visual replay.
  Round trip: Decode followed by encode, with exact byte equality required.

Invariants:
  - This translation unit is linked only by the Automation engine configuration.
  - Verification never schedules prediction work or presents a second path.
  - Legacy verification rebuilds canonical bytes after migration.
  - Failure strings are frozen probe-output schema.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp
  - tools/validate_replay_visual_fidelity.bat
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayPredictionArchive.h"
#include "ReplayPrediction.h"

#include "../Replay/ReplayPathPackets.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace SkullbonezCore::Runtime::ReplayPredictionArchiveOperations
{
namespace
{
void WriteAutomationReason( char* destination, std::size_t size, const char* message )
{

    if ( destination && size > 0 )
    {
        std::snprintf( destination, size, "%s", message ? message : "prediction archive failure" );
    }
}
} // namespace

bool VerifyReplayPredictionArchiveRoundTrip( std::span<const uint8_t> bytes, char* outReason, std::size_t reasonSize )
{

    // Lane P: this is bounded validation work performed after the sole live
    // capture. Temporary vectors are diagnostics artifacts, never steady-state
    // replay storage or a second presentation path.
    // Hazard: RunReplayPredictionState carries the whole prediction aggregate,
    // roughly 840 KB. Three of them as locals made this function probe about
    // 2.5 MB of stack at entry and fault in __chkstk before its first
    // statement, against a 1 MB default. Runtime allocation policy: an explicit
    // cold Automation-only verification action, never a steady runtime path.
    const auto restoredPathVisualizerStorage = std::make_unique<RunReplayPathVisualizerState>();
    const auto restoredPredictionStorage = std::make_unique<RunReplayPredictionState>();
    RunReplayPathVisualizerState& restoredPathVisualizer = *restoredPathVisualizerStorage;
    RunReplayPredictionState& restoredPrediction = *restoredPredictionStorage;

    if ( !LoadReplayPredictionArchive( bytes, restoredPathVisualizer, restoredPrediction, outReason, reasonSize ) )
    {
        return false;
    }

    std::vector<uint8_t> rebuiltBytes;

    if ( !BuildReplayPredictionArchive( restoredPathVisualizer, restoredPrediction, rebuiltBytes ) )
    {
        WriteAutomationReason( outReason, reasonSize, "could not rebuild prediction archive" );
        return false;
    }

    if ( rebuiltBytes.size() != bytes.size() ||
         ( !bytes.empty() && std::memcmp( rebuiltBytes.data(), bytes.data(), bytes.size() ) != 0 ) )
    {
        WriteAutomationReason( outReason, reasonSize, "prediction archive round-trip bytes diverged" );
        return false;
    }

    std::vector<uint8_t> legacyBytes;

    if ( !BuildReplayPredictionArchiveForSchemaValidation( restoredPathVisualizer, restoredPrediction, 2u, legacyBytes ) )
    {
        WriteAutomationReason( outReason, reasonSize, "could not build legacy prediction archive" );
        return false;
    }

    const auto migratedPathVisualizerStorage = std::make_unique<RunReplayPathVisualizerState>();
    const auto migratedPredictionStorage = std::make_unique<RunReplayPredictionState>();
    RunReplayPathVisualizerState& migratedPathVisualizer = *migratedPathVisualizerStorage;
    RunReplayPredictionState& migratedPrediction = *migratedPredictionStorage;

    if ( !LoadReplayPredictionArchive( legacyBytes, migratedPathVisualizer, migratedPrediction, outReason, reasonSize ) )
    {
        return false;
    }

    std::vector<uint8_t> migratedCanonicalBytes;

    if ( !BuildReplayPredictionArchive( migratedPathVisualizer, migratedPrediction, migratedCanonicalBytes ) ||
         migratedCanonicalBytes.size() != bytes.size() ||
         ( !bytes.empty() && std::memcmp( migratedCanonicalBytes.data(), bytes.data(), bytes.size() ) != 0 ) )
    {
        WriteAutomationReason( outReason, reasonSize, "legacy prediction archive migration diverged" );
        return false;
    }

    std::vector<uint8_t> futureBytes( bytes.begin(), bytes.end() );

    if ( futureBytes.size() < 8u )
    {
        WriteAutomationReason( outReason, reasonSize, "prediction archive header is truncated" );
        return false;
    }

    // Hazard: bytes 4..7 are the little-endian schema field immediately after
    // the fixed RVPD magic. A future value must fail closed before any payload
    // values are accepted.
    futureBytes[4] = 4u;
    futureBytes[5] = 0u;
    futureBytes[6] = 0u;
    futureBytes[7] = 0u;
    const auto rejectedPathVisualizerStorage = std::make_unique<RunReplayPathVisualizerState>();
    const auto rejectedPredictionStorage = std::make_unique<RunReplayPredictionState>();
    RunReplayPathVisualizerState& rejectedPathVisualizer = *rejectedPathVisualizerStorage;
    RunReplayPredictionState& rejectedPrediction = *rejectedPredictionStorage;
    char futureReason[128] = {};

    if ( LoadReplayPredictionArchive( futureBytes, rejectedPathVisualizer, rejectedPrediction, futureReason,
                                      sizeof( futureReason ) ) ||
         std::strcmp( futureReason, "invalid prediction archive header" ) != 0 )
    {
        WriteAutomationReason( outReason, reasonSize, "future prediction archive schema was not rejected" );
        return false;
    }

    return true;
}
} // namespace SkullbonezCore::Runtime::ReplayPredictionArchiveOperations
