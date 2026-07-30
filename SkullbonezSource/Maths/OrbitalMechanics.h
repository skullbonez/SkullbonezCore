/*
File: SkullbonezSource/Maths/OrbitalMechanics.h
Purpose:
  Declares allocation-free elliptic-orbit and single-revolution Lambert math.

Summary:
  The orbital library converts relative Cartesian state into classical elements,
  propagates those elements with Kepler's equation, samples fixed caller-owned
  polylines, and produces a recoverable Lambert velocity seed. It owns no engine
  state and depends only on Maths values.

Glossary:
  Orbital elements: Compact values describing one two-body conic at an epoch.
  Eccentric anomaly: Elliptic-orbit angle used to solve Kepler's equation.
  Lambert solution: Departure and arrival velocities joining two positions in
    a specified time under one central gravitational parameter.
  Gravitational parameter: Product GM, expressed in engine distance/time units.

Invariants:
  - V1 accepts elliptic elements only: semi-major axis > 0 and 0 <= e < 1.
  - Every iterative solver has a compile-time cap and reports failure by value.
  - Failure leaves output records finite and zero-initialized.
  - Polyline storage is supplied by the caller; this API performs no allocation.

Related:
  - SkullbonezSource/Maths/OrbitalMechanics.cpp
  - SkullbonezTests/TestOrbitalMechanics.cpp
  - Agentic/Reports/2026-07-24/solar-system-trajectory-planner-closure.md
*/
#pragma once

#include "Vector3.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore
{
namespace Math
{
namespace Orbital
{
enum class OrbitalStatus : uint8_t
{
    Ok,
    NotConverged,
    Degenerate,
    NotElliptic
};

struct OrbitalElements
{
    float semiMajorAxis = 0.0f;
    float eccentricity = 0.0f;
    float inclination = 0.0f;
    float longitudeAscendingNode = 0.0f;
    float argumentPeriapsis = 0.0f;
    float meanAnomalyAtEpoch = 0.0f;
    float mu = 0.0f;
};

struct LambertSolution
{
    Vector::Vector3 v1 = Vector::Vector3( 0.0f, 0.0f, 0.0f );
    Vector::Vector3 v2 = Vector::Vector3( 0.0f, 0.0f, 0.0f );
};

OrbitalStatus ElementsFromState( const Vector::Vector3& relativePosition, const Vector::Vector3& relativeVelocity, float mu,
                                 OrbitalElements& out );
OrbitalStatus PropagateToTime( const OrbitalElements& elements, float deltaSeconds, Vector::Vector3& outRelativePosition,
                               Vector::Vector3& outRelativeVelocity );
std::size_t SampleOrbitPolyline( const OrbitalElements& elements, std::span<Vector::Vector3> outPoints );
OrbitalStatus SolveLambert( const Vector::Vector3& r1, const Vector::Vector3& r2, float timeOfFlight, float mu,
                            bool prograde, LambertSolution& out );
} // namespace Orbital
} // namespace Math
} // namespace SkullbonezCore
