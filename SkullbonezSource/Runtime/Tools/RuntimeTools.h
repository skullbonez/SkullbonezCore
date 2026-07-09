/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.h
Purpose:
  Owns transient runtime tool state while tool behavior moves out of Run.

Mental model:
  RuntimeTools owns short-lived interaction state for launcher/tool behavior and
  render feedback instead of storing that state directly on Run.

Glossary:
  Asset system: Runtime-owned registry borrowed by editor ghost tracing when a
    placeable recipe comes from an asset library.
  Tool state: Runtime-owned launcher, mouse-pickup, editor, and overlay-trace
    data that persists between frames.
  Replay visual sample: Compact snapshot of tool visuals restored while replay
    scrubbing so debug feedback follows recorded frames.
  Replay target marker: Debug overlay outline/ring drawn around a replay body
    from live body/collider store values.
  Replay ribbon: Camera-facing overlay stroke generated from replay path or
    marker segments so the shader can apply smooth edges and glow.
  Gizmo drag group: Bounded set of selected model indices transformed as one
    editor gesture.
  Body store: Physics-owned dense body rows borrowed by tool hit tests and
    command paths without reading mirrored GameModel body state.
  Collider store: Physics-owned dense collider rows borrowed for shape-derived
    hit-test bounds.
  Physics body handle: Generational id for a live simulation body row; runtime
    tools store it when they need to issue physics commands.
  Model row hint: Cached dense model-order row paired with stable body/collider
    handles; resolve it before use because collection edits can move rows.
  Ring buffer: Fixed-size history where new launcher/raycast entries overwrite
    the oldest slots.
  Launcher tuning command: One-frame Physics-tab packet that edits launcher
    raycast visualization, impulse strength, or projectile speed.

Invariants:
  - RuntimeTools owns transient tool state only; world, model, terrain, camera,
    asset, and physics services are borrowed through method parameters.
  - Fixed-capacity arrays must stay bounded and replay-restorable.
  - Stored model-row hints must be resolved from stable handles/ids before use
    after model collection edits.
  - Mouse pickup stores only a physics body handle for live command paths; any
    model row used for gestures or UI is resolved locally from that handle.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
*/
#pragma once

#include "../../Core/Common.h"
#include "../Editor/LauncherLaser.h"
#include "../RuntimeCameraMode.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../UI/UITabEditor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore::GameObjects
{
class GameModelCollection;
} // namespace SkullbonezCore::GameObjects

namespace SkullbonezCore::Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Assets
{
class AssetSystem;
}

namespace SkullbonezCore::Geometry
{
class Terrain;
}

namespace SkullbonezCore::Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace SkullbonezCore::Environment

namespace SkullbonezCore::Rendering
{
class IRenderCommandContext;
} // namespace SkullbonezCore::Rendering

namespace SkullbonezCore::UI
{
struct UIPhysicsCommands;
} // namespace SkullbonezCore::UI

namespace SkullbonezCore::Basics
{
struct RunDebugState;
struct RunLaunchOptions;
struct RunRuntimeSettings;
struct RunSceneState;
struct ReplayLauncherVisualSample;

struct RunRayCastTestLine
{
    Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
    float ageSeconds = 0.0f;
    bool active = false;
    bool hit = false;
};

enum class RunLauncherFireMode
{
    Laser,
    Projectile
};

struct RunRayCastTestState
{
    static constexpr std::size_t MAX_LINES = 64;

