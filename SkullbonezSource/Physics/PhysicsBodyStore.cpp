/*
File: SkullbonezSource/Physics/PhysicsBodyStore.cpp
Purpose:
  Owns deterministic body-order mutable physics state for PhysicsEngine.

Summary:
  Descriptor reload copies cold metadata and initial hot state into separate
  store arrays at authoring boundaries. PhysicsEngine creation appends dense
  rows directly. PhysicsWorld mutates the aligned hot-field arrays through
  narrow borrowed views.

Glossary:
  Body: Simulated object state such as position, orientation, velocity, mass,
    and sleep flag.
  Sleep: Optimization that stops simulating stable bodies until something wakes
    them.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.
  Scene object id: Stable per-scene id used by replay and SkullScope traces.
  Model row hint: Caller-owned dense-row cache that can be stale after deletion
    compacts the store; resolver APIs repair or invalidate it.

Invariants:
  - Runtime cold records and hot arrays stay in scene/model slot order
    for current solver traversal, but public body handles are allocator-owned
    slots.
  - PhysicsEngine body rows are dense and handle-addressed; deletion may move
    the last row to close a hole without changing live handles.
  - Pending impulses and sleep state are preserved across descriptor refresh
    by handle identity, even if a descriptor refresh reorders slots.
  - Impulse application is all-or-nothing per linear/angular component; invalid
    zero mass or inertia absorbs that component rather than publishing NaNs.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
*/
#include "PhysicsBodyStore.h"
#include "BuoyancySystem.h"
#include "ColliderStore.h"
#include "PhysicsApi.h"
#include "TerrainSupportClassifier.h"
#include "PhysicsWorldForces.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include "../Core/Common.h"
#include "../Core/FatalError.h"
#include "../Core/Profiler.h"
using SkullbonezCore::Geometry::Plane;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius;
using SkullbonezCore::Math::CollisionDetection::GetShapeTerrainBottomOffset;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::CrossProduct;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMagSquared;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Physics::BuoyancyBodyFacts;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PHYSICS_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyHotFieldsConstView;
using SkullbonezCore::Physics::PhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyRecordList;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsFixedList;
using SkullbonezCore::Physics::PhysicsHandleAssignmentMask;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsTerrainView;
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

using PreservedRefreshStateList = PhysicsFixedList<PreservedRefreshState,
                                                   SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;

float PositiveInverseOrZero( float value )
{
    return value > 0.000001f ? 1.0f / value : 0.0f;
}

Vector3 PositiveComponentInverseOrZero( const Vector3& value )
{
    return Vector3( PositiveInverseOrZero( value.x ),
                    PositiveInverseOrZero( value.y ),
                    PositiveInverseOrZero( value.z ) );
}

