#pragma once


// --- Includes ---
#include "SkullbonezVector3.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;


namespace SkullbonezCore
{
namespace Geometry
{
/* -- Terrain Post -----------------------------------------------------------------------------------------------------------------------------------------------

    Contains 2 x 3d vectors, one for the position, and one for the normal at that position.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct TerrainPost
{
    Vector3 vPosition, vNormal;
};

/* -- Triangle ---------------------------------------------------------------------------------------------------------------------------------------------------

    Contains 3 x 3d vectors, one for each vertex of a triangle in 3d space.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct Triangle
{
    Vector3 v1, v2, v3;
};

/* -- Plane ------------------------------------------------------------------------------------------------------------------------------------------------------

    Contains a vector which is the normal to the front side of the plane
    and a scalar distance to represent displacement from the origin.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct Plane
{
    Vector3 m_normal;
    float m_distance;

    Plane& operator=( Plane plane )
    {
        m_normal = plane.m_normal;
        m_distance = plane.m_distance;

        return *this;
    }
};

/* -- XZ Bounds --------------------------------------------------------------------------------------------------------------------------------------------------

    Contains four scalars representing the boundaries of a XZ plane
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
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

/* -- Box --------------------------------------------------------------------------------------------------------------------------------------------------------

    A simple box structure - six integer scalars
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct Box
{
    int m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax;
};

/* -- XZ Coords --------------------------------------------------------------------------------------------------------------------------------------------------

    A structure to store X and Z coordinates, independent of the Y axis.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct XZCoords
{
    float x, z;
};

/* -- Ray --------------------------------------------------------------------------------------------------------------------------------------------------------

    A generic ray to represent a directed displacement.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Ray
{
  public:
    Vector3 origin;
    Vector3 vector3;

    Ray()
    {
    }
    Ray( const Vector3& vOrigin, const Vector3& vVector3 )
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
