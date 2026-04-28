/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_CAMERA_H
#define SKULLBONEZ_CAMERA_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;

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
    Vector3 m_position;                   // m_position
    Vector3 m_view;                       // m_view
    Vector3 m_upVector;                   // Up vector
    Vector3 m_movementBuffer;             // Temporary storage to test a translation before applying it
    float m_viewMagnitude;                // Keeps track of the magnitude of the view vector
    bool m_isFinishedTranslationRecursed; // Makes sure finished translation method is only recursed once (used for view vector magnitude preservation)
    bool m_doCalculateViewMagnitude;      // Specifies whether the view magnitude should be calculated or not (used for view vector magnitude preservation)
    bool m_doPreserveViewMagnitude;       // Specified whether view magnitude preservation should be enforced
    bool m_isLockedMode;                  // Locks the view vector from being moved
    XZBounds m_boundary;                  // m_boundary the camera must not translate beyond
    XZCoords m_xzStore;                   // Stores XZ coordinates for bounds checking

    Camera(); // Default constructor
    ~Camera() = default;
    void PrepareTranslation();                                                         // Assists in keeping translations within bounds, should be called before all translations
    void FinishTranslation();                                                          // Assists in keeping translations within bounds, should be called after all translations
    void ApplyMovementBuffer();                                                        // Applies a camera translation
    void ZeroCamera();                                                                 // Sets all vector members to zero vector
    Vector3 GetViewVectorNormalised();                                                 // Returns the normalised view vector
    Vector3 GetViewVectorRaw();                                                        // Returns the non-normalised view vector
    Vector3 GetRightVector();                                                          // Returns the normalised right vector
    float UpVectorViewVectorRotationCap( float requestRadians );                             // Returns a capped value in radians of what is safe to rotate before the view vector hits the up vector
    void RecoverViewMagnitude( bool isOnBoundX, bool isOnBoundZ );                           // Recovers view magnitude if under quota, indirectly recurses FinishTranslation function
    void SetAll( const Vector3& vPosition, const Vector3& vView, const Vector3& vUpVector ); // Set all by vectors
    void MoveCamera( const TravelDirection enumDir, float fQuantity );                       // Move the camera specified amount in specified direction
    void AddCamera( const Camera& updateCamera );                                            // Adds a camera to the current instance
    void RotateCamera( float xMove, float yMove );                                           // Offers an arbitrary rotation suitable for mouse input

    Camera& operator=( const Camera& target );  // Equality operator overload
    Camera& operator+=( const Camera& target ); // += Overload
    Camera operator-( const Camera& target );   // Binary subtraction operator overload
    Camera operator*( float f );                // Multiplication by scalar overload
};
} // namespace Environment
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
