/*
File: SkullbonezSource/Physics/ContactEnergyOracle.h
Purpose:
  Measures complete contact-solve energy and momentum from Physics-owned body state.

Summary:
  ContactEnergyMeasurement folds every dynamic body into one translational and
  rotational kinetic-energy total plus world-space linear and angular momentum.
  Tests compare the values before and after a complete solve instead of treating
  temporarily non-monotonic sequential impulse rows as independent systems.

Glossary:
  Closed solve: Contact solve with gravity, authored work, motors, buoyancy, and
    penetration bias disabled.
  Spin momentum: Angular momentum about a body's center of mass.
  Orbital momentum: Angular momentum contributed by linear motion about the
    selected world origin.
  Separation-work budget: Explicit energy allowance supplied by Baumgarte bias
    to remove penetration.

Invariants:
  - Fixed bodies are external anchors and do not enter dynamic energy/momentum
    totals; dynamic/fixed cases therefore assert energy, not momentum conservation.
  - Non-sphere spin energy and momentum rotate angular velocity into the body-
    principal inertia frame through the same orientation contract used by contact
    impulse response.
  - Accumulation uses double precision, allocates no storage, and does not mutate
    PhysicsBodyStore or solver state.
  - Tolerances scale from float precision and the measured reference magnitude;
    they never absorb restitution, friction, cached impulses, or positional work.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "PhysicsBodyStore.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace SkullbonezCore::Physics
{

struct ContactEnergyVector
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct ContactEnergyMeasurement
{
    double translationalKineticEnergy = 0.0;
    double rotationalKineticEnergy = 0.0;
    ContactEnergyVector linearMomentum;
    ContactEnergyVector angularMomentum;
    ContactEnergyVector linearMomentumScale;
    ContactEnergyVector angularMomentumScale;
    std::size_t dynamicBodyCount = 0u;

    double TotalKineticEnergy() const noexcept
    {
        return translationalKineticEnergy + rotationalKineticEnergy;
    }
};

// Concept: a complete solve is measured as one closed mechanical system.
// Linear momentum is summed directly. Angular momentum combines each body's
// center-of-mass spin with r x mv about one shared world origin so off-center
// impulses cannot hide energy in a rotational channel.
inline ContactEnergyMeasurement MeasureContactEnergy( const PhysicsBodyStore& bodyStore ) noexcept
{
    ContactEnergyMeasurement result;
    const auto records = bodyStore.Records();
    const auto hot = bodyStore.HotFields();

    for ( std::size_t bodyIndex = 0u; bodyIndex < records.size(); ++bodyIndex )
    {
        const PhysicsBodyRecord& record = records[bodyIndex];

        if ( hot.fixed[bodyIndex] != 0u || hot.inverseMass[bodyIndex] <= 0.0f )
        {
            continue;
        }

        const Math::Vector::Vector3 position = PhysicsBodyPosition( hot, bodyIndex );
        const Math::Vector::Vector3 linearVelocity = PhysicsBodyLinearVelocity( hot, bodyIndex );
        const Math::Vector::Vector3 angularVelocity = PhysicsBodyAngularVelocity( hot, bodyIndex );
        const Math::Transformation::RotationMatrix orientation = PhysicsBodyOrientation( hot, bodyIndex )
                                                                     .GetOrientationMatrix();
        const Math::Vector::Vector3 bodyAngularVelocity = record.usesWorldInertia
                                                              ? orientation.TransposeMultiply( angularVelocity )
                                                              : angularVelocity;
        const Math::Vector::Vector3 bodySpinMomentum = Math::Vector::VectorMultiply( record.rotationalInertia,
                                                                                     bodyAngularVelocity );
        const Math::Vector::Vector3 worldSpinMomentum = record.usesWorldInertia ? orientation * bodySpinMomentum
                                                                                : bodySpinMomentum;
        const double mass = static_cast<double>( record.mass );
        const double momentumX = mass * static_cast<double>( linearVelocity.x );
        const double momentumY = mass * static_cast<double>( linearVelocity.y );
        const double momentumZ = mass * static_cast<double>( linearVelocity.z );
        const double orbitalX = static_cast<double>( position.y ) * momentumZ -
                                static_cast<double>( position.z ) * momentumY;
        const double orbitalY = static_cast<double>( position.z ) * momentumX -
                                static_cast<double>( position.x ) * momentumZ;
        const double orbitalZ = static_cast<double>( position.x ) * momentumY -
                                static_cast<double>( position.y ) * momentumX;

        result.translationalKineticEnergy += 0.5 * mass *
                                             ( static_cast<double>( linearVelocity.x ) * linearVelocity.x +
                                               static_cast<double>( linearVelocity.y ) * linearVelocity.y +
                                               static_cast<double>( linearVelocity.z ) * linearVelocity.z );
        result.rotationalKineticEnergy += 0.5 * ( static_cast<double>( bodyAngularVelocity.x ) * bodySpinMomentum.x +
                                                  static_cast<double>( bodyAngularVelocity.y ) * bodySpinMomentum.y +
                                                  static_cast<double>( bodyAngularVelocity.z ) * bodySpinMomentum.z );
        result.linearMomentum.x += momentumX;
        result.linearMomentum.y += momentumY;
        result.linearMomentum.z += momentumZ;
        result.angularMomentum.x += orbitalX + static_cast<double>( worldSpinMomentum.x );
        result.angularMomentum.y += orbitalY + static_cast<double>( worldSpinMomentum.y );
        result.angularMomentum.z += orbitalZ + static_cast<double>( worldSpinMomentum.z );
        result.linearMomentumScale.x += std::abs( momentumX );
        result.linearMomentumScale.y += std::abs( momentumY );
        result.linearMomentumScale.z += std::abs( momentumZ );
        result.angularMomentumScale.x += std::abs( orbitalX ) + std::abs( static_cast<double>( worldSpinMomentum.x ) );
        result.angularMomentumScale.y += std::abs( orbitalY ) + std::abs( static_cast<double>( worldSpinMomentum.y ) );
        result.angularMomentumScale.z += std::abs( orbitalZ ) + std::abs( static_cast<double>( worldSpinMomentum.z ) );
        ++result.dynamicBodyCount;
    }

    return result;
}

inline double ContactEnergyPrecisionTolerance( double referenceEnergy ) noexcept
{
    // Invariant: this is only an accumulation-rounding allowance. Authored work,
    // penetration repair, or restitution above one must remain visible failures.
    return (std::max)( 1.0e-6, 64.0 * static_cast<double>( FLT_EPSILON ) * (std::max)( std::abs( referenceEnergy ), 1.0 ) );
}

inline double ContactMomentumPrecisionTolerance( double componentScale ) noexcept
{
    return (std::max)( 1.0e-6, 64.0 * static_cast<double>( FLT_EPSILON ) * (std::max)( std::abs( componentScale ), 1.0 ) );
}

inline double ContactBiasedEnergyTolerance( double referenceEnergy, double explicitSeparationWork ) noexcept
{
    const double scale = (std::max)( std::abs( referenceEnergy ), std::abs( explicitSeparationWork ) );
    return (std::max)( 1.0e-6, 128.0 * static_cast<double>( FLT_EPSILON ) * (std::max)( scale, 1.0 ) );
}

inline bool ContactEnergyIsFinite( const ContactEnergyMeasurement& measurement ) noexcept
{
    return std::isfinite( measurement.translationalKineticEnergy ) && std::isfinite( measurement.rotationalKineticEnergy ) &&
           std::isfinite( measurement.linearMomentum.x ) && std::isfinite( measurement.linearMomentum.y ) &&
           std::isfinite( measurement.linearMomentum.z ) && std::isfinite( measurement.angularMomentum.x ) &&
           std::isfinite( measurement.angularMomentum.y ) && std::isfinite( measurement.angularMomentum.z );
}

} // namespace SkullbonezCore::Physics
