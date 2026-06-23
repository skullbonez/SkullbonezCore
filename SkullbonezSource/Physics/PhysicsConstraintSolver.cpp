/*
File: SkullbonezSource/Physics/PhysicsConstraintSolver.cpp
Purpose:
  Solves authored ragdoll constraints as deterministic velocity rows.

Mental model:
  Contacts keep bodies from interpenetrating; constraints keep authored body
  relationships readable. This solver starts with conservative rows: anchor
  rows for ball sockets, angular damping/locks for hinges and cone twist, and
  configurable linear/angular rows for slider and six-DOF constraints.

Invariants:
  - Do not allocate per row while iterating constraints.
  - Clamp bias and velocity to prevent one bad joint from injecting huge energy.

Related:
  - SkullbonezSource/Physics/PhysicsConstraintSolver.h
  - SkullbonezSource/Physics/ContactSolverCommon.h
*/
#include "PhysicsConstraintSolver.h"

#include "../GameObjects/GameModel.h"
#include "../GameObjects/GameModelCollection.h"
#include "ContactSolverCommon.h"

#include <algorithm>
#include <cmath>

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::GameObjects::GameModelCollection;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::CrossProduct;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMag;
using SkullbonezCore::Math::Vector::VectorMultiply;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Physics::ConstraintAxisMode;
using SkullbonezCore::Physics::PhysicsConstraintDescriptor;
using SkullbonezCore::Physics::PhysicsConstraintLimit;
using SkullbonezCore::Physics::PhysicsConstraintSolver;
using SkullbonezCore::Physics::PhysicsConstraintSolverStats;
using SkullbonezCore::Physics::PhysicsConstraintType;
namespace ContactSolver = SkullbonezCore::Physics::ContactSolver;

