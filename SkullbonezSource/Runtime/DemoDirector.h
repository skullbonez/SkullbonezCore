/*
File: SkullbonezSource/Runtime/DemoDirector.h
Purpose:
  Defines the authored shot-list data and playback state used by the Demo Director.

Summary:
  A shot list is a compact sequence of operator-authored phases. Each phase
  owns a camera pose, optional render style path, and the rule that advances to
  the next phase. Runtime playback keeps a bounded active shot list plus timing
  state so Director mode can blend and time these records without inventing
  camera framing.

Glossary:
  Shot list: Ordered `.shot.json` file that choreographs a deterministic demo.
  Phase: One authored shot: camera pose, style path, and advance rule.
  Playback state: Runtime cursor, timers, and applied-style/reveal-rate memory
    for the active shot list.
  Camera pose: Eye, view target, and up vector stored in CameraCollection terms.
  Reveal threshold: Normalized prediction reveal progress that can advance a
    phase when the causal visualization reaches an authored point.

Invariants:
  - Phase storage is fixed-capacity so playback never needs runtime growth.
  - Playback state owns only presentation timing/style facts; it must not mutate
    deterministic physics state directly.
  - Style paths are borrowed text tokens; applying the style is a later phase.
  - Load/save are cold authoring paths and return bool status instead of throwing.

Related:
  - SkullbonezSource/Runtime/CameraCollection.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Maths/Vector3.h"

#include <array>
#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{
enum class PhaseAdvance
{
    Manual,
    Timer,
    RevealAtLeast
};

struct DemoCameraPose
{
    Math::Vector::Vector3 eye = Math::Vector::Vector3( 0.0f, 0.0f, 0.0f );
    Math::Vector::Vector3 view = Math::Vector::Vector3( 0.0f, 0.0f, -1.0f );
    Math::Vector::Vector3 up = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
};

struct DemoPhase
{
    static constexpr std::size_t NAME_BYTES = 48;
    static constexpr std::size_t STYLE_PATH_BYTES = 192;

    char name[NAME_BYTES] = {};
    DemoCameraPose camera;
    char stylePath[STYLE_PATH_BYTES] = {};              // `SkullbonezData/styles/*.style.json`, empty means keep current look.
    PhaseAdvance advance = PhaseAdvance::Manual;
    float timerSeconds = 4.0f;
    float revealThreshold = 1.0f;                       // Normalized 0..1 reveal progress.
    float blendInSeconds = 1.0f;
    float revealRate = 1.0f;
};

struct DemoShotList
{
    static constexpr int MAX_PHASES = 32;

    std::array<DemoPhase, MAX_PHASES> phases = {};
    int phaseCount = 0;
    bool loop = false;
};

struct DemoDirectorPlaybackState
{
    static constexpr int SHOT_LIST_PATH_BYTES = 260;

    // Concept: Director playback is camera presentation state. The shot list is
    // fixed-capacity authoring data, and these timers only decide which authored
    // pose should drive the camera on later playback slices.
    DemoShotList activeShotList;
    bool hasActiveShotList = false;
    char activeShotListPath[SHOT_LIST_PATH_BYTES] = {}; // Cold authoring save target captured from load.
    int currentPhaseIndex = -1;                         // -1 until a shot list chooses its first phase.
    int appliedStylePhaseIndex = -1;                    // Phase index whose stylePath last updated the live scene look.
    char appliedStylePath[DemoPhase::STYLE_PATH_BYTES] =
        {};                                             // Exact applied path; same-phase author edits can request a new look.
    int appliedStyleCount = 0;                          // Successful phase-entry style applications for automation proof.
    int appliedRevealRatePhaseIndex = -1;               // Phase index whose revealRate last updated replay presentation pacing.
    float appliedRevealRate = 1.0f;                     // Normalized runtime rate applied from the active phase.
    int appliedRevealRateCount = 0;                     // Successful phase-entry reveal-rate applications for automation proof.
    float phaseElapsedSeconds = 0.0f;                   // Seconds spent in currentPhaseIndex.
    float blendElapsedSeconds = 0.0f;                   // Seconds spent blending from blendStartPose.
    DemoCameraPose blendStartPose;                      // Pose captured when a phase/release blend starts.
    bool grabbed = false;                               // True while the operator temporarily owns free-fly framing.
    DemoCameraPose poseCapturedAtGrab;                  // Authored pose used to seed no-pop free-fly grab.
};

const char* PhaseAdvanceName( PhaseAdvance advance );
bool TryParsePhaseAdvance( const char* text, PhaseAdvance& outAdvance );

// Loads into outShotList only after the full document is valid.
bool LoadDemoShotList( const char* path, DemoShotList& outShotList );
// Writes the stable `.shot.json` schema and creates the parent folder if needed.
bool SaveDemoShotList( const char* path, const DemoShotList& shotList );

} // namespace Runtime
} // namespace SkullbonezCore