const ColliderRecord* ColliderRecordForModelIndex( const ColliderStore& colliderStore, int modelIndex )
{
    const auto colliders = colliderStore.Records();
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
bool FindClosestBoxTerrainVertex( SkullbonezCore::Core::Profiler* profiler,
                                  const PhysicsTerrainView& terrain,
                                  const PhysicsBodyHotState& hot,
                                  const BoundingBox& box,
                                  Vector3& outVertex,
                                  float& outTerrainHeight,
                                  Plane& outPlane,
                                  float& outGap )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/BoxClosestVertexProbe" );

    if ( !terrain.IsValid() )
    {
        return false;
    }

    const Vector3& he = box.GetHalfExtents();
    auto orientation = hot.orientation;
    const RotationMatrix rotMat = orientation.GetOrientationMatrix();

    bool found = false;
    float bestGap = 1.0e30f;
    for ( int v = 0; v < 8; ++v )
    {
        const Vector3 local( ( v & 1 ) ? he.x : -he.x, ( v & 2 ) ? he.y : -he.y, ( v & 4 ) ? he.z : -he.z );
        const Vector3 worldVertex = hot.position + ( rotMat * local );

        if ( !terrain.IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        float terrainHeight = 0.0f;
        Plane terrainPlane;
        terrain.HeightAndPlaneAt( worldVertex.x, worldVertex.z, terrainHeight, terrainPlane );
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

bool FindClosestHullTerrainVertex( SkullbonezCore::Core::Profiler* profiler,
                                   const PhysicsTerrainView& terrain,
                                   const PhysicsBodyHotState& hot,
                                   const ConvexHullShape& hull,
                                   Vector3& outVertex,
                                   float& outTerrainHeight,
                                   Plane& outPlane,
                                   float& outGap )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/HullClosestVertexProbe" );

    if ( !terrain.IsValid() )
    {
        return false;
    }

    auto orientation = hot.orientation;
    const RotationMatrix rotMat = orientation.GetOrientationMatrix();
    const Vector3 hullCenter = hot.position + ( rotMat * hull.GetPosition() );

    bool found = false;
    float bestGap = 1.0e30f;
    const uint16_t vertexCount = hull.GetVertexCount();
    for ( uint16_t v = 0; v < vertexCount; ++v )
    {
        const Vector3 worldVertex = hullCenter + ( rotMat * hull.GetVertex( v ) );

        if ( !terrain.IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        float terrainHeight = 0.0f;
        Plane terrainPlane;
        terrain.HeightAndPlaneAt( worldVertex.x, worldVertex.z, terrainHeight, terrainPlane );
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

void ClampBodyToTerrainSurface( SkullbonezCore::Core::Profiler* profiler,
                                const PhysicsTerrainView& terrain,
                                PhysicsBodyHotState& hot,
                                const ColliderRecord& collider )
{
    if ( !terrain.IsValid() )
    {
        return;
    }

    if ( !terrain.IsInBounds( hot.position.x, hot.position.z ) )
    {
        return;
    }

    if ( std::holds_alternative<BoundingBox>( collider.shape ) )
    {
        Vector3 closestVertex;
        float terrainHeight = 0.0f;
        Plane terrainPlane;
        float gap = 0.0f;
        if ( FindClosestBoxTerrainVertex( profiler,
                                          terrain,
                                          hot,
                                          std::get<BoundingBox>( collider.shape ),
                                          closestVertex,
                                          terrainHeight,
                                          terrainPlane,
                                          gap ) &&
             gap < 0.0f )
        {
            hot.position.y -= gap;
        }

        return;
    }

    if ( std::holds_alternative<ConvexHullShape>( collider.shape ) )
    {
        Vector3 closestVertex;
        float terrainHeight = 0.0f;
        Plane terrainPlane;
        float gap = 0.0f;
        if ( FindClosestHullTerrainVertex( profiler,
                                           terrain,
                                           hot,
                                           std::get<ConvexHullShape>( collider.shape ),
                                           closestVertex,
                                           terrainHeight,
                                           terrainPlane,
                                           gap ) &&
             gap < 0.0f )
        {
            hot.position.y -= gap;
        }

        return;
    }

    const float bottomOffset = GetShapeTerrainBottomOffset( collider.shape );
    const float terrainHeight = terrain.HeightAt( hot.position.x, hot.position.z );
    if ( hot.position.y - bottomOffset < terrainHeight )
    {
        hot.position.y = terrainHeight + bottomOffset;
    }
}

uint32_t NextHandleGeneration( uint32_t generation )
{
    ++generation;
    return generation == 0u ? PHYSICS_HANDLE_INITIAL_GENERATION : generation;
}

PreservedRefreshStateList CapturePreservedRefreshState( const PhysicsBodyRecordList& bodies,
                                                        const PhysicsBodyHotFieldsConstView& hotFields,
                                                        std::size_t handleSlotCount )
{
    PreservedRefreshStateList preserved( "PhysicsBodyStore.preservedRefreshStateByHandle" );
    preserved.resize( handleSlotCount );
    for ( std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex )
    {
        const PhysicsBodyRecord& record = bodies[bodyIndex];
        if ( !record.handle.IsValid() || record.handle.index >= preserved.size() )
        {
            continue;
        }

        PreservedRefreshState& state = preserved[static_cast<std::size_t>( record.handle.index )];
        state.pendingImpulse = record.pendingImpulse;
        state.pendingImpulseApplicationPoint = record.pendingImpulseApplicationPoint;
        state.hasPendingImpulse = record.hasPendingImpulse;
        state.isSleeping = hotFields.awake[bodyIndex] == 0u;
        state.hasState = true;
    }

    return preserved;
}

const PreservedRefreshState* PreservedStateForHandle( const PreservedRefreshStateList& preserved,
                                                      PhysicsBodyHandle handle )
{
    if ( !handle.IsValid() || handle.index >= preserved.size() )
    {
        return nullptr;
    }

    const PreservedRefreshState& state = preserved[static_cast<std::size_t>( handle.index )];
    return state.hasState ? &state : nullptr;
}

void ThrottleAngularVelocity( const PhysicsBodyRecord& record, PhysicsBodyHotState& hot )
{
    const float magSq = hot.angularVelocity.x * hot.angularVelocity.x + hot.angularVelocity.y * hot.angularVelocity.y +
                        hot.angularVelocity.z * hot.angularVelocity.z;

    const float limitSq = record.angularVelocityLimit * record.angularVelocityLimit;
    if ( magSq > limitSq )
    {
        const float scale = record.angularVelocityLimit / sqrtf( magSq );
        hot.angularVelocity.x *= scale;
        hot.angularVelocity.y *= scale;
        hot.angularVelocity.z *= scale;
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

float TerrainWaterScale( const PhysicsTerrainView& terrain,
                         const PhysicsWorldForces& worldForces,
                         const Vector3& worldPoint,
                         float sampleBand )
{
    if ( !terrain.IsValid() || !terrain.IsInBounds( worldPoint.x, worldPoint.z ) )
    {
        return 1.0f;
    }

    const float terrainHeight = terrain.HeightAt( worldPoint.x, worldPoint.z );
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

PhysicsBuoyancySample CalculateBuoyancySample( const PhysicsBodyHotState& hot,
                                               const ColliderRecord& collider,
                                               const PhysicsTerrainView& terrain,
                                               const PhysicsWorldForces& worldForces )
{
    const Vector3 bodyPosition = hot.position;
    auto orientation = hot.orientation;
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
                    const Vector3 local = localCenter +
                                          Vector3( halfExtents.x * sx, halfExtents.y * sy, halfExtents.z * sz );

                    const Vector3 worldPoint = bodyPosition + ( rotMat * local );
                    const float depth = worldForces.fluidSurfaceHeight - worldPoint.y;
                    const float waterWetness = worldForces.fluidSurfaceHeight >= center.y + verticalExtent
                                                   ? 1.0f
                                                   : (std::clamp)( 0.5f + depth / sampleBand, 0.0f, 1.0f );

                    const float wetness = waterWetness *
                                          TerrainWaterScale( terrain, worldForces, worldPoint, sampleBand );

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
                sample.submergedVolumePercent = (std::clamp)( ( ONE_OVER_THREE * _PI * ( ( 3.0f * radius ) - yValue ) *
                                                                yValue * yValue ) /
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

float CalculateTerrainSupportFactor( const BuoyancyBodyFacts& buoyancyFacts,
                                     const PhysicsBodyHotState& hot,
                                     const ColliderRecord& collider,
                                     const PhysicsTerrainView& terrain,
                                     const RotationMatrix& rotMat )
{
    if ( !terrain.IsValid() )
    {
        return 0.0f;
    }

    int closeSamples = 0;
    int terrainSamples = 0;
    const Vector3 position = hot.position;
    const float supportGap = buoyancyFacts.contactEpsilon + SkullbonezCore::Physics::BOX_TERRAIN_VERTEX_SUPPORT_SLACK;
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
                    const Vector3 local = shape.GetPosition() +
                                          SkullbonezCore::Physics::GetBoxTerrainLocalCorner( halfExtents, corner );

                    const Vector3 worldPoint = position + ( rotMat * local );
                    if ( !terrain.IsInBounds( worldPoint.x, worldPoint.z ) )
                    {
                        continue;
                    }

                    ++terrainSamples;
                    const float terrainHeight = terrain.HeightAt( worldPoint.x, worldPoint.z );
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
                    if ( !terrain.IsInBounds( worldPoint.x, worldPoint.z ) )
                    {
                        continue;
                    }

                    ++terrainSamples;
                    const float terrainHeight = terrain.HeightAt( worldPoint.x, worldPoint.z );
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
                                         const BuoyancyBodyFacts& buoyancyFacts,
                                         const PhysicsBodyHotState& hot,
                                         const ColliderRecord& collider,
                                         const PhysicsTerrainView& terrain,
                                         const PhysicsWorldForces& worldForces,
                                         float buoyancyForce,
                                         float submergedVolumePercent )
{
    if ( hot.fixed || collider.shapeKind == ColliderShapeKind::Sphere || buoyancyForce <= TOLERANCE ||
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

    auto orientation = hot.orientation;
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

    const float minThickness = (std::min)( stableHalfExtents.x,
                                           (std::min)( stableHalfExtents.y, stableHalfExtents.z ) );

    const float maxThickness = (std::max)( stableHalfExtents.x,
                                           (std::max)( stableHalfExtents.y, stableHalfExtents.z ) );

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
    const float supportBlend = 1.0f -
                               CalculateTerrainSupportFactor( buoyancyFacts, hot, collider, terrain, rotMat ) * 0.85f;

    const float torqueMagnitude = cappedLift * hot.boundingRadius * anisotropy * waterCoupling * supportBlend * error;
    return correctionAxis * torqueMagnitude;
}

void ApplyWorldImpulse( const PhysicsBodyRecord& record,
                        PhysicsBodyHotState& hot,
                        const Vector3& worldImpulse,
                        const Vector3& worldTorqueImpulse )
{
    // Why: malformed zero mass/inertia is caller-reachable authored data, not
    // lane F. On failure the invalid component absorbs the impulse, and no
    // partial velocity write escapes.
    Vector3 linearImpulseDelta;
    if ( worldImpulse.TryDivided( record.mass, linearImpulseDelta ) )
    {
        hot.linearVelocity += linearImpulseDelta;
    }

    const RotationMatrix orientation = hot.orientation.GetOrientationMatrix();
    Vector3 localAngularImpulse;
    if ( orientation.TransposeMultiply( worldTorqueImpulse )
             .TryDivided( record.rotationalInertia, localAngularImpulse ) )
    {
        hot.angularVelocity += orientation * localAngularImpulse;
    }
}

void ApplyPendingImpulse( PhysicsBodyRecord& record, PhysicsBodyHotState& hot )
{
    if ( !record.hasPendingImpulse )
    {
        return;
    }

    // Why: pending impulses use the same all-or-nothing fallback as immediate
    // impulses; zero mass or inertia consumes that invalid component.
    Vector3 linearImpulseDelta;
    if ( record.pendingImpulse.TryDivided( record.mass, linearImpulseDelta ) )
    {
        hot.linearVelocity += linearImpulseDelta;
    }

    const Vector3 torque = CrossProduct( record.pendingImpulseApplicationPoint, record.pendingImpulse );
    Vector3 angularImpulseDelta;
    if ( torque.TryDivided( record.rotationalInertia, angularImpulseDelta ) )
    {
        hot.angularVelocity += angularImpulseDelta;
    }

    record.pendingImpulse = ZERO_VECTOR;
    record.pendingImpulseApplicationPoint = ZERO_VECTOR;
    record.hasPendingImpulse = false;
}

// Concept: this is the store-owned force integration path.
//
// The record carries mutable body state, the collider carries exact shape data,
// and PhysicsWorldForces carries scene-wide fluid/gravity scalars. Keeping all
// force math here prevents hot physics paths from borrowing authoring owners to
// mutate velocities.
void ApplyWorldForces( PhysicsBodyRecord& record,
                       const BuoyancyBodyFacts& buoyancyFacts,
                       PhysicsBodyHotState& hot,
                       const ColliderRecord& collider,
                       const PhysicsTerrainView& terrain,
                       const PhysicsWorldForces& worldForces,
                       float deltaSeconds,
                       const Vector3* precomputedMutualGravityForce )
{
    Vector3 worldForce = ZERO_VECTOR;
    Vector3 worldTorque = ZERO_VECTOR;

    const PhysicsBuoyancySample buoyancySample = CalculateBuoyancySample( hot, collider, terrain, worldForces );
    const float submergedVolumePercent = buoyancySample.submergedVolumePercent;

    worldForce.y += CalculateGravityForce( worldForces, record.mass );
    // Why: mutual gravity is accumulated in PhysicsWorld's serial pair pass,
    // then injected per body so worker force integration stays order-neutral.
    if ( precomputedMutualGravityForce )
    {
        worldForce += *precomputedMutualGravityForce;
    }

    const float buoyancyForce = CalculateBuoyancyForce( worldForces, buoyancyFacts.volume * submergedVolumePercent );
    const Vector3 buoyancyForceVector( 0.0f, buoyancyForce, 0.0f );
    const Vector3 buoyancyArm = buoyancySample.centerOfBuoyancy - hot.position;
    worldForce += buoyancyForceVector;
    worldTorque += CrossProduct( buoyancyArm, buoyancyForceVector );
    worldTorque += CalculateBuoyancyRightingTorque( record,
                                                    buoyancyFacts,
                                                    hot,
                                                    collider,
                                                    terrain,
                                                    worldForces,
                                                    buoyancyForce,
                                                    submergedVolumePercent );

    if ( deltaSeconds > TOLERANCE && buoyancyForce > TOLERANCE && submergedVolumePercent > TOLERANCE )
    {
        const float waterCoupling = sqrtf( (std::clamp)( submergedVolumePercent, 0.0f, 1.0f ) );
        const float weight = fabsf( worldForces.gravity ) * record.mass;
        const float maxDampingForce = (std::max)( fabsf( buoyancyForce ), weight ) * 3.0f;
        Vector3 linearDampingImpulse = hot.linearVelocity * ( -record.mass * waterCoupling * 0.006f );
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

            const Vector3 arm = buoyancySample.wetPoints[i] - hot.position;
            const Vector3 pointVelocity = CrossProduct( hot.angularVelocity, arm );
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
            if ( sphereSpinDampingRate > TOLERANCE && !hot.angularVelocity.IsCloseToZero() )
            {
                Vector3 sphereAngularDampingTorque(
                    -hot.angularVelocity.x * record.rotationalInertia.x * sphereSpinDampingRate,
                    -hot.angularVelocity.y * record.rotationalInertia.y * sphereSpinDampingRate,
                    -hot.angularVelocity.z * record.rotationalInertia.z * sphereSpinDampingRate );
                sphereAngularDampingTorque.x = ClampAngularDragTorqueAxis( sphereAngularDampingTorque.x,
                                                                           hot.angularVelocity.x,
                                                                           record.rotationalInertia.x,
                                                                           deltaSeconds );

                sphereAngularDampingTorque.y = ClampAngularDragTorqueAxis( sphereAngularDampingTorque.y,
                                                                           hot.angularVelocity.y,
                                                                           record.rotationalInertia.y,
                                                                           deltaSeconds );

                sphereAngularDampingTorque.z = ClampAngularDragTorqueAxis( sphereAngularDampingTorque.z,
                                                                           hot.angularVelocity.z,
                                                                           record.rotationalInertia.z,
                                                                           deltaSeconds );

                worldTorque += sphereAngularDampingTorque;
            }
        }
    }

    worldForce += CalculateViscousDrag( worldForces,
                                        hot.linearVelocity,
                                        submergedVolumePercent,
                                        buoyancyFacts.dragCoefficient,
                                        buoyancyFacts.projectedSurfaceArea );

    if ( !hot.angularVelocity.IsCloseToZero() )
    {
        const float radius = hot.boundingRadius;
        const float avgDensity = ( worldForces.gasDensity * ( 1.0f - submergedVolumePercent ) ) +
                                 ( worldForces.fluidDensity * submergedVolumePercent *
                                   worldForces.angularDragMultiplier );

        const float angularDragCoeff = buoyancyFacts.dragCoefficient * avgDensity * radius * radius * radius;
        Vector3 angularDragTorque = hot.angularVelocity * ( -angularDragCoeff );

        angularDragTorque.x = ClampAngularDragTorqueAxis( angularDragTorque.x,
                                                          hot.angularVelocity.x,
                                                          record.rotationalInertia.x,
                                                          deltaSeconds );

        angularDragTorque.y = ClampAngularDragTorqueAxis( angularDragTorque.y,
                                                          hot.angularVelocity.y,
                                                          record.rotationalInertia.y,
                                                          deltaSeconds );

        angularDragTorque.z = ClampAngularDragTorqueAxis( angularDragTorque.z,
                                                          hot.angularVelocity.z,
                                                          record.rotationalInertia.z,
                                                          deltaSeconds );

        worldTorque += angularDragTorque;
    }

    ApplyWorldImpulse( record, hot, worldForce * deltaSeconds, worldTorque * deltaSeconds );
}

// Concept: store-owned pose integration advances the authoritative body row.
//
// Keeping this here means solver hot paths mutate only physics records when
// advancing position and orientation.
void IntegrateBodyRecordPose( PhysicsBodyHotState& hot, float deltaSeconds )
{
    hot.linearVelocity.Simplify();
    hot.angularVelocity.Simplify();

    hot.position += hot.linearVelocity * deltaSeconds;

    const Vector3 omega = hot.angularVelocity;
    const float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
    if ( omegaMag > 0.0001f )
    {
        const Vector3 axis( omega.x / omegaMag, omega.y / omegaMag, omega.z / omegaMag );
        hot.orientation.RotateAboutAxis( axis, omegaMag * deltaSeconds );
    }
}

void ApplyBodyDescriptorState( const PhysicsBodyCreateDesc& desc, PhysicsBodyRecord& cold, PhysicsBodyHotState& hot )
{
    hot.position = desc.position;
    hot.orientation = desc.orientation;
    hot.linearVelocity = desc.linearVelocity;
    hot.angularVelocity = desc.angularVelocity;
    cold.rotationalInertia = desc.rotationalInertia;
    hot.inverseRotationalInertia = desc.motionKind == PhysicsBodyMotionKind::Fixed
                                       ? ZERO_VECTOR
                                       : PositiveComponentInverseOrZero( desc.rotationalInertia );

    cold.mass = desc.mass;
    hot.inverseMass = desc.motionKind == PhysicsBodyMotionKind::Fixed || desc.mass <= 0.0f ? 0.0f : 1.0f / desc.mass;
    // Why: descriptor refresh carries body-only scalars that are not derivable
    // from collider rows. Fluid and terrain-support facts are stamped by the
    // aligned BuoyancySystem owner at the same boundary.
    hot.boundingRadius = desc.boundingRadius > 0.0f ? desc.boundingRadius : GetShapeBoundingRadius( desc.shape );
    cold.contactReleaseImpulseThreshold = desc.contactReleaseImpulseThreshold;
    cold.angularVelocityLimit = desc.angularVelocityLimit;
    hot.fixed = desc.motionKind == PhysicsBodyMotionKind::Fixed;
    cold.usesWorldInertia = desc.usesWorldInertia;
    cold.releasesFromFixedOnContact = desc.releasesFromFixedOnContact;
    cold.fixedTreeReleaseRootIndex = desc.fixedTreeReleaseRootIndex;
}

PhysicsBodyCreateRecord MakeBodyRecord( const PhysicsBodyCreateDesc& desc, bool sleepEnabled )
{
    PhysicsBodyCreateRecord record;
    record.cold.sceneObjectId = desc.sceneObjectId;
    ApplyBodyDescriptorState( desc, record.cold, record.hot );
    record.hot.awake = !( sleepEnabled && desc.startsAsleep );
    return record;
}

} // namespace


PhysicsBodyStore::PhysicsBodyStore() = default;


void PhysicsBodyStore::ClearHotFields()
{
    m_positionX.clear();
    m_positionY.clear();
    m_positionZ.clear();
    m_orientationX.clear();
    m_orientationY.clear();
    m_orientationZ.clear();
    m_orientationW.clear();
    m_linearVelocityX.clear();
    m_linearVelocityY.clear();
    m_linearVelocityZ.clear();
    m_angularVelocityX.clear();
    m_angularVelocityY.clear();
    m_angularVelocityZ.clear();
    m_inverseMass.clear();
    m_inverseInertiaX.clear();
    m_inverseInertiaY.clear();
    m_inverseInertiaZ.clear();
    m_boundingRadius.clear();
    m_fixed.clear();
    m_awake.clear();
}


void PhysicsBodyStore::ResizeHotFields( std::size_t count )
{
    m_positionX.resize( count );
    m_positionY.resize( count );
    m_positionZ.resize( count );
    m_orientationX.resize( count );
    m_orientationY.resize( count );
    m_orientationZ.resize( count );
    m_orientationW.resize( count );
    m_linearVelocityX.resize( count );
    m_linearVelocityY.resize( count );
    m_linearVelocityZ.resize( count );
    m_angularVelocityX.resize( count );
    m_angularVelocityY.resize( count );
    m_angularVelocityZ.resize( count );
    m_inverseMass.resize( count );
    m_inverseInertiaX.resize( count );
    m_inverseInertiaY.resize( count );
    m_inverseInertiaZ.resize( count );
    m_boundingRadius.resize( count );
    m_fixed.resize( count );
    m_awake.resize( count );
}


PhysicsBodyHotState PhysicsBodyStore::HotStateForModelIndex( int modelIndex ) const
{
    assert( modelIndex >= 0 && modelIndex < Count() );
    const std::size_t index = static_cast<std::size_t>( modelIndex );
    PhysicsBodyHotState state;
    state.position = Vector3( m_positionX[index], m_positionY[index], m_positionZ[index] );
    state.orientation = Math::Orientation::Quaternion( m_orientationX[index],
                                                       m_orientationY[index],
                                                       m_orientationZ[index],
                                                       m_orientationW[index] );

    state.linearVelocity = Vector3( m_linearVelocityX[index], m_linearVelocityY[index], m_linearVelocityZ[index] );
    state.angularVelocity = Vector3( m_angularVelocityX[index], m_angularVelocityY[index], m_angularVelocityZ[index] );
    state.inverseMass = m_inverseMass[index];
    state.inverseRotationalInertia = Vector3( m_inverseInertiaX[index],
                                              m_inverseInertiaY[index],
                                              m_inverseInertiaZ[index] );

    state.boundingRadius = m_boundingRadius[index];
    state.fixed = m_fixed[index] != 0u;
    state.awake = m_awake[index] != 0u;
    return state;
}


void PhysicsBodyStore::StoreHotStateAt( int modelIndex, const PhysicsBodyHotState& state )
{
    assert( modelIndex >= 0 && modelIndex < Count() );
    const std::size_t index = static_cast<std::size_t>( modelIndex );
    m_positionX[index] = state.position.x;
    m_positionY[index] = state.position.y;
    m_positionZ[index] = state.position.z;
    state.orientation.GetComponents( m_orientationX[index],
                                     m_orientationY[index],
                                     m_orientationZ[index],
                                     m_orientationW[index] );

    m_linearVelocityX[index] = state.linearVelocity.x;
    m_linearVelocityY[index] = state.linearVelocity.y;
    m_linearVelocityZ[index] = state.linearVelocity.z;
    m_angularVelocityX[index] = state.angularVelocity.x;
    m_angularVelocityY[index] = state.angularVelocity.y;
    m_angularVelocityZ[index] = state.angularVelocity.z;
    m_inverseMass[index] = state.inverseMass;
    m_inverseInertiaX[index] = state.inverseRotationalInertia.x;
    m_inverseInertiaY[index] = state.inverseRotationalInertia.y;
    m_inverseInertiaZ[index] = state.inverseRotationalInertia.z;
    m_boundingRadius[index] = state.boundingRadius;
    m_fixed[index] = state.fixed ? 1u : 0u;
    m_awake[index] = state.awake ? 1u : 0u;
}


static uint32_t NextSceneObjectIdValueAfter( const PhysicsBodyRecordList& bodies )
{
    uint32_t nextSceneObjectIdValue = 1;
    const uint32_t maxSceneObjectIdValue = ( std::numeric_limits<uint32_t>::max )();
    for ( const PhysicsBodyRecord& body : bodies )
    {
        if ( body.sceneObjectId.value == maxSceneObjectIdValue )
        {
            return maxSceneObjectIdValue;
        }

        if ( body.sceneObjectId.IsValid() )
        {
            nextSceneObjectIdValue = (std::max)( nextSceneObjectIdValue, body.sceneObjectId.value + 1u );
        }
    }

    return nextSceneObjectIdValue;
}


void ReportSceneObjectIdReloadCapacityExceeded( int requested, std::size_t capacity, int currentCount )
{
    std::fprintf( stderr,
                  "FATAL: PhysicsBodyStore scene object id reload capacity exceeded owner=%s requested=%d "
                  "capacity=%zu count=%d phase=%s.\n",
                  "PhysicsBodyStore.sceneObjectIds",
                  requested,
                  capacity,
                  currentCount,
                  "descriptor-reload" );

    std::fprintf( stdout,
                  "FATAL: PhysicsBodyStore scene object id reload capacity exceeded owner=%s requested=%d "
                  "capacity=%zu count=%d phase=%s.\n",
                  "PhysicsBodyStore.sceneObjectIds",
                  requested,
                  capacity,
                  currentCount,
                  "descriptor-reload" );

    std::fflush( stderr );
    std::fflush( stdout );
}


std::vector<PhysicsSceneObjectId> PhysicsBodyStore::BuildSceneObjectIdsForReload( int sceneEntityCount ) const
{
    const std::size_t capacity = m_bodies.capacity();
    // Hazard: descriptor repair receives a scene-row count from the caller. Keep
    // the cap check in the body-store owner so invalid topology reports the
    // store, requested count, fixed capacity, live count, and cold repair phase.
    if ( sceneEntityCount < 0 || static_cast<std::size_t>( sceneEntityCount ) > capacity )
    {
        ReportSceneObjectIdReloadCapacityExceeded( sceneEntityCount, capacity, Count() );
        assert( false && "PhysicsBodyStore scene object id reload capacity exceeded" );
        std::abort();
    }

    std::vector<PhysicsSceneObjectId> sceneObjectIds;
    sceneObjectIds.reserve( static_cast<std::size_t>( sceneEntityCount ) );
    uint32_t nextSceneObjectIdValue = NextSceneObjectIdValueAfter( m_bodies );
    for ( int i = 0; i < sceneEntityCount; ++i )
    {
        PhysicsSceneObjectId sceneObjectId;
        if ( const PhysicsBodyRecord* body = RecordForModelIndex( i ) )
        {
            sceneObjectId = body->sceneObjectId;
        }

        // Why: descriptor repair is cold. Existing rows preserve store-owned
        // scene object identity, while scene rows that do not have a body yet receive
        // fresh ids from the same scanned range before handle reassignment.
        if ( !sceneObjectId.IsValid() )
        {
            if ( nextSceneObjectIdValue == ( std::numeric_limits<uint32_t>::max )() )
            {
                SB_FATAL( "PhysicsBodyStore",
                          "Scene object id scratch range exhausted while rebuilding %d scene rows.",
                          sceneEntityCount );
            }

            sceneObjectId = MakePhysicsSceneObjectId( nextSceneObjectIdValue++ );
        }

        sceneObjectIds.push_back( sceneObjectId );
    }

    return sceneObjectIds;
}


// Concept: body handles identify allocator slots, not legacy model positions.
//
// Scene object ids let the store preserve identity when a compatible scene refresh
// shifts a body to a different model slot. Retired slots bump generation before
// reuse so stale handles fail Contains/ModelIndexForHandle deterministically.
PhysicsBodyHandle PhysicsBodyStore::ResolveHandleForModelIndex( int modelIndex,
                                                                PhysicsSceneObjectId sceneObjectId,
                                                                PhysicsHandleAssignmentMask& assignedHandleSlots )
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
        m_handleSceneObjectIds[static_cast<std::size_t>( slot )] = sceneObjectId;

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
             m_handleSceneObjectIds[static_cast<std::size_t>( previous.index )] == sceneObjectId &&
             ( previous.index >= assignedHandleSlots.size() ||
               assignedHandleSlots[static_cast<std::size_t>( previous.index )] == 0 ) )
        {
            return assignSlot( previous.index );
        }
    }

    if ( sceneObjectId.IsValid() )
    {
        for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleSceneObjectIds.size() ); ++slot )
        {
            if ( m_handleAlive[slot] != 0 && m_handleSceneObjectIds[slot] == sceneObjectId &&
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
        m_handleSceneObjectIds.push_back( {} );
    }

    return assignSlot( slot );
}


void PhysicsBodyStore::RetireUnassignedHandles( const PhysicsHandleAssignmentMask& assignedHandleSlots )
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
        m_handleSceneObjectIds[slot] = {};
        m_handleGenerations[slot] = NextHandleGeneration( m_handleGenerations[slot] );
        m_freeHandleSlots.push_back( slot );
    }
}


void PhysicsBodyStore::Clear()
{
    m_bodies.clear();
    ClearHotFields();
    m_modelBodyHandles.clear();
    for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleAlive.size() ); ++slot )
    {
        if ( m_handleAlive[slot] != 0 )
        {
            m_handleGenerations[slot] = NextHandleGeneration( m_handleGenerations[slot] );
        }

        m_handleAlive[slot] = 0;
        m_handleModelIndices[slot] = -1;
        m_handleSceneObjectIds[slot] = {};
    }

    m_freeHandleSlots.clear();
    // Invariant: CreateBodyRecord pops from the back of the free list. Push in
    // reverse so a full Clear() reuses low handle indices first while still
    // advancing generations for stale-handle rejection.
    for ( uint32_t remaining = static_cast<uint32_t>( m_handleGenerations.size() ); remaining > 0; --remaining )
    {
        m_freeHandleSlots.push_back( remaining - 1u );
    }
}


void PhysicsBodyStore::LoadFromDescriptors( std::span<const PhysicsBodyCreateDesc> bodyDescs,
                                            std::span<const uint8_t> sleepStates )
{
    const PreservedRefreshStateList preservedStateByHandle = CapturePreservedRefreshState( m_bodies,
                                                                                           HotFields(),
                                                                                           m_handleGenerations.size() );

    m_assignedHandleScratch.assign( m_handleGenerations.size(), 0 );
    PhysicsHandleAssignmentMask& assignedHandleSlots = m_assignedHandleScratch;
    m_bodies.resize( bodyDescs.size() );
    ResizeHotFields( bodyDescs.size() );
    m_modelBodyHandles.resize( bodyDescs.size() );
    for ( std::size_t i = 0; i < bodyDescs.size(); ++i )
    {
        const PhysicsBodyCreateDesc& desc = bodyDescs[i];
        PhysicsBodyRecord& record = m_bodies[i];
        PhysicsBodyHotState hot;
        const PhysicsSceneObjectId sceneObjectId = desc.sceneObjectId;
        const PhysicsBodyHandle handle = ResolveHandleForModelIndex( static_cast<int>( i ),
                                                                     sceneObjectId,
                                                                     assignedHandleSlots );

        // Why: refresh copies descriptor state at cold repair edges, but a
        // pending tool impulse and sleep seed are physics-owned one-shot state.
        // Preserve them by handle slot so allocator-owned identity survives a
        // same-scene reorder instead of accidentally following the model slot.
        const PreservedRefreshState* preservedState = PreservedStateForHandle( preservedStateByHandle, handle );
        record.handle = handle;
        record.sceneObjectId = sceneObjectId;
        ApplyBodyDescriptorState( desc, record, hot );
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

        hot.awake = !( ( preservedState && preservedState->isSleeping ) ||
                       ( i < sleepStates.size() && sleepStates[i] != 0 ) );

        StoreHotStateAt( static_cast<int>( i ), hot );
        m_modelBodyHandles[i] = record.handle;
    }

    RetireUnassignedHandles( assignedHandleSlots );
}


PhysicsBodyHandle PhysicsBodyStore::CreateBodyRecord( const PhysicsBodyCreateRecord& initialRecord )
{
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
        m_handleSceneObjectIds.push_back( {} );
    }

    const int recordIndex = static_cast<int>( m_bodies.size() );
    PhysicsBodyHandle handle;
    handle.index = slot;
    handle.generation = m_handleGenerations[static_cast<std::size_t>( slot )];

    PhysicsBodyRecord record = initialRecord.cold;
    record.handle = handle;
    if ( !record.sceneObjectId.IsValid() )
    {
        record.sceneObjectId = PhysicsSceneObjectId { handle.index + 1u };
    }

    m_handleAlive[static_cast<std::size_t>( slot )] = 1;
    m_handleModelIndices[static_cast<std::size_t>( slot )] = recordIndex;
    m_handleSceneObjectIds[static_cast<std::size_t>( slot )] = record.sceneObjectId;
    m_bodies.push_back( record );
    ResizeHotFields( m_bodies.size() );
    StoreHotStateAt( recordIndex, initialRecord.hot );
    m_modelBodyHandles.push_back( handle );
    return handle;
}


PhysicsBodyHandle PhysicsBodyStore::CreateBodyRecord( const PhysicsBodyCreateDesc& desc, bool sleepEnabled )
{
    return CreateBodyRecord( MakeBodyRecord( desc, sleepEnabled ) );
}


bool PhysicsBodyStore::DestroyBodyRecord( PhysicsBodyHandle handle )
{
    if ( !Contains( handle ) )
    {
        return false;
    }

    const std::size_t handleSlot = static_cast<std::size_t>( handle.index );
    const int recordIndex = m_handleModelIndices[handleSlot];
    const int lastRecordIndex = Count() - 1;
    if ( recordIndex < 0 || recordIndex > lastRecordIndex )
    {
        return false;
    }

    // Invariant: rows are dense for simulation scans, while handles remain
    // allocator identities. Removing a row updates only the moved handle's row
    // map; no live handle encodes the previous vector position.
    if ( recordIndex != lastRecordIndex )
    {
        PhysicsBodyRecord& destination = m_bodies[static_cast<std::size_t>( recordIndex )];
        PhysicsBodyRecord& moved = m_bodies[static_cast<std::size_t>( lastRecordIndex )];
        destination = moved;
        const PhysicsBodyHotState movedHot = HotStateForModelIndex( lastRecordIndex );
        StoreHotStateAt( recordIndex, movedHot );
        m_modelBodyHandles[static_cast<std::size_t>( recordIndex )] = destination.handle;
        if ( destination.handle.IsValid() && destination.handle.index < m_handleModelIndices.size() )
        {
            m_handleModelIndices[static_cast<std::size_t>( destination.handle.index )] = recordIndex;
        }
    }

    m_bodies.pop_back();
    ResizeHotFields( m_bodies.size() );
    m_modelBodyHandles.pop_back();
    m_handleAlive[handleSlot] = 0;
    m_handleModelIndices[handleSlot] = -1;
    m_handleSceneObjectIds[handleSlot] = {};
    m_handleGenerations[handleSlot] = NextHandleGeneration( m_handleGenerations[handleSlot] );
    m_freeHandleSlots.push_back( handle.index );
    return true;
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


// Invariant: shrinking model-order bodies must retire handle slots for removed
// records. A stale handle surviving replay restore could target a different
// body after allocator reuse.
bool PhysicsBodyStore::TrimToCount( int bodyCount )
{
    if ( bodyCount < 0 || bodyCount > Count() )
    {
        return false;
    }

    m_assignedHandleScratch.assign( m_handleGenerations.size(), 0 );
    PhysicsHandleAssignmentMask& assignedHandleSlots = m_assignedHandleScratch;
    for ( int i = 0; i < bodyCount; ++i )
    {
        const PhysicsBodyRecord& record = m_bodies[static_cast<std::size_t>( i )];
        const PhysicsBodyHandle handle = record.handle;
        if ( handle.IsValid() && handle.index < m_handleGenerations.size() )
        {
            const std::size_t handleIndex = static_cast<std::size_t>( handle.index );
            if ( m_handleAlive[handleIndex] != 0 && m_handleGenerations[handleIndex] == handle.generation )
            {
                assignedHandleSlots[handleIndex] = 1;
            }
        }
    }

    m_bodies.resize( static_cast<std::size_t>( bodyCount ) );
    ResizeHotFields( static_cast<std::size_t>( bodyCount ) );
    m_modelBodyHandles.resize( static_cast<std::size_t>( bodyCount ) );
    for ( int i = 0; i < bodyCount; ++i )
    {
        m_modelBodyHandles[static_cast<std::size_t>( i )] = m_bodies[static_cast<std::size_t>( i )].handle;
    }

    RetireUnassignedHandles( assignedHandleSlots );
    return true;
}


// Concept: replay restore writes recorded physics values into the store.
//
// Presentation owners may cache draw-facing state, but the body record must not
// reload pose, velocity, mass, or inertia from presentation rows.
bool PhysicsBodyStore::RestoreReplayBodyState( const PhysicsBodyRestoreState& restore )
{
    PhysicsBodyRecord* record = MutableRecordForHandle( restore.body );
    if ( !record || record->sceneObjectId != restore.sceneObjectId )
    {
        return false;
    }

    const int modelIndex = ModelIndexForHandle( restore.body );
    PhysicsBodyHotState hot = HotStateForModelIndex( modelIndex );
    hot.position = restore.position;
    hot.orientation = restore.orientation;
    hot.linearVelocity = restore.linearVelocity;
    hot.angularVelocity = restore.angularVelocity;
    record->mass = restore.mass;
    hot.inverseMass = restore.fixed ? 0.0f : restore.inverseMass;
    record->rotationalInertia = restore.rotationalInertia;
    hot.inverseRotationalInertia = restore.fixed ? ZERO_VECTOR : restore.inverseRotationalInertia;
    hot.fixed = restore.fixed;
    record->pendingImpulse = ZERO_VECTOR;
    record->pendingImpulseApplicationPoint = ZERO_VECTOR;
    record->hasPendingImpulse = false;
    StoreHotStateAt( modelIndex, hot );
    return true;
}


void PhysicsBodyStore::RefreshRecordFromDescriptorAt( const PhysicsBodyCreateDesc& desc, int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 )
    {
        return;
    }

    PhysicsBodyHotState hot = HotStateForModelIndex( modelIndex );
    ApplyBodyDescriptorState( desc, *record, hot );
    StoreHotStateAt( modelIndex, hot );
}


void PhysicsBodyStore::CopySleepStatesFrom( std::span<const uint8_t> sleepStates )
{
    PhysicsBodyHotFieldsView hotFields = MutableHotFields();
    const int bodyCount = static_cast<int>( hotFields.awake.size() );
    for ( int i = 0; i < bodyCount; ++i )
    {
        hotFields.awake[static_cast<std::size_t>( i )] = i < static_cast<int>( sleepStates.size() ) &&
                                                                 sleepStates[static_cast<std::size_t>( i )] != 0
                                                             ? 0u
                                                             : 1u;
    }
}


void PhysicsBodyStore::CopySleepStatesTo( std::vector<uint8_t>& sleepStates ) const
{
    const PhysicsBodyHotFieldsConstView hotFields = HotFields();
    sleepStates.resize( hotFields.awake.size(), 0 );
    for ( std::size_t i = 0; i < hotFields.awake.size(); ++i )
    {
        sleepStates[i] = hotFields.awake[i] == 0u ? 1u : 0u;
    }
}


// Why: fixed records keep their authored mass and inertia even while solver
// reciprocals are zero. Release paths must restore those reciprocals in-place
// so they do not need a full body-store reload.
bool PhysicsBodyStore::ReleaseFixedBody( int modelIndex,
                                         const Vector3& seedLinearVelocity,
                                         const Vector3& seedAngularVelocity )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record )
    {
        return false;
    }

    PhysicsBodyHotState hot = HotStateForModelIndex( modelIndex );
    hot.fixed = false;
    hot.awake = true;
    hot.inverseMass = PositiveInverseOrZero( record->mass );
    hot.inverseRotationalInertia = PositiveComponentInverseOrZero( record->rotationalInertia );
    hot.linearVelocity = seedLinearVelocity;
    hot.angularVelocity = seedAngularVelocity;
    StoreHotStateAt( modelIndex, hot );
    return true;
}


