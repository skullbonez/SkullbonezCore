/*
File: SkullbonezSource/Maths/GeometricStructures.h
Purpose:
  Defines small geometry structs used by math, collision, and rendering code.

Summary:
  The shared plain-data contracts carry rays, planes, and terrain samples
  without importing their Physics, World, or Rendering consumers. TerrainPost
  pairs one authored position with the normal at that position.

Invariants:
  - Small geometry structs are plain data contracts shared by math, physics,
    terrain, and rendering code.
  - Ray.vector3 is a displacement vector, not necessarily a normalized
    direction; callers own the parameter range interpretation.

Related:
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "Vector3.h"

namespace SkullbonezCore
{
namespace Geometry
{

struct TerrainPost
{
    Math::Vector::Vector3 vPosition, vNormal;
};

struct Triangle
{
    Math::Vector::Vector3 v1, v2, v3;
};

struct Plane
{
    Math::Vector::Vector3 m_normal;
    float m_distance;

    Plane& operator=( Plane plane )
    {
        m_normal = plane.m_normal;
        m_distance = plane.m_distance;

        return *this;
    }
};

struct XZBounds
{
    float m_xMin, m_xMax, m_zMin, m_zMax;

    XZBounds& operator=( XZBounds bounds )
    {
        m_xMin = bounds.m_xMin;
        m_xMax = bounds.m_xMax;
        m_zMin = bounds.m_zMin;
        m_zMax = bounds.m_zMax;

        return *this;
    }
};

struct Box
{
    int m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax;
};

struct XZCoords
{
    float x, z;
};

class Ray
{
  public:
    Math::Vector::Vector3 origin;
    Math::Vector::Vector3 vector3;

    Ray()
    {
    }
    Ray( const Math::Vector::Vector3& vOrigin, const Math::Vector::Vector3& vVector3 )
        : origin( vOrigin ), vector3( vVector3 )
    {
    }
    ~Ray()
    {
    }

    Ray operator*( float f )
    {
        return Ray( origin * f, vector3 * f );
    }
    Ray& operator*=( float f )
    {
        *this = *this * f;
        return *this;
    }
};
} // namespace Geometry
} // namespace SkullbonezCore
