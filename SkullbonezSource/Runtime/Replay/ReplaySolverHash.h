/*
File: SkullbonezSource/Runtime/Replay/ReplaySolverHash.h
Purpose:
  Declares the recorder-owned deterministic solver hash field walk shared by
  retained replay samples and forward-prediction samples.

Summary:
  A solver hash is built in two stages: seed it with frame-wide world and
  hidden-solver state, then append bodies in dense model-row order. Prediction
  and recording must call these same functions or they could compare two
  different definitions of physics state.

Glossary:
  Solver hash: Deterministic digest of the replay fields that influence or
    describe one committed fixed step.
  Field walk: Fixed ordering in which typed values are packed into the digest.
  Prediction frame: Replay-owned sample produced by the private physics engine.

Invariants:
  - Recorder and prediction hashes use one implementation and one field order.
  - Bodies are appended in dense model-row order after the frame-wide seed.
  - Hash construction performs no allocation and does not mutate physics.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
*/
#pragma once

#include "ReplayRecorder.h"

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Physics
{
struct ColliderRecord;
} // namespace Physics

namespace Runtime
{
// Starts the canonical solver hash with frame-wide values. Callers append each
// ReplaySolverBodySample in model-row order with AppendReplaySolverBodyHash.
uint64_t BeginReplaySolverHash( const ReplayWorldPresentationSample& world,
                                int modelCount,
                                std::size_t contactCount,
                                std::size_t pipelineRecordCount,
                                const ReplayLauncherVisualSample& launcherVisual,
                                const ReplaySolverWorldSnapshot& worldSnapshot );

uint64_t AppendReplaySolverBodyHash( uint64_t hash, const ReplaySolverBodySample& body );

// Maps physics-owned collider shape vocabulary to the stable replay value used
// by both recorder and prediction body hashes.
ReplayBodyShapeKind ReplayShapeKindForCollider( const Physics::ColliderRecord& collider );
} // namespace Runtime
} // namespace SkullbonezCore
