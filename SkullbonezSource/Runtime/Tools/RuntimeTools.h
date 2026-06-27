/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.h
Purpose:
  Owns transient runtime tool state while tool behavior moves out of Run.

Mental model:
  RuntimeTools is the Phase 6 compatibility boundary. Existing Run methods can
  still execute launcher/tool behavior, but launcher state and render feedback
  ownership live here instead of directly on Run.
*/
#pragma once

#include "../../Core/Common.h"
#include "../Editor/LauncherLaser.h"
#include "../RuntimeCameraMode.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/CollisionShape.h"
#include "../../UI/UITabEditor.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace SkullbonezCore::GameObjects
{
class GameModel;
class GameModelCollection;
} // namespace SkullbonezCore::GameObjects

namespace SkullbonezCore::Geometry
{
class Terrain;
}

namespace SkullbonezCore::Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace SkullbonezCore::Environment

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

    std::array<RunRayCastTestLine, MAX_LINES> lines = {};
    int nextLine = 0;
    RunLauncherFireMode fireMode = RunLauncherFireMode::Laser;
    bool visualizeRays = false;
    float impulseStrength = 1800.0f;
    float projectileSpeed = 160.0f;
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
    int modelIndex = -1;
    Math::Vector::Vector3 planePoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 planeNormal = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
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
    int selectedModelIndex = -1;
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
    Math::Orientation::Quaternion gizmoDragStartOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::CollisionDetection::CollisionShape gizmoDragStartShape;
    int gizmoDragGroupCount = 0;
    std::array<int, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupIndices = {};
    std::array<Math::Vector::Vector3, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupStartPositions = {};
    std::array<Math::Orientation::Quaternion, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupStartOrientations = {};
};

class RunEditorTracer
{
  private:
    std::vector<float> m_lineData;

    void EmitLine( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitArrow( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitRing( const Math::Vector::Vector3& center, int axis, float radius, float r, float g, float bl );
    void EmitSphere( const Math::Vector::Vector3& center, float radius, float r, float g, float bl );
    void EmitBox( const Math::Vector::Vector3& center,
                  const Math::Vector::Vector3& xAxis,
                  const Math::Vector::Vector3& yAxis,
                  const Math::Vector::Vector3& zAxis,
                  float r,
                  float g,
                  float bl );

  public:
    RunEditorTracer();
    void Clear();
    void AddPlacementRay( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& hitPoint );
    void AddPlacementGhost( int objectType,
                            const Math::Vector::Vector3& center,
                            const Math::Vector::Vector3& terrainPoint,
                            const Math::Vector::Vector3& placementScale,
                            const Math::Orientation::Quaternion& orientation );
    void
    AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, float alpha, bool hit );
    void AddReplayPathSegment( const Math::Vector::Vector3& start,
                               const Math::Vector::Vector3& end,
                               float r,
                               float g,
                               float b );
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
    void AddReplayFutureTargetMarker( const Math::Vector::Vector3& center, float radius, int depth );
    void AddReplayTargetMarker( const GameObjects::GameModel& model );
    void AddAttachedCameraTargetMarker( const GameObjects::GameModel& model, bool activeFollow );
    void AddSelectionOutline( const GameObjects::GameModel& model );
    void AddGizmo( const Math::Vector::Vector3& origin,
                   float radius,
                   int hotTranslateAxis,
                   int hotRotationAxis,
                   int activeAxis,
                   bool activeRotation,
                   bool scaleMode,
                   bool activeScale );
    void AddReplayVelocityGizmo( const GameObjects::GameModel& model,
                                 int hotLinearAxis,
                                 int hotAngularAxis,
                                 int activeAxis,
                                 bool activeAngular );
    void Render( const Math::Transformation::Matrix4& viewProjection );
};

class RuntimeTools
{
  public:
    RunRayCastTestState& RayCastTest();
    const RunRayCastTestState& RayCastTest() const;
    void ClearRayCastTestLines();
    void AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, bool hit );
    void TickRayCastTestLines( float dt );
    void BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const;
    void RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample );
    bool TryRayCastTestHit( const std::vector<GameObjects::GameModel>& models,
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
                          Environment::WorldEnvironment& world,
                          Geometry::Terrain* terrain,
                          int activeModelCapacity,
                          const Math::Vector::Vector3& rayOrigin,
                          const Math::Vector::Vector3& rayDirection,
                          const Math::Vector::Vector3& cameraUp );
    void FireLauncherLaser( GameObjects::GameModelCollection& collection,
                            Geometry::Terrain* terrain,
                            const Math::Vector::Vector3& rayOrigin,
                            const Math::Vector::Vector3& rayDirection,
                            const Math::Vector::Vector3& cameraUp );
    bool FireLauncherProjectile( GameObjects::GameModelCollection& collection,
                                 Environment::WorldEnvironment& world,
                                 Geometry::Terrain* terrain,
                                 int activeModelCapacity,
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
