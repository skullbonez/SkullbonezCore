/*
File: DemoDirector.h
Purpose:
  Defines the authored shot-list data used by the Demo Director.

Mental model:
  A shot list is a compact sequence of operator-authored phases. Each phase
  owns a camera pose, optional render style path, and the rule that advances to
  the next phase. Runtime playback can later blend and time these records
  without inventing camera framing.

Glossary:
  Shot list: Ordered `.shot.json` file that choreographs a deterministic demo.
  Phase: One authored shot: camera pose, style path, and advance rule.
  Camera pose: Eye, view target, and up vector stored in CameraCollection terms.
  Reveal threshold: Normalized prediction reveal progress that can advance a
    phase when the causal visualization reaches an authored point.

Invariants:
  - Phase storage is fixed-capacity so playback never needs runtime growth.
  - Style paths are borrowed text tokens; applying the style is a later phase.
  - Load/save are cold authoring paths and return bool status instead of throwing.

Related:
  - fable_plans/08-demo-director-progress.md
  - SkullbonezSource/Runtime/CameraCollection.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Maths/Vector3.h"

#include <array>
#include <cstddef>

namespace SkullbonezCore
{
namespace Basics
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
    char stylePath[STYLE_PATH_BYTES] = {}; // `SkullbonezData/styles/*.style.json`, empty means keep current look.
    PhaseAdvance advance = PhaseAdvance::Manual;
    float timerSeconds = 4.0f;
    float revealThreshold = 1.0f;          // Normalized 0..1 reveal progress.
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

const char* PhaseAdvanceName( PhaseAdvance advance );
bool TryParsePhaseAdvance( const char* text, PhaseAdvance& outAdvance );

// Loads into outShotList only after the full document is valid.
bool LoadDemoShotList( const char* path, DemoShotList& outShotList );
// Writes the stable `.shot.json` schema and creates the parent folder if needed.
bool SaveDemoShotList( const char* path, const DemoShotList& shotList );

} // namespace Basics
} // namespace SkullbonezCore
