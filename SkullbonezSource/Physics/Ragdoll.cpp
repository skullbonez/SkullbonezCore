/*
File: SkullbonezSource/Physics/Ragdoll.cpp
Purpose:
  Defines simple humanoid ragdoll descriptors and point-joint solving.

Summary:
  The body layout is prefab value data. Scene owners turn the descriptors into
  authored objects; Physics keeps handle-keyed point-joint descriptors, solver
  rows, and one retained world-space warm-start impulse per row. That vector affects
  byte-exact replay continuation. This keeps the ragdoll feature isolated and
  leaves a clear migration path to a full constraint solver. Neck swing
  correction uses the repository-owned deterministic vector-angle and axis-
  angle routines.

Glossary:
  Neck swing limit: Special angular clamp applied to the head/torso joint.
  Prefab descriptor: Immutable local part or joint facts consumed by authored
    scene setup.

Invariants:
  - Body and constraint creation order must stay deterministic.
  - Constraint solving must not allocate per row while physics is stepping.
  - This file does not construct scene objects; callers build renderable bodies
    from descriptors before registering point joints by handle.
  - Neck correction reuses one cross product for angle magnitude and axis,
    clamps its rounded dot input, and caps each correction at 0.20 radians.

Related:
  - SkullbonezSource/Physics/Ragdoll.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Maths/DeterministicMath.h
  - Agentic/Reference/engine-glossary.md
*/
#include "Ragdoll.h"

#include "../Core/Common.h"
#include "../Maths/DeterministicMath.h"
#include "ContactSolverCommon.h"
#include "PhysicsBodyStore.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

