/*
File: SkullbonezSource/SkullbonezConvexHullShape.h
Purpose:
  Defines immutable authored convex hull collision geometry.

Mental model:
  Physics is deterministic fixed-step state update. Convex hull data is built
  and validated at load time, then read without heap allocation by narrowphase.

Glossary:
  Convex hull: Closed polytope whose faces enclose a volume with no concavity.
  Face: One planar polygon on the hull boundary.
  Edge: Undirected segment shared by exactly two faces.

Invariants:
  - Hull topology is immutable after load-time validation.
  - Face, edge, and vertex ordering is deterministic and becomes feature ID
    input for persistent contact warm starting.

Related:
  - SkullbonezSource/SkullbonezConvexHullShape.cpp
  - SkullbonezSource/SkullbonezObjectContactManifold.cpp
*/
#pragma once

#include <array>
#include <cstdint>
#include "SkullbonezCommon.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezPhysicsMass.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
class BoundingBox;
class BoundingSphere;

struct ConvexHullFace
{
    Vector::Vector3 normalLocal = Vector::ZERO_VECTOR;
    float planeOffsetLocal = 0.0f;
    uint16_t firstIndex = 0;
    uint8_t indexCount = 0;
};

struct ConvexHullEdge
{
    uint16_t vertexA = 0;
    uint16_t vertexB = 0;
    uint16_t faceA = 0;
    uint16_t faceB = 0;
};

class ConvexHullShape
{
  public:
    static constexpr uint16_t MAX_VERTICES = 64;
    static constexpr uint16_t MAX_FACES = 96;
    static constexpr uint16_t MAX_EDGES = 160;
    static constexpr uint16_t MAX_FACE_VERTICES = 16;
    static constexpr uint16_t MAX_FACE_INDICES = MAX_FACES * MAX_FACE_VERTICES;

  private:
    std::array<Vector::Vector3, MAX_VERTICES> m_vertices = {};
    std::array<ConvexHullFace, MAX_FACES> m_faces = {};
    std::array<ConvexHullEdge, MAX_EDGES> m_edges = {};
    std::array<uint16_t, MAX_FACE_INDICES> m_faceIndices = {};
    Vector::Vector3 m_position = Vector::ZERO_VECTOR;
    Vector::Vector3 m_authoredCenterOfMass = Vector::ZERO_VECTOR;
    Vector::Vector3 m_inertiaHalfExtents = Vector::Vector3( 1.0f, 1.0f, 1.0f );
    Vector::Vector3 m_unitInertia = Vector::Vector3( 0.6666667f, 0.6666667f, 0.6666667f );
    uint16_t m_vertexCount = 0;
    uint16_t m_faceCount = 0;
    uint16_t m_edgeCount = 0;
    uint16_t m_faceIndexCount = 0;
    float m_boundingRadius = 0.0f;
    float m_volume = 0.0f;
    float m_projectedSurfaceArea = 0.0f;
    float m_defaultMass = Physics::MIN_DYNAMIC_MASS;
    char m_name[64] = {};

  public:
    ConvexHullShape();

    static ConvexHullShape LoadFromFile( const char* path );

    Transformation::Matrix4 GetModelMatrix( const Vector::Vector3& worldPos, const Transformation::Matrix4& rotation ) const;
    float GetVolume() const;
    float GetDefaultMass() const;
    float GetSubmergedVolumePercent( float fluidSurfaceHeight ) const;
    float GetDragCoefficient() const;
    float GetProjectedSurfaceArea() const;
    float GetBoundingRadius() const;
    const Vector::Vector3& GetPosition() const;
    const Vector::Vector3& GetAuthoredCenterOfMass() const;
    const Vector::Vector3& GetInertiaHalfExtents() const;
    Vector::Vector3 ComputeBoxApproxInertia( float mass ) const;
    void ScaleAxis( int axis, float factor ); // Editor/runtime copy scale; authored asset files remain unchanged.

    uint16_t GetVertexCount() const;
    uint16_t GetFaceCount() const;
    uint16_t GetEdgeCount() const;
    const Vector::Vector3& GetVertex( uint16_t index ) const;
    const ConvexHullFace& GetFace( uint16_t index ) const;
    const ConvexHullEdge& GetEdge( uint16_t index ) const;
    uint16_t GetFaceIndex( uint16_t index ) const;
    const char* GetName() const;

    float TestCollision( const BoundingSphere& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
    float TestCollision( const BoundingBox& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
    float TestCollision( const ConvexHullShape& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
