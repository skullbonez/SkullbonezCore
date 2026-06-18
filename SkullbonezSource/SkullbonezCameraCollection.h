/*
File: SkullbonezSource/SkullbonezCameraCollection.h
Purpose:
  Owns scene cameras and camera cycling state.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezCameraCollection.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezCamera.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezMatrix4.h"

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
    inline static CameraCollection* pInstance = nullptr;
    Camera m_cameraArray[TOTAL_CAMERA_COUNT];          // Fixed camera slots keyed by m_cameraHashes.
    Camera m_primaryStore;                             // Primary snapshot used to keep relative cameras coherent.
    Camera m_tweenPath;                                // Source-to-destination pose delta for the active tween.
    Camera m_tweenCamera;                              // Interpolated pose while tweening.
    Camera m_tweenStart;                               // Primary pose at the start of the active tween.
    Camera m_renderCamera;                             // Snapshot used to build m_currentViewMatrix this frame.
    uint32_t m_cameraHashes[TOTAL_CAMERA_COUNT];       // Scene hash key for each camera slot.
    int m_arrayPosition;                               // Current array m_position
    int m_selectedCamera;                              // Current selected camera
    float m_tweenSpeed;                                // Camera tweening speed
    bool m_isTweening;                                 // Render camera follows m_tweenCamera while this is true.
    float m_tweenProgress;                             // Normalized tween progress through m_tweenPath.
    Geometry::Terrain* m_terrain;                      // Borrowed scene terrain used to keep tweened cameras collision-aware.
    Math::Transformation::Matrix4 m_currentViewMatrix; // Current view matrix (updated each frame by SetCamera)

    CameraCollection();
    ~CameraCollection() = default;
    void SetViewMatrix( const Camera& cCameraData ); // Frame view matrix comes from the pose selected for rendering.
    int FindIndex( uint32_t hash );                  // Throws when the scene asks for an unregistered camera hash.
    Camera GetCameraDelta();                         // Primary movement delta used to update relative cameras.
    void UpdateTweenPath();                          // Retargets active tweens because the destination camera can move.
    void SetTweenPath( int fromIndex, int toIndex ); // fromIndex=-1 starts from the current tween pose.

  public:
    static CameraCollection* Instance();
    static void Destroy();
    const Math::Vector::Vector3& GetCameraView();
    const Math::Vector::Vector3& GetCameraTranslation();
    const Math::Vector::Vector3& GetCameraUp();
    const Math::Vector::Vector3& GetRenderCameraView() const;        // Render pose may be the tween camera instead of the primary camera.
    const Math::Vector::Vector3& GetRenderCameraTranslation() const; // Render eye may be the tween camera instead of the primary camera.
    const Math::Vector::Vector3& GetRenderCameraUp() const;
    const Math::Vector::Vector3& GetCameraTranslation( uint32_t hash );
    void SetViewCoordinates( const Math::Vector::Vector3& vView ); // Keeps primary camera focused on a tracked world point.
    void SetPrimaryPosition( const Math::Vector::Vector3& vPos );  // Tracking cameras can bypass movement-buffer translation.
    void SetTweenSpeed( float fTweenSpeed );
    void SetCamera(); // Call once per frame after camera updates to refresh render pose and view matrix.
    bool IsPrimaryLocked();
    void SetLockedMode( bool fIsLocked );
    void AmmendPrimaryY( float yCoordinate ); // Pins primary camera height to a world-space Y value.
    void SetCameraXZBounds( const Geometry::XZBounds bounds );
    void ResetRelativity(); // Call after camera updates so relative cameras use the new primary snapshot.
    bool IsCameraTweening();
    const Math::Transformation::Matrix4& GetViewMatrix() const
    {
        return m_currentViewMatrix;
    }
    uint32_t GetSelectedCameraName();
    bool IsCameraSelected( uint32_t hash );
    void ApplyPrimaryMovementBuffer();
    const Math::Vector::Vector3& GetPrimaryMovementBuffer();
    void SetTerrain( Geometry::Terrain* cTerrain ); // Borrowed terrain pointer used for camera collision bounds.
    void RotatePrimary( float xMove, float yMove );

    void SetCameraXZBounds( uint32_t hash, const Geometry::XZBounds bounds );
    void RelativeUpdate( uint32_t hash, float yMin, float yMax ); // Keeps a secondary camera offset from primary within its Y limits.
    void MovePrimary( Camera::TravelDirection enumDir, float fQuantity );
    void SelectCamera( uint32_t hash, bool fTween ); // Optional tween preserves visual continuity between cameras.
    void CancelTween();                              // Immediate cut to the selected camera.

    void AddCamera( const Math::Vector::Vector3& vPosition, const Math::Vector::Vector3& vView, const Math::Vector::Vector3& vUp, uint32_t hash );
    void Reset(); // Scene reload path; retains the singleton allocation.
};
} // namespace Environment
} // namespace SkullbonezCore