namespace
{
constexpr float RAGDOLL_SURFACE_EPSILON = 0.08f;
constexpr float RAGDOLL_DEFAULT_SCALE = 1.0f;
constexpr float RAGDOLL_MIN_SCALE = 0.25f;
constexpr float RAGDOLL_MAX_SCALE = 8.0f;
constexpr float RAGDOLL_JOINT_MAX_BIAS_SPEED = 28.0f;
constexpr float RAGDOLL_JOINT_MAX_POSITION_CORRECTION = 0.35f;
constexpr float RAGDOLL_JOINT_MAX_LINEAR_SPEED = 70.0f;
constexpr float RAGDOLL_JOINT_MAX_ANGULAR_SPEED = 18.0f;
constexpr float RAGDOLL_NECK_MAX_SWING_RADIANS = 0.52359878f;
constexpr float RAGDOLL_NECK_MAX_SWING_COSINE = 0.86602539f;
constexpr float RAGDOLL_NECK_MAX_CORRECTION_RADIANS = 0.20f;
constexpr float RAGDOLL_NECK_ANGULAR_DAMPING = 0.45f;
constexpr int RAGDOLL_SOLVER_ITERATIONS = 4;

enum SimplePart
{
    PART_TORSO,
    PART_HEAD,
    PART_LEFT_UPPER_ARM,
    PART_LEFT_LOWER_ARM,
    PART_RIGHT_UPPER_ARM,
    PART_RIGHT_LOWER_ARM,
    PART_LEFT_UPPER_LEG,
    PART_LEFT_LOWER_LEG,
    PART_RIGHT_UPPER_LEG,
    PART_RIGHT_LOWER_LEG,
    PART_COUNT
};

static_assert( PART_COUNT == Ragdoll::SIMPLE_PART_COUNT, "simple ragdoll part count mismatch" );

Vector3 ScaleVector( const Vector3& value, float scale )
{
    return Vector3( value.x * scale, value.y * scale, value.z * scale );
}

void AppendPreviewLine( std::vector<float>& lineData, const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    lineData.insert( lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
}

void AppendPreviewBox( std::vector<float>& lineData, const Vector3& center, const RotationMatrix& rotation,
                       const Vector3& halfExtents, float r, float g, float b )
{
    const Vector3 xAxis = rotation * Vector3( halfExtents.x, 0.0f, 0.0f );
    const Vector3 yAxis = rotation * Vector3( 0.0f, halfExtents.y, 0.0f );
    const Vector3 zAxis = rotation * Vector3( 0.0f, 0.0f, halfExtents.z );
    const Vector3 corners[8] = {
        center - xAxis - yAxis - zAxis, center + xAxis - yAxis - zAxis, center + xAxis + yAxis - zAxis,
        center - xAxis + yAxis - zAxis, center - xAxis - yAxis + zAxis, center + xAxis - yAxis + zAxis,
        center + xAxis + yAxis + zAxis, center - xAxis + yAxis + zAxis,
    };

    constexpr int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
        { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };

    for ( const auto& edge : edges )
    {
        AppendPreviewLine( lineData, corners[edge[0]], corners[edge[1]], r, g, b );
    }
}

RotationMatrix BodyRotation( const PhysicsBodyHotState& hot )
{
    Quaternion q = hot.orientation;
    return q.GetOrientationMatrix();
}

Vector3 ApplyRecordInvInertia( const PhysicsBodyRecord& record, const PhysicsBodyHotState& hot, const Vector3& value )
{
    if ( !record.usesWorldInertia )
    {
        return VectorMultiply( hot.inverseRotationalInertia, value );
    }

    const RotationMatrix rotation = BodyRotation( hot );
    Vector3 worldResult;
    const auto multiplyByBodyInverseInertia = [&]( const Vector3& bodyValue, Vector3& outBodyResult )
    {
        outBodyResult = VectorMultiply( hot.inverseRotationalInertia, bodyValue );

        return true;
    };

    // Why: ragdoll rows keep their value-returning, infallible inverse-inertia
    // seam, while the frame conversion is owned by the same helper as contact,
    // force, and gameplay impulses. The direct isotropic path above stays in
    // place so its arithmetic and hot-loop cost remain byte-for-byte unchanged.
    const bool applied = TryApplyWorldInertiaResponse( rotation, true, value, multiplyByBodyInverseInertia, worldResult );
    return applied ? worldResult : ZERO_VECTOR;
}

Vector3 ClampVectorMagnitude( const Vector3& value, float limit )
{
    if ( !std::isfinite( value.x ) || !std::isfinite( value.y ) || !std::isfinite( value.z ) )
    {
        return ZERO_VECTOR;
    }

    const float limitSq = limit * limit;
    const float magSq = Dot( value, value );

    if ( magSq <= limitSq || magSq <= TOLERANCE )
    {
        return value;
    }

    return value * ( limit / sqrtf( magSq ) );
}

void ClampRagdollBodyVelocity( PhysicsBodyHotState& hot )
{
    hot.linearVelocity = ClampVectorMagnitude( hot.linearVelocity, RAGDOLL_JOINT_MAX_LINEAR_SPEED );
    hot.angularVelocity = ClampVectorMagnitude( hot.angularVelocity, RAGDOLL_JOINT_MAX_ANGULAR_SPEED );
}

struct ConstraintImpulseBody
{
    PhysicsBodyRecord& record;
    PhysicsBodyHotState& hot;
    const Vector3& leverArm;
    float inverseMass = 0.0f;

    Vector3 AnchorVelocityResponse( const Vector3& impulse ) const
    {
        if ( inverseMass <= 0.0f )
        {
            return ZERO_VECTOR;
        }
        return impulse * inverseMass +
               CrossProduct( ApplyRecordInvInertia( record, hot, CrossProduct( leverArm, impulse ) ), leverArm );
    }

    void ApplyPositive( const Vector3& impulse ) const
    {
        if ( inverseMass > 0.0f )
        {
            hot.linearVelocity += impulse * inverseMass;
            hot.angularVelocity += ApplyRecordInvInertia( record, hot, CrossProduct( leverArm, impulse ) );
        }
    }

    void ApplyNegative( const Vector3& impulse ) const
    {
        if ( inverseMass > 0.0f )
        {
            hot.linearVelocity -= impulse * inverseMass;
            hot.angularVelocity -= ApplyRecordInvInertia( record, hot, CrossProduct( leverArm, impulse ) );
        }
    }

    void ClampVelocity() const
    {
        if ( inverseMass > 0.0f )
        {
            ClampRagdollBodyVelocity( hot );
        }
    }
};

void ApplyConstraintImpulse( const ConstraintImpulseBody& bodyA, const ConstraintImpulseBody& bodyB, const Vector3& impulse )
{
    // Invariant: A then B impulse application and A then B clamping preserve
    // the established per-pair floating-point and saturation order.
    bodyA.ApplyPositive( impulse );
    bodyB.ApplyNegative( impulse );
    bodyA.ClampVelocity();
    bodyB.ClampVelocity();
}


// Concept: K = (mA^-1 + mB^-1) I - [rA]x IA^-1 [rA]x - [rB]x IB^-1 [rB]x.
// Columns are the relative anchor-velocity response to unit world impulses.
// The fixed-order, scaled Cholesky solve rejects non-finite or ill-conditioned
// blocks before division; it never substitutes a preferred world axis.
class PointJointEffectiveMass
{
    float m_scale = 0.0f;
    float m_l00 = 0.0f, m_l10 = 0.0f, m_l20 = 0.0f;
    float m_l11 = 0.0f, m_l21 = 0.0f, m_l22 = 0.0f;
    float m_minimumScaledPivot = 0.0f;

  public:
    PointJointEffectiveMass( const ConstraintImpulseBody& bodyA, const ConstraintImpulseBody& bodyB )
    {
        const auto response = [&]( const Vector3& axis )
        { return bodyA.AnchorVelocityResponse( axis ) + bodyB.AnchorVelocityResponse( axis ); };
        const Vector3 x = response( Vector3( 1.0f, 0.0f, 0.0f ) );
        const Vector3 y = response( Vector3( 0.0f, 1.0f, 0.0f ) );
        const Vector3 z = response( Vector3( 0.0f, 0.0f, 1.0f ) );
        const float scale = (std::max)( x.x, (std::max)( y.y, z.z ) );
        if ( !std::isfinite( scale ) || scale <= 0.0f )
        {
            return;
        }
        constexpr float minimumPivot = 1.0e-8f;
        const float xx = x.x / scale;
        const float yx = ( x.y * 0.5f + y.x * 0.5f ) / scale;
        const float zx = ( x.z * 0.5f + z.x * 0.5f ) / scale;
        const float yy = y.y / scale;
        const float zy = ( y.z * 0.5f + z.y * 0.5f ) / scale;
        const float zz = z.z / scale;
        if ( !std::isfinite( xx ) || xx <= minimumPivot )
        {
            return;
        }
        m_l00 = sqrtf( xx );
        m_l10 = yx / m_l00;
        m_l20 = zx / m_l00;
        const float pivot1 = yy - m_l10 * m_l10;
        if ( !std::isfinite( pivot1 ) || pivot1 <= minimumPivot )
        {
            return;
        }
        m_l11 = sqrtf( pivot1 );
        m_l21 = ( zy - m_l20 * m_l10 ) / m_l11;
        const float pivot2 = zz - m_l20 * m_l20 - m_l21 * m_l21;
        if ( !std::isfinite( pivot2 ) || pivot2 <= minimumPivot )
        {
            return;
        }
        m_l22 = sqrtf( pivot2 );
        m_scale = scale;
        m_minimumScaledPivot = (std::min)( xx, (std::min)( pivot1, pivot2 ) );
    }
    bool IsValid() const
    {
        return m_scale > 0.0f;
    }
    float MinimumScaledPivot() const
    {
        return m_minimumScaledPivot;
    }
    bool Solve( const Vector3& velocityTarget, Vector3& outImpulse ) const
    {
        if ( !IsValid() )
        {
            return false;
        }
        const Vector3 rhs = velocityTarget / m_scale;
        const float a = rhs.x / m_l00;
        const float b = ( rhs.y - m_l10 * a ) / m_l11;
        const float c = ( rhs.z - m_l20 * a - m_l21 * b ) / m_l22;
        const float iz = c / m_l22;
        const float iy = ( b - m_l21 * iz ) / m_l11;
        const float ix = ( a - m_l10 * iy - m_l20 * iz ) / m_l00;
        if ( !std::isfinite( ix ) || !std::isfinite( iy ) || !std::isfinite( iz ) )
        {
            return false;
        }
        outImpulse = Vector3( ix, iy, iz );
        return true;
    }
};


bool IsBodySleeping( int bodyIndex, std::span<const uint8_t> sleepState )
{
    return bodyIndex >= 0 && bodyIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyIndex] != 0;
}


bool ApplyNeckSwingLimits( PhysicsBodyStore& bodyStore, std::span<const PointJointConstraint> constraints,
                           std::span<const uint8_t> sleepState )
{
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const int modelCount = bodyStore.Count();
    bool changed = false;

    for ( const PointJointConstraint& constraint : constraints )
    {
        if ( ( constraint.flags & PointJointConstraint::FLAG_LIMIT_NECK_SWING ) == 0 )
        {
            continue;
        }

        const int bodyAIndex = constraint.BodyAIndex( bodyStore );
        const int bodyBIndex = constraint.BodyBIndex( bodyStore );

        if ( bodyAIndex < 0 || bodyBIndex < 0 || bodyAIndex >= modelCount || bodyBIndex >= modelCount )
        {
            continue;
        }

        const size_t headIndex = static_cast<size_t>( bodyBIndex );

        if ( hotFields.fixed[headIndex] != 0u || IsBodySleeping( bodyBIndex, sleepState ) )
        {
            continue;
        }

        const PhysicsBodyHotState torsoHot = LoadPhysicsBodyHotState( hotFields, static_cast<size_t>( bodyAIndex ) );
        PhysicsBodyHotState headHot = LoadPhysicsBodyHotState( hotFields, headIndex );
        const RotationMatrix torsoRot = BodyRotation( torsoHot );
        const RotationMatrix headRot = BodyRotation( headHot );
        Vector3 torsoUp = torsoRot * Vector3( 0.0f, 1.0f, 0.0f );
        Vector3 headUp = headRot * Vector3( 0.0f, 1.0f, 0.0f );
        torsoUp.Normalise();
        headUp.Normalise();

        const float rawDot = Dot( headUp, torsoUp );
        const Vector3 correctionCross = CrossProduct( headUp, torsoUp );
        const Vector3 fallbackAxis = torsoRot * Vector3( 1.0f, 0.0f, 0.0f );
        Vector3 correctionAxis;
        float correctionAngle = 0.0f;

        if ( !Ragdoll::TryBuildNeckSwingCorrection( rawDot, correctionCross, fallbackAxis, correctionAxis,
                                                    correctionAngle ) )
        {
            continue;
        }

        Quaternion orientation = headHot.orientation;
        orientation.RotateAboutAxis( correctionAxis, correctionAngle );
        headHot.orientation = orientation;
        headHot.angularVelocity = headHot.angularVelocity * RAGDOLL_NECK_ANGULAR_DAMPING;
        StorePhysicsBodyHotState( hotFields, headIndex, headHot );
        changed = true;
    }

    return changed;
}

} // namespace

