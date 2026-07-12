/*
File: RenderDefaultsStore.h
Purpose:
  Owns the startup cinematic baseline and deferred render-default persistence requests.

Summary:
  The process-start cinematic baseline is captured once for later scene resets.
  UI submits only which defaults family to save; at the named end-of-input
  checkpoint, the store samples final live values and rewrites engine.cfg.

Glossary:
  Ordinary defaults: Non-cinematic lighting, shadow, water, and material values.
  Cinematic defaults: Sky, exposure, cloud, and post-processing values.
  Cinematic baseline: Process-start engine.cfg value restored before applying scene style overrides.
  Frame checkpoint: Point after all UI mutations where persisted values are sampled.

Invariants:
  - Requests never snapshot config at submission time.
  - Scene/style code receives the immutable process-start baseline, not mutable shell state.
  - Successful saves alone appear in the returned accepted-event batch.
  - Capacity exhaustion is fatal; steady runtime never grows this store.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "../Core/Config.h"
#include "../Core/SbResult.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Basics
{
constexpr int RENDER_DEFAULTS_REQUEST_CAPACITY = 16;

enum class RenderDefaultsRequestType
{
    Ordinary,
    Cinematic,
};

struct RenderDefaultsSaveBatchResult
{
    SbResult status = SbResult::Success();
    RenderDefaultsRequestType saved[RENDER_DEFAULTS_REQUEST_CAPACITY];
    std::size_t savedCount = 0;
    std::size_t failedCount = 0;
};

class RenderDefaultsStore
{
  public:
    void CaptureStartupCinematicBaseline( const CinematicRenderConfig& cinematic );
    const CinematicRenderConfig& CinematicBaseline() const;
    void SubmitOrdinarySave();
    void SubmitCinematicSave();
    RenderDefaultsSaveBatchResult DrainAtFrameCheckpoint( const OrdinaryRenderConfig& ordinary,
                                                          const CinematicRenderConfig& cinematic );
    std::size_t PendingCount() const;
    RenderDefaultsRequestType PendingTypeAt( std::size_t index ) const;

  private:
    void Submit( RenderDefaultsRequestType type );

    RenderDefaultsRequestType m_requests[RENDER_DEFAULTS_REQUEST_CAPACITY]; // Fixed persistence-intent ring.
    int m_head = 0;                                                         // Oldest save request.
    int m_count = 0;                                                        // Occupied request slots.
    CinematicRenderConfig m_cinematicBaseline;                              // Process-start style restored by scene resets.
};
} // namespace Basics
} // namespace SkullbonezCore