void PhysicsBodyStore::ReleaseAttachedFixedTreeParts( const PhysicsFixedTreeReleaseEvent& event,
                                                      PhysicsBodyIndexList& outReleasedBodyIndices )
{
    outReleasedBodyIndices.clear();
    const int sourceIndex = event.sourceIndex;
    if ( sourceIndex < 0 || sourceIndex >= Count() )
    {
        return;
    }

    const PhysicsBodyRecord& sourceRecord = m_bodies[static_cast<std::size_t>( sourceIndex )];
    const int sourceRootModelIndex = sourceRecord.fixedTreeReleaseRootIndex;
    if ( sourceRootModelIndex < 0 )
    {
        return;
    }

    // Why: fixed-tree grouping is copied into the body row during refresh, so
    // same-frame releases do not borrow collection owners while the solver is
    // mutating live body state.
    const PhysicsBodyHotFieldsConstView hotFields = HotFields();
    const float sourceY = hotFields.positionY[static_cast<std::size_t>( sourceIndex )];
    const int bodyCount = Count();
    for ( int i = 0; i < bodyCount; ++i )
    {
        if ( i == sourceIndex )
        {
            continue;
        }

        const PhysicsBodyRecord& record = m_bodies[static_cast<std::size_t>( i )];
        if ( record.fixedTreeReleaseRootIndex != sourceRootModelIndex )
        {
            continue;
        }

        if ( hotFields.positionY[static_cast<std::size_t>( i )] + 0.05f < sourceY )
        {
            continue;
        }

        if ( hotFields.fixed[static_cast<std::size_t>( i )] != 0u )
        {
            if ( !record.releasesFromFixedOnContact )
            {
                continue;
            }

            ReleaseFixedBody( i, event.seedLinearVelocity, event.seedAngularVelocity );
        }

        outReleasedBodyIndices.push_back( i );
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
        return PhysicsBodyHandle {};
    }

    return m_modelBodyHandles[static_cast<std::size_t>( modelIndex )];
}