int PointJointConstraint::BodyAIndex( const PhysicsBodyStore& bodyStore ) const
{
    return bodyStore.ModelIndexForHandle( bodyA );
}


int PointJointConstraint::BodyBIndex( const PhysicsBodyStore& bodyStore ) const
{
    return bodyStore.ModelIndexForHandle( bodyB );
}


float Ragdoll::ClampScale( float scale )
{
    if ( !std::isfinite( scale ) )
    {
        return RAGDOLL_DEFAULT_SCALE;
    }

    return std::clamp( scale, RAGDOLL_MIN_SCALE, RAGDOLL_MAX_SCALE );
}


float Ragdoll::SurfaceEpsilon()
{
    return RAGDOLL_SURFACE_EPSILON;
}


bool Ragdoll::TryBuildNeckSwingCorrection( float rawDot, const Vector3& correctionCross, const Vector3& fallbackAxis,
                                           Vector3& outCorrectionAxis, float& outCorrectionAngle ) noexcept
{
    // Invariant: roundoff may move a dot just outside [-1, 1]. Clamp before
    // deterministic Atan2 so both endpoint domains remain finite and explicit.
    const float dot = SkullbonezCore::Math::ClampUnit( rawDot );

    if ( dot >= RAGDOLL_NECK_MAX_SWING_COSINE )
    {
        return false;
    }

    // Concept: vector angle is atan2(|cross|, dot). The same cross vector owns
    // both the sine-like magnitude and correction axis, avoiding acos domain
    // failure and keeping the geometric inputs paired.
    const float crossMagnitude = VectorMag( correctionCross );
    const float swingAngle = SkullbonezCore::Math::Deterministic::Atan2( crossMagnitude, dot );

    if ( swingAngle <= RAGDOLL_NECK_MAX_SWING_RADIANS )
    {
        return false;
    }

    outCorrectionAxis = correctionCross;

    if ( crossMagnitude <= TOLERANCE )
    {
        // Invariant: the torso's world X axis is the established deterministic
        // choice when parallel or anti-parallel up vectors provide no axis.
        outCorrectionAxis = fallbackAxis;
    }

    outCorrectionAxis.Normalise();
    outCorrectionAngle = (std::min)( swingAngle - RAGDOLL_NECK_MAX_SWING_RADIANS, RAGDOLL_NECK_MAX_CORRECTION_RADIANS );
    return true;
}