    // Invariant: Ray lines are a visual ring buffer. Recording/replay restores
    // the cursor and line payload, so wrap behavior is part of the replay ABI.
    std::array<RunRayCastTestLine, MAX_LINES> lines = {};
    int nextLine = 0;
    RunLauncherFireMode fireMode = RunLauncherFireMode::Laser;
    bool visualizeRays = false;
    float impulseStrength = 1800.0f;
    float projectileSpeed = 160.0f;
};

struct RayCastLauncherTuningUICommandResult
{
    bool setImpulseStrength = false;
    bool setProjectileSpeed = false;
    // Invariant: replay records one config event per accepted slider request.
    // Each payload captures launcher values immediately after that request, so
    // same-frame impulse and projectile edits replay in the original order.
    uint32_t impulseConfigChangedFlags = 0;
    float impulseConfigImpulseStrength = 0.0f;
    float impulseConfigProjectileSpeed = 0.0f;
    uint32_t projectileConfigChangedFlags = 0;
    float projectileConfigImpulseStrength = 0.0f;
    float projectileConfigProjectileSpeed = 0.0f;
};

#ifdef _DEBUG
struct LauncherReproSnapshotContext
{
    GameObjects::GameModelCollection& collection;
    Environment::CameraCollection* cameras;
    Geometry::Terrain* terrain;
    Environment::WorldEnvironment& world;
    const RunSceneState& sceneState;
    const std::string* currentScenePath;
    const RunLaunchOptions& launchOptions;
    const RunRuntimeSettings& runtimeSettings;
    float contactEpsilon;                                                   // Physics contact tolerance captured from Run config for repro output.
    float frictionCoeff;                                                    // Physics friction setting captured from Run config for repro output.
    const RunDebugState& debug;
    const char* rendererName;
    double simulationSeconds;
};

enum class LauncherReproSnapshotStatus
{
    Wrote,
    NoTarget,
    WriteFailed
};
#endif

struct RunMousePickupState
{
    bool active = false;
    bool mouseCaptured = false;
    Physics::PhysicsBodyHandle body;
    Math::Vector::Vector3 planePoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 planeNormal = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    float cameraPlaneDistance = 0.0f;                                       // World units from camera eye to the camera-facing pickup plane.
    Math::Vector::Vector3 grabOffset = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 targetPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 preservedAngularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 lastImpulse = Math::Vector::ZERO_VECTOR;
};

struct RunEditorPlacementState
{
    static constexpr std::size_t GIZMO_DRAG_GROUP_CAPACITY = 16;

    bool editorModeEnabled = false;
    bool placementModeEnabled = false;
    bool placeStaticObject = false;
    bool autoTerrainAlign = false;
    RunCameraMode restoreCameraModeAfterEditor = RunCameraMode::Demo;
    bool viewportLookActive = false;
    bool placementPreviewVisible = false;
    bool placementScaleActive = false;
    bool gizmoDragActive = false;
    bool gizmoDragIsRotation = false;
    bool gizmoDragIsScale = false;
    bool altShortcutWasDown = false;
    bool tabShortcutWasDown = false;
    bool tildeShortcutWasDown = false;
    int objectType = UI::EditorTab::OBJECT_BOX;
    int placedObjectSerial = 0;
    // Lifetime: selectedBody/selectedCollider are live store identities. The
    // row hint accelerates UI/report lookups but is repaired from selectedBody.
    Physics::ModelRowHint selectedModelRow;
    Physics::PhysicsBodyHandle selectedBody;
    Physics::PhysicsColliderHandle selectedCollider;
    int hotGizmoAxis = -1;
    int hotRotationAxis = -1;
    int activeGizmoAxis = -1;
    float gizmoDragStartAxisT = 0.0f;
    float gizmoDragStartRotationAngle = 0.0f;
    float placementYawRadians = 0.0f;
    int placementAltitudeSteps = 0;
    int placementScaleWheelSteps = 0;
    Math::Vector::Vector3 placementTerrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementCenter = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementRayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementRayHit = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementScale = Math::Vector::Vector3( 6.0f, 6.0f, 6.0f );
    Math::Vector::Vector3 placementScaleStart = Math::Vector::Vector3( 6.0f, 6.0f, 6.0f );
    Math::Vector::Vector3 placementScaleTerrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementScaleRayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion placementOrientation = Math::Orientation::IDENTITY_QUATERNION;
    POINT placementScaleStartClient = {};
    Math::Vector::Vector3 gizmoDragStartPosition = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 gizmoDragPlaneNormal = Math::Vector::ZERO_VECTOR; // Unit normal frozen for an axis drag.
    Math::Orientation::Quaternion gizmoDragStartOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::CollisionDetection::CollisionShape gizmoDragStartShape;
    // Lifetime: Drag-group indices and start transforms are valid only for the
    // active gesture that captured them.
    int gizmoDragGroupCount = 0;
    std::array<int, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupIndices = {};
    std::array<Math::Vector::Vector3, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupStartPositions = {};
    std::array<Math::Orientation::Quaternion, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupStartOrientations = {};
};

class RunEditorTracer
{
  private:
    struct ReplayRibbonStyle
    {
        float width = 0.25f;                                                // World-space ribbon width.
        float alpha = 0.80f;                                                // Blend weight before shader edge falloff.
        float edgeFeather = 0.38f;                                          // Fraction of half-width used for antialias fading.
        float hdrScale = 1.0f;                                              // Brightness multiplier for bloom/emphasis.
    };

