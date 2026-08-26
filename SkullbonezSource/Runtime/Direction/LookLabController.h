/*
File: SkullbonezSource/Runtime/Direction/LookLabController.h
Purpose:
  Owns the live Look Lab candidate, save transaction, and bounded status.

Summary:
  LookLabController resolves one deterministic presentation candidate against
  the active scene's non-randomizable geometry, quality, and shadow-participation
  values. App borrows the resulting detached snapshot to SceneController. A
  save retains that exact snapshot and receipt facts until Capture returns its
  post-render token.

Glossary:
  Resolved candidate: Generator output after scene-scale, basin-mask, water,
    shadow-quality, and shadow-participation facts have been copied from the
    active presentation.
  Look Lab status: Fixed-capacity facts describing the latest owner transition;
    UI consumers borrow a detached value and never gain candidate authority.
  Save transaction: One style/receipt publication plus a token-matched Capture
    completion that atomically revises the receipt.

Invariants:
  - Only the private Look Lab generator stream chooses randomized values.
  - Scene-coordinate basin values, resource-quality shadow values, and
    cast/receive participation policy are copied exactly from the active
    presentation before publication.
  - A scene clear discards the candidate so it cannot cross scene lifetimes.
  - Reroll and another save are rejected while one screenshot is pending.
  - The owner retains no Scene, UI, renderer, Capture, or live filesystem-state
    pointer; its one process-lifetime publication port exposes only the cohesive
    three-step bundle transaction.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabController.cpp
  - SkullbonezSource/Runtime/Direction/LookLabGenerator.h
  - SkullbonezSource/Runtime/Scene/SceneController.Style.cpp
*/
#pragma once

#include "LookLabBundleWriter.h"
#include "LookLabGenerator.h"

#include <array>
#include <cstdint>
#include <optional>

namespace SkullbonezCore::Runtime
{
enum class LookLabStatusKind : uint8_t
{
    Idle = 0,
    Resolved,
    Applied,
    BundlePending,
    BundleSaved,
    BundlePartialFailure,
    BundleCancelled,
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
    bool savePending = false;
    std::array<char, 128> detail = {};
    std::array<char, 512> bundleDirectory = {};
};

struct LookLabSaveRequest
{
    // Invariant: these synchronous values describe one bundle revision and are
    // copied into controller-owned bounded facts before this call returns;
    // TestLookLabController.cpp proves the request cannot outlive its borrows.
    const char* lookLabRoot = nullptr;
    const char* localTimestamp = nullptr;
    int utcOffsetMinutes = 0;
    const char* sourceScenePath = nullptr;
    const char* sourceSceneDisplayName = nullptr;
};

struct LookLabSaveStartResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool captureRequested = false;
    uint64_t captureToken = 0;
    std::array<char, 512> screenshotPath = {};
};

// Publication port: the controller owns transaction state while this cohesive
// dependency owns directory/style/receipt filesystem effects. Tests substitute
// it to prove failure transitions without timing-sensitive filesystem races.
class LookLabBundlePublication
{
  public:
    virtual ~LookLabBundlePublication() = default;
    virtual Core::SbResult CreateBundleDirectory( Core::SbDiagnosticStore& diagnostics, const LookLabSaveRequest& request,
                                                  uint64_t seed, LookLabBundlePaths& output ) = 0;
    virtual Core::SbResult SaveStyleAtomic( Core::SbDiagnosticStore& diagnostics,
                                            const Scene::StandaloneStyleSnapshot& snapshot,
                                            const LookLabBundlePaths& paths ) = 0;
    virtual Core::SbResult SaveReceiptAtomic( Core::SbDiagnosticStore& diagnostics, const LookLabReceiptFacts& facts,
                                              const Scene::StandaloneStyleSnapshot& snapshot,
                                              const LookLabBundlePaths& paths ) = 0;
};

// Produces the exact detached value consumed by live application and later
// bundle serialization. This seam is public so deterministic tests can prove
// the retained-value boundary without constructing a renderer or window.
LookLabCandidate ResolveLookLabCandidateForScene( uint64_t seed, const Core::CinematicRenderConfig& activePresentation );
LookLabCandidateIssue ValidateResolvedLookLabCandidate( const LookLabCandidate& candidate,
                                                        const Core::CinematicRenderConfig& activePresentation );
Scene::StandaloneStyleSnapshot BuildLookLabStyleSnapshot( const LookLabCandidate& candidate );
Core::SbResult ValidateLookLabSaveRequest( Core::SbDiagnosticStore& diagnostics, const LookLabSaveRequest& request,
                                           uint64_t seed );

class LookLabController
{
  public:
    LookLabController();
    explicit LookLabController( LookLabBundlePublication& publication );

    // Replaces the current candidate only when scene resolution and final
    // validation both succeed; rejection preserves the prior candidate.
    bool ResolveSeed( uint64_t seed, const Core::CinematicRenderConfig& activePresentation );
    uint64_t NextAuthoringSeed();

    // App calls this only after SceneController accepts the current snapshot.
    void MarkApplied();

    // Publishes style plus the pending receipt before returning a Capture token.
    // Completion or cancellation atomically revises that exact receipt.
    LookLabSaveStartResult BeginSave( Core::SbDiagnosticStore& diagnostics, const LookLabSaveRequest& request );
    Core::SbResult CompleteSaveCapture( Core::SbDiagnosticStore& diagnostics, uint64_t token,
                                        const Core::SbResult& captureResult );
    Core::SbResult CancelPendingSave( Core::SbDiagnosticStore& diagnostics, const char* reason );

    // Cancels the candidate before a scene transition mutates the process
    // presentation; App separately restores its startup cinematic baseline.
    void ClearForSceneTransition();

    bool HasCandidate() const;
    bool HasPendingSave() const;
    uint64_t PendingSaveToken() const;

    // Returns an empty default snapshot when no candidate exists; save/apply
    // callers must gate this operation with HasCandidate().
    Scene::StandaloneStyleSnapshot BuildCurrentSnapshot() const;
    LookLabStatusView Status() const;

  private:

    // Invariant: every retained field describes the same candidate and bundle;
    // TestLookLabController.cpp pins pending, completion, failure, and cancellation.
    struct PendingSave
    {
        Scene::StandaloneStyleSnapshot snapshot;
        LookLabBundlePaths paths;
        LookLabReceiptFacts facts;
        uint64_t token = 0;
    };

    void PublishStatus( LookLabStatusKind kind, const char* detail );
    void PublishBundlePath( const char* path );

    // Lifetime: this is the sole retained live authoring value. Save transaction
    // metadata belongs beside it in this owner rather than in an input context.
    std::optional<LookLabCandidate> m_candidate;
    std::optional<PendingSave> m_pendingSave;

    // Lifetime: production binds the process-lifetime filesystem publisher;
    // focused tests must keep their injected publisher alive with the controller.
    LookLabBundlePublication* m_publication = nullptr;
    LookLabStatusView m_status;
    uint64_t m_authoringSequence = 0;
    uint64_t m_nextSaveToken = 1;
};
} // namespace SkullbonezCore::Runtime
