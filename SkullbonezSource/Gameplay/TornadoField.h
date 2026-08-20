/*
File: SkullbonezSource/Gameplay/TornadoField.h
Purpose:
  Owns tornado configuration and deterministic active-vortex evolution.

Summary:
  Gameplay retains tornado values projected by Runtime, evolves their active
  strengths and positions, and exposes sampling used by presentation. The
  fixed-step Physics boundary is published separately by TornadoGameplay.

Invariants:
  - Physics-visible behavior must remain deterministic; the direct force
    witness and byte-exact baselines are the validation contract.
  - Active vortices retain source order, including after inactive rows are
    omitted; no sort or unordered reduction is permitted.
  - Tornado vector visualization samples this math from runtime-side render
    passes; physics never submits draw commands.

Related:
  - SkullbonezSource/Gameplay/TornadoField.cpp
  - SkullbonezSource/Physics/Stages/ExternalForceStage.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include <cstddef>
#include <vector>
#include "../Maths/Vector3.h"


namespace SkullbonezCore
{
namespace Gameplay
{
// Invariant: authored content is rejected before mutation when it exceeds this
// fixed gameplay budget; an admitted scene retains every authored field.
// Runtime allocation policy: steady gameplay never grows or truncates the
// fixed field set.
inline constexpr std::size_t MAX_TORNADO_ACTIVE_FORCE_FIELDS = 64u;

struct TornadoFieldConfig
{
    bool enabled = false;
    bool visualizeVelocityField = false;

    // Units: center/radius/height use metres; acceleration terms use m/s^2;
    // exposure/cooldown use seconds; maxDeltaVelocity uses m/s per fixed step.
    Math::Vector::Vector3 center = Math::Vector::Vector3( 620.0f, 25.0f, 615.0f );
    float radius = 210.0f;
    float height = 140.0f;
    float inwardAcceleration = 150.0f;
    float swirlAcceleration = 185.0f;
    float liftAcceleration = 64.0f;
    float ejectAcceleration = 260.0f;
    float ejectUpAcceleration = 70.0f;
    float ejectBand = 0.96f;
    float minCaptureSeconds = 2.50f;
    float ejectCooldownSeconds = 3.50f;
    float maxDeltaVelocity = 24.0f;
};

struct TornadoVortexConfig
{
    TornadoFieldConfig field;

    // Units: lifecycle values use seconds, drift phase uses radians, drift
    // speed uses radians/second, radii use metres, and repulsion is a scalar.
    float spawnSeconds = 0.0f;
    float timeToLiveSeconds = 0.0f;
    float growSeconds = 2.0f;
    float shrinkSeconds = 2.0f;
    float driftRadius = 0.0f;
    float driftSpeed = 0.0f;
    float driftPhase = 0.0f;
    float repulsionRadius = 0.0f;
    float repulsionStrength = 0.0f;
};

struct TornadoSystemConfig
{
    bool enabled = false;
    bool visualizeVelocityField = false;

    // Lifetime: this authored vector is populated during cold scene/replay
    // setup and is reserved before steady gameplay begins.
    std::vector<TornadoVortexConfig> vortices;
};

struct TornadoActiveVortex
{
    TornadoFieldConfig field;
    float strength = 0.0f;
    float ageSeconds = 0.0f;
    int sourceIndex = -1;
};

class TornadoField
{
  public:
    void SetConfig( const TornadoFieldConfig& config );
    bool ToggleEnabled();
    void ToggleVelocityFieldVisualization();
    void SetFieldValue( float TornadoFieldConfig::* field, float value );
    const TornadoFieldConfig& GetConfig() const
    {
        return m_config;
    }

    static Math::Vector::Vector3 SampleAccelerationForConfig( const TornadoFieldConfig& config,
                                                              const Math::Vector::Vector3& position );
    std::size_t DynamicMemoryBytes() const;

  private:
    TornadoFieldConfig m_config;
};

class TornadoSystem
{
  public:
    TornadoSystem();
    void SetConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetConfig() const
    {
        return m_config;
    }
    bool IsEnabled() const;
    bool HasAuthoredVortices() const;
    bool ToggleEnabled();
    void ToggleVelocityFieldVisualization();
    void SetFieldValue( float TornadoFieldConfig::* field, float value );
    void ResetElapsedSeconds();
    void SetElapsedSeconds( float seconds );
    float GetElapsedSeconds() const
    {
        return m_elapsedSeconds;
    }
    void Tick( float dt );
    const std::vector<TornadoActiveVortex>& ActiveVortices() const
    {
        return m_activeVortices;
    }
    std::size_t DynamicMemoryBytes() const;

    static void BuildActiveVortices( const TornadoSystemConfig& config, float elapsedSeconds,
                                     std::vector<TornadoActiveVortex>& outVortices );

  private:
    TornadoSystemConfig m_config;
    float m_elapsedSeconds = 0.0f;
    std::vector<TornadoActiveVortex> m_activeVortices;

    void RebuildActiveVortices();
};
} // namespace Gameplay
} // namespace SkullbonezCore
