/*
File: SkullbonezSource/Physics/PhysicsBodyStore.cpp
Purpose:
  Owns deterministic body-order mutable physics state loaded from GameModel.

Mental model:
  LoadFromModels copies legacy construction/runtime state into the store.
  PhysicsWorld mutates records during the step, then WriteBackToModels keeps
  older render, replay, tool, terrain, and shape callers working until they move
  to store-backed views.

Glossary:
  Body: Simulated object state such as position, orientation, velocity, mass,
    and sleep flag.
  Sleep: Optimization that stops simulating stable bodies until something wakes
    them.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.
  Replay body id: Stable per-scene id used by replay and SkullScope traces.

Invariants:
  - Body records stay in GameModelCollection physics model order for current
    solver traversal, but public body handles are allocator-owned slots.
  - Pending impulses and sleep state are preserved across compatibility refresh
    by handle identity, even if a compatible model refresh reorders slots.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
*/
#include "PhysicsBodyStore.h"
#include "ColliderStore.h"
#include "PhysicsModelAccess.h"
#include "PhysicsWorldForces.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

#include "../Core/Common.h"
#include "../Core/Profiler.h"
#include "../GameObjects/GameModel.h"
#include "../World/Terrain.h"
#include "../World/TerrainSupportClassifier.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Geometry::Plane;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::CollisionDetection::GetShapeTerrainBottomOffset;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::CrossProduct;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMagSquared;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PHYSICS_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsWorldForces;

namespace
{
struct PhysicsBuoyancySample
{
    static constexpr uint8_t MAX_WET_POINTS = 32;

    float submergedVolumePercent = 0.0f;
    Vector3 centerOfBuoyancy = ZERO_VECTOR;
    std::array<Vector3, MAX_WET_POINTS> wetPoints = {};
    std::array<float, MAX_WET_POINTS> wetWeights = {};
    uint8_t wetPointCount = 0;
    float wetWeightTotal = 0.0f;
};

struct PreservedRefreshState
{
    Vector3 pendingImpulse = ZERO_VECTOR;
    Vector3 pendingImpulseApplicationPoint = ZERO_VECTOR;
    bool hasPendingImpulse = false;
    bool isSleeping = false;
    bool hasState = false;
};

const ColliderRecord* ColliderRecordForModelIndex( const ColliderStore& colliderStore, int modelIndex )
{
    const std::vector<ColliderRecord>& colliders = colliderStore.Records();
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( colliders.size() ) )
    {
        return nullptr;
    }
    return &colliders[static_cast<std::size_t>( modelIndex )];
}

