/*
File: ContinuousOrbitalStability.cpp
Purpose:
  Implements Planning-owned continuous-orbit stability policy.

Summary:
  Each complete tick is validated before orbital checks. The analyzer preserves
  first-failure evidence, tracks sustained escape per member, and updates exact
  authored softened-energy and primary-relative angular-momentum diagnostics.

Invariants:
  - Invalid or incomplete tick publication is globally blocking.
  - Auxiliary orbital failures never mutate the system-wide blocking latch.
  - A blocking latch does not prevent later diagnostic observations.

Related:
  - ContinuousOrbitalStability.h
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
*/
#include "ContinuousOrbitalStability.h"

#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SkullbonezCore::Runtime
{
namespace
{
bool FiniteVector( const Math::Vector::Vector3& value ) noexcept
{
    return std::isfinite( value.x ) && std::isfinite( value.y ) && std::isfinite( value.z );
}

bool FiniteQuaternion( const Math::Orientation::Quaternion& value ) noexcept
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    value.GetComponents( x, y, z, w );
    return std::isfinite( x ) && std::isfinite( y ) && std::isfinite( z ) && std::isfinite( w );
}

double DotDouble( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) noexcept
{
    return static_cast<double>( a.x ) * b.x + static_cast<double>( a.y ) * b.y + static_cast<double>( a.z ) * b.z;
}

double Length( const Math::Vector::Vector3& value ) noexcept
{
    return std::sqrt( DotDouble( value, value ) );
}

bool SameId( Physics::PhysicsSceneObjectId a, Physics::PhysicsSceneObjectId b ) noexcept
{
    return a.value == b.value;
}

bool IsBlockingRole( Scene::OrbitalStabilityMemberRole role ) noexcept
{
    return role == Scene::OrbitalStabilityMemberRole::Primary || role == Scene::OrbitalStabilityMemberRole::CoreOrbiter;
}
} // namespace

bool ContinuousOrbitalStabilityAnalyzer::Begin( const Scene::OrbitalStabilityContract& contract,
                                                double gravitationalConstant, double softeningLength,
                                                std::span<const ContinuousOrbitalBodySample> seedBodies ) noexcept
{
    std::lock_guard lock( m_mutex );
    m_contract = contract;
    m_gravitationalConstant = gravitationalConstant;
    m_softeningLength = softeningLength;
    m_escapeConsecutiveTicks.fill( 0u );
    m_view = {};
    m_seedConservation = {};
    m_escapeGraceTicks = 0u;
    m_begun = false;
    m_view.configured = contract.enabled;

    if ( !ValidateContract() || !std::isfinite( gravitationalConstant ) || gravitationalConstant <= 0.0 ||
         !std::isfinite( softeningLength ) || softeningLength <= 0.0 )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::InvalidContract, 0u );
        return false;
    }

    const double graceTicks = contract.escapeGraceSeconds / static_cast<double>( PHYSICS_FIXED_DT );
    const double roundedGraceTicks = std::round( graceTicks );

    // Invariant: grace is authored in seconds but stability is decided only on
    // complete fixed ticks. Reject a duration that cannot map to one tick count.
    if ( !std::isfinite( graceTicks ) || roundedGraceTicks < 1.0 ||
         roundedGraceTicks > static_cast<double>( ( std::numeric_limits<std::uint32_t>::max )() ) ||
         std::abs( graceTicks - roundedGraceTicks ) > 1.0e-3 )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::InvalidContract, 0u );
        return false;
    }

    m_escapeGraceTicks = static_cast<std::uint32_t>( roundedGraceTicks );

    if ( !ValidateBodies( seedBodies ) )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::InvalidPublication, 0u );
        return false;
    }

    if ( !BodiesFinite( seedBodies ) )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::NonFiniteState, 0u );
        return false;
    }

    m_seedConservation = MeasureConservation( seedBodies );

    if ( !m_seedConservation.finite )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::NonFiniteState, 0u );
        return false;
    }

    const double seedAngularLength = std::hypot( m_seedConservation.angularX, m_seedConservation.angularY,
                                                 m_seedConservation.angularZ );

    if ( !std::isfinite( seedAngularLength ) )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::NonFiniteState, 0u );
        return false;
    }

    m_view.conservation.energyDriftAvailable = std::abs( m_seedConservation.energy ) > 0.0;
    m_view.conservation.angularMomentumDriftAvailable = seedAngularLength > 0.0;
    m_view.numericalHealthy = true;
    m_view.systemOrbitalHealthy = true;
    m_view.auxiliaryOrbitalHealthy = true;
    m_begun = true;
    return true;
}

