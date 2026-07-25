/*
File: SkullbonezSource/Runtime/Replay/ReplayPredictionArchive.Automation.cpp
Purpose:
  Verifies the RVPD prediction archive by restoring and rebuilding its bytes in
  the Automation configuration.

Summary:
  The sole replay-fidelity process captures one completed prediction. This
  non-presenting verifier restores that payload into temporary domain values
  and demands exact re-serialization before the process exits.

Glossary:
  RVPD (Replay Visual Prediction Data): Bounded little-endian payload containing
    the completed prediction values required by durable visual replay.
  Round trip: Decode followed by encode, with exact byte equality required.

Invariants:
  - This translation unit is linked only by the Automation engine configuration.
  - Verification never schedules prediction work or presents a second path.
  - Failure strings are frozen probe-output schema.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayPredictionArchive.cpp
  - tools/validate_replay_visual_fidelity.bat
*/
#include "ReplayPredictionArchive.h"

#include "ReplayRuntime.h"

#include <cstdio>
#include <cstring>
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
    RunReplayPathVisualizerState restoredPathVisualizer;
    RunReplayPredictionState restoredPrediction;
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

    return true;
}
} // namespace SkullbonezCore::Runtime::ReplayPredictionArchiveOperations