namespace
{
constexpr float CONSTRAINT_MAX_BIAS_SPEED = 28.0f;
constexpr float CONSTRAINT_MAX_POSITION_CORRECTION = 0.35f;
constexpr float CONSTRAINT_MAX_LINEAR_SPEED = 70.0f;
constexpr float CONSTRAINT_MAX_ANGULAR_SPEED = 18.0f;
constexpr int CONSTRAINT_SOLVER_ITERATIONS = 6;

RotationMatrix BodyRotation( const GameModel& model )
{
    Quaternion q = model.GetOrientation();
    return q.GetOrientationMatrix();
}

Vector3 NormalizeOrFallback( const Vector3& value, const Vector3& fallback )
{
    const float mag = VectorMag( value );
    if ( mag <= TOLERANCE || !std::isfinite( mag ) )
    {
        return fallback;
    }
    return value / mag;
}

Vector3 ApplyModelInvInertia( GameModel& model, const Vector3& value )
{
    const Vector3& invInertia = model.GetInvertedRotationalInertia();
    if ( !model.UsesWorldInertia() )
    {
        return VectorMultiply( invInertia, value );
    }

    const RotationMatrix rotation = BodyRotation( model );
    const Vector3 local = rotation.TransposeMultiply( value );
    return rotation * VectorMultiply( invInertia, local );
}

Vector3 ClampVectorMagnitude( const Vector3& value, float limit )
{
    if ( !std::isfinite( value.x ) || !std::isfinite( value.y ) || !std::isfinite( value.z ) )
    {
        return ZERO_VECTOR;
    }

    const float limitSq = limit * limit;
    const float magSq = value * value;
    if ( magSq <= limitSq || magSq <= TOLERANCE )
    {
        return value;
    }

    return value * ( limit / sqrtf( magSq ) );
}

void ClampConstraintBodyVelocity( GameModel& model )
{
    model.SetLinearVelocity( ClampVectorMagnitude( model.GetVelocity(), CONSTRAINT_MAX_LINEAR_SPEED ) );
    model.SetAngularVelocity( ClampVectorMagnitude( model.GetAngularVelocity(), CONSTRAINT_MAX_ANGULAR_SPEED ) );
}

void ApplyLinearImpulse( GameModel& a,
                         GameModel& b,
                         const Vector3& rA,
                         const Vector3& rB,
                         const Vector3& impulse,
                         float invMassA,
                         float invMassB )
{
    if ( invMassA > 0.0f )
    {
        a.SetLinearVelocity( a.GetVelocity() + impulse * invMassA );
        a.SetAngularVelocity( a.GetAngularVelocity() + ApplyModelInvInertia( a, CrossProduct( rA, impulse ) ) );
    }
    if ( invMassB > 0.0f )
    {
        b.SetLinearVelocity( b.GetVelocity() - impulse * invMassB );
        b.SetAngularVelocity( b.GetAngularVelocity() - ApplyModelInvInertia( b, CrossProduct( rB, impulse ) ) );
    }
    if ( invMassA > 0.0f )
    {
        ClampConstraintBodyVelocity( a );
    }
    if ( invMassB > 0.0f )
    {
        ClampConstraintBodyVelocity( b );
    }
}

float SolveLinearRow( GameModel& a,
                      GameModel& b,
                      const Vector3& rA,
                      const Vector3& rB,
                      const Vector3& axis,
                      float biasSpeed,
                      float damping,
                      float invMassA,
                      float invMassB )
{
    const Vector3 velA = a.GetVelocity() + CrossProduct( a.GetAngularVelocity(), rA );
    const Vector3 velB = b.GetVelocity() + CrossProduct( b.GetAngularVelocity(), rB );
    const float relVel = ( velB - velA ) * axis;
    const float velocityTarget = std::clamp( ( relVel + biasSpeed ) * ( 1.0f + damping ),
                                             -CONSTRAINT_MAX_BIAS_SPEED,
                                             CONSTRAINT_MAX_BIAS_SPEED );
    const float effectiveMass = ContactSolver::ComputeTwoBodyEffectiveMass(
        invMassA,
        invMassB,
        axis,
        rA,
        rB,
        [&]( const Vector3& v ) { return invMassA > 0.0f ? ApplyModelInvInertia( a, v ) : ZERO_VECTOR; },
        [&]( const Vector3& v ) { return invMassB > 0.0f ? ApplyModelInvInertia( b, v ) : ZERO_VECTOR; } );
    if ( effectiveMass <= 0.0f )
    {
        return 0.0f;
    }

    const float impulseMagnitude = effectiveMass * velocityTarget;
    ApplyLinearImpulse( a, b, rA, rB, axis * impulseMagnitude, invMassA, invMassB );
    return fabsf( impulseMagnitude );
}

float SolveAngularRow( GameModel& a,
                       GameModel& b,
                       const Vector3& axis,
                       float biasSpeed,
                       float damping,
                       float invMassA,
                       float invMassB )
{
    const Vector3 normalizedAxis = NormalizeOrFallback( axis, Vector3( 0.0f, 1.0f, 0.0f ) );
    const float relOmega = ( b.GetAngularVelocity() - a.GetAngularVelocity() ) * normalizedAxis;
    const float velocityTarget = std::clamp( ( relOmega + biasSpeed ) * ( 1.0f + damping ),
                                             -CONSTRAINT_MAX_BIAS_SPEED,
                                             CONSTRAINT_MAX_BIAS_SPEED );
    const Vector3 invA = invMassA > 0.0f ? ApplyModelInvInertia( a, normalizedAxis ) : ZERO_VECTOR;
    const Vector3 invB = invMassB > 0.0f ? ApplyModelInvInertia( b, normalizedAxis ) : ZERO_VECTOR;
    const float k = normalizedAxis * ( invA + invB );
    if ( k <= TOLERANCE )
    {
        return 0.0f;
    }

    const float impulseMagnitude = velocityTarget / k;
    const Vector3 impulse = normalizedAxis * impulseMagnitude;
    if ( invMassA > 0.0f )
    {
        a.SetAngularVelocity( a.GetAngularVelocity() + ApplyModelInvInertia( a, impulse ) );
        ClampConstraintBodyVelocity( a );
    }
    if ( invMassB > 0.0f )
    {
        b.SetAngularVelocity( b.GetAngularVelocity() - ApplyModelInvInertia( b, impulse ) );
        ClampConstraintBodyVelocity( b );
    }
    return fabsf( impulseMagnitude );
}

void BuildJointBasis( const RotationMatrix& rot,
                      const Vector3& localPrimary,
                      const Vector3& localSecondary,
                      Vector3& primary,
                      Vector3& secondary,
                      Vector3& tertiary )
{
    primary = NormalizeOrFallback( rot * localPrimary, Vector3( 0.0f, 1.0f, 0.0f ) );
    secondary = rot * localSecondary;
    secondary -= primary * ( secondary * primary );
    secondary =
        NormalizeOrFallback( secondary,
                             fabsf( primary.y ) > 0.8f ? Vector3( 1.0f, 0.0f, 0.0f ) : Vector3( 0.0f, 1.0f, 0.0f ) );
    tertiary = NormalizeOrFallback( CrossProduct( primary, secondary ), Vector3( 0.0f, 0.0f, 1.0f ) );
}

float ClampAngleUnitDot( float dot )
{
    return std::clamp( dot, -1.0f, 1.0f );
}

void SolveAnchorRows( GameModel& a,
                      GameModel& b,
                      const PhysicsConstraintDescriptor& constraint,
                      const Vector3& rA,
                      const Vector3& rB,
                      const Vector3& anchorA,
                      const Vector3& anchorB,
                      float invMassA,
                      float invMassB,
                      float invDt,
                      PhysicsConstraintSolverStats& stats )
{
    Vector3 error = anchorB - anchorA;
    const float distance = VectorMag( error );
    stats.maxAnchorError = (std::max)( stats.maxAnchorError, distance );

    const float distanceError = (std::max)( 0.0f, distance - constraint.slack );
    const float biasScale = distanceError > TOLERANCE ? constraint.stiffness * invDt : 0.0f;
    const Vector3 axes[3] = {
        Vector3( 1.0f, 0.0f, 0.0f ),
        Vector3( 0.0f, 1.0f, 0.0f ),
        Vector3( 0.0f, 0.0f, 1.0f ),
    };
    for ( const Vector3& axis : axes )
    {
        const float biasSpeed =
            std::clamp( ( error * axis ) * biasScale, -CONSTRAINT_MAX_BIAS_SPEED, CONSTRAINT_MAX_BIAS_SPEED );
        stats.maxImpulse =
            (std::max)( stats.maxImpulse,
                        SolveLinearRow( a, b, rA, rB, axis, biasSpeed, constraint.damping, invMassA, invMassB ) );
        ++stats.solvedRows;
    }

    const float totalInvMass = invMassA + invMassB;
    if ( totalInvMass > TOLERANCE && distanceError > TOLERANCE )
    {
        const Vector3 axis = NormalizeOrFallback( error, Vector3( 1.0f, 0.0f, 0.0f ) );
        const float correctionAmount =
            (std::min)( distanceError * constraint.stiffness, CONSTRAINT_MAX_POSITION_CORRECTION );
        const Vector3 correction = axis * ( correctionAmount / totalInvMass );
        if ( invMassA > 0.0f )
        {
            a.SetPosition( a.GetPosition() + correction * invMassA );
        }
        if ( invMassB > 0.0f )
        {
            b.SetPosition( b.GetPosition() - correction * invMassB );
        }
    }
}

void SolveHingeAngularRows( GameModel& a,
                            GameModel& b,
                            const PhysicsConstraintDescriptor& constraint,
                            const Vector3& primaryA,
                            const Vector3& secondaryA,
                            const Vector3& tertiaryA,
                            const Vector3& primaryB,
                            const Vector3& secondaryB,
                            float invMassA,
                            float invMassB,
                            PhysicsConstraintSolverStats& stats )
{
    const Vector3 hingeAxis = NormalizeOrFallback( primaryA + primaryB, primaryA );
    stats.maxImpulse = (std::max)( stats.maxImpulse,
                                   SolveAngularRow( a,
                                                    b,
                                                    secondaryA,
                                                    ( primaryB * tertiaryA ) * CONSTRAINT_MAX_BIAS_SPEED * 0.12f,
                                                    constraint.angularDamping,
                                                    invMassA,
                                                    invMassB ) );
    stats.maxImpulse = (std::max)( stats.maxImpulse,
                                   SolveAngularRow( a,
                                                    b,
                                                    tertiaryA,
                                                    -( primaryB * secondaryA ) * CONSTRAINT_MAX_BIAS_SPEED * 0.12f,
                                                    constraint.angularDamping,
                                                    invMassA,
                                                    invMassB ) );
    stats.solvedRows += 2;

    const PhysicsConstraintLimit& limit = constraint.angularLimits[0];
    const float sinAngle = CrossProduct( secondaryA, secondaryB ) * hingeAxis;
    const float cosAngle = ClampAngleUnitDot( secondaryA * secondaryB );
    const float angle = atan2f( sinAngle, cosAngle );
    float violation = 0.0f;
    if ( angle < limit.minValue )
    {
        violation = angle - limit.minValue;
    }
    else if ( angle > limit.maxValue )
    {
        violation = angle - limit.maxValue;
    }
    if ( fabsf( violation ) > TOLERANCE )
    {
        stats.maxLimitViolation = (std::max)( stats.maxLimitViolation, fabsf( violation ) );
        stats.maxImpulse = (std::max)( stats.maxImpulse,
                                       SolveAngularRow( a,
                                                        b,
                                                        hingeAxis,
                                                        violation * CONSTRAINT_MAX_BIAS_SPEED * 0.25f,
                                                        limit.damping + constraint.angularDamping,
                                                        invMassA,
                                                        invMassB ) );
        ++stats.solvedRows;
    }
}

void SolveConeTwistRows( GameModel& a,
                         GameModel& b,
                         const PhysicsConstraintDescriptor& constraint,
                         const Vector3& primaryA,
                         const Vector3& secondaryA,
                         const Vector3& primaryB,
                         const Vector3& secondaryB,
                         float invMassA,
                         float invMassB,
                         PhysicsConstraintSolverStats& stats )
{
    const PhysicsConstraintLimit& swingLimit = constraint.angularLimits[0];
    const float swingDot = ClampAngleUnitDot( primaryA * primaryB );
    const float swingAngle = acosf( swingDot );
    if ( swingAngle > swingLimit.maxValue )
    {
        Vector3 swingAxis = CrossProduct( primaryB, primaryA );
        swingAxis = NormalizeOrFallback( swingAxis, secondaryA );
        const float violation = swingAngle - swingLimit.maxValue;
        stats.maxLimitViolation = (std::max)( stats.maxLimitViolation, violation );
        stats.maxImpulse = (std::max)( stats.maxImpulse,
                                       SolveAngularRow( a,
                                                        b,
                                                        swingAxis,
                                                        violation * CONSTRAINT_MAX_BIAS_SPEED * 0.18f,
                                                        swingLimit.damping + constraint.angularDamping,
                                                        invMassA,
                                                        invMassB ) );
        ++stats.solvedRows;
    }

    const PhysicsConstraintLimit& twistLimit = constraint.angularLimits[1];
    const Vector3 projectedSecondaryB =
        NormalizeOrFallback( secondaryB - primaryA * ( secondaryB * primaryA ), secondaryA );
    const float twistSin = CrossProduct( secondaryA, projectedSecondaryB ) * primaryA;
    const float twistCos = ClampAngleUnitDot( secondaryA * projectedSecondaryB );
    const float twistAngle = atan2f( twistSin, twistCos );
    float twistViolation = 0.0f;
    if ( twistAngle < twistLimit.minValue )
    {
        twistViolation = twistAngle - twistLimit.minValue;
    }
    else if ( twistAngle > twistLimit.maxValue )
    {
        twistViolation = twistAngle - twistLimit.maxValue;
    }
    if ( fabsf( twistViolation ) > TOLERANCE )
    {
        stats.maxLimitViolation = (std::max)( stats.maxLimitViolation, fabsf( twistViolation ) );
        stats.maxImpulse = (std::max)( stats.maxImpulse,
                                       SolveAngularRow( a,
                                                        b,
                                                        primaryA,
                                                        twistViolation * CONSTRAINT_MAX_BIAS_SPEED * 0.18f,
                                                        twistLimit.damping + constraint.angularDamping,
                                                        invMassA,
                                                        invMassB ) );
        ++stats.solvedRows;
    }
}

void SolveSliderRows( GameModel& a,
                      GameModel& b,
                      const PhysicsConstraintDescriptor& constraint,
                      const Vector3& rA,
                      const Vector3& rB,
                      const Vector3& anchorA,
                      const Vector3& anchorB,
                      const Vector3& primaryA,
                      const Vector3& secondaryA,
                      const Vector3& tertiaryA,
                      float invMassA,
                      float invMassB,
                      float invDt,
                      PhysicsConstraintSolverStats& stats )
{
    const Vector3 error = anchorB - anchorA;
    const Vector3 axes[3] = { primaryA, secondaryA, tertiaryA };
    for ( int axisIndex = 0; axisIndex < 3; ++axisIndex )
    {
        const ConstraintAxisMode mode = constraint.linearAxisModes[static_cast<size_t>( axisIndex )];
        if ( mode == ConstraintAxisMode::Free )
        {
            continue;
        }
        const float distance = error * axes[axisIndex];
        float violation = distance;
        if ( mode == ConstraintAxisMode::Limited )
        {
            const PhysicsConstraintLimit& limit = constraint.linearLimits[static_cast<size_t>( axisIndex )];
            if ( distance < limit.minValue )
            {
                violation = distance - limit.minValue;
            }
            else if ( distance > limit.maxValue )
            {
                violation = distance - limit.maxValue;
            }
            else
            {
                continue;
            }
        }
        stats.maxLimitViolation = (std::max)( stats.maxLimitViolation, fabsf( violation ) );
        stats.maxImpulse = (std::max)( stats.maxImpulse,
                                       SolveLinearRow( a,
                                                       b,
                                                       rA,
                                                       rB,
                                                       axes[axisIndex],
                                                       violation * constraint.stiffness * invDt,
                                                       constraint.damping,
                                                       invMassA,
                                                       invMassB ) );
        ++stats.solvedRows;
    }
}

float MeasureAngleAroundAxis( const Vector3& axis, const Vector3& referenceA, const Vector3& referenceB )
{
    const Vector3 normalizedAxis = NormalizeOrFallback( axis, Vector3( 0.0f, 1.0f, 0.0f ) );
    Vector3 projectedA = referenceA - normalizedAxis * ( referenceA * normalizedAxis );
    projectedA = NormalizeOrFallback( projectedA, Vector3( 1.0f, 0.0f, 0.0f ) );
    Vector3 projectedB = referenceB - normalizedAxis * ( referenceB * normalizedAxis );
    projectedB = NormalizeOrFallback( projectedB, projectedA );
    const float sinAngle = CrossProduct( projectedA, projectedB ) * normalizedAxis;
    const float cosAngle = ClampAngleUnitDot( projectedA * projectedB );
    return atan2f( sinAngle, cosAngle );
}

void SolveAngularAxisModeRows( GameModel& a,
                               GameModel& b,
                               const PhysicsConstraintDescriptor& constraint,
                               const Vector3 axes[3],
                               const Vector3 referencesA[3],
                               const Vector3 referencesB[3],
                               float invMassA,
                               float invMassB,
                               PhysicsConstraintSolverStats& stats )
{
    for ( int axisIndex = 0; axisIndex < 3; ++axisIndex )
    {
        const ConstraintAxisMode mode = constraint.angularAxisModes[static_cast<size_t>( axisIndex )];
        if ( mode == ConstraintAxisMode::Free )
        {
            continue;
        }

        const float angle = MeasureAngleAroundAxis( axes[axisIndex], referencesA[axisIndex], referencesB[axisIndex] );
        float violation = angle;
        bool solveRow = true;
        const PhysicsConstraintLimit& limit = constraint.angularLimits[static_cast<size_t>( axisIndex )];
        if ( mode == ConstraintAxisMode::Limited )
        {
            if ( angle < limit.minValue )
            {
                violation = angle - limit.minValue;
            }
            else if ( angle > limit.maxValue )
            {
                violation = angle - limit.maxValue;
            }
            else
            {
                solveRow = false;
            }
        }

        if ( solveRow )
        {
            stats.maxLimitViolation = (std::max)( stats.maxLimitViolation, fabsf( violation ) );
            stats.maxImpulse = (std::max)( stats.maxImpulse,
                                           SolveAngularRow( a,
                                                            b,
                                                            axes[axisIndex],
                                                            violation * CONSTRAINT_MAX_BIAS_SPEED * 0.20f,
                                                            limit.damping + constraint.angularDamping,
                                                            invMassA,
                                                            invMassB ) );
            ++stats.solvedRows;
        }
    }
}
} // namespace

