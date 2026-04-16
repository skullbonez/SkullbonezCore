/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                                _______
                                                                             .-"       "-.
                                                                            /             \
                                                                           /               \
                                                                           �   .--. .--.   �
                                                                           � )/   � �   \( �
                                                                           �/ \__/   \__/ \�
                                                                           /      /^\      \
                                                                           \__    '='    __/
                                                                             �\         /�
                                                                             �\'"VUUUV"'/�
                                                                             \ `"""""""` /
                                                                              `-._____.-'

                                                                         www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_CAMERA_COLLECTION_H
#define SKULLBONEZ_CAMERA_COLLECTION_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezCamera.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezMatrix4.h"

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math::Transformation;

namespace SkullbonezCore
{
namespace Environment
{
/* -- Camera Collection ------------------------------------------------------------------------------------------------------------------------------------------

    A singleton class that holds a collection of Camera objects and performs operations on these multiple cameras such as tweens, camera changes etc.
    This class is a friend of the Camera class.  The camera class has no public interface - cameras must be used through the CameraCollection class.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class CameraCollection
{

  private:
    static CameraCollection* pInstance;        // Singleton instance pointer
    Camera cameraArray[TOTAL_CAMERA_COUNT];    // Holds the cameras
    Camera primaryStore;                       // Holds initial position of primary camera after selection as primary (helps calculate how much to update the other cameras when the current primary loses its focus)
    Camera tweenPath;                          // Holds the current tweening path
    Camera tweenCamera;                        // Holds the state of the current tween
    Camera tweenStart;                         // Holds the tweens starting camera
    uint32_t cameraHashes[TOTAL_CAMERA_COUNT]; // Holds the hashed camera name keys
    int arrayPosition;                         // Current array position
    int selectedCamera;                        // Current selected camera
    float tweenSpeed;                          // Camera tweening speed
    bool isTweening;                           // Flag indicating whether the camera is in the middle of a tween or not
    float tweenProgress;                       // Stores the state of the current tween
    Terrain* terrain;                          // Stores a pointer to the terrain for tweening collision purposes
    Matrix4 currentViewMatrix;                 // Current view matrix (updated each frame by SetCamera)

    CameraCollection( void ); // Constructor
    ~CameraCollection( void ) = default;
    void SetViewMatrix( Camera& cCameraData ); // Computes currentViewMatrix from the supplied camera data
    int FindIndex( uint32_t hash );            // Returns the index of the specified camera
    Camera GetUpdateCamera( void );            // Returns the current update camera for relative updates
    void UpdateTweenPath( void );              // Alters the tween path member to end at the required destination (it is important to call this during tweens as the destination can move about the scene during the tween)
    void SetTweenPath( int fromIndex,
                       int toIndex ); // Sets the tween path member to the required tween path (specify fromIndex as -1 to use the tween camera as the from camera)

  public:
    static CameraCollection* Instance( void );            // Returns a pointer to the sole class instance
    static void Destroy( void );                          // Deletes the sole class instance
    const Vector3& GetCameraView( void );                 // Returns the current view of the primary camera
    const Vector3& GetCameraTranslation( void );          // Returns the current translation of the primary camera
    const Vector3& GetCameraTranslation( uint32_t hash ); // Returns the current translation of the specified camera
    void SetViewCoordinates( const Vector3& vView );      // Sets the primary camera view position (helpful for keeping focused on an object)
    void SetTweenSpeed( float fTweenSpeed );              // Sets the tween transition speed
    void SetCamera( void );                               // Takes care of setting the camera in the specified position.  Should be called once per frame when the camera has been updated
    bool IsPrimaryLocked( void );                         // Returns whether the primary camera is in locked mode or not
    void SetLockedMode( bool fIsLocked );                 // Sets the camera to or from locked mode
    void AmmendPrimaryY( float yCoordinate );             // Translates the primary cameras Y position to the specified world coordinate
    void SetCameraXZBounds( const XZBounds bounds );      // Set a camera boundary for all cameras
    void ResetRelativity( void );                         // Resets the difference camera to the current camera (call this after all camera updates have been made)
    bool IsCameraTweening( void );                        // Returns a flag indicating if the camera is currently tweening or not
    float DEBUG_GetViewMag( void );                       // Gets the magnitude of the primary view vector (debug routine)
    float DEBUG_GetViewMagTarget( void );                 // Returns the target magnitude of the primary view vector (debug routine)
    const Matrix4& GetViewMatrix( void ) const
    {
        return currentViewMatrix;
    }                                                // Returns the current view matrix
    uint32_t GetSelectedCameraName( void );          // Returns the hash of the selected camera
    bool IsCameraSelected( uint32_t hash );          // Returns a flag indicating if the specified camera is selected
    void ApplyPrimaryMovementBuffer( void );         // Applies the movement buffer to the primary camera
    const Vector3& GetPrimaryMovementBuffer( void ); // Returns the primary cameras movement buffer
    void SetTerrain( Terrain* cTerrain );            // Sets the terrain pointer
    void RotatePrimary( float xMove, float yMove );  // Rotates the primary camera

    void SetCameraXZBounds( uint32_t hash, const XZBounds bounds );       // Set a camera boundary for a specific camera
    void RelativeUpdate( uint32_t hash, float yMin, float yMax );         // Updates specified camera relative to primary, limit to specified y coordinate (minimum)
    void MovePrimary( Camera::TravelDirection enumDir, float fQuantity ); // Moves the primary camera in the specified direction
    void SelectCamera( uint32_t hash, bool fTween );                      // Selects a camera as primary
    void CancelTween( void );                                             // Immediately stops any in-progress camera tween

    void AddCamera( const Vector3& vPosition, const Vector3& vView, const Vector3& vUp, uint32_t hash ); // Add a camera to the camera collection
};
} // namespace Environment
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/