// Concept: integrated terrain clamp samples the real support vertices.
//
// Boxes and hulls should be lifted only by their deepest actual vertex
// penetration. A center-height clamp would make tilted or uneven-terrain bodies
// visibly float and would change the deterministic physics baseline.
bool FindClosestBoxTerrainVertex( const PhysicsBodyRecord& record,
                                  const BoundingBox& box,
                                  Vector3& outVertex,
                                  float& outTerrainHeight,
                                  Plane& outPlane,
                                  float& outGap )
{
    PROFILE_SCOPED( "Frame/Physics/Terrain/BoxClosestVertexProbe" );

    if ( !record.terrain )
    {
        return false;
    }

    const Vector3& he = box.GetHalfExtents();
    auto orientation = record.orientation;
    const RotationMatrix rotMat = orientation.GetOrientationMatrix();

    bool found = false;
    float bestGap = 1.0e30f;
    for ( int v = 0; v < 8; ++v )
    {
        const Vector3 local( ( v & 1 ) ? he.x : -he.x,
                             ( v & 2 ) ? he.y : -he.y,
                             ( v & 4 ) ? he.z : -he.z );
        const Vector3 worldVertex = record.position + ( rotMat * local );

        if ( !record.terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        float terrainHeight = 0.0f;
        Plane terrainPlane;
        record.terrain->GetTerrainHeightAndPlaneAt( worldVertex.x, worldVertex.z, terrainHeight, terrainPlane );
        const float gap = worldVertex.y - terrainHeight;
        if ( !found || gap < bestGap )
        {
            found = true;
            bestGap = gap;
            outVertex = worldVertex;
            outTerrainHeight = terrainHeight;
            outPlane = terrainPlane;
            outGap = gap;
        }
    }

    return found;
}

bool FindClosestHullTerrainVertex( const PhysicsBodyRecord& record,
                                   const ConvexHullShape& hull,
                                   Vector3& outVertex,
                                   float& outTerrainHeight,
                                   Plane& outPlane,
                                   float& outGap )
{
    PROFILE_SCOPED( "Frame/Physics/Terrain/HullClosestVertexProbe" );

    if ( !record.terrain )
    {
        return false;
    }

    auto orientation = record.orientation;
    const RotationMatrix rotMat = orientation.GetOrientationMatrix();
    const Vector3 hullCenter = record.position + ( rotMat * hull.GetPosition() );

    bool found = false;
    float bestGap = 1.0e30f;
    const uint16_t vertexCount = hull.GetVertexCount();
    for ( uint16_t v = 0; v < vertexCount; ++v )
    {
        const Vector3 worldVertex = hullCenter + ( rotMat * hull.GetVertex( v ) );

        if ( !record.terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        float terrainHeight = 0.0f;
        Plane terrainPlane;
        record.terrain->GetTerrainHeightAndPlaneAt( worldVertex.x, worldVertex.z, terrainHeight, terrainPlane );
        const float gap = worldVertex.y - terrainHeight;
        if ( !found || gap < bestGap )
        {
            found = true;
            bestGap = gap;
            outVertex = worldVertex;
            outTerrainHeight = terrainHeight;
            outPlane = terrainPlane;
            outGap = gap;
        }
    }

    return found;
}

void ClampBodyToTerrainSurface( PhysicsBodyRecord& record, const ColliderRecord& collider )
{
    if ( !record.terrain )
    {
        return;
    }

    if ( !record.terrain->IsInBounds( record.position.x, record.position.z ) )
    {
        return;
    }

    if ( std::holds_alternative<BoundingBox>( collider.shape ) )
    {
        Vector3 closestVertex;
        float terrainHeight = 0.0f;
        Plane terrainPlane;
        float gap = 0.0f;
        if ( FindClosestBoxTerrainVertex( record,
                                          std::get<BoundingBox>( collider.shape ),
                                          closestVertex,
                                          terrainHeight,
                                          terrainPlane,
                                          gap ) &&
             gap < 0.0f )
        {
            record.position.y -= gap;
        }
        return;
    }

    if ( std::holds_alternative<ConvexHullShape>( collider.shape ) )
    {
        Vector3 closestVertex;
        float terrainHeight = 0.0f;
        Plane terrainPlane;
        float gap = 0.0f;
        if ( FindClosestHullTerrainVertex( record,
                                           std::get<ConvexHullShape>( collider.shape ),
                                           closestVertex,
                                           terrainHeight,
                                           terrainPlane,
                                           gap ) &&
             gap < 0.0f )
        {
            record.position.y -= gap;
        }
        return;
    }

    const float bottomOffset = GetShapeTerrainBottomOffset( collider.shape );
    const float terrainHeight = record.terrain->GetTerrainHeightAt( record.position.x, record.position.z );
    if ( record.position.y - bottomOffset < terrainHeight )
    {
        record.position.y = terrainHeight + bottomOffset;
    }
}

uint32_t NextHandleGeneration( uint32_t generation )
{
    ++generation;
    return generation == 0u ? PHYSICS_HANDLE_INITIAL_GENERATION : generation;
}

std::vector<PreservedRefreshState> CapturePreservedRefreshState( const std::vector<PhysicsBodyRecord>& bodies,
                                                                 std::size_t handleSlotCount )
{
    std::vector<PreservedRefreshState> preserved( handleSlotCount );
    for ( const PhysicsBodyRecord& record : bodies )
    {
        if ( !record.handle.IsValid() || record.handle.index >= preserved.size() )
        {
            continue;
        }

        PreservedRefreshState& state = preserved[static_cast<std::size_t>( record.handle.index )];
        state.pendingImpulse = record.pendingImpulse;
        state.pendingImpulseApplicationPoint = record.pendingImpulseApplicationPoint;
        state.hasPendingImpulse = record.hasPendingImpulse;
        state.isSleeping = record.isSleeping;
        state.hasState = true;
    }
    return preserved;
}

const PreservedRefreshState* PreservedStateForHandle( const std::vector<PreservedRefreshState>& preserved,
                                                      PhysicsBodyHandle handle )
{
    if ( !handle.IsValid() || handle.index >= preserved.size() )
    {
        return nullptr;
    }
    const PreservedRefreshState& state = preserved[static_cast<std::size_t>( handle.index )];
    return state.hasState ? &state : nullptr;
}

void ThrottleAngularVelocity( PhysicsBodyRecord& record )
{
    const float magSq = record.angularVelocity.x * record.angularVelocity.x +
                        record.angularVelocity.y * record.angularVelocity.y +
                        record.angularVelocity.z * record.angularVelocity.z;
    const float limitSq = record.angularVelocityLimit * record.angularVelocityLimit;
    if ( magSq > limitSq )
    {
        const float scale = record.angularVelocityLimit / sqrtf( magSq );
        record.angularVelocity.x *= scale;
        record.angularVelocity.y *= scale;
        record.angularVelocity.z *= scale;
    }
}

float ClampAngularDragTorqueAxis( float torque, float angularVelocity, float inertia, float deltaSeconds )
{
    if ( fabsf( angularVelocity ) <= TOLERANCE || inertia <= TOLERANCE || deltaSeconds <= TOLERANCE )
    {
        return torque;
    }

    const float maxDampingTorque = fabsf( angularVelocity ) * inertia / deltaSeconds;
    return (std::clamp)( torque, -maxDampingTorque, maxDampingTorque );
}

float CalculateGravityForce( const PhysicsWorldForces& worldForces, float objectMass )
{
    return objectMass * worldForces.gravity;
}

float CalculateBuoyancyForce( const PhysicsWorldForces& worldForces, float submergedObjectVolume )
{
    return worldForces.gravity * worldForces.fluidDensity * submergedObjectVolume * -1.0f;
}

Vector3 CalculateViscousDrag( const PhysicsWorldForces& worldForces,
                              Vector3 velocityVector,
                              float submergedVolumePercent,
                              float dragCoefficient,
                              float projectedSurfaceArea )
{
    if ( velocityVector.IsCloseToZero() )
    {
        return ZERO_VECTOR;
    }

    const float distanceSquared = VectorMagSquared( velocityVector );
    velocityVector.Normalise();
    velocityVector *= -1.0f;
    return velocityVector * 0.5f *
           ( ( worldForces.gasDensity * ( 1.0f - submergedVolumePercent ) ) +
             ( worldForces.fluidDensity * submergedVolumePercent ) ) *
           distanceSquared * dragCoefficient * projectedSurfaceArea;
}

float TerrainWaterScale( Terrain* terrain,
                         const PhysicsWorldForces& worldForces,
                         const Vector3& worldPoint,
                         float sampleBand )
{
    if ( !terrain || !terrain->IsInBounds( worldPoint.x, worldPoint.z ) )
    {
        return 1.0f;
    }

    const float terrainHeight = terrain->GetTerrainHeightAt( worldPoint.x, worldPoint.z );
    if ( terrainHeight >= worldForces.fluidSurfaceHeight - TOLERANCE )
    {
        return 0.0f;
    }

    const float clearanceAboveTerrain = worldPoint.y - terrainHeight;
    if ( clearanceAboveTerrain <= TOLERANCE )
    {
        return 0.0f;
    }

    return (std::clamp)( clearanceAboveTerrain / (std::max)( sampleBand, TOLERANCE ), 0.0f, 1.0f );
}

PhysicsBuoyancySample CalculateBuoyancySample( const PhysicsBodyRecord& record,
                                               const ColliderRecord& collider,
                                               const PhysicsWorldForces& worldForces )
{
    const Vector3 bodyPosition = record.position;
    auto orientation = record.orientation;
    const RotationMatrix rotMat = orientation.GetOrientationMatrix();
    PhysicsBuoyancySample sample;
    sample.centerOfBuoyancy = bodyPosition;

    auto addWetPoint = [&sample]( const Vector3& point, float weight, Vector3& weightedSum, float& wetWeight )
    {
        weight = (std::clamp)( weight, 0.0f, 1.0f );
        if ( weight <= TOLERANCE )
        {
            return;
        }

        weightedSum += point * weight;
        wetWeight += weight;
        if ( sample.wetPointCount < PhysicsBuoyancySample::MAX_WET_POINTS )
        {
            sample.wetPoints[sample.wetPointCount] = point;
            sample.wetWeights[sample.wetPointCount] = weight;
            ++sample.wetPointCount;
        }
    };

    auto finishWeightedSample = [&sample]( const Vector3& weightedSum, float wetWeight, const Vector3& fallback )
    {
        sample.wetWeightTotal = wetWeight;
        if ( wetWeight <= TOLERANCE )
        {
            sample.centerOfBuoyancy = fallback;
            sample.submergedVolumePercent = 0.0f;
            return;
        }

        sample.centerOfBuoyancy = weightedSum / wetWeight;
        sample.submergedVolumePercent = (std::clamp)( wetWeight / 27.0f, 0.0f, 1.0f );
    };

    auto sampleOrientedBoxVolume = [&]( const Vector3& localCenter, const Vector3& halfExtents )
    {
        const Vector3 center = bodyPosition + ( rotMat * localCenter );
        const float verticalExtent = rotMat.SupportExtentY( halfExtents );
        sample.centerOfBuoyancy = center;

        if ( verticalExtent <= TOLERANCE || worldForces.fluidSurfaceHeight <= center.y - verticalExtent )
        {
            return;
        }

        static constexpr float SAMPLE_COORDS[3] = { -0.6666667f, 0.0f, 0.6666667f };
        const float sampleBand = (std::max)( 0.25f, verticalExtent * 0.5f );
        Vector3 weightedSum = ZERO_VECTOR;
        float wetWeight = 0.0f;
        for ( float sx : SAMPLE_COORDS )
        {
            for ( float sy : SAMPLE_COORDS )
            {
                for ( float sz : SAMPLE_COORDS )
                {
                    const Vector3 local =
                        localCenter + Vector3( halfExtents.x * sx, halfExtents.y * sy, halfExtents.z * sz );
                    const Vector3 worldPoint = bodyPosition + ( rotMat * local );
                    const float depth = worldForces.fluidSurfaceHeight - worldPoint.y;
                    const float waterWetness = worldForces.fluidSurfaceHeight >= center.y + verticalExtent
                                                   ? 1.0f
                                                   : (std::clamp)( 0.5f + depth / sampleBand, 0.0f, 1.0f );
                    const float wetness =
                        waterWetness * TerrainWaterScale( record.terrain, worldForces, worldPoint, sampleBand );
                    addWetPoint( worldPoint, wetness, weightedSum, wetWeight );
                }
            }
        }

        finishWeightedSample( weightedSum, wetWeight, center );
    };

    std::visit(
        [&]( const auto& shape )
        {
            using ShapeT = std::decay_t<decltype( shape )>;
            if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
            {
                const Vector3 center = bodyPosition + ( rotMat * shape.GetPosition() );
                sample.centerOfBuoyancy = center;
                const float radius = shape.GetRadius();
                const float fluidHeightRelativeToCenter = worldForces.fluidSurfaceHeight - center.y;

                if ( fluidHeightRelativeToCenter <= -radius )
                {
                    sample.submergedVolumePercent = 0.0f;
                    return;
                }
                if ( fluidHeightRelativeToCenter >= radius )
                {
                    sample.submergedVolumePercent = 1.0f;
                    return;
                }

                const float yValue = fluidHeightRelativeToCenter + radius;
                sample.submergedVolumePercent =
                    (std::clamp)( ( ONE_OVER_THREE * _PI * ( ( 3.0f * radius ) - yValue ) * yValue * yValue ) /
                                      shape.GetVolume(),
                                  0.0f,
                                  1.0f );
            }
            else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
            {
                sampleOrientedBoxVolume( shape.GetPosition(), shape.GetHalfExtents() );
            }
            else
            {
                sampleOrientedBoxVolume( shape.GetPosition(), shape.GetInertiaHalfExtents() );
            }
        },
        collider.shape );

    return sample;
}

float CalculateTerrainSupportFactor( const PhysicsBodyRecord& record,
                                     const ColliderRecord& collider,
                                     const RotationMatrix& rotMat )
{
    if ( !record.terrain )
    {
        return 0.0f;
    }

    int closeSamples = 0;
    int terrainSamples = 0;
    const Vector3 position = record.position;
    const float supportGap = record.contactEpsilon + SkullbonezCore::Physics::BOX_TERRAIN_VERTEX_SUPPORT_SLACK;
    std::visit(
        [&]( const auto& shape )
        {
            using ShapeT = std::decay_t<decltype( shape )>;
            if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
            {
                (void)shape;
            }
            else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
            {
                const Vector3& halfExtents = shape.GetHalfExtents();
                for ( int corner = 0; corner < 8; ++corner )
                {
                    const Vector3 local =
                        shape.GetPosition() + SkullbonezCore::Physics::GetBoxTerrainLocalCorner( halfExtents, corner );
                    const Vector3 worldPoint = position + ( rotMat * local );
                    if ( !record.terrain->IsInBounds( worldPoint.x, worldPoint.z ) )
                    {
                        continue;
                    }

                    ++terrainSamples;
                    const float terrainHeight = record.terrain->GetTerrainHeightAt( worldPoint.x, worldPoint.z );
                    if ( worldPoint.y - terrainHeight <= supportGap )
                    {
                        ++closeSamples;
                    }
                }
            }
            else
            {
                const Vector3 hullCenter = position + ( rotMat * shape.GetPosition() );
                const uint16_t vertexCount = shape.GetVertexCount();
                for ( uint16_t vertex = 0; vertex < vertexCount; ++vertex )
                {
                    const Vector3 worldPoint = hullCenter + ( rotMat * shape.GetVertex( vertex ) );
                    if ( !record.terrain->IsInBounds( worldPoint.x, worldPoint.z ) )
                    {
                        continue;
                    }

                    ++terrainSamples;
                    const float terrainHeight = record.terrain->GetTerrainHeightAt( worldPoint.x, worldPoint.z );
                    if ( worldPoint.y - terrainHeight <= supportGap )
                    {
                        ++closeSamples;
                    }
                }
            }
        },
        collider.shape );

    if ( terrainSamples <= 0 || closeSamples <= 0 )
    {
        return 0.0f;
    }
    return (std::clamp)( static_cast<float>( closeSamples ) / 3.0f, 0.0f, 1.0f );
}

Vector3 CalculateBuoyancyRightingTorque( const PhysicsBodyRecord& record,
                                         const ColliderRecord& collider,
                                         const PhysicsWorldForces& worldForces,
                                         float buoyancyForce,
                                         float submergedVolumePercent )
{
    if ( record.isFixed || collider.shapeKind == ColliderShapeKind::Sphere || buoyancyForce <= TOLERANCE ||
         submergedVolumePercent <= TOLERANCE )
    {
        return ZERO_VECTOR;
    }

    const Vector3& inertia = record.rotationalInertia;
    const float maxInertia = (std::max)( inertia.x, (std::max)( inertia.y, inertia.z ) );
    float minInertia = inertia.y;
    minInertia = (std::min)( minInertia, inertia.x );
    minInertia = (std::min)( minInertia, inertia.z );
    if ( maxInertia <= TOLERANCE )
    {
        return ZERO_VECTOR;
    }

    const float anisotropy = ( maxInertia - minInertia ) / maxInertia;
    if ( anisotropy < 0.08f )
    {
        return ZERO_VECTOR;
    }

    auto orientation = record.orientation;
    const RotationMatrix rotMat = orientation.GetOrientationMatrix();
    Vector3 stableHalfExtents( 1.0f, 1.0f, 1.0f );
    bool hasStableHalfExtents = false;
    std::visit(
        [&]( const auto& shape )
        {
            using ShapeT = std::decay_t<decltype( shape )>;
            if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
            {
                stableHalfExtents = shape.GetHalfExtents();
                hasStableHalfExtents = true;
            }
            else if constexpr ( std::is_same_v<ShapeT, ConvexHullShape> )
            {
                stableHalfExtents = shape.GetInertiaHalfExtents();
                hasStableHalfExtents = true;
            }
            else
            {
                (void)shape;
            }
        },
        collider.shape );
    if ( !hasStableHalfExtents )
    {
        return ZERO_VECTOR;
    }

    const float minThickness =
        (std::min)( stableHalfExtents.x, (std::min)( stableHalfExtents.y, stableHalfExtents.z ) );
    const float maxThickness =
        (std::max)( stableHalfExtents.x, (std::max)( stableHalfExtents.y, stableHalfExtents.z ) );
    if ( minThickness <= TOLERANCE || maxThickness <= TOLERANCE )
    {
        return ZERO_VECTOR;
    }

    Vector3 stableWorldAxis = ZERO_VECTOR;
    float bestAxisScore = -1.0f;
    const Vector3 localAxes[3] = {
        Vector3( 1.0f, 0.0f, 0.0f ),
        Vector3( 0.0f, 1.0f, 0.0f ),
        Vector3( 0.0f, 0.0f, 1.0f ),
    };
    const float localInertia[3] = { inertia.x, inertia.y, inertia.z };
    const float localThickness[3] = { stableHalfExtents.x, stableHalfExtents.y, stableHalfExtents.z };
    for ( int axisIndex = 0; axisIndex < 3; ++axisIndex )
    {
        const float candidateInertia = localInertia[axisIndex];
        const float candidateThickness = localThickness[axisIndex];
        if ( candidateThickness > minThickness * 1.20f )
        {
            continue;
        }

        Vector3 candidateWorldAxis = rotMat * localAxes[axisIndex];
        const float axisLengthSq = VectorMagSquared( candidateWorldAxis );
        if ( axisLengthSq <= TOLERANCE * TOLERANCE )
        {
            continue;
        }

        candidateWorldAxis /= sqrtf( axisLengthSq );
        if ( candidateWorldAxis.y < 0.0f )
        {
            candidateWorldAxis = -candidateWorldAxis;
        }

        const float verticalAlignment = (std::clamp)( candidateWorldAxis.y, 0.0f, 1.0f );
        const float thicknessPreference = minThickness / (std::max)( candidateThickness, TOLERANCE );
        const float inertiaPreference = candidateInertia / maxInertia;
        const float score = verticalAlignment + thicknessPreference * 0.20f + inertiaPreference * 0.03f;
        if ( score > bestAxisScore )
        {
            bestAxisScore = score;
            stableWorldAxis = candidateWorldAxis;
        }
    }

    if ( bestAxisScore < 0.0f )
    {
        return ZERO_VECTOR;
    }

    const Vector3 worldUp( 0.0f, 1.0f, 0.0f );
    Vector3 correctionAxis = CrossProduct( stableWorldAxis, worldUp );
    const float errorSq = VectorMagSquared( correctionAxis );
    if ( errorSq <= TOLERANCE * TOLERANCE )
    {
        return ZERO_VECTOR;
    }

    const float error = sqrtf( errorSq );
    correctionAxis /= error;

    const float gravityMagnitude = fabsf( worldForces.gravity );
    const float weight = record.mass * gravityMagnitude;
    const float cappedLift = (std::min)( buoyancyForce, weight * 6.0f );
    const float waterCoupling = sqrtf( (std::clamp)( submergedVolumePercent, 0.0f, 1.0f ) );
    const float supportBlend = 1.0f - CalculateTerrainSupportFactor( record, collider, rotMat ) * 0.85f;
    const float torqueMagnitude =
        cappedLift * record.boundingRadius * anisotropy * waterCoupling * supportBlend * error;
    return correctionAxis * torqueMagnitude;
}

void ApplyWorldImpulse( PhysicsBodyRecord& record, const Vector3& worldImpulse, const Vector3& worldTorqueImpulse )
{
    record.linearVelocity += worldImpulse / record.mass;

    const RotationMatrix orientation = record.orientation.GetOrientationMatrix();
    const Vector3 localAngularImpulse = orientation.TransposeMultiply( worldTorqueImpulse ) / record.rotationalInertia;
    record.angularVelocity += orientation * localAngularImpulse;
}

void ApplyPendingImpulse( PhysicsBodyRecord& record )
{
    if ( !record.hasPendingImpulse )
    {
        return;
    }

    record.linearVelocity += record.pendingImpulse / record.mass;
    const Vector3 torque = CrossProduct( record.pendingImpulseApplicationPoint, record.pendingImpulse );
    record.angularVelocity += torque / record.rotationalInertia;
    record.pendingImpulse = ZERO_VECTOR;
    record.pendingImpulseApplicationPoint = ZERO_VECTOR;
    record.hasPendingImpulse = false;
}

// Concept: this is the store-owned successor to GameModel::ApplyForces.
//
// The record carries mutable body state, the collider carries exact shape data,
// and PhysicsWorldForces carries scene-wide fluid/gravity scalars. Keeping all
// force math here prevents hot physics paths from borrowing GameModel only to
// mutate velocities.
void ApplyWorldForces( PhysicsBodyRecord& record,
                       const ColliderRecord& collider,
                       const PhysicsWorldForces& worldForces,
                       float deltaSeconds )
{
    Vector3 worldForce = ZERO_VECTOR;
    Vector3 worldTorque = ZERO_VECTOR;

    const PhysicsBuoyancySample buoyancySample = CalculateBuoyancySample( record, collider, worldForces );
    const float submergedVolumePercent = buoyancySample.submergedVolumePercent;

    worldForce.y += CalculateGravityForce( worldForces, record.mass );

    const float buoyancyForce = CalculateBuoyancyForce( worldForces, record.volume * submergedVolumePercent );
    const Vector3 buoyancyForceVector( 0.0f, buoyancyForce, 0.0f );
    const Vector3 buoyancyArm = buoyancySample.centerOfBuoyancy - record.position;
    worldForce += buoyancyForceVector;
    worldTorque += CrossProduct( buoyancyArm, buoyancyForceVector );
    worldTorque +=
        CalculateBuoyancyRightingTorque( record, collider, worldForces, buoyancyForce, submergedVolumePercent );

    if ( deltaSeconds > TOLERANCE && buoyancyForce > TOLERANCE && submergedVolumePercent > TOLERANCE )
    {
        const float waterCoupling = sqrtf( (std::clamp)( submergedVolumePercent, 0.0f, 1.0f ) );
        const float weight = fabsf( worldForces.gravity ) * record.mass;
        const float maxDampingForce = (std::max)( fabsf( buoyancyForce ), weight ) * 3.0f;
        Vector3 linearDampingImpulse = record.linearVelocity * ( -record.mass * waterCoupling * 0.006f );
        linearDampingImpulse.y *= 1.5f;

        Vector3 linearDampingForce = linearDampingImpulse / deltaSeconds;
        linearDampingForce.x = (std::clamp)( linearDampingForce.x, -maxDampingForce, maxDampingForce );
        linearDampingForce.y = (std::clamp)( linearDampingForce.y, -maxDampingForce, maxDampingForce );
        linearDampingForce.z = (std::clamp)( linearDampingForce.z, -maxDampingForce, maxDampingForce );
        worldForce += linearDampingForce;

        const bool hasWetPoints = buoyancySample.wetPointCount > 0 && buoyancySample.wetWeightTotal > TOLERANCE;
        for ( uint8_t i = 0; hasWetPoints && i < buoyancySample.wetPointCount; ++i )
        {
            const float pointShare = buoyancySample.wetWeights[i] / buoyancySample.wetWeightTotal;
            if ( pointShare <= TOLERANCE )
            {
                continue;
            }

            const Vector3 arm = buoyancySample.wetPoints[i] - record.position;
            const Vector3 pointVelocity = CrossProduct( record.angularVelocity, arm );
            Vector3 dampingImpulse = pointVelocity * ( -record.mass * waterCoupling * pointShare * 0.035f );

            Vector3 dampingForce = dampingImpulse / deltaSeconds;
            const float pointForceLimit = maxDampingForce * pointShare;
            dampingForce.x = (std::clamp)( dampingForce.x, -pointForceLimit, pointForceLimit );
            dampingForce.y = (std::clamp)( dampingForce.y, -pointForceLimit, pointForceLimit );
            dampingForce.z = (std::clamp)( dampingForce.z, -pointForceLimit, pointForceLimit );
            if ( VectorMagSquared( dampingForce ) <= TOLERANCE * TOLERANCE )
            {
                continue;
            }

            worldTorque += CrossProduct( arm, dampingForce );
        }

        if ( collider.shapeKind == ColliderShapeKind::Sphere )
        {
            const float sphereSpinDampingRate = waterCoupling * worldForces.angularDragMultiplier * 0.35f;
            if ( sphereSpinDampingRate > TOLERANCE && !record.angularVelocity.IsCloseToZero() )
            {
                Vector3 sphereAngularDampingTorque(
                    -record.angularVelocity.x * record.rotationalInertia.x * sphereSpinDampingRate,
                    -record.angularVelocity.y * record.rotationalInertia.y * sphereSpinDampingRate,
                    -record.angularVelocity.z * record.rotationalInertia.z * sphereSpinDampingRate );
                sphereAngularDampingTorque.x = ClampAngularDragTorqueAxis( sphereAngularDampingTorque.x,
                                                                           record.angularVelocity.x,
                                                                           record.rotationalInertia.x,
                                                                           deltaSeconds );
                sphereAngularDampingTorque.y = ClampAngularDragTorqueAxis( sphereAngularDampingTorque.y,
                                                                           record.angularVelocity.y,
                                                                           record.rotationalInertia.y,
                                                                           deltaSeconds );
                sphereAngularDampingTorque.z = ClampAngularDragTorqueAxis( sphereAngularDampingTorque.z,
                                                                           record.angularVelocity.z,
                                                                           record.rotationalInertia.z,
                                                                           deltaSeconds );
                worldTorque += sphereAngularDampingTorque;
            }
        }
    }

    worldForce += CalculateViscousDrag( worldForces,
                                        record.linearVelocity,
                                        submergedVolumePercent,
                                        record.dragCoefficient,
                                        record.projectedSurfaceArea );

    if ( !record.angularVelocity.IsCloseToZero() )
    {
        const float radius = record.boundingRadius;
        const float avgDensity =
            ( worldForces.gasDensity * ( 1.0f - submergedVolumePercent ) ) +
            ( worldForces.fluidDensity * submergedVolumePercent * worldForces.angularDragMultiplier );
        const float angularDragCoeff = record.dragCoefficient * avgDensity * radius * radius * radius;
        Vector3 angularDragTorque = record.angularVelocity * ( -angularDragCoeff );

        angularDragTorque.x = ClampAngularDragTorqueAxis( angularDragTorque.x,
                                                          record.angularVelocity.x,
                                                          record.rotationalInertia.x,
                                                          deltaSeconds );
        angularDragTorque.y = ClampAngularDragTorqueAxis( angularDragTorque.y,
                                                          record.angularVelocity.y,
                                                          record.rotationalInertia.y,
                                                          deltaSeconds );
        angularDragTorque.z = ClampAngularDragTorqueAxis( angularDragTorque.z,
                                                          record.angularVelocity.z,
                                                          record.rotationalInertia.z,
                                                          deltaSeconds );
        worldTorque += angularDragTorque;
    }

    ApplyWorldImpulse( record, worldForce * deltaSeconds, worldTorque * deltaSeconds );
}

// Concept: store-owned pose integration is the data-record successor to
// RigidBody::UpdatePosition plus GameModel::ClampToTerrainSurface. Keeping this
// here means solver hot paths no longer round-trip through GameModel just to
// advance a position and orientation.
void IntegrateBodyRecordPose( PhysicsBodyRecord& record, float deltaSeconds )
{
    record.linearVelocity.Simplify();
    record.angularVelocity.Simplify();

    record.position += record.linearVelocity * deltaSeconds;

    const Vector3 omega = record.angularVelocity;
    const float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
    if ( omegaMag > 0.0001f )
    {
        const Vector3 axis( omega.x / omegaMag, omega.y / omegaMag, omega.z / omegaMag );
        record.orientation.RotateAboutAxis( axis, omegaMag * deltaSeconds );
    }
}

void CaptureMutableBodyState( GameModel& model, PhysicsBodyRecord& record )
{
    record.position = model.GetPosition();
    record.orientation = model.GetOrientation();
    record.linearVelocity = model.GetVelocity();
    record.angularVelocity = model.GetAngularVelocity();
    record.rotationalInertia = model.GetRotationalInertia();
    record.invRotationalInertia = model.GetInvertedRotationalInertia();
    record.mass = model.GetMass();
    record.invMass = model.GetInvertedMass();
    record.boundingRadius = model.GetBoundingRadius();
    record.volume = model.GetVolume();
    record.projectedSurfaceArea = model.GetProjectedSurfaceArea();
    record.dragCoefficient = model.GetDragCoefficient();
    // Why: buoyancy sampling is deliberately targeted. Ordinary body refreshes
    // clear this field, and underwater sleep probes refresh only the candidate.
    record.submergedVolumePercent = 0.0f;
    record.contactReleaseImpulseThreshold = model.GetContactReleaseImpulseThreshold();
    record.angularVelocityLimit = model.GetAngularVelocityLimit();
    record.contactEpsilon = model.GetContactEpsilon();
    record.terrain = model.GetTerrain();
    record.isFixed = model.IsFixed();
    record.usesWorldInertia = model.UsesWorldInertia();
    record.releasesFromFixedOnContact = model.ReleasesFromFixedOnContact();
}

void WriteRecordToCompatibilityModel( const PhysicsBodyRecord& record, GameModel& model )
{
    model.SetFixed( record.isFixed );
    model.SetPosition( record.position );
    model.SetOrientation( record.orientation );
    model.SetLinearVelocity( record.linearVelocity );
    model.SetAngularVelocity( record.angularVelocity );
    if ( record.hasPendingImpulse )
    {
        model.SetImpulseForce( record.pendingImpulse, record.pendingImpulseApplicationPoint );
    }
    else
    {
        model.ClearImpulseForce();
    }
}

} // namespace


PhysicsBodyStore::PhysicsBodyStore()
{
    m_bodies.reserve( MAX_GAME_MODELS );
    m_modelBodyHandles.reserve( MAX_GAME_MODELS );
    m_handleGenerations.reserve( MAX_GAME_MODELS );
    m_handleAlive.reserve( MAX_GAME_MODELS );
    m_handleModelIndices.reserve( MAX_GAME_MODELS );
    m_handleReplayBodyIds.reserve( MAX_GAME_MODELS );
    m_freeHandleSlots.reserve( MAX_GAME_MODELS );
}


// Concept: body handles identify allocator slots, not legacy model positions.
//
// Replay ids let the store preserve identity when a compatible scene refresh
// shifts a body to a different model slot. Retired slots bump generation before
// reuse so stale handles fail Contains/ModelIndexForHandle deterministically.
PhysicsBodyHandle PhysicsBodyStore::ResolveHandleForModelIndex( int modelIndex,
                                                                uint32_t replayBodyId,
                                                                std::vector<uint8_t>& assignedHandleSlots )
{
    auto assignSlot = [&]( uint32_t slot ) -> PhysicsBodyHandle
    {
        if ( slot >= assignedHandleSlots.size() )
        {
            assignedHandleSlots.resize( static_cast<std::size_t>( slot ) + 1u, 0 );
        }
        assignedHandleSlots[static_cast<std::size_t>( slot )] = 1;
        m_handleAlive[static_cast<std::size_t>( slot )] = 1;
        m_handleModelIndices[static_cast<std::size_t>( slot )] = modelIndex;
        m_handleReplayBodyIds[static_cast<std::size_t>( slot )] = replayBodyId;

        PhysicsBodyHandle handle;
        handle.index = slot;
        handle.generation = m_handleGenerations[static_cast<std::size_t>( slot )];
        return handle;
    };

    if ( modelIndex >= 0 && modelIndex < static_cast<int>( m_modelBodyHandles.size() ) )
    {
        const PhysicsBodyHandle previous = m_modelBodyHandles[static_cast<std::size_t>( modelIndex )];
        if ( previous.IsValid() && previous.index < m_handleGenerations.size() &&
             previous.generation == m_handleGenerations[static_cast<std::size_t>( previous.index )] &&
             m_handleAlive[static_cast<std::size_t>( previous.index )] != 0 &&
             m_handleReplayBodyIds[static_cast<std::size_t>( previous.index )] == replayBodyId &&
             ( previous.index >= assignedHandleSlots.size() ||
               assignedHandleSlots[static_cast<std::size_t>( previous.index )] == 0 ) )
        {
            return assignSlot( previous.index );
        }
    }

    if ( replayBodyId != 0 )
    {
        for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleReplayBodyIds.size() ); ++slot )
        {
            if ( m_handleAlive[slot] != 0 && m_handleReplayBodyIds[slot] == replayBodyId &&
                 slot < m_handleGenerations.size() &&
                 ( slot >= assignedHandleSlots.size() || assignedHandleSlots[slot] == 0 ) )
            {
                return assignSlot( slot );
            }
        }
    }

    uint32_t slot = 0;
    if ( !m_freeHandleSlots.empty() )
    {
        slot = m_freeHandleSlots.back();
        m_freeHandleSlots.pop_back();
    }
    else
    {
        slot = static_cast<uint32_t>( m_handleGenerations.size() );
        m_handleGenerations.push_back( PHYSICS_HANDLE_INITIAL_GENERATION );
        m_handleAlive.push_back( 0 );
        m_handleModelIndices.push_back( -1 );
        m_handleReplayBodyIds.push_back( 0 );
    }

    return assignSlot( slot );
}