    std::vector<float> m_lineData;
    std::vector<float> m_priorityLineData;
    std::vector<float> m_renderLineData;
    std::vector<float> m_replayRibbonSegments;                              // Packed 13-float replay segments before camera-facing expansion.
    std::vector<float>
        m_priorityReplayRibbonSegments;                                     // Retained causal marker segments that survive ordinary path overflow.
    std::vector<float> m_replayRibbonVertexData;                            // Packed 11-float vertices consumed by the soft-additive ribbon style.

    void EmitLineTo( std::vector<float>& lineData,
                     const Math::Vector::Vector3& a,
                     const Math::Vector::Vector3& b,
                     float r,
                     float g,
                     float bl );
    void EmitLine( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitArrow( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitRing( const Math::Vector::Vector3& center, int axis, float radius, float r, float g, float bl );
    void EmitSphereTo( std::vector<float>& lineData,
                       const Math::Vector::Vector3& center,
                       float radius,
                       float r,
                       float g,
                       float bl );
    void EmitSphere( const Math::Vector::Vector3& center, float radius, float r, float g, float bl );
    void EmitBoxTo( std::vector<float>& lineData,
                    const Math::Vector::Vector3& center,
                    const Math::Vector::Vector3& xAxis,
                    const Math::Vector::Vector3& yAxis,
                    const Math::Vector::Vector3& zAxis,
                    float r,
                    float g,
                    float bl );
    void EmitBox( const Math::Vector::Vector3& center,
                  const Math::Vector::Vector3& xAxis,
                  const Math::Vector::Vector3& yAxis,
                  const Math::Vector::Vector3& zAxis,
                  float r,
                  float g,
                  float bl );
    void EmitShapeOutlineTo( std::vector<float>& lineData,
                             const Math::Vector::Vector3& position,
                             const Math::Orientation::Quaternion& orientation,
                             const Math::CollisionDetection::CollisionShape& shape,
                             float r,
                             float g,
                             float b );
    void EmitShapeOutline( const Math::Vector::Vector3& position,
                           const Math::Orientation::Quaternion& orientation,
                           const Math::CollisionDetection::CollisionShape& shape,
                           float r,
                           float g,
                           float b );
    void EmitReplayRibbonSegmentTo( std::vector<float>& ribbonData,
                                    const Math::Vector::Vector3& a,
                                    const Math::Vector::Vector3& b,
                                    float r,
                                    float g,
                                    float bl,
                                    const ReplayRibbonStyle& style );
    void EmitReplayRibbonGlowPairTo( std::vector<float>& ribbonData,
                                     const Math::Vector::Vector3& a,
                                     const Math::Vector::Vector3& b,
                                     float r,
                                     float g,
                                     float bl,
                                     const ReplayRibbonStyle& glow,
                                     const ReplayRibbonStyle& core );
    void EmitReplayRibbonShapeOutlineTo( std::vector<float>& ribbonData,
                                         const Math::Vector::Vector3& position,
                                         const Math::Orientation::Quaternion& orientation,
                                         const Math::CollisionDetection::CollisionShape& shape,
                                         float r,
                                         float g,
                                         float b,
                                         const ReplayRibbonStyle& style );
    void BuildReplayRibbonVertices( const Math::Vector::Vector3& cameraEye, const Math::Vector::Vector3& cameraUp );

  public:
    RunEditorTracer();
    void Clear();
    void AddPlacementRay( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& hitPoint );
    void AddPlacementGhost( int objectType,
                            const Math::Vector::Vector3& center,
                            const Math::Vector::Vector3& terrainPoint,
                            const Math::Vector::Vector3& placementScale,
                            const Math::Orientation::Quaternion& orientation,
                            const Assets::AssetSystem& assets );
    void
    AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, float alpha, bool hit );
    void AddReplayPathSegment( const Math::Vector::Vector3& start,
                               const Math::Vector::Vector3& end,
                               float r,
                               float g,
                               float b );
    void AddReplayCausalTrailSegment( const Math::Vector::Vector3& start,
                                      const Math::Vector::Vector3& end,
                                      float r,
                                      float g,
                                      float b );
    // Draws the cold baseline root path with smooth replay ribbons so old-vs-new
    // butterfly-effect captures remain readable under bloom/glow.
    void AddReplayBaselinePathSegment( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end );
    void AddReplayContactMarker( const Math::Vector::Vector3& point,
                                 const Math::Vector::Vector3& normal,
                                 float r,
                                 float g,
                                 float b );
    void AddReplayImpulseVector( const Math::Vector::Vector3& point,
                                 const Math::Vector::Vector3& impulse,
                                 float r,
                                 float g,
                                 float b );
    // Draws the downstream replay collision marker from the exact collider
    // shape at the predicted contact frame. Callers pass explicit pose/shape so
    // future-node overlays never fall back to broadphase radius rings.
    void AddReplayFutureTargetMarker( const Math::Vector::Vector3& position,
                                      const Math::Orientation::Quaternion& orientation,
                                      const Math::CollisionDetection::CollisionShape& shape,
                                      int depth );
    // Draws the yellow causal-entry outline: a predicted body's in-place pose
    // at the prediction start (perfect formation for a wall brick). Pose comes
    // from prediction samples, never from live model state.
    void AddReplayCausalEntryMarker( const Math::Vector::Vector3& position,
                                     const Math::Orientation::Quaternion& orientation,
                                     const Math::CollisionDetection::CollisionShape& shape );
    // Draws the grey causal-rest outline: a predicted body's final resting
    // pose. Callers place it only when the completed prediction ends with the
    // body at rest; bodies still moving at the horizon get no grey box.
    void AddReplayCausalRestMarker( const Math::Vector::Vector3& position,
                                    const Math::Orientation::Quaternion& orientation,
                                    const Math::CollisionDetection::CollisionShape& shape );
    void AddReplayCausalHorizonMarker( const Math::Vector::Vector3& position,
                                       const Math::Orientation::Quaternion& orientation,
                                       const Math::CollisionDetection::CollisionShape& shape );
    // Draws cold baseline entry/rest outlines from the retained old future.
    // Callers pass explicit pose/shape; live model state is not consulted.
    void AddReplayBaselineEntryMarker( const Math::Vector::Vector3& position,
                                       const Math::Orientation::Quaternion& orientation,
                                       const Math::CollisionDetection::CollisionShape& shape );
    void AddReplayBaselineRestMarker( const Math::Vector::Vector3& position,
                                      const Math::Orientation::Quaternion& orientation,
                                      const Math::CollisionDetection::CollisionShape& shape );
    // Draws a replay target marker from explicit store values. Replay may still
    // resolve identity by model order, but marker geometry must not read legacy
    // model-side body state.
    void AddReplayTargetMarker( const Math::Vector::Vector3& position,
                                const Math::Orientation::Quaternion& orientation,
                                const Math::CollisionDetection::CollisionShape& shape,
                                float radius );
    void AddAttachedCameraTargetMarker( const Math::Vector::Vector3& position,
                                        const Math::Orientation::Quaternion& orientation,
                                        const Math::CollisionDetection::CollisionShape& shape,
                                        float radius,
                                        bool activeFollow );
    // Draws a shape-accurate outline from explicit pose/shape values. Replay
    // velocity edit uses this so overlay drawing does not need legacy model-side
    // body state.
    void AddSelectionOutline( const Math::Vector::Vector3& position,
                              const Math::Orientation::Quaternion& orientation,
                              const Math::CollisionDetection::CollisionShape& shape );
    void AddGizmo( const Math::Vector::Vector3& origin,
                   float radius,
                   int hotTranslateAxis,
                   int hotRotationAxis,
                   int activeAxis,
                   bool activeRotation,
                   bool scaleMode,
                   bool activeScale );
    void AddReplayVelocityGizmo( const Math::Vector::Vector3& origin,
                                 const Math::Orientation::Quaternion& orientation,
                                 const Math::CollisionDetection::CollisionShape& shape,
                                 float radius,
                                 const Math::Vector::Vector3& linearVelocity,
                                 const Math::Vector::Vector3& angularVelocity,
                                 int hotLinearAxis,
                                 int hotAngularAxis,
                                 int activeAxis,
                                 bool activeAngular );
    void Render( const Math::Transformation::Matrix4& viewProjection,
                 const Math::Vector::Vector3& cameraEye,
                 const Math::Vector::Vector3& cameraUp,
                 Rendering::IRenderCommandContext& renderCommands );
};

class RuntimeTools
{
  public:
    RunRayCastTestState& RayCastTest();
    const RunRayCastTestState& RayCastTest() const;
    bool ApplyRayCastVisualizationUICommand( const UI::UIPhysicsCommands& commands );
    RayCastLauncherTuningUICommandResult ApplyRayCastLauncherTuningUICommands( const UI::UIPhysicsCommands& commands );
    void ClearRayCastTestLines();
    void AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, bool hit );
    void TickRayCastTestLines( float dt );
    bool HasLingeredRayCastLine( float maxAgeSeconds ) const;
    bool HasSelectionOverlayWork( int modelCount, RunCameraMode cameraMode ) const;
    bool HasMousePickupOverlayWork() const;
    bool HasLauncherShots() const;
    const char* LauncherFireModeLabel() const;
    void BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const;
    void RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample );
    bool TryRayCastTestHit( const Physics::PhysicsBodyStore& bodyStore,
                            const Physics::ColliderStore& colliderStore,
                            const Math::Vector::Vector3& rayOrigin,
                            const Math::Vector::Vector3& rayDirection,
                            float maxDistance,
                            int& outIndex,
                            float& outT ) const;
    bool TryLauncherTerrainHit( Geometry::Terrain* terrain,
                                const Math::Vector::Vector3& rayOrigin,
                                const Math::Vector::Vector3& rayDirection,
                                float maxDistance,
                                float& outT ) const;
    bool TryBuildLauncherCameraRay( Environment::CameraCollection* cameras,
                                    Math::Vector::Vector3& outOrigin,
                                    Math::Vector::Vector3& outDirection,
                                    Math::Vector::Vector3& outCameraUp ) const;
    bool FireLauncherRay( GameObjects::GameModelCollection& collection,
                          RunSceneState& scene,
                          Geometry::Terrain* terrain,
                          int activeModelCapacity,
                          const Math::Vector::Vector3& rayOrigin,
                          const Math::Vector::Vector3& rayDirection,
                          const Math::Vector::Vector3& cameraUp );
    void FireLauncherLaser( Physics::PhysicsEngine& physics,
                            int modelCount,
                            Geometry::Terrain* terrain,
                            const Math::Vector::Vector3& rayOrigin,
                            const Math::Vector::Vector3& rayDirection,
                            const Math::Vector::Vector3& cameraUp );
    bool FireLauncherProjectile( GameObjects::GameModelCollection& collection,
                                 Physics::PhysicsEngine& physics,
                                 RunSceneState& scene,
                                 Geometry::Terrain* terrain,
                                 int activeModelCapacity,
                                 int modelCount,
                                 const Math::Vector::Vector3& rayOrigin,
                                 const Math::Vector::Vector3& rayDirection,
                                 const Math::Vector::Vector3& cameraUp );
#ifdef _DEBUG
    bool PickLauncherReproTarget( GameObjects::GameModelCollection& collection,
                                  Environment::CameraCollection* cameras,
                                  int& outIndex,
                                  float& outRayT,
                                  float& outCrosshairDistance ) const;
    LauncherReproSnapshotStatus WriteLauncherReproSnapshot( const LauncherReproSnapshotContext& context ) const;
    LauncherReproSnapshotStatus
    WriteLauncherReproSnapshotWithStatusMessage( const LauncherReproSnapshotContext& context,
                                                 RunDebugState& debug ) const;
#endif

    LauncherLaser& Laser();
    const LauncherLaser& Laser() const;

    RunMousePickupState& MousePickup();
    const RunMousePickupState& MousePickup() const;

    RunEditorPlacementState& Editor();
    const RunEditorPlacementState& Editor() const;

    RunEditorTracer& EditorTracer();
    const RunEditorTracer& EditorTracer() const;

  private:
    RunRayCastTestState m_rayCastTest;
    LauncherLaser m_laser;
    RunMousePickupState m_mousePickup;
    RunEditorPlacementState m_editor;
    RunEditorTracer m_editorTracer;
};
} // namespace SkullbonezCore::Basics
