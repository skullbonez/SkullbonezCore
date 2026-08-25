/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h
Purpose:
  Declares the value-only prediction-state payload embedded in visual replay artifacts.

Summary:
  A sectioned current artifact records lightweight presentation state plus
  bounded unique event-frame solver evidence when captured in High detail.
  Loading treats captured capability and the active High/Low preference as
  separate values, validates into cold candidate owners, and commits by swap.
  The archive never stores a physics engine, worker task, pointer, renderer
  resource, or other process-local owner.

Glossary:
  Prediction archive: Typed future frames and presentation caches required to
    reproduce a completed replay prediction without running prediction physics.
  Offline reconstruction: Restoring saved values without a window, render
    submission, or prediction-generation capability.

Invariants:
  - The payload is little-endian, bounded, and field-serialized.
  - Low payloads contain no solver-evidence section or retained evidence capacity.
  - Legacy v2/v3 and sectioned v4 payloads remain readable; precise lightweight
    v5 payloads preserve the tornado clock and report Low captured capability.
  - Debug-contact presentation scratch and private prediction-engine state are excluded.
  - Failure leaves the destination state and evidence banks unchanged.
  - Loading always leaves prediction generation disabled and build state clean.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct RunReplayPathVisualizerState;
struct RunReplayPredictionState;
class ReplayPredictionSolverEvidenceBanks;
class ReplayPredictionSolverEvidenceStore;
enum class ReplayPredictionArchiveDetailCapability : uint8_t;
enum class ReplayPredictionDetailMode : uint8_t;

namespace ReplayPredictionArchiveOperations
{
bool BuildReplayPredictionArchive( const RunReplayPathVisualizerState& pathVisualizer,
                                   const RunReplayPredictionState& prediction, ReplayPredictionDetailMode detailMode,
                                   const ReplayPredictionSolverEvidenceStore& evidence, std::vector<uint8_t>& outBytes );

bool LoadReplayPredictionArchive( std::span<const uint8_t> bytes, RunReplayPathVisualizerState& pathVisualizer,
                                  RunReplayPredictionState& prediction, ReplayPredictionSolverEvidenceBanks& evidence,
                                  ReplayPredictionDetailMode activePreference,
                                  ReplayPredictionArchiveDetailCapability& outCapturedCapability, char* outReason,
                                  std::size_t reasonSize );

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
// Validation seam: emits an authentic supported schema, including that
// schema's historical quaternion representation. Runtime save paths use the
// current-schema builder above.
bool BuildReplayPredictionArchiveForSchemaValidation( const RunReplayPathVisualizerState& pathVisualizer,
                                                      const RunReplayPredictionState& prediction, uint32_t schema,
                                                      std::vector<uint8_t>& outBytes );

// Automation restores and rebuilds one captured RVPD payload. Product
// configurations deliberately expose no verifier declaration or fallback.
bool VerifyReplayPredictionArchiveRoundTrip( std::span<const uint8_t> bytes, char* outReason, std::size_t reasonSize );
#endif
} // namespace ReplayPredictionArchiveOperations
} // namespace SkullbonezCore::Runtime