void PhysicsBodyStore::RetireUnassignedHandles( const std::vector<uint8_t>& assignedHandleSlots )
{
    for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleAlive.size() ); ++slot )
    {
        if ( m_handleAlive[slot] == 0 )
        {
            continue;
        }
        if ( slot < assignedHandleSlots.size() && assignedHandleSlots[slot] != 0 )
        {
            continue;
        }

        m_handleAlive[slot] = 0;
        m_handleModelIndices[slot] = -1;
        m_handleReplayBodyIds[slot] = 0;
        m_handleGenerations[slot] = NextHandleGeneration( m_handleGenerations[slot] );
        m_freeHandleSlots.push_back( slot );
    }
}


void PhysicsBodyStore::Clear()
{
    m_bodies.clear();
    m_modelBodyHandles.clear();
    for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleAlive.size() ); ++slot )
    {
        if ( m_handleAlive[slot] != 0 )
        {
            m_handleGenerations[slot] = NextHandleGeneration( m_handleGenerations[slot] );
        }
        m_handleAlive[slot] = 0;
        m_handleModelIndices[slot] = -1;
        m_handleReplayBodyIds[slot] = 0;
    }
    m_freeHandleSlots.clear();
    for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleGenerations.size() ); ++slot )
    {
        m_freeHandleSlots.push_back( slot );
    }
}