bool ContinuousOrbitalStabilityAnalyzer::ObserveTick( const ContinuousOrbitalTickInput& input ) noexcept
{
    std::lock_guard lock( m_mutex );

    if ( !m_begun )
    {
        return false;
    }

    if ( input.absoluteTick == 0u || input.absoluteTick != m_view.observedThroughTick + 1u )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::InvalidPublication, input.absoluteTick );
        return false;
    }

    if ( !input.publicationValid )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::InvalidPublication, input.absoluteTick );
        return false;
    }

    if ( !input.privateStepSucceeded )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::PrivateStepFailure, input.absoluteTick );
    }

    if ( !ValidateBodies( input.bodies ) )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::InvalidPublication, input.absoluteTick );
        return false;
    }

    if ( !BodiesFinite( input.bodies ) )
    {
        m_view.observedThroughTick = input.absoluteTick;
        m_view.observedThroughSeconds = static_cast<double>( input.absoluteTick ) * static_cast<double>( PHYSICS_FIXED_DT );
        LatchBlocking( ContinuousOrbitalInstabilityCause::NonFiniteState, input.absoluteTick );
        return false;
    }

    m_view.observedThroughTick = input.absoluteTick;
    m_view.observedThroughSeconds = static_cast<double>( input.absoluteTick ) * static_cast<double>( PHYSICS_FIXED_DT );
    const ConservationMeasurement conservation = MeasureConservation( input.bodies );

    if ( !conservation.finite )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::NonFiniteState, input.absoluteTick );
        return false;
    }

    if ( !UpdateConservation( conservation ) )
    {
        LatchBlocking( ContinuousOrbitalInstabilityCause::NonFiniteState, input.absoluteTick );
        return false;
    }

    EvaluateContacts( input.contacts, input.absoluteTick );
    EvaluateOrbiters( input.bodies, input.absoluteTick );
    return true;
}

void ContinuousOrbitalStabilityAnalyzer::Reset() noexcept
{
    std::lock_guard lock( m_mutex );
    m_contract = {};
    m_escapeConsecutiveTicks.fill( 0u );
    m_view = {};
    m_seedConservation = {};
    m_gravitationalConstant = 0.0;
    m_softeningLength = 0.0;
    m_escapeGraceTicks = 0u;
    m_begun = false;
}

ContinuousOrbitalStabilityView ContinuousOrbitalStabilityAnalyzer::View() const noexcept
{
    std::lock_guard lock( m_mutex );
    return m_view;
}

const ContinuousOrbitalBodySample*
ContinuousOrbitalStabilityAnalyzer::FindBody( std::span<const ContinuousOrbitalBodySample> bodies,
                                              Physics::PhysicsSceneObjectId id ) const noexcept
{
    for ( const ContinuousOrbitalBodySample& body : bodies )
    {
        if ( SameId( body.sceneObjectId, id ) )
        {
            return &body;
        }
    }

    return nullptr;
}

const Scene::OrbitalStabilityMemberContract*
ContinuousOrbitalStabilityAnalyzer::FindMember( Physics::PhysicsSceneObjectId id ) const noexcept
{
    for ( std::size_t index = 0; index < m_contract.memberCount; ++index )
    {
        if ( SameId( m_contract.members[index].sceneObjectId, id ) )
        {
            return &m_contract.members[index];
        }
    }

    return nullptr;
}

