// Immutable authored hull topology and complete center-of-mass inertia.
// Load and copy-scale run before collider identity binding; narrowphase only
// reads those rows. Version 3 mass properties come from closed-volume integrals.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include "../Core/Common.h"
#include "../Core/SbResult.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/Matrix4.h"
#include "../Maths/SymmetricMatrix3.h"
#include "PhysicsMass.h"
#include "../Maths/Vector3.h"

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

struct ConvexHullMotionAxis
{
    Vector::Vector3 normalLocal = Vector::ZERO_VECTOR;
    float halfWidthSquared = 0.0f;
};

class ConvexHullShape
{
  public:
    static constexpr uint16_t MAX_VERTICES = 64;
    static constexpr uint16_t MAX_FACES = 96;
    static constexpr uint16_t MAX_EDGES = 160;
    static constexpr uint16_t MAX_FACE_VERTICES = 16;
    static constexpr uint16_t MAX_FACE_INDICES = MAX_FACES * MAX_FACE_VERTICES;
    static constexpr uint16_t MAX_MOTION_AXES = 512;

  private:
    struct MotionAxisCache
    {
        std::array<ConvexHullMotionAxis, MAX_MOTION_AXES> axes = {};
        uint16_t count = 0;
    };

    bool RefreshMotionAxes();
    SkullbonezCore::Core::SbResult ValidateBakedTopology( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path ) const;
    SkullbonezCore::Core::SbResult ReadBakedFace( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, char*& context, const char* path, int lineNumber );
    SkullbonezCore::Core::SbResult ReadBakedEdge( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, char*& context, const char* path, int lineNumber );
    SkullbonezCore::Core::SbResult ReadBakedVertex( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, char*& context, const char* path, int lineNumber );


    std::array<Vector::Vector3, MAX_VERTICES> m_vertices = {};
    std::array<ConvexHullFace, MAX_FACES> m_faces = {};
    std::array<ConvexHullEdge, MAX_EDGES> m_edges = {};
    std::array<uint16_t, MAX_FACE_INDICES> m_faceIndices = {};
    std::shared_ptr<const MotionAxisCache> m_motionAxisCache;
    Vector::Vector3 m_position = Vector::ZERO_VECTOR;
    Vector::Vector3 m_authoredCenterOfMass = Vector::ZERO_VECTOR;
    Vector::Vector3 m_inertiaHalfExtents = Vector::Vector3( 1.0f, 1.0f, 1.0f );
    Transformation::SymmetricMatrix3 m_unitInertia;
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

    // Recoverable error: hull assets are external input. Callers that load scene/editor
    // data should use this overload so malformed files report recoverable
    // diagnostics instead of escaping through runtime code.
    static SkullbonezCore::Core::SbResult TryLoadFromFile( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path, ConvexHullShape& outHull );

    Transformation::Matrix4 GetModelMatrix( const Vector::Vector3& worldPos, const Transformation::Matrix4& rotation ) const;
    float GetVolume() const;
    float GetDefaultMass() const;
    float GetDragCoefficient() const;
    float GetProjectedSurfaceArea() const;
    float GetBoundingRadius() const;
    const Vector::Vector3& GetPosition() const;
    const Vector::Vector3& GetAuthoredCenterOfMass() const;
    const Vector::Vector3& GetInertiaHalfExtents() const;
    Transformation::SymmetricMatrix3 ComputeInertia( float mass ) const;
    void ScaleAxis( int axis, float factor ); // Editor/runtime copy scale; authored asset files remain unchanged.

    uint16_t GetVertexCount() const;
    uint16_t GetFaceCount() const;
    uint16_t GetEdgeCount() const;
    const Vector::Vector3& GetVertex( uint16_t index ) const;
    const ConvexHullFace& GetFace( uint16_t index ) const;
    const ConvexHullEdge& GetEdge( uint16_t index ) const;
    std::span<const ConvexHullMotionAxis> GetMotionAxes() const;
    uint16_t GetFaceIndex( uint16_t index ) const;
    const char* GetName() const;

    float TestCollision( const BoundingSphere& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
    float TestCollision( const BoundingBox& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
    float TestCollision( const ConvexHullShape& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