const RagdollPartDesc* Ragdoll::SimpleParts()
{
    // Invariant: this table order is the prefab body index order and is paired
    // with SimpleJoints plus SIMPLE_PART_COUNT.
    static const RagdollPartDesc parts[PART_COUNT] = {
        { SIMPLE_PART_SUFFIXES[PART_TORSO], Vector3( 0.0f, 12.8f, 0.0f ), Vector3( 2.2f, 3.2f, 1.1f ), 0.18f, 0.62f, 0.72f,
          1.0f },
        { SIMPLE_PART_SUFFIXES[PART_HEAD], Vector3( 0.0f, 17.25f, 0.0f ), Vector3( 1.2f, 1.2f, 1.2f ), 0.15f, 0.95f, 0.82f,
          0.58f },
        { SIMPLE_PART_SUFFIXES[PART_LEFT_UPPER_ARM], Vector3( -3.0f, 13.8f, 0.0f ), Vector3( 0.65f, 2.2f, 0.65f ), 0.14f,
          0.42f, 0.50f, 0.90f },
        { SIMPLE_PART_SUFFIXES[PART_LEFT_LOWER_ARM], Vector3( -3.0f, 9.4f, 0.0f ), Vector3( 0.58f, 2.2f, 0.58f ), 0.14f,
          0.42f, 0.50f, 0.90f },
        { SIMPLE_PART_SUFFIXES[PART_RIGHT_UPPER_ARM], Vector3( 3.0f, 13.8f, 0.0f ), Vector3( 0.65f, 2.2f, 0.65f ), 0.14f,
          0.42f, 0.50f, 0.90f },
        { SIMPLE_PART_SUFFIXES[PART_RIGHT_LOWER_ARM], Vector3( 3.0f, 9.4f, 0.0f ), Vector3( 0.58f, 2.2f, 0.58f ), 0.14f,
          0.42f, 0.50f, 0.90f },
        { SIMPLE_PART_SUFFIXES[PART_LEFT_UPPER_LEG], Vector3( -0.85f, 7.2f, 0.0f ), Vector3( 0.8f, 2.4f, 0.75f ), 0.12f,
          0.36f, 0.42f, 0.80f },
        { SIMPLE_PART_SUFFIXES[PART_LEFT_LOWER_LEG], Vector3( -0.85f, 2.4f, 0.0f ), Vector3( 0.72f, 2.4f, 0.72f ), 0.12f,
          0.36f, 0.42f, 0.80f },
        { SIMPLE_PART_SUFFIXES[PART_RIGHT_UPPER_LEG], Vector3( 0.85f, 7.2f, 0.0f ), Vector3( 0.8f, 2.4f, 0.75f ), 0.12f,
          0.36f, 0.42f, 0.80f },
        { SIMPLE_PART_SUFFIXES[PART_RIGHT_LOWER_LEG], Vector3( 0.85f, 2.4f, 0.0f ), Vector3( 0.72f, 2.4f, 0.72f ), 0.12f,
          0.36f, 0.42f, 0.80f },
    };

    return parts;
}


