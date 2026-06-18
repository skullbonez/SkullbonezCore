/*
File: SkullbonezSource/SkullbonezCamera.h
Purpose:
  Stores camera pose and builds view/projection transforms for rendering.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezCamera.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection; // Forward declaration

/* -- Camera ------------------------------------------------------------------------------------------------------------------------------------------------------

    The camera class provides the base functionality for a manouverable camera in 3d space.  Uses matrix rotations, therefore Gimbel lock safe.
    This class has no public interface, but defines CameraCollection as a Friend.
*/
class Camera
{
    friend CameraCollection; // Friend declaration

  public:
    enum class TravelDirection
    {
        Forward,
        Left,
        Right,
        Backward
    }; // Helps with moving the camera around the scene

  private:
    Math::Vector::Vector3 m_position;       // m_position
    Math::Vector::Vector3 m_view;           // m_view
    Math::Vector::Vector3 m_upVector;       // Up vector
    Math::Vector::Vector3 m_movementBuffer; // Temporary storage to test a translation before applying it
    float m_viewMagnitude;                  // Keeps track of the magnitude of the view vector
    bool m_isFinishedTranslationRecursed;   // Makes sure finished translation method is only recursed once (used for view vector magnitude preservation)
    bool m_doCalculateViewMagnitude;        // Specifies whether the view magnitude should be calculated or not (used for view vector magnitude preservation)
    bool m_doPreserveViewMagnitude;         // Specified whether view magnitude preservation should be enforced
    bool m_isLockedMode;                    // Locks the view vector from being moved
    Geometry::XZBounds m_boundary;          // m_boundary the camera must not translate beyond
    Geometry::XZCoords m_xzStore;           // Stores XZ coordinates for bounds checking

    Camera();
    ~Camera() = default;
    void PrepareTranslation();  // Assists in keeping translations within bounds, should be called before all translations
    void FinishTranslation();   // Assists in keeping translations within bounds, should be called after all translations
    void ApplyMovementBuffer(); // Applies a camera translation
    void ZeroCamera();          // Sets all vector members to zero vector
    Math::Vector::Vector3 GetViewVectorNormalised();
    Math::Vector::Vector3 GetViewVectorRaw();
    Math::Vector::Vector3 GetRightVector();
    float UpVectorViewVectorRotationCap( float requestRadians );   // Caps pitch so view and up vectors cannot collapse into the same direction.
    void RecoverViewMagnitude( bool isOnBoundX, bool isOnBoundZ ); // Recovers view magnitude if under quota, indirectly recurses FinishTranslation function
    void SetAll( const Math::Vector::Vector3& vPosition, const Math::Vector::Vector3& vView, const Math::Vector::Vector3& vUpVector );
    void MoveCamera( const TravelDirection enumDir, float fQuantity ); // Move the camera specified amount in specified direction
    void ApplyDelta( const Camera& delta );                            // Applies a camera position/view/up delta to this camera
    void RotateCamera( float xMove, float yMove );                     // Offers an arbitrary rotation suitable for mouse input

    Camera& operator=( const Camera& target );  // Equality operator overload
    Camera& operator+=( const Camera& target ); // += Overload
    Camera operator-( const Camera& target );   // Binary subtraction operator overload
    Camera operator*( float f );                // Multiplication by scalar overload
};
} // namespace Environment
} // namespace SkullbonezCore
