/*
File: ContinuousOrbitalStability.h
Purpose:
  Declares Planning-owned continuous-orbit health and stability analysis.

Summary:
  The analyzer consumes complete detached tick values, applies the authored
  primary/core/auxiliary policy, latches first failures, and retains conservation
  diagnostics while the lower prediction producer remains free to advance.

Glossary:
  Numerical health: Finite, complete, sequential private-step publication for
    every configured member; failure is globally blocking.
  Sustained escape: Positive softened primary-relative energy, outward motion,
    and the authored minimum radius on every tick of the grace interval.
  Conservation drift: Informational normalized change from the seed snapshot;
    unavailable denominators never become instability.

Invariants:
  - The first blocking and first auxiliary failures are immutable after latch.
  - Numerical health covers primary, core, and auxiliary members equally.
  - Only pairs wholly inside primary-plus-core block on collision.
  - Finite conservation drift is diagnostic-only; an unrepresentable
    calculation is a globally blocking numerical failure.

Related:
  - SkullbonezSource/Scene/OrbitalStabilityContract.h
  - SkullbonezSource/Runtime/Prediction/ContinuousPredictionProducer.h
  - SkullbonezTests/TestContinuousOrbitalStability.cpp
*/
#pragma once

#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Scene/OrbitalStabilityContract.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <span>

namespace SkullbonezCore::Runtime
{
enum class ContinuousOrbitalInstabilityCause : std::uint8_t
{
    None = 0,
    InvalidContract,
    NonFiniteState,
    PrivateStepFailure,
    InvalidPublication,
    InnerEnvelope,
    OuterEnvelope,
    SustainedEscape,
    Collision,
};

struct ContinuousOrbitalBodySample
{
    Physics::PhysicsSceneObjectId sceneObjectId;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    double mass = 0.0;
};

struct ContinuousOrbitalContactSample
{
    Physics::PhysicsSceneObjectId bodyA;
    Physics::PhysicsSceneObjectId bodyB;
};

struct ContinuousOrbitalTickInput
{
    std::span<const ContinuousOrbitalBodySample> bodies;
    std::span<const ContinuousOrbitalContactSample> contacts;
    std::uint64_t absoluteTick = 0u;
    bool privateStepSucceeded = true;
    bool publicationValid = true;
};

struct ContinuousOrbitalFailure
{
    ContinuousOrbitalInstabilityCause cause = ContinuousOrbitalInstabilityCause::None;
    Physics::PhysicsSceneObjectId subject;
    Physics::PhysicsSceneObjectId other;
    std::uint64_t absoluteTick = 0u;
    double simulatedSeconds = 0.0;
    bool latched = false;
};

struct ContinuousOrbitalConservationView
{
    double energyDrift = 0.0;
    double angularMomentumDrift = 0.0;
    double maximumAbsoluteEnergyDrift = 0.0;
    double maximumAngularMomentumDrift = 0.0;
    bool energyDriftAvailable = false;
    bool angularMomentumDriftAvailable = false;
};

struct ContinuousOrbitalStabilityView
{
    ContinuousOrbitalFailure firstBlockingFailure;
    ContinuousOrbitalFailure firstAuxiliaryFailure;
    ContinuousOrbitalConservationView conservation;
    std::uint64_t observedThroughTick = 0u;
    double observedThroughSeconds = 0.0;
    bool configured = false;
    bool numericalHealthy = false;
    bool systemOrbitalHealthy = false;
    bool auxiliaryOrbitalHealthy = false;
};

class ContinuousOrbitalStabilityAnalyzer
{
  public:

    // Seeds one authored contract from a complete tick-zero publication. A
    // failed Begin must be followed by another Begin or Reset before observing.
    bool Begin( const Scene::OrbitalStabilityContract& contract, double gravitationalConstant, double softeningLength,
                std::span<const ContinuousOrbitalBodySample> seedBodies ) noexcept;

    // Accepts only the next complete absolute tick. Failures latch evidence but
    // valid later ticks continue to advance diagnostics and auxiliary status.
    bool ObserveTick( const ContinuousOrbitalTickInput& input ) noexcept;
    void Reset() noexcept;
    ContinuousOrbitalStabilityView View() const noexcept;

  private:
    struct ConservationMeasurement
    {
        double energy = 0.0;
        double angularX = 0.0;
        double angularY = 0.0;
        double angularZ = 0.0;
        bool finite = false;
    };

    const ContinuousOrbitalBodySample* FindBody( std::span<const ContinuousOrbitalBodySample> bodies,
                                                 Physics::PhysicsSceneObjectId id ) const noexcept;
    const Scene::OrbitalStabilityMemberContract* FindMember( Physics::PhysicsSceneObjectId id ) const noexcept;
    bool ValidateContract() const noexcept;
    bool ValidateBodies( std::span<const ContinuousOrbitalBodySample> bodies ) const noexcept;
    bool BodiesFinite( std::span<const ContinuousOrbitalBodySample> bodies ) const noexcept;
    ConservationMeasurement MeasureConservation( std::span<const ContinuousOrbitalBodySample> bodies ) const noexcept;
    bool UpdateConservation( const ConservationMeasurement& measurement ) noexcept;
    void LatchBlocking( ContinuousOrbitalInstabilityCause cause, std::uint64_t tick,
                        Physics::PhysicsSceneObjectId subject = {}, Physics::PhysicsSceneObjectId other = {} ) noexcept;
    void LatchAuxiliary( ContinuousOrbitalInstabilityCause cause, std::uint64_t tick,
                         Physics::PhysicsSceneObjectId subject = {}, Physics::PhysicsSceneObjectId other = {} ) noexcept;
    void EvaluateContacts( std::span<const ContinuousOrbitalContactSample> contacts, std::uint64_t tick ) noexcept;
    void EvaluateOrbiters( std::span<const ContinuousOrbitalBodySample> bodies, std::uint64_t tick ) noexcept;

    mutable std::mutex m_mutex;
    Scene::OrbitalStabilityContract m_contract;
    std::array<std::uint32_t, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> m_escapeConsecutiveTicks = {};
    ContinuousOrbitalStabilityView m_view;
    ConservationMeasurement m_seedConservation;
    double m_gravitationalConstant = 0.0;
    double m_softeningLength = 0.0;
    std::uint32_t m_escapeGraceTicks = 0u;
    bool m_begun = false;
};
} // namespace SkullbonezCore::Runtime