bool ContinuousOrbitalStabilityAnalyzer::ValidateContract() const noexcept
{
    if ( !m_contract.enabled || m_contract.memberCount < 2u ||
         m_contract.memberCount > Scene::ORBITAL_STABILITY_MEMBER_CAPACITY ||
         !std::isfinite( m_contract.escapeGraceSeconds ) || m_contract.escapeGraceSeconds <= 0.0 )
    {
        return false;
    }

    std::size_t primaryCount = 0u;
    std::size_t coreCount = 0u;

    for ( std::size_t index = 0; index < m_contract.memberCount; ++index )
    {
        const Scene::OrbitalStabilityMemberContract& member = m_contract.members[index];

        if ( !member.sceneObjectId.IsValid() || ( member.role != Scene::OrbitalStabilityMemberRole::Primary &&
                                                  member.role != Scene::OrbitalStabilityMemberRole::CoreOrbiter &&
                                                  member.role != Scene::OrbitalStabilityMemberRole::Auxiliary ) )
        {
            return false;
        }

        for ( std::size_t previous = 0; previous < index; ++previous )
        {
            if ( SameId( member.sceneObjectId, m_contract.members[previous].sceneObjectId ) )
            {
                return false;
            }
        }

        if ( member.role == Scene::OrbitalStabilityMemberRole::Primary )
        {
            ++primaryCount;
            continue;
        }

        coreCount += member.role == Scene::OrbitalStabilityMemberRole::CoreOrbiter ? 1u : 0u;

        if ( !std::isfinite( member.innerRadius ) || !std::isfinite( member.outerRadius ) ||
             !std::isfinite( member.escapeStartRadius ) || member.innerRadius <= 0.0 ||
             member.outerRadius <= member.innerRadius || member.escapeStartRadius <= 0.0 )
        {
            return false;
        }
    }

    return primaryCount == 1u && coreCount > 0u;
}

bool ContinuousOrbitalStabilityAnalyzer::ValidateBodies( std::span<const ContinuousOrbitalBodySample> bodies ) const noexcept
{
    for ( std::size_t memberIndex = 0; memberIndex < m_contract.memberCount; ++memberIndex )
    {
        const Physics::PhysicsSceneObjectId id = m_contract.members[memberIndex].sceneObjectId;
        const ContinuousOrbitalBodySample* match = nullptr;

        for ( const ContinuousOrbitalBodySample& body : bodies )
        {
            if ( SameId( body.sceneObjectId, id ) )
            {
                if ( match )
                {
                    return false;
                }

                match = &body;
            }
        }

        if ( !match )
        {
            return false;
        }
    }

    return true;
}

bool ContinuousOrbitalStabilityAnalyzer::BodiesFinite( std::span<const ContinuousOrbitalBodySample> bodies ) const noexcept
{
    for ( std::size_t memberIndex = 0; memberIndex < m_contract.memberCount; ++memberIndex )
    {
        const ContinuousOrbitalBodySample* body = FindBody( bodies, m_contract.members[memberIndex].sceneObjectId );

        if ( !body || !FiniteVector( body->position ) || !FiniteQuaternion( body->orientation ) ||
             !FiniteVector( body->linearVelocity ) || !FiniteVector( body->angularVelocity ) ||
             !std::isfinite( body->mass ) || body->mass <= 0.0 )
        {
            return false;
        }
    }

    return true;
}

ContinuousOrbitalStabilityAnalyzer::ConservationMeasurement
ContinuousOrbitalStabilityAnalyzer::MeasureConservation( std::span<const ContinuousOrbitalBodySample> bodies ) const noexcept
{
    // Concept: this diagnostic measures every configured member. Energy uses
    // softened all-pair potential; angular momentum uses primary-relative
    // position and velocity because the authored primary is fixed.
    ConservationMeasurement result;
    const Scene::OrbitalStabilityMemberContract* primaryMember = nullptr;

    for ( std::size_t index = 0; index < m_contract.memberCount; ++index )
    {
        if ( m_contract.members[index].role == Scene::OrbitalStabilityMemberRole::Primary )
        {
            primaryMember = &m_contract.members[index];
            break;
        }
    }

    const ContinuousOrbitalBodySample* primary = primaryMember ? FindBody( bodies, primaryMember->sceneObjectId ) : nullptr;

    if ( !primary )
    {
        return result;
    }

    for ( std::size_t index = 0; index < m_contract.memberCount; ++index )
    {
        const Scene::OrbitalStabilityMemberContract& member = m_contract.members[index];
        const ContinuousOrbitalBodySample* body = FindBody( bodies, member.sceneObjectId );

        if ( !body )
        {
            return result;
        }

        result.energy += 0.5 * body->mass * DotDouble( body->linearVelocity, body->linearVelocity );

        if ( member.role != Scene::OrbitalStabilityMemberRole::Primary )
        {
            const Math::Vector::Vector3 relativePosition = body->position - primary->position;
            const Math::Vector::Vector3 relativeVelocity = body->linearVelocity - primary->linearVelocity;
            result.angularX += body->mass * ( static_cast<double>( relativePosition.y ) * relativeVelocity.z -
                                              static_cast<double>( relativePosition.z ) * relativeVelocity.y );
            result.angularY += body->mass * ( static_cast<double>( relativePosition.z ) * relativeVelocity.x -
                                              static_cast<double>( relativePosition.x ) * relativeVelocity.z );
            result.angularZ += body->mass * ( static_cast<double>( relativePosition.x ) * relativeVelocity.y -
                                              static_cast<double>( relativePosition.y ) * relativeVelocity.x );
        }
    }

    const double softenedSquared = m_softeningLength * m_softeningLength;

    for ( std::size_t a = 0; a < m_contract.memberCount; ++a )
    {
        const ContinuousOrbitalBodySample* bodyA = FindBody( bodies, m_contract.members[a].sceneObjectId );

        for ( std::size_t b = a + 1u; b < m_contract.memberCount; ++b )
        {
            const ContinuousOrbitalBodySample* bodyB = FindBody( bodies, m_contract.members[b].sceneObjectId );
            const Math::Vector::Vector3 delta = bodyB->position - bodyA->position;
            result.energy -= m_gravitationalConstant * bodyA->mass * bodyB->mass /
                             std::sqrt( DotDouble( delta, delta ) + softenedSquared );
        }
    }

    result.finite = std::isfinite( result.energy ) && std::isfinite( result.angularX ) && std::isfinite( result.angularY ) &&
                    std::isfinite( result.angularZ );
    return result;
}