void PhysicsBodyStore::Refresh( std::vector<GameModel>& models, const std::vector<uint8_t>& sleepStates )
{
    LoadFromModels( models, sleepStates );
}


void PhysicsBodyStore::LoadFromModels( std::vector<GameModel>& models, const std::vector<uint8_t>& sleepStates )
{
    const std::vector<PreservedRefreshState> preservedStateByHandle =
        CapturePreservedRefreshState( m_bodies, m_handleGenerations.size() );
    std::vector<uint8_t> assignedHandleSlots( m_handleGenerations.size(), 0 );
    m_bodies.resize( models.size() );
    m_modelBodyHandles.resize( models.size() );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        GameModel& model = models[i];
        PhysicsBodyRecord& record = m_bodies[i];
        const uint32_t replayBodyId = model.GetReplayBodyId();
        const PhysicsBodyHandle handle =
            ResolveHandleForModelIndex( static_cast<int>( i ), replayBodyId, assignedHandleSlots );
        // Why: refresh copies live compatibility state every frame, but a
        // pending tool impulse and sleep seed are physics-owned one-shot state.
        // Preserve them by handle slot so allocator-owned identity survives a
        // same-scene reorder instead of accidentally following the model slot.
        const PreservedRefreshState* preservedState = PreservedStateForHandle( preservedStateByHandle, handle );
        record.handle = handle;
        record.replayBodyId = replayBodyId;
        record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( record.replayBodyId );
        CaptureMutableBodyState( model, record );
        if ( preservedState && preservedState->hasPendingImpulse )
        {
            record.pendingImpulse = preservedState->pendingImpulse;
            record.pendingImpulseApplicationPoint = preservedState->pendingImpulseApplicationPoint;
            record.hasPendingImpulse = true;
        }
        else
        {
            record.pendingImpulse = ZERO_VECTOR;
            record.pendingImpulseApplicationPoint = ZERO_VECTOR;
            record.hasPendingImpulse = false;
        }
        record.isSleeping =
            ( preservedState && preservedState->isSleeping ) || ( i < sleepStates.size() && sleepStates[i] != 0 );
        m_modelBodyHandles[i] = record.handle;
    }
    RetireUnassignedHandles( assignedHandleSlots );
}


