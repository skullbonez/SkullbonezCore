/*
File: SkullbonezSource/Runtime/Replay/ReplayIdentity.h
Purpose:
  Defines replay-wide fixed capacities and schema-neutral value constants.

Summary:
  These constants are shared by multiple replay owners without exposing owner state or services.

Glossary:
  Capacity: A fixed upper bound established before steady runtime work.
  Frame index: Monotonic replay-timeline position shared by retained tracks.

Invariants:
  - This header contains values only: no mutable state, services, or callbacks.
  - ReplayFrameIndex is the single replay-wide frame-cursor representation.
  - M2 preserves the moved definition bodies verbatim.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "../Scene/SceneCapacity.h"

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
using ReplayFrameIndex = uint64_t;

inline constexpr std::size_t REPLAY_PREDICTION_GHOST_MAX_FRAMES = 24;
inline constexpr std::size_t REPLAY_PREDICTION_GHOST_REQUEST_CAPACITY =
    ( REPLAY_PREDICTION_GHOST_MAX_FRAMES + 2u ) *
    static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
inline constexpr std::size_t REPLAY_PREDICTION_MARKER_CAPACITY =
    static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
inline constexpr std::size_t REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY = 261u;
// Runtime allocation policy: live replay path-target picks rotate inside this
// fixed vector budget instead of growing while gameplay is running.
inline constexpr std::size_t REPLAY_PATH_MAX_ROOT_TARGETS = 100u;
inline constexpr std::size_t REPLAY_CAUSE_TREE_CONTACT_CAPACITY =
    static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS ) * 4u;
inline constexpr std::size_t REPLAY_CAUSE_TREE_ROW_CAPACITY =
    1u + static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS ) +
    REPLAY_CAUSE_TREE_CONTACT_CAPACITY * 3u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;

// Why: both presentation camera focus and authoring cause rows persist this
// value. It is a schema-neutral discriminator, not either owner's mutable state.
enum class RunReplayCauseTreeRowKind
{
    Body,
    Manifold,
    SolverRow,
    PredictionContact,
    PredictionMotion
};

} // namespace Runtime
} // namespace SkullbonezCore
