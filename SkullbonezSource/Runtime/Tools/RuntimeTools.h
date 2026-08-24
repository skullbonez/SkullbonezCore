/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.h
Purpose:
  Owns transient runtime tool state while tool behavior moves out of Run.

Summary:
  RuntimeTools groups bounded launcher, pickup, replay, and overlay tool state
  and applies their commands through explicitly borrowed owners.
  RuntimeTools owns tool payload and render feedback instead of storing those
  values directly on Run. RuntimeInteractionController alone owns which
  gesture is active; tools retain only the start values needed to apply it.

Glossary:
  Tool state: Runtime-owned launcher, mouse-pickup, and overlay-trace data that
    persists between frames.
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
  - Scene-clear observation cancels typed capture before clearing tool payload;
    it never retains the borrowed replacement world or interaction owners.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayPresentation.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include "../../Core/Common.h"
#include "../../Core/MainMemoryStats.h"
#include "EditorTracer.h"
#include "LauncherLaser.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Scene/SceneLifecycle.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../Replay/ReplayEventCommand.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore::Core
{
class SbDiagnosticStore;
struct CinematicRenderConfig;
struct ReplayTrajectoryAppearanceConfig;
} // namespace SkullbonezCore::Core

namespace SkullbonezCore::Runtime
{
class SceneWorld;
} // namespace SkullbonezCore::Runtime

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
class Ray;
class Terrain;
}

namespace SkullbonezCore::Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace SkullbonezCore::Environment

namespace SkullbonezCore::Rendering
{
class Dx12GeometryOwner;
} // namespace SkullbonezCore::Rendering

namespace SkullbonezCore::UI
{
struct UIPhysicsCommands;
} // namespace SkullbonezCore::UI

namespace SkullbonezCore::Runtime
{
struct RunLaunchOptions;
struct SceneSessionState;
struct ReplayLauncherVisualSample;
class SceneEntityStore;
class InputRouter;
class RuntimeInteractionController;
enum class WorldInteractionOwner;
enum class InteractionExitReason;
struct RuntimeInteractionCommand;
struct RuntimeInteractionEvent;
struct RuntimeInteractionSelectionPlan;

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
    static constexpr std::size_t MAX_LINES = REPLAY_LAUNCHER_RAY_LINE_CAPACITY;

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

struct ToolOverlayBuildInput
{
    float rayLingerSeconds = 0.0f;
    bool inspectGizmoActive = false;
    bool scaleMode = false;
    RuntimeInteractionGesture gesture;
    int attachedCameraTargetIndex = -1;
    bool attachedCameraActiveFollow = false;
};

struct ToolEditorOverlayValues
{
    static constexpr std::size_t SELECTION_CAPACITY = 16;
    bool editorModeEnabled = false;
    bool placementModeEnabled = false;
    bool placementPreviewVisible = false;
    int objectType = 0;
    int hotGizmoAxis = -1;
    int hotRotationAxis = -1;
    Math::Vector::Vector3 placementTerrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementCenter = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementRayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementRayHit = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementScale = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion placementOrientation = Math::Orientation::IDENTITY_QUATERNION;
    std::array<Physics::PhysicsBodyHandle, SELECTION_CAPACITY> selectionBodies = {};
    std::array<Physics::PhysicsColliderHandle, SELECTION_CAPACITY> selectionColliders = {};
    std::size_t selectionCount = 0;
};

#ifdef _DEBUG
// Lifetime: a synchronous Debug dump borrows SceneWorld once and resolves its
// cameras, terrain, entities, and physics rows locally. Scalar process policy
// values are copied or borrowed only until the file write returns.
struct LauncherReproSnapshotContext
{
    SceneWorld& world;
    const SceneSessionState& sceneState;
    const std::string* currentScenePath;
    const RunLaunchOptions& launchOptions;
    bool physicsSleepEnabled;
    bool vsyncEnabled;
    bool pipelineSyncEnabled;
    float contactEpsilon;                                                                          // Physics contact tolerance captured from Run config for repro output.
    float frictionCoeff;                                                                           // Physics friction setting captured from Run config for repro output.
    bool waterHidden;
    bool terrainHidden;
    bool collisionVisualizer;
    const char* rendererName;
    double simulationSeconds;
};

enum class LauncherReproSnapshotStatus
{
    Wrote,
    NoTarget,
    WriteFailed
};

struct LauncherReproSnapshotResult
{
    LauncherReproSnapshotStatus status = LauncherReproSnapshotStatus::WriteFailed;
    std::array<char, 128> message = {};
    double messageUntil = 0.0;
};
#endif

struct RunMousePickupState
{
    Physics::PhysicsBodyHandle body;
    Math::Vector::Vector3 planePoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 planeNormal = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    float cameraPlaneDistance = 0.0f;                                                              // World units from camera eye to the camera-facing pickup plane.
    Math::Vector::Vector3 grabOffset = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 targetPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 preservedAngularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 lastImpulse = Math::Vector::ZERO_VECTOR;
};

struct MousePickupPointerResult
{
    bool consumed = false;                                                                         // Prevents later world owners from seeing this pointer gesture.
    bool enteredInteractive = false;                                                               // Composition disables automation quit after a successful grab begins.
};

struct LauncherPointerInput
{
    bool launcherMode = false;
    bool leftPressed = false;
    bool suppressWorldAction = false;
    bool uiWantsNativeCursor = false;
    int activeModelCapacity = 0;
};

struct LauncherPointerResult
{
    ReplayEventCommand replayEvent;
    bool consumed = false;
    bool enteredInteractive = false;
    bool recordReplayEvent = false;
};

class RuntimeTools
{
  public:
    explicit RuntimeTools( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
        : m_resultDiagnostics( resultDiagnostics ), m_editorTracer( resultDiagnostics )
    {
    }

    RunRayCastTestState& RayCastTest();
    bool ApplyRayCastVisualizationUICommand( const UI::UIPhysicsCommands& commands );
    RayCastLauncherTuningUICommandResult ApplyRayCastLauncherTuningUICommands( const UI::UIPhysicsCommands& commands );
    void ClearRayCastTestLines();
    void AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, bool hit );
    void TickRayCastTestLines( float dt );
    bool HasLingeredRayCastLine( float maxAgeSeconds ) const;
    bool HasSelectionOverlayWork( const ToolEditorOverlayValues& editor, int modelCount, RunCameraMode cameraMode ) const;
    bool HasMousePickupOverlayWork( const RuntimeInteractionGesture& gesture ) const;
    bool HasLauncherShots() const;
    const char* LauncherFireModeLabel() const;
    void BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const;
    void RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample );
    bool TryRayCastTestHit( const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                            const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                            float maxDistance, int& outIndex, float& outT ) const;
    bool TryLauncherTerrainHit( Geometry::Terrain* terrain, const Math::Vector::Vector3& rayOrigin,
                                const Math::Vector::Vector3& rayDirection, float maxDistance, float& outT ) const;
    bool TryBuildLauncherCameraRay( Environment::CameraCollection* cameras, Math::Vector::Vector3& outOrigin,
                                    Math::Vector::Vector3& outDirection, Math::Vector::Vector3& outCameraUp ) const;
    bool FireLauncherRay( SceneWorld& world, SceneSessionState& scene, int activeModelCapacity,
                          const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                          const Math::Vector::Vector3& cameraUp );
    LauncherPointerResult RouteLauncherPointer( const LauncherPointerInput& input, SceneWorld& world,
                                                SceneSessionState& scene );
    void FireLauncherLaser( Physics::PhysicsEngine& physics, int modelCount, Geometry::Terrain* terrain,
                            const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                            const Math::Vector::Vector3& cameraUp );
    bool FireLauncherProjectile( SceneWorld& world, SceneSessionState& scene, int activeModelCapacity, int modelCount,
                                 const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                                 const Math::Vector::Vector3& cameraUp );
#ifdef _DEBUG
    bool PickLauncherReproTarget( const SceneWorld& world, int& outIndex, float& outRayT,
                                  float& outCrosshairDistance ) const;
    LauncherReproSnapshotStatus WriteLauncherReproSnapshot( const LauncherReproSnapshotContext& context ) const;
    LauncherReproSnapshotResult WriteLauncherReproSnapshotWithStatusMessage( const LauncherReproSnapshotContext& context ) const;
#endif