PhysicsBodyHandle PhysicsBodyStore::HandleForSceneObjectId( PhysicsSceneObjectId sceneObjectId,
                                                            int modelIndexHint ) const
{
    if ( !sceneObjectId.IsValid() )
    {
        return PhysicsBodyHandle {};
    }

    const PhysicsBodyHandle hintedHandle = HandleForModelIndex( modelIndexHint );
    if ( Contains( hintedHandle ) )
    {
        const std::size_t hintedSlot = static_cast<std::size_t>( hintedHandle.index );
        if ( hintedSlot < m_handleSceneObjectIds.size() && m_handleSceneObjectIds[hintedSlot] == sceneObjectId )
        {
            return hintedHandle;
        }
    }

    const std::size_t slotCount = (std::min)( m_handleGenerations.size(),
                                              (std::min)( m_handleAlive.size(), m_handleSceneObjectIds.size() ) );

    for ( std::size_t slot = 0; slot < slotCount; ++slot )
    {
        if ( m_handleAlive[slot] == 0 || m_handleSceneObjectIds[slot] != sceneObjectId )
        {
            continue;
        }

        PhysicsBodyHandle handle;
        handle.index = static_cast<uint32_t>( slot );
        handle.generation = m_handleGenerations[slot];
        if ( Contains( handle ) )
        {
            return handle;
        }
    }

    return PhysicsBodyHandle {};
}