void PhysicsBodyStore::LoadFromModels( PhysicsModelMutableRange models, const std::vector<uint8_t>& sleepStates )
{
    const int modelCount = models.Count();
    const std::vector<PreservedRefreshState> preservedStateByHandle =
        CapturePreservedRefreshState( m_bodies, m_handleGenerations.size() );
    std::vector<uint8_t> assignedHandleSlots( m_handleGenerations.size(), 0 );
    m_bodies.resize( static_cast<std::size_t>( modelCount ) );
    m_modelBodyHandles.resize( static_cast<std::size_t>( modelCount ) );
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models[static_cast<std::size_t>( i )];
        PhysicsBodyRecord& record = m_bodies[static_cast<std::size_t>( i )];
        const uint32_t replayBodyId = model.GetReplayBodyId();
        const PhysicsBodyHandle handle = ResolveHandleForModelIndex( i, replayBodyId, assignedHandleSlots );
        const PreservedRefreshState* preservedState = PreservedStateForHandle( preservedStateByHandle, handle );
        record.handle = handle;
        record.replayBodyId = replayBodyId;
        record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( record.replayBodyId );
        CaptureMutableBodyState( model, record );
        if ( preservedState && preservedState->hasPendingImpulse )
        {
            record.pendingImpulse = preservedState->pendingImpulse;
            record.pendingImpulseApplicationPoint = preservedState->pendingImpulseApplicationPoint;
            record.hasPendingImpulse = true;
        }
        else
        {
            record.pendingImpulse = ZERO_VECTOR;
            record.pendingImpulseApplicationPoint = ZERO_VECTOR;
            record.hasPendingImpulse = false;
        }
        record.isSleeping =
            ( preservedState && preservedState->isSleeping ) ||
            ( i < static_cast<int>( sleepStates.size() ) && sleepStates[static_cast<std::size_t>( i )] != 0 );
        m_modelBodyHandles[static_cast<std::size_t>( i )] = record.handle;
    }
    RetireUnassignedHandles( assignedHandleSlots );
}


