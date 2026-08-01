/*
File: SkullbonezSource/Runtime/Direction/LookLabController.h
Purpose:
  Owns the current live Look Lab candidate and its bounded operator status.

Summary:
  LookLabController resolves one deterministic presentation candidate against
  the active scene's non-randomizable geometry and quality values. App borrows
  the resulting detached snapshot and hands it to SceneController without a
  scene load or file round-trip.

Glossary:
  Resolved candidate: Generator output after scene-scale, basin-mask, water,
    and shadow-quality facts have been copied from the active presentation.
  Look Lab status: Fixed-capacity facts describing the latest owner transition;
    UI consumers borrow a detached value and never gain candidate authority.

Invariants:
  - Only the private Look Lab generator stream chooses randomized values.
  - Scene-coordinate basin values and resource-quality shadow values are copied
    exactly from the active presentation before publication.
  - A scene clear discards the candidate so it cannot cross scene lifetimes.
  - The owner retains no Scene, UI, renderer, filesystem, or Capture pointer.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabController.cpp
  - SkullbonezSource/Runtime/Direction/LookLabGenerator.h
  - SkullbonezSource/Runtime/Scene/SceneController.Style.cpp
  - Agentic/Reports/2026-08-01/look-lab-random-style-authoring-ll0-census.md
*/
#pragma once

#include "LookLabGenerator.h"
#include "../Scene/SceneLifecycle.h"

#include <array>
#include <cstdint>
#include <optional>

namespace SkullbonezCore::Scene
{
struct StandaloneStyleSnapshot;
}

namespace SkullbonezCore::Runtime
{
enum class LookLabStatusKind : uint8_t
{
    Idle = 0,
    Resolved,
    Applied,
    ClearedForSceneLoad,
    Rejected
};

struct LookLabStatusView
{

    // Invariant: these fields describe one candidate transition and travel as
    // one detached UI value. TestLookLabController pins publication and clearing.
    LookLabStatusKind kind = LookLabStatusKind::Idle;
    uint64_t seed = 0;
    uint64_t fingerprint = 0;
    uint32_t generatorVersion = 0;
    LookLabRecipeFamily recipe = LookLabRecipeFamily::GoldenRealism;
    bool hasCandidate = false;
    std::array<char, 128> detail = {};
};

// Produces the exact detached value consumed by live application and later
// bundle serialization. This seam is public so deterministic tests can prove
// the retained-value boundary without constructing a renderer or window.
LookLabCandidate ResolveLookLabCandidateForScene( uint64_t seed, const Core::CinematicRenderConfig& activePresentation );
LookLabCandidateIssue ValidateResolvedLookLabCandidate( const LookLabCandidate& candidate,
                                                        const Core::CinematicRenderConfig& activePresentation );
Scene::StandaloneStyleSnapshot BuildLookLabStyleSnapshot( const LookLabCandidate& candidate );

class LookLabController
{
  public:

    // Replaces the current candidate only when scene resolution and final
    // validation both succeed; rejection preserves the prior candidate.
    bool ResolveSeed( uint64_t seed, const Core::CinematicRenderConfig& activePresentation );

    // App calls this only after SceneController accepts the current snapshot.
    void MarkApplied();

    // Cancels the candidate before a scene transition mutates the process
    // presentation; App separately restores its startup cinematic baseline.
    void ClearForSceneTransition();

    // Clears scene-local authoring state once per lifecycle generation.
    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet );

    bool HasCandidate() const;
    const LookLabCandidate* CurrentCandidate() const;

    // Returns an empty default snapshot when no candidate exists; save/apply
    // callers must gate this operation with HasCandidate().
    Scene::StandaloneStyleSnapshot BuildCurrentSnapshot() const;
    LookLabStatusView Status() const;

  private:
    void PublishStatus( LookLabStatusKind kind, const char* detail );

    // Lifetime: this is the sole retained live authoring value. Save transaction
    // metadata belongs beside it in this owner rather than in an input context.
    std::optional<LookLabCandidate> m_candidate;
    LookLabStatusView m_status;
    SceneLifecycleGenerationObserver m_sceneClearObserver;
};
} // namespace SkullbonezCore::Runtime