const RagdollJointDesc* Ragdoll::SimpleJoints( int& outCount )
{
    static const RagdollJointDesc joints[] = {
        { PART_TORSO, PART_HEAD, Vector3( 0.0f, 3.2f, 0.0f ), Vector3( 0.0f, -1.2f, 0.0f ), 0.28f,
          PointJointConstraint::FLAG_LIMIT_NECK_SWING },
        { PART_TORSO, PART_LEFT_UPPER_ARM, Vector3( -2.2f, 2.25f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.35f, 0 },
        { PART_LEFT_UPPER_ARM, PART_LEFT_LOWER_ARM, Vector3( 0.0f, -2.2f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.30f, 0 },
        { PART_TORSO, PART_RIGHT_UPPER_ARM, Vector3( 2.2f, 2.25f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.35f, 0 },
        { PART_RIGHT_UPPER_ARM, PART_RIGHT_LOWER_ARM, Vector3( 0.0f, -2.2f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.30f, 0 },
        { PART_TORSO, PART_LEFT_UPPER_LEG, Vector3( -0.85f, -3.2f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.35f, 0 },
        { PART_LEFT_UPPER_LEG, PART_LEFT_LOWER_LEG, Vector3( 0.0f, -2.4f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.30f, 0 },
        { PART_TORSO, PART_RIGHT_UPPER_LEG, Vector3( 0.85f, -3.2f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.35f, 0 },
        { PART_RIGHT_UPPER_LEG, PART_RIGHT_LOWER_LEG, Vector3( 0.0f, -2.4f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.30f, 0 },
    };

    outCount = static_cast<int>( sizeof( joints ) / sizeof( joints[0] ) );
    return joints;
}


Vector3 Ragdoll::DefaultPreviewCenter( const Vector3& terrainPoint, float scale, const Quaternion& orientation )
{
    const RagdollPartDesc* parts = SimpleParts();
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;

    for ( int i = 0; i < PART_COUNT; ++i )
    {
        minY = (std::min)( minY, parts[i].localCenter.y - parts[i].halfExtents.y );
        maxY = (std::max)( maxY, parts[i].localCenter.y + parts[i].halfExtents.y );
    }

    Quaternion q = orientation;
    const RotationMatrix rotation = q.GetOrientationMatrix();
    const float clampedScale = ClampScale( scale );
    return terrainPoint + rotation * Vector3( 0.0f, ( minY + maxY ) * 0.5f * clampedScale, 0.0f );
}

void Ragdoll::AddPreviewLines( std::vector<float>& lineData, const Vector3& terrainPoint, float scale,
                               const Quaternion& orientation, float r, float g, float b )
{
    const RagdollPartDesc* parts = SimpleParts();
    Quaternion q = orientation;
    const RotationMatrix rotation = q.GetOrientationMatrix();
    const float clampedScale = ClampScale( scale );
    const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, RAGDOLL_SURFACE_EPSILON, 0.0f );

    for ( int i = 0; i < PART_COUNT; ++i )
    {
        AppendPreviewBox( lineData, base + rotation * ScaleVector( parts[i].localCenter, clampedScale ), rotation,
                          ScaleVector( parts[i].halfExtents, clampedScale ), r, g, b );
    }
}

bool Ragdoll::SolvePointJoints( PhysicsBodyStore& bodyStore, std::span<PointJointConstraint> constraints,
                                std::span<const uint8_t> sleepState, float dt,
                                std::span<PointJointIterationSample> diagnostics )
{
    std::fill( diagnostics.begin(), diagnostics.end(), PointJointIterationSample {} );

    if ( constraints.empty() || dt <= TOLERANCE )
    {
        return false;
    }

    const auto bodyRecords = bodyStore.MutableRecords();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const int modelCount = bodyStore.Count();
    const float invDt = 1.0f / dt;

    for ( int iteration = -1; iteration < RAGDOLL_SOLVER_ITERATIONS; ++iteration )
    {
        for ( PointJointConstraint& constraint : constraints )
        {
            const int bodyAIndex = constraint.BodyAIndex( bodyStore );
            const int bodyBIndex = constraint.BodyBIndex( bodyStore );

            if ( bodyAIndex < 0 || bodyBIndex < 0 || bodyAIndex >= modelCount || bodyBIndex >= modelCount )
            {
                continue;
            }

            PhysicsBodyRecord& bodyA = bodyRecords[static_cast<size_t>( bodyAIndex )];
            PhysicsBodyRecord& bodyB = bodyRecords[static_cast<size_t>( bodyBIndex )];
            const size_t hotAIndex = static_cast<size_t>( bodyAIndex );
            const size_t hotBIndex = static_cast<size_t>( bodyBIndex );
            PhysicsBodyHotState hotA = LoadPhysicsBodyHotState( hotFields, hotAIndex );
            PhysicsBodyHotState hotB = LoadPhysicsBodyHotState( hotFields, hotBIndex );
            const bool aSleeping = bodyAIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyAIndex] != 0;
            const bool bSleeping = bodyBIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyBIndex] != 0;
            const float invMassA = ( hotA.fixed || aSleeping ) ? 0.0f : hotA.inverseMass;
            const float invMassB = ( hotB.fixed || bSleeping ) ? 0.0f : hotB.inverseMass;
            const float totalInvMass = invMassA + invMassB;

            if ( totalInvMass <= TOLERANCE )
            {
                continue;
            }

            const RotationMatrix rotA = BodyRotation( hotA );
            const RotationMatrix rotB = BodyRotation( hotB );
            const Vector3 rA = rotA * constraint.localAnchorA;
            const Vector3 rB = rotB * constraint.localAnchorB;
            const Vector3 anchorA = hotA.position + rA;
            const Vector3 anchorB = hotB.position + rB;
            Vector3 error = anchorB - anchorA;
            float distance = VectorMag( error );

            Vector3 axis( 1.0f, 0.0f, 0.0f );

            if ( distance > TOLERANCE )
            {
                axis = error / distance;
            }

            const PointJointEffectiveMass effectiveMass( { bodyA, hotA, rA, invMassA }, { bodyB, hotB, rB, invMassB } );
            if ( !effectiveMass.IsValid() )
            {
                constraint.accumulatedImpulse = ZERO_VECTOR;
                continue;
            }

            // Lifetime: warm start is a world-space vector for this body pair,
            // independent of the current error direction, including zero error.
            if ( iteration < 0 )
            {
                // Invariant: every joint applies its cached impulse before any
                // joint solves. Interleaving warm start and solve would expose
                // only half the supporting force to an upstream chain link.
                ApplyConstraintImpulse( { bodyA, hotA, rA, invMassA }, { bodyB, hotB, rB, invMassB },
                                        constraint.accumulatedImpulse );
                StorePhysicsBodyHotState( hotFields, hotAIndex, hotA );
                StorePhysicsBodyHotState( hotFields, hotBIndex, hotB );
                continue;
            }

            const Vector3 velA = hotA.linearVelocity + CrossProduct( hotA.angularVelocity, rA );
            const Vector3 velB = hotB.linearVelocity + CrossProduct( hotB.angularVelocity, rB );
            const float distanceError = (std::max)( 0.0f, distance - constraint.slack );
            const float biasSpeed = std::clamp( distanceError * constraint.stiffness * invDt, 0.0f,
                                                RAGDOLL_JOINT_MAX_BIAS_SPEED );
            const Vector3 velocityTarget = ClampVectorMagnitude( ( velB - velA + axis * biasSpeed ) *
                                                                     ( 1.0f + constraint.damping ),
                                                                 RAGDOLL_JOINT_MAX_BIAS_SPEED );
            Vector3 impulseDelta = ZERO_VECTOR;
            if ( effectiveMass.Solve( velocityTarget, impulseDelta ) )
            {
                constraint.accumulatedImpulse += impulseDelta;
                ApplyConstraintImpulse( { bodyA, hotA, rA, invMassA }, { bodyB, hotB, rB, invMassB }, impulseDelta );
            }

            if ( distanceError > TOLERANCE )
            {
                const float correctionAmount = (std::min)( distanceError * constraint.stiffness,
                                                           RAGDOLL_JOINT_MAX_POSITION_CORRECTION );

                const Vector3 correction = axis * ( correctionAmount / totalInvMass );

                if ( invMassA > 0.0f )
                {
                    hotA.position += correction * invMassA;
                }

                if ( invMassB > 0.0f )
                {
                    hotB.position -= correction * invMassB;
                }
            }

            const std::size_t diagnosticIndex = static_cast<std::size_t>( &constraint - constraints.data() ) *
                                                    RAGDOLL_SOLVER_ITERATIONS +
                                                static_cast<std::size_t>( iteration );
            if ( diagnosticIndex < diagnostics.size() )
            {
                PointJointIterationSample& sample = diagnostics[diagnosticIndex];
                sample.constraint = constraint.handle;
                sample.iteration = iteration;
                sample.anchorErrorBeforeCorrection = error;
                sample.accumulatedImpulse = constraint.accumulatedImpulse;
                sample.relativeAnchorVelocity = hotB.linearVelocity + CrossProduct( hotB.angularVelocity, rB ) -
                                                hotA.linearVelocity - CrossProduct( hotA.angularVelocity, rA );
                sample.minimumScaledPivot = effectiveMass.MinimumScaledPivot();
            }

            StorePhysicsBodyHotState( hotFields, hotAIndex, hotA );
            StorePhysicsBodyHotState( hotFields, hotBIndex, hotB );
        }
    }

    (void)ApplyNeckSwingLimits( bodyStore, constraints, sleepState );
    return true;
}