void PhysicsBodyStore::ClearPendingImpulses()
{
    for ( PhysicsBodyRecord& record : m_bodies )
    {
        record.pendingImpulse = ZERO_VECTOR;
        record.pendingImpulseApplicationPoint = ZERO_VECTOR;
        record.hasPendingImpulse = false;
    }
}


void PhysicsBodyStore::WriteBackToModels( std::vector<GameModel>& models ) const
{
    const int modelCount = (std::min)( static_cast<int>( models.size() ), Count() );
    for ( int i = 0; i < modelCount; ++i )
    {
        WriteBackToModelAt( models, i );
    }
}


void PhysicsBodyStore::WriteBackToModels( PhysicsModelMutableRange models ) const
{
    const int modelCount = (std::min)( models.Count(), Count() );
    for ( int i = 0; i < modelCount; ++i )
    {
        WriteBackToModelAt( models, i );
    }
}


void PhysicsBodyStore::WriteBackToModelAt( std::vector<GameModel>& models, int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) ||
         modelIndex >= static_cast<int>( m_bodies.size() ) )
    {
        return;
    }

    WriteRecordToCompatibilityModel( m_bodies[static_cast<std::size_t>( modelIndex )],
                                     models[static_cast<std::size_t>( modelIndex )] );
}


void PhysicsBodyStore::WriteBackToModelAt( PhysicsModelMutableRange models, int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= models.Count() || modelIndex >= static_cast<int>( m_bodies.size() ) )
    {
        return;
    }

    WriteRecordToCompatibilityModel( m_bodies[static_cast<std::size_t>( modelIndex )],
                                     models[static_cast<std::size_t>( modelIndex )] );
}