bool ContinuousOrbitalStabilityAnalyzer::UpdateConservation( const ConservationMeasurement& measurement ) noexcept
{
    if ( m_view.conservation.energyDriftAvailable )
    {
        m_view.conservation.energyDrift = ( measurement.energy - m_seedConservation.energy ) /
                                          std::abs( m_seedConservation.energy );
        m_view.conservation.maximumAbsoluteEnergyDrift = (std::max)( m_view.conservation.maximumAbsoluteEnergyDrift,
                                                                     std::abs( m_view.conservation.energyDrift ) );
    }

    if ( m_view.conservation.angularMomentumDriftAvailable )
    {
        const double dx = measurement.angularX - m_seedConservation.angularX;
        const double dy = measurement.angularY - m_seedConservation.angularY;
        const double dz = measurement.angularZ - m_seedConservation.angularZ;
        const double seedLength = std::hypot( m_seedConservation.angularX, m_seedConservation.angularY,
                                              m_seedConservation.angularZ );
        m_view.conservation.angularMomentumDrift = std::hypot( dx, dy, dz ) / seedLength;
        m_view.conservation.maximumAngularMomentumDrift = (std::max)( m_view.conservation.maximumAngularMomentumDrift,
                                                                      m_view.conservation.angularMomentumDrift );
    }

    return std::isfinite( m_view.conservation.energyDrift ) && std::isfinite( m_view.conservation.angularMomentumDrift ) &&
           std::isfinite( m_view.conservation.maximumAbsoluteEnergyDrift ) &&
           std::isfinite( m_view.conservation.maximumAngularMomentumDrift );
}

void ContinuousOrbitalStabilityAnalyzer::LatchBlocking( ContinuousOrbitalInstabilityCause cause, std::uint64_t tick,
                                                        Physics::PhysicsSceneObjectId subject,
                                                        Physics::PhysicsSceneObjectId other ) noexcept
{
    if ( !m_view.firstBlockingFailure.latched )
    {
        m_view.firstBlockingFailure = { cause, subject, other, tick, static_cast<double>( tick ) * static_cast<double>( PHYSICS_FIXED_DT ), true };
    }

    m_view.systemOrbitalHealthy = false;

    if ( cause == ContinuousOrbitalInstabilityCause::InvalidContract ||
         cause == ContinuousOrbitalInstabilityCause::NonFiniteState ||
         cause == ContinuousOrbitalInstabilityCause::PrivateStepFailure ||
         cause == ContinuousOrbitalInstabilityCause::InvalidPublication )
    {
        m_view.numericalHealthy = false;
    }
}

void ContinuousOrbitalStabilityAnalyzer::LatchAuxiliary( ContinuousOrbitalInstabilityCause cause, std::uint64_t tick,
                                                         Physics::PhysicsSceneObjectId subject,
                                                         Physics::PhysicsSceneObjectId other ) noexcept
{
    if ( !m_view.firstAuxiliaryFailure.latched )
    {
        m_view.firstAuxiliaryFailure = { cause, subject, other, tick, static_cast<double>( tick ) * static_cast<double>( PHYSICS_FIXED_DT ), true };
    }

    m_view.auxiliaryOrbitalHealthy = false;
}

