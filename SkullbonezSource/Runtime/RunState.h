/*
File: SkullbonezSource/Runtime/RunState.h
Purpose:
  Collects Run-owned state aggregates that are shared by split runtime files.

Mental model:
  Run remains the composition root, but its state shelves are named here so the
  public Run facade is no longer a catalogue of every timer, toggle, and render
  resource aggregate. Narrower owners should move state out of this file over
  time; this header is a staging boundary, not a destination for new features.

Glossary:
  State shelf: Run-owned aggregate that groups related fields while split
  implementation files are being decomposed.
  Runtime setting: Live toggle or tuning value applied while a scene is running.
  Contact-audio flash mode: Render-only selector for which audio decisions get
    a body flash after physics, independent of deterministic simulation.
  Attached camera target: Camera-owned follow identity that stores physics
    handles for live motion and keeps model-order facts only as UI fallback.
  Director playback: Presentation-owned shot-list state that times authored
    camera/style phases without changing deterministic physics state.
  Borrowed subsystem pointer: Non-owning pointer to state owned elsewhere in
    the Run composition root.
  Interaction automation: CLI-driven validation state that injects bounded
    input snapshots, then verifies runtime-owned state through JSON reports.

Invariants:
  - Owning state should use value members or smart pointers; raw pointers here
    are borrowed subsystem links and must be validated before use.
  - Settings that affect deterministic physics must be synchronized through the
    explicit helpers instead of being read independently by multiple owners.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunInternal.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include "../Assets/AssetSystem.h"
#include "../Assets/TextureCollection.h"
#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Physics/PhysicsHandles.h"
#include "../World/SkyBox.h"
#include "CameraCollection.h"
#include "DemoDirector.h"
#include "Input.h"
#include "Render/RuntimeRenderResources.h"
#include "RuntimeCameraMode.h"

#include <memory>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
}
namespace Geometry
{
class SkyBox;
class Terrain;
} // namespace Geometry
namespace Textures
{
class TextureCollection;
}

namespace Basics
{
class Window;
} // namespace Basics

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Basics
{
struct RunSubsystemState
{
    Assets::AssetSystem assets;
    Textures::TextureCollection textureCollection;
    Environment::CameraCollection cameraCollection;
    std::unique_ptr<Geometry::Terrain> terrain;
    std::unique_ptr<Geometry::SkyBox> skyBoxOwner;
    bool isFlatSlopeTerrain = false;
    // Lifetime: all pass resources are released before backend teardown/rebuild
    // and lazily recreated by the ensure hooks that own their target size and
    // shader contracts.
    RunRenderPassResources renderPasses;

    Environment::CameraCollection* cameras = nullptr;          // Borrowed alias of cameraCollection after Initialise wires services.
    Textures::TextureCollection* textures = nullptr;           // Borrowed alias of textureCollection after Initialise wires services.
    const EngineConfig* config = nullptr;                      // Borrowed process config sampled through the Run composition root.
    Threading::WorkerPool* workerPool = nullptr;               // Borrowed worker service initialised and shut down by Runtime/Init.cpp.
    Window* window = nullptr;
    Geometry::SkyBox* skyBox = nullptr;                        // Borrowed alias of skyBoxOwner after Initialise wires services.

    void BindStartupServices(
        Window& windowOwner,
        Threading::WorkerPool& workerPoolOwner,
        const EngineConfig& configOwner );                     // Binds process-start services and config-derived camera policy.
};

struct RunCameraState
{
    Hardware::InputState input = {};                           // Snapshot consumed by camera controls for this frame.

    int selectedCamera = 0;                                    // Keeps track of which camera is selected
    RunCameraMode mode = RunCameraMode::Demo;                  // Explicit operator camera mode shown in the minimized HUD.
    RunCameraMode modeBeforeLauncher = RunCameraMode::Inspect; // N returns to the last non-launcher workspace.
    RunCameraMode modeBeforeAttach = RunCameraMode::Inspect;   // Pre-Attach workspace used by explicit attach-restore paths.
    DemoDirectorPlaybackState director;                        // Fixed shot-list playback state for Director camera mode.
    bool needsMouseLookReset = true;                           // Discard stale absolute mouse deltas after UI/focus/fly transitions
    bool hasMouseLookLastClient = false;
    POINT mouseLookLastClient = {};
    float cameraTime = 0.0f;                                   // Camera helper clock
    int trackBallIndex = -1;                                   // Index of ball to track with camera (-1 = no tracking)
    float trackHeight = 300.0f;                                // Camera height above tracked ball
    float autoCycleInterval = -1.0f;                           // Seconds between per-ball auto screenshots (-1 = disabled)
    float autoCycleAccum = 0.0f;                               // Accumulated real-time seconds since last shot
    int autoCycleShotsTaken = 0;                               // Number of per-ball screenshots taken so far
};

enum class AttachedCameraSubmode
{
    FixedRelative,
    VelocityForward,
    RagdollEyes,
    Count
};

struct AttachedCameraTarget
{
    Physics::PhysicsBodyHandle body;                           // Primary live physics identity for follow/orbit sampling.
    Physics::PhysicsColliderHandle collider;                   // Shape/radius identity paired with body.
    int modelIndex = -1;                                       // UI/presentation hint; revalidated before use.
    uint32_t replayBodyId = 0;                                 // Stable scene-local identity used to recover stale indices.
    char name[64] = {};                                        // Human/debug fallback when replay id cannot recover the target.
};

struct AttachedCameraState
{
    AttachedCameraTarget target;                               // Camera-owned target; replay/editor selections are only seeds.
    AttachedCameraSubmode submode = AttachedCameraSubmode::FixedRelative;
    bool activeFollow = true;                                  // false means pinned in world space with mouse released to UI.
    bool hasFixedOffset = false;
    bool hasOrbit = false;
    bool hasLastLookDirection = false;
    bool hasReturnCameraPose = false;
    bool needsEntryTween = false;                              // Next valid follow solve should glide from the visible pose.
    uint32_t returnCameraHash = CAMERA_FREE;                   // Selected slot Attach should restore before applying returnEye/view/up.
    float orbitYawRadians = 0.0f;
    float orbitPitchRadians = 0.30f;
    float orbitDistance = 8.0f;
    Math::Vector::Vector3 localEyeOffset = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localViewOffset = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 localUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 lastLookDirection = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 returnEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 returnView = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 returnUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
};

} // namespace Basics
} // namespace SkullbonezCore