void PhysicsBodyStore::CaptureMutableStateFromModelAt( std::vector<GameModel>& models, int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) )
    {
        return;
    }

    CaptureMutableBodyState( models[static_cast<std::size_t>( modelIndex )], *record );
}


void PhysicsBodyStore::CaptureMutableStateFromModelAt( PhysicsModelMutableRange models, int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 || modelIndex >= models.Count() )
    {
        return;
    }

    CaptureMutableBodyState( models[static_cast<std::size_t>( modelIndex )], *record );
}


void PhysicsBodyStore::CopySleepStatesFrom( const std::vector<uint8_t>& sleepStates )
{
    const int bodyCount = Count();
    for ( int i = 0; i < bodyCount; ++i )
    {
        m_bodies[static_cast<std::size_t>( i )].isSleeping =
            i < static_cast<int>( sleepStates.size() ) && sleepStates[static_cast<std::size_t>( i )] != 0;
    }
}


void PhysicsBodyStore::CopySleepStatesTo( std::vector<uint8_t>& sleepStates ) const
{
    sleepStates.resize( m_bodies.size(), 0 );
    for ( std::size_t i = 0; i < m_bodies.size(); ++i )
    {
        sleepStates[i] = m_bodies[i].isSleeping ? 1 : 0;
    }
}


