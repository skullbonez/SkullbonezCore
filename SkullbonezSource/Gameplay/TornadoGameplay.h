/*
File: SkullbonezSource/Gameplay/TornadoGameplay.h
Purpose:
  Owns tornado gameplay state, body effects, production visuals, and debug lines.

Summary:
  Tornado gameplay advances authored vortex content, publishes one bounded
  external-force frame to Physics, and registers its production visual through
  Rendering's synchronous world-extension seam. Physics receives only force
  values; Rendering receives only a typed frame callback and packed debug lines.

Glossary:
  Capture timer: Per-body time spent inside an active tornado before an eject
    slot is allowed to fire.
  Eject cooldown: Per-body delay after an ejection impulse before another eject
    impulse can fire.
  Force frame: Ordered cylindrical field values plus per-body timer spans
    borrowed by Physics for exactly one fixed tick.
  Visual registration: One frame-scoped callback that appends the production
    art pass without exposing gameplay ownership to RuntimeRenderer.

Invariants:
  - Active fields retain authored/source order; Physics must accumulate them
    left-to-right without sorting or reduction.
  - Capture and cooldown arrays are dense body rows and are sized before the
    frame borrow is published.
  - At most 64 active fields cross one fixed step; the storage never grows in
    steady gameplay.
  - Visual and debug publications retain no renderer, scene, or replay owner.

Related:
  - SkullbonezSource/Gameplay/TornadoGameplay.cpp
  - SkullbonezSource/Physics/Stages/ExternalForceStage.h
  - SkullbonezSource/Gameplay/TornadoVisualPass.h
*/
#pragma once

#include "TornadoField.h"
#include "TornadoVisualPass.h"
#include "../Physics/Stages/ExternalForceStage.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Gameplay
{
struct TornadoGameplayReplayState
{
    TornadoFieldConfig field;
    TornadoSystemConfig system;
    float systemElapsedSeconds = 0.0f;
    std::vector<float> captureSeconds;
    std::vector<float> ejectCooldownSeconds;
};

class TornadoGameplay
{
  public:
    static constexpr std::size_t MAX_ACTIVE_FORCE_FIELDS = MAX_TORNADO_ACTIVE_FORCE_FIELDS;

    TornadoGameplay();

    // Selects serial or worker-partitioned body-row evaluation. Physics receives
    // only this choice in the resulting per-frame value packet.
    void SetParallelForceEvaluation( bool enabled );
    bool ParallelForceEvaluation() const;
    void ReserveBodyCapacity( int capacity );
    void ReserveVisualCapacity();
    void Clear();

    void SetFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetFieldConfig() const;
    void SetSystemConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetSystemConfig() const;
    float GetSystemElapsedSeconds() const;

    // Applies operator-facing content edits directly to owner storage. These
    // calls never copy or grow the authored vortex vector during input frames.
    bool ToggleEnabled();
    void ToggleFieldVectors();
    void ToggleVisualEnabled();
    void SetFieldRadius( float value );
    void SetFieldHeight( float value );
    void SetFieldInwardAcceleration( float value );
    void SetFieldSwirlAcceleration( float value );
    void SetFieldLiftAcceleration( float value );
    void SetReplayState( const std::vector<float>& captureSeconds, const std::vector<float>& ejectCooldownSeconds,
                         const TornadoFieldConfig& fieldConfig, const TornadoSystemConfig& systemConfig,
                         float systemElapsedSeconds );

    const std::vector<float>& CaptureSeconds() const;
    const std::vector<float>& EjectCooldownSeconds() const;

    const TornadoVisualSettings& VisualSettings() const;
    void SetVisualSettings( const TornadoVisualSettings& settings );
    bool VisualAutoEnableWithTornado() const;
    void SetVisualEnabled( bool enabled );
    Rendering::WorldRenderExtensionRegistration PrepareVisualFrame( const TornadoVisualTimeCandidates& time );
    void ReleaseVisualResources();

    // Lifetime: returned float rows alias owner scratch until the next call.
    // Each line vertex is position.xyz + color.rgb and is consumed by the
    // current render frame before gameplay advances again.
    std::span<const float> BuildDebugLineVertices();

    // Lifetime: returned spans alias this owner until the next mutation. The
    // fixed-step composition edge must pass them directly into Physics::Step.
    Physics::ExternalForceFrameInput BuildForceFrame( float dt, int bodyCount );

    uint64_t CollectMemoryBytes() const;
    uint64_t CollectDebugMemoryBytes() const;

  private:
    void EnsureStateBuffers( int modelCount );
    void AppendForceField( const TornadoFieldConfig& config );

    TornadoField m_field;
    TornadoSystem m_system;
    std::vector<float> m_captureSeconds;
    std::vector<float> m_ejectCooldownSeconds;
    TornadoVisualPass m_visualPass;
    std::vector<float> m_debugLineVertices;
    std::vector<TornadoActiveVortex> m_debugVortices;
    std::array<Physics::ExternalCylindricalForceField, MAX_ACTIVE_FORCE_FIELDS> m_forceFields {};
    std::size_t m_forceFieldCount = 0u;
    bool m_parallelForceEvaluation = false;

    void SetFieldValue( float TornadoFieldConfig::* field, float value );
};
} // namespace Gameplay
} // namespace SkullbonezCore
