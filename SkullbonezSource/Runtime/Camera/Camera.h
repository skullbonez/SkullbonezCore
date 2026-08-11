/*
File: SkullbonezSource/Runtime/Camera/Camera.h
Purpose:
  Stores one camera pose and the scratch state used to stage bounded camera
  movement.

Summary:
  CameraCollection is the public facade. Camera keeps eye, view, and up
  vectors private, stages movement in a buffer, then lets FinishTranslation
  clamp or repair the pose before render code reads it.

Glossary:
  XZ bounds: Horizontal world-space rectangle that prevents camera movement
    outside the playable terrain.
  View vector: Stored look target for the camera; older camera code keeps it as
  a point paired with the eye position.
  Locked orbit: Camera mode where movement preserves the authored view target
  and adjusts only the eye position.

Invariants:
  - Bounded movement must go through PrepareTranslation and FinishTranslation
  so the position, view target, and preserved view distance stay coherent.

Related:
  - SkullbonezSource/Runtime/Camera/Camera.cpp
  - Agentic/Reference/runtime-reference.md
*/
#pragma once


#include "../../Core/Common.h"
#include "../../Maths/Vector3.h"
#include "../../Maths/GeometricStructures.h"

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;                                                      // Forward declaration

struct CameraMovementSettings
{
    // Snapshot of config values needed while private Camera methods clamp a
    // pose. Camera stays owned by CameraCollection and does not reach into the
    // process-global config service.
    float minViewMag = 2.0f;
    float maxViewMag = 300.0f;
    float minCameraHeight = 1.5f;
    float cameraCollisionThreshold = 0.01f;
};

class Camera
{
    friend CameraCollection;                                                 // CameraCollection is the only owner/caller of raw camera state.

  public:
    enum class TravelDirection
    {
        Forward,
        Left,
        Right,
        Backward
    }; // Camera-local keyboard travel command consumed by MoveCamera.

  private:
    Math::Vector::Vector3 m_position;                                        // Camera eye in world space.
    Math::Vector::Vector3 m_view;                                            // World-space look target paired with m_position.
    Math::Vector::Vector3 m_upVector;                                        // Camera up basis used for pitch caps and view matrices.
    Math::Vector::Vector3 m_movementBuffer;                                  // Candidate translation staged until XZ bounds accept it.
    float m_viewMagnitude;                                                   // Eye-to-view distance preserved by locked and bounded movement.
    bool m_isFinishedTranslationRecursed;                                    // Guard for the single repair pass inside FinishTranslation.
    bool m_doCalculateViewMagnitude;                                         // Next translation refreshes m_viewMagnitude before preserving it.
    bool m_doPreserveViewMagnitude;                                          // Translation must recover the previous eye-to-view distance.
    bool m_isLockedMode;                                                     // Locked orbit mode moves the eye while keeping the view target fixed.
    Geometry::XZBounds m_boundary;                                           // Horizontal movement limits for this camera.
    Geometry::XZCoords m_xzStore;                                            // Scratch coordinates used by bounds checks.

    Camera();
    ~Camera() = default;
    void PrepareTranslation();                                               // Starts a bounded translation and captures state needed by FinishTranslation.
    void FinishTranslation( const CameraMovementSettings& settings );        // Commits or clamps the staged translation while preserving view distance.
    void ApplyMovementBuffer( const CameraMovementSettings& settings );      // Commits m_movementBuffer after bounds and

    // locked-mode rules are enforced.
    void ZeroCamera();                                                       // Reset path for scene camera slots before authored values are loaded.
    Math::Vector::Vector3 GetViewVectorNormalised();
    Math::Vector::Vector3 GetViewVectorRaw();
    Math::Vector::Vector3 GetRightVector();
    float
    UpVectorViewVectorRotationCap( float requestRadians,
                                   const CameraMovementSettings& settings ); // Caps pitch so view and up vectors cannot

    // collapse into the same direction.
    void RecoverViewMagnitude( bool isOnBoundX, bool isOnBoundZ,
                               const CameraMovementSettings& settings );     // One-axis bound clamps may need a guarded

    // repair pass to restore eye-to-view distance.
    void SetAll( const Math::Vector::Vector3& position, const Math::Vector::Vector3& view, const Math::Vector::Vector3& up );
    void MoveCamera( const TravelDirection direction, float amount,
                     const CameraMovementSettings& settings );               // amount is world-space travel along camera-local axes.

    void RotateCamera( float xMove, float yMove,
                       const CameraMovementSettings& settings );             // Mouse-look delta path; clamps pitch before mutating the view target.

    Camera& operator=( const Camera& target );
    Camera& operator+=( const Camera& target );
    Camera operator-( const Camera& target );
    Camera operator*( float f );
};
} // namespace Environment
} // namespace SkullbonezCore