int PhysicsBodyStore::ModelIndexForHandle( PhysicsBodyHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return -1;
    }

    return m_handleModelIndices[static_cast<std::size_t>( handle.index )];
}


int PhysicsBodyStore::ResolveModelRow( PhysicsBodyHandle handle, ModelRowHint& hint ) const
{
    const PhysicsBodyHandle hintedHandle = HandleForModelIndex( hint.value );
    if ( hintedHandle == handle && Contains( hintedHandle ) )
    {
        return hint.value;
    }

    const int resolvedRow = ModelIndexForHandle( handle );
    hint.value = resolvedRow;
    return resolvedRow;
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


std::span<const PhysicsBodyRecord> PhysicsBodyStore::Records() const
{
    return { m_bodies.data(), m_bodies.size() };
}


std::span<PhysicsBodyRecord> PhysicsBodyStore::MutableRecords()
{
    return { m_bodies.data(), m_bodies.size() };
}


SkullbonezCore::Physics::PhysicsBodyHotFieldsConstView PhysicsBodyStore::HotFields() const
{
    return { { m_positionX.data(), m_positionX.size() },
             { m_positionY.data(), m_positionY.size() },
             { m_positionZ.data(), m_positionZ.size() },
             { m_orientationX.data(), m_orientationX.size() },
             { m_orientationY.data(), m_orientationY.size() },
             { m_orientationZ.data(), m_orientationZ.size() },
             { m_orientationW.data(), m_orientationW.size() },
             { m_linearVelocityX.data(), m_linearVelocityX.size() },
             { m_linearVelocityY.data(), m_linearVelocityY.size() },
             { m_linearVelocityZ.data(), m_linearVelocityZ.size() },
             { m_angularVelocityX.data(), m_angularVelocityX.size() },
             { m_angularVelocityY.data(), m_angularVelocityY.size() },
             { m_angularVelocityZ.data(), m_angularVelocityZ.size() },
             { m_inverseMass.data(), m_inverseMass.size() },
             { m_inverseInertiaX.data(), m_inverseInertiaX.size() },
             { m_inverseInertiaY.data(), m_inverseInertiaY.size() },
             { m_inverseInertiaZ.data(), m_inverseInertiaZ.size() },
             { m_boundingRadius.data(), m_boundingRadius.size() },
             { m_fixed.data(), m_fixed.size() },
             { m_awake.data(), m_awake.size() } };
}


SkullbonezCore::Physics::PhysicsBodyHotFieldsView PhysicsBodyStore::MutableHotFields()
{
    return { { m_positionX.data(), m_positionX.size() },
             { m_positionY.data(), m_positionY.size() },
             { m_positionZ.data(), m_positionZ.size() },
             { m_orientationX.data(), m_orientationX.size() },
             { m_orientationY.data(), m_orientationY.size() },
             { m_orientationZ.data(), m_orientationZ.size() },
             { m_orientationW.data(), m_orientationW.size() },
             { m_linearVelocityX.data(), m_linearVelocityX.size() },
             { m_linearVelocityY.data(), m_linearVelocityY.size() },
             { m_linearVelocityZ.data(), m_linearVelocityZ.size() },
             { m_angularVelocityX.data(), m_angularVelocityX.size() },
             { m_angularVelocityY.data(), m_angularVelocityY.size() },
             { m_angularVelocityZ.data(), m_angularVelocityZ.size() },
             { m_inverseMass.data(), m_inverseMass.size() },
             { m_inverseInertiaX.data(), m_inverseInertiaX.size() },
             { m_inverseInertiaY.data(), m_inverseInertiaY.size() },
             { m_inverseInertiaZ.data(), m_inverseInertiaZ.size() },
             { m_boundingRadius.data(), m_boundingRadius.size() },
             { m_fixed.data(), m_fixed.size() },
             { m_awake.data(), m_awake.size() } };
}


std::size_t PhysicsBodyStore::RecordCapacity() const
{
    return m_bodies.capacity();
}


PhysicsBodyRecord* PhysicsBodyStore::MutableRecordForHandle( PhysicsBodyHandle handle )
{
    if ( !Contains( handle ) )
    {
        return nullptr;
    }

    return MutableRecordForModelIndex( m_handleModelIndices[static_cast<std::size_t>( handle.index )] );
}


const PhysicsBodyRecord* PhysicsBodyStore::RecordForHandle( PhysicsBodyHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return nullptr;
    }

    return RecordForModelIndex( m_handleModelIndices[static_cast<std::size_t>( handle.index )] );
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


bool PhysicsBodyStore::WakeBody( PhysicsBodyHandle body )
{
    const int modelIndex = ModelIndexForHandle( body );
    if ( modelIndex < 0 || m_fixed[static_cast<std::size_t>( modelIndex )] != 0u )
    {
        return false;
    }

    m_awake[static_cast<std::size_t>( modelIndex )] = 1u;
    return true;
}


bool PhysicsBodyStore::SeedBodyAsleep( PhysicsBodyHandle body )
{
    const int modelIndex = ModelIndexForHandle( body );
    if ( modelIndex < 0 || m_fixed[static_cast<std::size_t>( modelIndex )] != 0u )
    {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>( modelIndex );
    m_linearVelocityX[index] = 0.0f;
    m_linearVelocityY[index] = 0.0f;
    m_linearVelocityZ[index] = 0.0f;
    m_angularVelocityX[index] = 0.0f;
    m_angularVelocityY[index] = 0.0f;
    m_angularVelocityZ[index] = 0.0f;
    m_awake[index] = 0u;
    return true;
}


bool PhysicsBodyStore::SetBodyVelocity( PhysicsBodyHandle body,
                                        const Vector3& linearVelocity,
                                        const Vector3& angularVelocity )
{
    const int modelIndex = ModelIndexForHandle( body );
    if ( modelIndex < 0 || m_fixed[static_cast<std::size_t>( modelIndex )] != 0u )
    {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>( modelIndex );
    m_linearVelocityX[index] = linearVelocity.x;
    m_linearVelocityY[index] = linearVelocity.y;
    m_linearVelocityZ[index] = linearVelocity.z;
    m_angularVelocityX[index] = angularVelocity.x;
    m_angularVelocityY[index] = angularVelocity.y;
    m_angularVelocityZ[index] = angularVelocity.z;
    return true;
}


bool PhysicsBodyStore::SetPendingBodyImpulse( PhysicsBodyHandle body,
                                              const Vector3& impulse,
                                              const Vector3& localApplicationPoint )
{
    PhysicsBodyRecord* record = MutableRecordForHandle( body );
    if ( !record )
    {
        return false;
    }

    record->pendingImpulse = impulse;
    record->pendingImpulseApplicationPoint = localApplicationPoint;
    record->hasPendingImpulse = true;
    return true;
}


bool PhysicsBodyStore::ApplyBodyImpulse( PhysicsBodyHandle body,
                                         const Vector3& impulse,
                                         const Vector3& localApplicationPoint )
{
    const bool pending = SetPendingBodyImpulse( body, impulse, localApplicationPoint );
    WakeBody( body );
    return pending;
}


// Concept: pending impulses are one-shot velocity edits owned by hot arrays.
//
// Runtime force integration consumes them through this store hook so impulse
// math stays in one cache-local body path.
bool PhysicsBodyStore::ConsumePendingBodyImpulse( int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record )
    {
        return false;
    }

    if ( !record->hasPendingImpulse )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    PhysicsBodyHotState hot;
    hot.linearVelocity = Vector3( m_linearVelocityX[bodyIndex],
                                  m_linearVelocityY[bodyIndex],
                                  m_linearVelocityZ[bodyIndex] );

    hot.angularVelocity = Vector3( m_angularVelocityX[bodyIndex],
                                   m_angularVelocityY[bodyIndex],
                                   m_angularVelocityZ[bodyIndex] );

    ApplyPendingImpulse( *record, hot );
    // Why: an impulse can only change velocity. Writing the entire 20-field row
    // here needlessly pollutes the scalar hot path and obscures that invariant.
    m_linearVelocityX[bodyIndex] = hot.linearVelocity.x;
    m_linearVelocityY[bodyIndex] = hot.linearVelocity.y;
    m_linearVelocityZ[bodyIndex] = hot.linearVelocity.z;
    m_angularVelocityX[bodyIndex] = hot.angularVelocity.x;
    m_angularVelocityY[bodyIndex] = hot.angularVelocity.y;
    m_angularVelocityZ[bodyIndex] = hot.angularVelocity.z;
    return true;
}


bool PhysicsBodyStore::IntegrateBodyPose( Core::Profiler* profiler,
                                          const ColliderStore& colliderStore,
                                          const PhysicsTerrainView& terrain,
                                          BuoyancyBodyFacts& buoyancyFacts,
                                          int modelIndex,
                                          float deltaSeconds )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    const ColliderRecord* collider = ColliderRecordForModelIndex( colliderStore, modelIndex );
    if ( !record || !collider || modelIndex < 0 || deltaSeconds <= 0.0f )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    PhysicsBodyHotState hot;
    hot.position = Vector3( m_positionX[bodyIndex], m_positionY[bodyIndex], m_positionZ[bodyIndex] );
    hot.orientation = Math::Orientation::Quaternion( m_orientationX[bodyIndex],
                                                     m_orientationY[bodyIndex],
                                                     m_orientationZ[bodyIndex],
                                                     m_orientationW[bodyIndex] );

    hot.linearVelocity = Vector3( m_linearVelocityX[bodyIndex],
                                  m_linearVelocityY[bodyIndex],
                                  m_linearVelocityZ[bodyIndex] );

    hot.angularVelocity = Vector3( m_angularVelocityX[bodyIndex],
                                   m_angularVelocityY[bodyIndex],
                                   m_angularVelocityZ[bodyIndex] );

    hot.fixed = m_fixed[bodyIndex] != 0u;
    hot.awake = m_awake[bodyIndex] != 0u;
    if ( hot.fixed || !hot.awake )
    {
        return false;
    }

    IntegrateBodyRecordPose( hot, deltaSeconds );
    ClampBodyToTerrainSurface( profiler, terrain, hot, *collider );
    // Why: this value is a targeted underwater-sleep probe, not general body
    // state. Any pose integration invalidates the previous water sample.
    buoyancyFacts.submergedVolumePercent = 0.0f;
    // Invariant: pose integration simplifies both velocity vectors and mutates
    // only pose plus velocity, so unrelated mass, radius, and flags stay cold.
    m_positionX[bodyIndex] = hot.position.x;
    m_positionY[bodyIndex] = hot.position.y;
    m_positionZ[bodyIndex] = hot.position.z;
    hot.orientation.GetComponents( m_orientationX[bodyIndex],
                                   m_orientationY[bodyIndex],
                                   m_orientationZ[bodyIndex],
                                   m_orientationW[bodyIndex] );

    m_linearVelocityX[bodyIndex] = hot.linearVelocity.x;
    m_linearVelocityY[bodyIndex] = hot.linearVelocity.y;
    m_linearVelocityZ[bodyIndex] = hot.linearVelocity.z;
    m_angularVelocityX[bodyIndex] = hot.angularVelocity.x;
    m_angularVelocityY[bodyIndex] = hot.angularVelocity.y;
    m_angularVelocityZ[bodyIndex] = hot.angularVelocity.z;
    return true;
}


bool PhysicsBodyStore::ApplyForces( const PhysicsWorldForces& worldForces,
                                    const ColliderStore& colliderStore,
                                    const PhysicsTerrainView& terrain,
                                    const BuoyancyBodyFacts& buoyancyFacts,
                                    int modelIndex,
                                    float deltaSeconds,
                                    const Vector3* precomputedMutualGravityForce )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    const ColliderRecord* collider = ColliderRecordForModelIndex( colliderStore, modelIndex );
    if ( !record || !collider )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    PhysicsBodyHotState hot;
    hot.position = Vector3( m_positionX[bodyIndex], m_positionY[bodyIndex], m_positionZ[bodyIndex] );
    hot.orientation = Math::Orientation::Quaternion( m_orientationX[bodyIndex],
                                                     m_orientationY[bodyIndex],
                                                     m_orientationZ[bodyIndex],
                                                     m_orientationW[bodyIndex] );

    hot.linearVelocity = Vector3( m_linearVelocityX[bodyIndex],
                                  m_linearVelocityY[bodyIndex],
                                  m_linearVelocityZ[bodyIndex] );

    hot.angularVelocity = Vector3( m_angularVelocityX[bodyIndex],
                                   m_angularVelocityY[bodyIndex],
                                   m_angularVelocityZ[bodyIndex] );

    hot.boundingRadius = m_boundingRadius[bodyIndex];
    hot.fixed = m_fixed[bodyIndex] != 0u;
    if ( hot.fixed )
    {
        m_linearVelocityX[bodyIndex] = 0.0f;
        m_linearVelocityY[bodyIndex] = 0.0f;
        m_linearVelocityZ[bodyIndex] = 0.0f;
        m_angularVelocityX[bodyIndex] = 0.0f;
        m_angularVelocityY[bodyIndex] = 0.0f;
        m_angularVelocityZ[bodyIndex] = 0.0f;
        return false;
    }

    ThrottleAngularVelocity( *record, hot );
    ApplyWorldForces( *record,
                      buoyancyFacts,
                      hot,
                      *collider,
                      terrain,
                      worldForces,
                      deltaSeconds,
                      precomputedMutualGravityForce );

    ApplyPendingImpulse( *record, hot );
    // Invariant: force and pending-impulse integration are velocity-only edits.
    // Keeping the writes narrow avoids a 20-field round trip per active body.
    m_linearVelocityX[bodyIndex] = hot.linearVelocity.x;
    m_linearVelocityY[bodyIndex] = hot.linearVelocity.y;
    m_linearVelocityZ[bodyIndex] = hot.linearVelocity.z;
    m_angularVelocityX[bodyIndex] = hot.angularVelocity.x;
    m_angularVelocityY[bodyIndex] = hot.angularVelocity.y;
    m_angularVelocityZ[bodyIndex] = hot.angularVelocity.z;
    return true;
}