void ContinuousOrbitalStabilityAnalyzer::EvaluateContacts( std::span<const ContinuousOrbitalContactSample> contacts,
                                                           std::uint64_t tick ) noexcept
{
    for ( const ContinuousOrbitalContactSample& contact : contacts )
    {
        const Scene::OrbitalStabilityMemberContract* a = FindMember( contact.bodyA );
        const Scene::OrbitalStabilityMemberContract* b = FindMember( contact.bodyB );

        if ( !a || !b || SameId( contact.bodyA, contact.bodyB ) )
        {
            continue;
        }

        if ( IsBlockingRole( a->role ) && IsBlockingRole( b->role ) )
        {
            LatchBlocking( ContinuousOrbitalInstabilityCause::Collision, tick, contact.bodyA, contact.bodyB );
        }
        else if ( a->role == Scene::OrbitalStabilityMemberRole::Auxiliary ||
                  b->role == Scene::OrbitalStabilityMemberRole::Auxiliary )
        {
            const Physics::PhysicsSceneObjectId auxiliary = a->role == Scene::OrbitalStabilityMemberRole::Auxiliary
                                                                ? contact.bodyA
                                                                : contact.bodyB;
            LatchAuxiliary( ContinuousOrbitalInstabilityCause::Collision, tick, auxiliary,
                            SameId( auxiliary, contact.bodyA ) ? contact.bodyB : contact.bodyA );
        }
    }
}

void ContinuousOrbitalStabilityAnalyzer::EvaluateOrbiters( std::span<const ContinuousOrbitalBodySample> bodies,
                                                           std::uint64_t tick ) noexcept
{
    const Scene::OrbitalStabilityMemberContract* primaryMember = nullptr;

    for ( std::size_t index = 0; index < m_contract.memberCount; ++index )
    {
        if ( m_contract.members[index].role == Scene::OrbitalStabilityMemberRole::Primary )
        {
            primaryMember = &m_contract.members[index];
            break;
        }
    }

    const ContinuousOrbitalBodySample* primary = FindBody( bodies, primaryMember->sceneObjectId );
    const double softenedSquared = m_softeningLength * m_softeningLength;

    for ( std::size_t index = 0; index < m_contract.memberCount; ++index )
    {
        const Scene::OrbitalStabilityMemberContract& member = m_contract.members[index];

        if ( member.role == Scene::OrbitalStabilityMemberRole::Primary )
        {
            continue;
        }

        const ContinuousOrbitalBodySample* body = FindBody( bodies, member.sceneObjectId );
        const Math::Vector::Vector3 relativePosition = body->position - primary->position;
        const Math::Vector::Vector3 relativeVelocity = body->linearVelocity - primary->linearVelocity;
        const double radius = Length( relativePosition );
        const bool auxiliary = member.role == Scene::OrbitalStabilityMemberRole::Auxiliary;
        const auto latch = [&]( ContinuousOrbitalInstabilityCause cause )
        {
            if ( auxiliary )
            {
                LatchAuxiliary( cause, tick, member.sceneObjectId, primaryMember->sceneObjectId );
            }
            else
            {
                LatchBlocking( cause, tick, member.sceneObjectId, primaryMember->sceneObjectId );
            }
        };

        if ( radius < member.innerRadius )
        {
            latch( ContinuousOrbitalInstabilityCause::InnerEnvelope );
        }
        else if ( radius > member.outerRadius )
        {
            latch( ContinuousOrbitalInstabilityCause::OuterEnvelope );
        }

        // This is the exact fixed-primary softened law used by PhysicsForceStage.
        const double specificEnergy = 0.5 * DotDouble( relativeVelocity, relativeVelocity ) -
                                      m_gravitationalConstant * primary->mass /
                                          std::sqrt( DotDouble( relativePosition, relativePosition ) + softenedSquared );
        const bool escaping = specificEnergy > 0.0 && DotDouble( relativePosition, relativeVelocity ) > 0.0 &&
                              radius >= member.escapeStartRadius;

        if ( escaping )
        {
            if ( m_escapeConsecutiveTicks[index] < m_escapeGraceTicks )
            {
                ++m_escapeConsecutiveTicks[index];
            }

            if ( m_escapeConsecutiveTicks[index] == m_escapeGraceTicks )
            {
                latch( ContinuousOrbitalInstabilityCause::SustainedEscape );
            }
        }
        else
        {
            m_escapeConsecutiveTicks[index] = 0u;
        }
    }
}
} // namespace SkullbonezCore::Runtime