PhysicsConstraintSolverStats
PhysicsConstraintSolver::Solve( GameModelCollection& collection,
                                const std::vector<PhysicsConstraintDescriptor>& constraints,
                                const std::vector<uint8_t>& sleepState,
                                float dt )
{
    PhysicsConstraintSolverStats stats;
    stats.descriptorCount = static_cast<int>( constraints.size() );
    stats.solverIterations = CONSTRAINT_SOLVER_ITERATIONS;
    if ( constraints.empty() || dt <= TOLERANCE )
    {
        return stats;
    }

    std::vector<GameModel>& models = collection.PhysicsModels();
    const int modelCount = static_cast<int>( models.size() );
    const float invDt = 1.0f / dt;
    for ( int iteration = 0; iteration < CONSTRAINT_SOLVER_ITERATIONS; ++iteration )
    {
        for ( const PhysicsConstraintDescriptor& constraint : constraints )
        {
            if ( constraint.bodyA < 0 || constraint.bodyB < 0 || constraint.bodyA >= modelCount ||
                 constraint.bodyB >= modelCount || constraint.bodyA == constraint.bodyB )
            {
                continue;
            }

            GameModel& a = models[static_cast<size_t>( constraint.bodyA )];
            GameModel& b = models[static_cast<size_t>( constraint.bodyB )];
            const bool aSleeping =
                constraint.bodyA < static_cast<int>( sleepState.size() ) && sleepState[constraint.bodyA] != 0;
            const bool bSleeping =
                constraint.bodyB < static_cast<int>( sleepState.size() ) && sleepState[constraint.bodyB] != 0;
            const float invMassA = ( a.IsFixed() || aSleeping ) ? 0.0f : a.GetInvertedMass();
            const float invMassB = ( b.IsFixed() || bSleeping ) ? 0.0f : b.GetInvertedMass();
            if ( invMassA + invMassB <= TOLERANCE )
            {
                continue;
            }

            const RotationMatrix rotA = BodyRotation( a );
            const RotationMatrix rotB = BodyRotation( b );
            const Vector3 rA = rotA * constraint.localAnchorA;
            const Vector3 rB = rotB * constraint.localAnchorB;
            const Vector3 anchorA = a.GetPosition() + rA;
            const Vector3 anchorB = b.GetPosition() + rB;
            Vector3 primaryA;
            Vector3 secondaryA;
            Vector3 tertiaryA;
            Vector3 primaryB;
            Vector3 secondaryB;
            Vector3 tertiaryB;
            BuildJointBasis( rotA,
                             constraint.localAxisA,
                             constraint.localSecondaryAxisA,
                             primaryA,
                             secondaryA,
                             tertiaryA );
            BuildJointBasis( rotB,
                             constraint.localAxisB,
                             constraint.localSecondaryAxisB,
                             primaryB,
                             secondaryB,
                             tertiaryB );

            if ( constraint.type == PhysicsConstraintType::Slider || constraint.type == PhysicsConstraintType::SixDof )
            {
                SolveSliderRows( a,
                                 b,
                                 constraint,
                                 rA,
                                 rB,
                                 anchorA,
                                 anchorB,
                                 primaryA,
                                 secondaryA,
                                 tertiaryA,
                                 invMassA,
                                 invMassB,
                                 invDt,
                                 stats );
            }
            else
            {
                SolveAnchorRows( a, b, constraint, rA, rB, anchorA, anchorB, invMassA, invMassB, invDt, stats );
            }

            if ( constraint.type == PhysicsConstraintType::Hinge )
            {
                SolveHingeAngularRows( a,
                                       b,
                                       constraint,
                                       primaryA,
                                       secondaryA,
                                       tertiaryA,
                                       primaryB,
                                       secondaryB,
                                       invMassA,
                                       invMassB,
                                       stats );
            }
            else if ( constraint.type == PhysicsConstraintType::ConeTwist )
            {
                SolveConeTwistRows( a,
                                    b,
                                    constraint,
                                    primaryA,
                                    secondaryA,
                                    primaryB,
                                    secondaryB,
                                    invMassA,
                                    invMassB,
                                    stats );
            }
            else if ( constraint.type == PhysicsConstraintType::Slider ||
                      constraint.type == PhysicsConstraintType::SixDof )
            {
                const Vector3 axes[3] = { primaryA, secondaryA, tertiaryA };
                const Vector3 referencesA[3] = { secondaryA, tertiaryA, primaryA };
                const Vector3 referencesB[3] = { secondaryB, tertiaryB, primaryB };
                SolveAngularAxisModeRows( a, b, constraint, axes, referencesA, referencesB, invMassA, invMassB, stats );
            }
        }
    }

    collection.InvalidatePhysicsStreams();
    return stats;
}