    LauncherLaser& Laser();
    const LauncherLaser& Laser() const;

    RunMousePickupState& MousePickup();

    // Called only after editor routing declines the pointer and composition
    // proves manipulator mode is the active world owner. All borrows expire
    // before the method returns; pickup retains only its typed body handle and
    // camera-plane values.
    MousePickupPointerResult
    RouteMousePickupPointer( const RuntimePointerEvent& pointer, bool hasWorldRay, const Geometry::Ray& worldRay,
                             bool hasClampedWorldRay, const Geometry::Ray& clampedWorldRay,
                             const Math::Vector::Vector3& cameraEye, const Math::Vector::Vector3& cameraView,
                             const SceneWorld& world, InputRouter& inputRouter, RuntimeInteractionController& interaction );

    // Applies the manipulator spring at the fixed-step boundary. Tool state is
    // owned here; scene physics and input/interaction owners are synchronous borrows.
    void ApplyMousePickupPhysicsStep( SceneWorld& world, InputRouter& inputRouter,
                                      RuntimeInteractionController& interaction );
    void RestoreMousePickupAngularVelocity( SceneWorld& world, InputRouter& inputRouter,
                                            RuntimeInteractionController& interaction );
    void CancelMousePickup( InputRouter& inputRouter, RuntimeInteractionController& interaction );
    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet, InputRouter& inputRouter,
                                RuntimeInteractionController& interaction );

    EditorTracer& Tracer();

    // Rebuilds the fixed-capacity tool draw records before RuntimeRenderer
    // submits them. World/model/asset owners remain borrowed for this call.
    void PrepareOverlayTrace( SceneWorld& world, const ToolEditorOverlayValues& editor,
                              const ToolOverlayBuildInput& input );

  private:
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    RunRayCastTestState m_rayCastTest;
    LauncherLaser m_laser;
    RunMousePickupState m_mousePickup;
    EditorTracer m_editorTracer;
    SceneLifecycleGenerationObserver m_sceneLifecycleObserver;
};
} // namespace SkullbonezCore::Runtime
