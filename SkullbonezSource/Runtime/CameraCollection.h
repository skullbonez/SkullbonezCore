/*
File: SkullbonezSource/Runtime/CameraCollection.h
Purpose:
  Owns scene cameras and camera cycling state.

Summary:
  CameraCollection.h owns scene cameras and camera cycling state. As a public
  header, keep edits anchored on local owner boundaries and call direction and
  on the glossary/invariants below.

Glossary:
  Primary camera: Camera slot controlled directly by player/debug input.
  Relative camera: Secondary camera that preserves an authored offset from the
  primary camera.
  Tween: Time-based interpolation between camera poses for non-jarring cuts.
  Render pose: The eye/view/up triple actually used for the current frame; it
    can differ from the selected camera slot while a tween is active.

Invariants:
  - Camera slots are fixed-size and keyed by m_cameraHashes; scene code must
    register a camera before selecting it by hash.
  - m_terrain is borrowed scene state and must not be freed by the camera
    collection.
  - The active collection is value-owned by SceneController; frame, input,
    replay, and render paths borrow that concrete scene owner directly.

Related:
  - SkullbonezSource/Runtime/CameraCollection.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "Camera.h"
#include "../World/Terrain.h"
#include "../Maths/Matrix4.h"

namespace SkullbonezCore
{
namespace Environment
{
/* -- Camera Collection
------------------------------------------------------------------------------------------------------------------------------------------

    Runtime-owned collection of Camera objects that performs operations on
    multiple cameras such as tweens and camera changes. This class is a friend
    of the Camera class. The camera class has no public interface; cameras must
    be used through the CameraCollection class.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class CameraCollection
{

  private:
    Camera m_cameraArray[SkullbonezCore::Scene::Capacity::TOTAL_CAMERA_COUNT];    // Fixed camera slots keyed by
                                                                               // m_cameraHashes.
    Camera m_primaryStore;                                                        // Primary snapshot used to keep relative cameras coherent.
    Camera m_tweenPath;                                                           // Source-to-destination pose delta for the active tween.
    Camera m_tweenCamera;                                                         // Interpolated pose while tweening.
    Camera m_tweenStart;                                                          // Primary pose at the start of the active tween.
    Camera m_renderCamera;                                                        // Snapshot used to build m_currentViewMatrix this frame.
    uint32_t m_cameraHashes[SkullbonezCore::Scene::Capacity::TOTAL_CAMERA_COUNT]; // Scene hash key for each camera slot.
    int m_arrayPosition;                                                          // Active camera array index after hash lookup.
    int m_selectedCamera;                                                         // Selected camera slot used by UI/debug cycling.
    float m_tweenSpeed;                                                           // Camera interpolation speed in normalized tween units.
    bool m_isTweening;                                                            // Render camera follows m_tweenCamera while this is true.
    float m_tweenProgress;                                                        // Normalized tween progress through m_tweenPath.
    CameraMovementSettings m_movementSettings;                                    // Cached runtime tuning used by private camera clamp paths.
    Geometry::Terrain* m_terrain;                                                 // Borrowed scene terrain used to keep tweened cameras collision-aware.
    Math::Transformation::Matrix4 m_currentViewMatrix;                            // Render-facing view matrix refreshed once per frame.

    void SetViewMatrix( const Camera& cCameraData );                              // Frame view matrix comes from the pose selected for rendering.
    int FindIndex( uint32_t hash );                                               // Throws when the scene asks for an unregistered camera hash.
    Camera GetCameraDelta();                                                      // Primary movement delta used to update relative cameras.
    Camera GetTweenSourcePose() const;                                            // Starts new tweens from the visible frame pose when available.
    void UpdateTweenPath();                                                       // Retargets active tweens because the destination camera can move.
    void SetTweenPath( int fromIndex, int toIndex );                              // fromIndex=-1 starts from the current tween pose.

  public:
    CameraCollection();
    ~CameraCollection() = default;
    CameraCollection( const CameraCollection& ) = delete;
    CameraCollection& operator=( const CameraCollection& ) = delete;

    void ApplyMovementSettings( const CameraMovementSettings& settings );
    const Math::Vector::Vector3& GetCameraView() const;
    const Math::Vector::Vector3& GetCameraTranslation() const;
    const Math::Vector::Vector3& GetCameraUp() const;
    const Math::Vector::Vector3&
    GetRenderCameraView() const;                                                  // Render pose may be the tween camera instead of the primary camera.
    const Math::Vector::Vector3&
    GetRenderCameraTranslation() const;                                           // Render eye may be the tween camera instead of the primary camera.
    const Math::Vector::Vector3& GetRenderCameraUp() const;
    const Math::Vector::Vector3& GetCameraTranslation( uint32_t hash );
    void SetViewCoordinates( const Math::Vector::Vector3& vView );                // Keeps primary camera focused on a tracked world point.
    void
    SetPrimaryPosition( const Math::Vector::Vector3& vPos );                      // Tracking cameras can bypass movement-buffer translation.
    void SetPrimaryUp( const Math::Vector::Vector3& vUp );                        // Replay/debug camera restore can preserve the full pose.
    void SetPrimaryPose(
        const Math::Vector::Vector3& position,
        const Math::Vector::Vector3& view,
        const Math::Vector::Vector3& up );                                        // Updates the selected slot without changing the current render pose.
    void TweenPrimaryToPose(
        const Math::Vector::Vector3& position,
        const Math::Vector::Vector3& view,
        const Math::Vector::Vector3& up );                                        // Blends from the visible render pose to a selected-slot destination.
    void SetTweenSpeed( float fTweenSpeed );
    void SetCamera();                                                             // Call once per frame after camera updates to refresh render pose and view matrix.
    void OverrideRenderCameraForFrame( const Math::Vector::Vector3& position,
                                       const Math::Vector::Vector3& view,
                                       const Math::Vector::Vector3& up );
    bool IsPrimaryLocked();
    void SetLockedMode( bool fIsLocked );
    void AmmendPrimaryY( float yCoordinate );                                     // Pins primary camera height to a world-space Y value.
    void SetCameraXZBounds( const Geometry::XZBounds bounds );
    void ResetRelativity();                                                       // Call after camera updates so relative cameras use the new primary snapshot.
    bool IsCameraTweening();
    const Math::Transformation::Matrix4& GetViewMatrix() const
    {
        return m_currentViewMatrix;
    }
    uint32_t GetSelectedCameraName();
    bool HasCamera( uint32_t hash ) const;                                        // Lets teardown paths probe stale hashes without throwing.
    bool IsCameraSelected( uint32_t hash );
    void ApplyPrimaryMovementBuffer();
    const Math::Vector::Vector3& GetPrimaryMovementBuffer();
    void SetTerrain( Geometry::Terrain* cTerrain );                               // Borrowed terrain pointer used for camera collision bounds.
    void RotatePrimary( float xMove, float yMove );

    void SetCameraXZBounds( uint32_t hash, const Geometry::XZBounds bounds );
    void RelativeUpdate( uint32_t hash,
                         float yMin,
                         float yMax );                                            // Keeps a secondary camera offset from primary within its Y limits.
    void MovePrimary( Camera::TravelDirection enumDir, float fQuantity );
    void SelectCamera( uint32_t hash, bool fTween );                              // Optional tween preserves visual continuity between cameras.
    void CancelTween();                                                           // Immediate cut to the selected camera.

    void AddCamera( const Math::Vector::Vector3& vPosition,
                    const Math::Vector::Vector3& vView,
                    const Math::Vector::Vector3& vUp,
                    uint32_t hash );
    void Reset();                                                                 // Scene reload path; preserves Run-owned storage.
};
} // namespace Environment
} // namespace SkullbonezCore