const PhysicsBodyRecord* PhysicsBodyStore::Data() const
{
    return m_bodies.empty() ? nullptr : m_bodies.data();
}


int PhysicsBodyStore::Count() const
{
    return static_cast<int>( m_bodies.size() );
}


bool PhysicsBodyStore::Empty() const
{
    return m_bodies.empty();
}


PhysicsBodyHandle PhysicsBodyStore::HandleForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_modelBodyHandles.size() ) )
    {
        return PhysicsBodyHandle{};
    }

    return m_modelBodyHandles[static_cast<std::size_t>( modelIndex )];
}


int PhysicsBodyStore::ModelIndexForHandle( PhysicsBodyHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return -1;
    }

    return m_handleModelIndices[static_cast<std::size_t>( handle.index )];
}


bool PhysicsBodyStore::Contains( PhysicsBodyHandle handle ) const
{
    if ( !handle.IsValid() || handle.index >= m_handleGenerations.size() )
    {
        return false;
    }
    const std::size_t slot = static_cast<std::size_t>( handle.index );
    if ( m_handleAlive[slot] == 0 || m_handleGenerations[slot] != handle.generation )
    {
        return false;
    }

    const int modelIndex = m_handleModelIndices[slot];
    return modelIndex >= 0 && modelIndex < static_cast<int>( m_bodies.size() ) &&
           m_bodies[static_cast<std::size_t>( modelIndex )].handle == handle;
}


const std::vector<PhysicsBodyRecord>& PhysicsBodyStore::Records() const
{
    return m_bodies;
}


std::vector<PhysicsBodyRecord>& PhysicsBodyStore::MutableRecords()
{
    return m_bodies;
}


PhysicsBodyRecord* PhysicsBodyStore::MutableRecordForModelIndex( int modelIndex )
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_bodies.size() ) )
    {
        return nullptr;
    }

    return &m_bodies[static_cast<std::size_t>( modelIndex )];
}


const PhysicsBodyRecord* PhysicsBodyStore::RecordForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_bodies.size() ) )
    {
        return nullptr;
    }

    return &m_bodies[static_cast<std::size_t>( modelIndex )];
}


bool PhysicsBodyStore::WakeBody( int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || record->isFixed )
    {
        return false;
    }

    record->isSleeping = false;
    return true;
}


bool PhysicsBodyStore::SeedBodyAsleep( int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || record->isFixed )
    {
        return false;
    }

    record->linearVelocity = ZERO_VECTOR;
    record->angularVelocity = ZERO_VECTOR;
    record->isSleeping = true;
    return true;
}


bool PhysicsBodyStore::SetPendingBodyImpulse( int modelIndex,
                                              const Vector3& impulse,
                                              const Vector3& localApplicationPoint )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record )
    {
        return false;
    }

    record->pendingImpulse = impulse;
    record->pendingImpulseApplicationPoint = localApplicationPoint;
    record->hasPendingImpulse = true;
    return true;
}


bool PhysicsBodyStore::ApplyBodyImpulse( int modelIndex, const Vector3& impulse, const Vector3& localApplicationPoint )
{
    const bool pending = SetPendingBodyImpulse( modelIndex, impulse, localApplicationPoint );
    WakeBody( modelIndex );
    return pending;
}


bool PhysicsBodyStore::IntegrateBodyPose( const ColliderStore& colliderStore, int modelIndex, float deltaSeconds )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    const ColliderRecord* collider = ColliderRecordForModelIndex( colliderStore, modelIndex );
    if ( !record || !collider || record->isFixed || record->isSleeping || deltaSeconds <= 0.0f )
    {
        return false;
    }

    IntegrateBodyRecordPose( *record, deltaSeconds );
    ClampBodyToTerrainSurface( *record, *collider );
    // Why: this value is a targeted underwater-sleep probe, not general body
    // state. Any pose integration invalidates the previous water sample.
    record->submergedVolumePercent = 0.0f;
    return true;
}


bool PhysicsBodyStore::ApplyForces( const PhysicsWorldForces& worldForces,
                                    const ColliderStore& colliderStore,
                                    int modelIndex,
                                    float deltaSeconds )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    const ColliderRecord* collider = ColliderRecordForModelIndex( colliderStore, modelIndex );
    if ( !record || !collider )
    {
        return false;
    }
    if ( record->isFixed )
    {
        record->linearVelocity = ZERO_VECTOR;
        record->angularVelocity = ZERO_VECTOR;
        return false;
    }

    ThrottleAngularVelocity( *record );
    ApplyWorldForces( *record, *collider, worldForces, deltaSeconds );
    ApplyPendingImpulse( *record );
    return true;
}
