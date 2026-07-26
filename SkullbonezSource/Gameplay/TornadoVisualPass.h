/*
File: SkullbonezSource/Gameplay/TornadoVisualPass.h
Purpose:
  Owns tornado production-art settings, transient geometry, and graph callback.

Summary:
  Gameplay selects the visual clock from value-only live/replay candidates,
  expands active vortices into ribbons and dust, and registers one synchronous
  pass through Rendering's content-neutral world-extension seam.

Glossary:
  Visual candidates: Scalar clocks projected by higher composition; no replay
    owner or retained sample crosses this boundary.
  Live visual clock: Presentation-only clock that pauses while replay holds live
    advancement.
  Extension registration: Typed callback borrow consumed in the current frame.

Invariants:
  - The graph callback appends exactly one pass at the renderer-selected slot.
  - Raster state, timing label, vertex layout, and draw order match the existing
    production visual contract.
  - Frame pointers borrow TornadoGameplay-owned configuration only until the
    synchronous registration completes.

Related:
  - SkullbonezSource/Gameplay/TornadoGameplay.h
  - SkullbonezSource/Rendering/WorldRenderExtension.h
*/
#pragma once

#include "TornadoField.h"
#include "../Rendering/WorldRenderExtension.h"

#include <vector>

namespace SkullbonezCore::Gameplay
{
struct TornadoVisualSettings
{
    bool enabled = true;
    bool autoEnableWithTornado = true;
    float shellAlpha = 0.14f;
    float dustAlpha = 0.20f;
    float ribbonWidth = 5.5f;
    int ribbonCount = 7;
    int ribbonSegments = 48;
    int particleCount = 96;
    float rotationSpeed = 1.25f;
};

struct TornadoVisualTimeCandidates
{
    double simulationSourceSeconds = 0.0;
    double presentationSeconds = 0.0;
    double solverSeconds = 0.0;
    double predictionSeconds = 0.0;
    float solverSystemSeconds = 0.0f;
    float predictionSystemSeconds = 0.0f;
    bool hasPresentation = false;
    bool hasSolver = false;
    bool hasPrediction = false;
    bool liveAdvanceHeld = false;
};

class TornadoVisualPass
{
  public:
    TornadoVisualPass();
    void ReserveCapacity();

    const TornadoVisualSettings& Settings() const;
    void SetSettings( const TornadoVisualSettings& settings );
    void SetEnabled( bool enabled );
    bool AutoEnableWithTornado() const;
    uint64_t DynamicMemoryBytes() const;

    Rendering::WorldRenderExtensionRegistration PrepareFrame( const TornadoFieldConfig& field,
                                                              const TornadoSystemConfig& system,
                                                              float systemElapsedSeconds,
                                                              const TornadoVisualTimeCandidates& time );
    void ReleaseResources();

  private:
    static constexpr int MAX_VISUAL_RIBBONS = 16;
    static constexpr int MAX_VISUAL_RIBBON_SEGMENTS = 96;
    static constexpr int MAX_VISUAL_PARTICLES = 256;
    static constexpr int MAX_VISUAL_DUST_BANDS = 3;
    static constexpr int MAX_VISUAL_DUST_SEGMENTS = 56;
    static constexpr std::size_t VISUAL_FLOATS_PER_VERTEX = 11u;
    static constexpr std::size_t MAX_VISUAL_VERTEX_COUNT = MAX_TORNADO_ACTIVE_FORCE_FIELDS *
                                                           ( MAX_VISUAL_RIBBONS * MAX_VISUAL_RIBBON_SEGMENTS * 6 +
                                                             MAX_VISUAL_DUST_BANDS * MAX_VISUAL_DUST_SEGMENTS * 6 +
                                                             MAX_VISUAL_PARTICLES * 6 );
    static constexpr std::size_t MAX_VISUAL_FLOAT_CAPACITY = MAX_VISUAL_VERTEX_COUNT * VISUAL_FLOATS_PER_VERTEX;

    struct FrameSnapshot
    {
        const TornadoFieldConfig* field = nullptr;
        const TornadoSystemConfig* system = nullptr;
        TornadoVisualTimeCandidates time;
        float systemElapsedSeconds = 0.0f;
    };

    struct GraphCallbackData
    {
        TornadoVisualPass* pass = nullptr;
        const Rendering::WorldRenderExtensionFrameView* frame = nullptr;
        bool rendered = false;
    };

    static bool RegisterGraphPass( TornadoVisualPass& pass, Rendering::WorldRenderExtensionScope& scope );
    static void ExecuteGraphPass( const Rendering::RenderGraphPassContext& context, GraphCallbackData& data );
    void EnsureTransientCapacity();
    bool Render( const Rendering::WorldRenderExtensionFrameView& frame );

    TornadoVisualSettings m_settings;
    FrameSnapshot m_frame;
    std::vector<float> m_vertices;
    std::vector<TornadoActiveVortex> m_activeVisualVortices;
    float m_liveVisualTimeSeconds = 0.0f;
    double m_lastLiveVisualSourceSeconds = 0.0;
    bool m_hasLiveVisualTime = false;
};
} // namespace SkullbonezCore::Gameplay
