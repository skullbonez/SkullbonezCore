/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h
Purpose:
  Names the borrowed runtime inputs consumed by frame rendering.

Summary:
  Runtime render code should receive a small view of the systems and state it
  needs for one frame, not the entire Run object. These structs are references
  only; ownership remains in concrete renderer, scene, UI, and tool owners.

Glossary:
  Render services: Borrowed references to systems required by render passes.
  Render inputs: One-frame wrapper around the current render services.
  Borrowed pointer: Nullable dependency retained by a concrete process or scene
    owner.
  DXR (DirectX Raytracing): Optional render capability used for hardware ray
  traversal when the active backend publishes it.

Invariants:
  - RuntimeRenderInputs is rebuilt for the current render call and is not
    stored by render passes.
  - References and pointers here do not transfer ownership.
  - Optional pointers remain nullable to match the current Run-owned subsystem
    lifetime.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "../../Core/MainMemoryStats.h"

namespace SkullbonezCore
{
namespace Textures
{
class TextureCollection;
}

namespace Assets
{
class AssetSystem;
}

namespace Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
struct PhysicsDebugContact;
struct PhysicsPipelineRecord;
} // namespace Physics

namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment

namespace Geometry
{
class SkyBox;
class Terrain;
} // namespace Geometry

namespace Rendering
{
class IRenderCommandContext;
class IRenderDiagnostics;
class IRenderRayTracing;
class IRenderResourceFactory;
class RenderInstanceStore;
struct RenderInstancePresentationRecord;
} // namespace Rendering

namespace Threading
{
class WorkerPool;
}

namespace UI
{
class InGameUI;
}

namespace Basics
{
class Window;
class ReplayRuntime;
class RuntimeTools;
struct CinematicRenderConfig;
struct RenderToolOverlayView;

struct RuntimeRenderFramePolicy
{
    // Value-only presentation facts sampled after input and before submission.
    // RuntimeRenderer may not retain the debug, timer, camera, or tool owners
    // from which these facts were derived.
    bool textOnly = false;
    bool terrainHidden = false;
    bool collisionVisualizer = false;
    bool physicsDebugTransparent = false;
    float physicsDebugAlpha = 1.0f;
    bool waterHidden = false;
    bool waterFlatDebug = false;
    bool waterNoReflect = false;
    bool waterRTReflect = false;
    bool waterFreezeDebug = false;
    float frozenWaterTime = 0.0f;
    bool broadphaseOverlay = false;
    uint32_t physicsDebugFlags = 0u;
    int physicsDebugPipelineStageCursor = 0;
    float physicsDebugContactLinger = 0.0f;
    double simulationSeconds = 0.0;
    double totalSimulationSeconds = 0.0;
};

struct RuntimeRenderModelFrameView
{
    Rendering::RenderInstanceStore& renderInstances;
    const Physics::ColliderStore& colliders;
    const Physics::PhysicsBodyStore& bodyStore;
    Physics::PhysicsEngine& physicsEngine;
    const std::vector<Rendering::RenderInstancePresentationRecord>& presentationRecords;
    const std::vector<uint8_t>& collisionVisualContacts;
    const std::vector<uint8_t>& sleepStates;
    const std::vector<int>& sleepIslandVisualIds;
    const std::vector<uint8_t>& sleepSupportedStates;
    const std::vector<uint8_t>& sleepInhibitedStates;
    const std::vector<Physics::PhysicsDebugContact>& physicsDebugContacts;
    const std::vector<Physics::PhysicsPipelineRecord>& physicsPipelineTrace;
    Threading::WorkerPool* renderWorkerPool;
    int modelCount = 0;
    bool renderCollisionVolumes = false;
    bool shadowParallelPrep = false;
    double sceneKineticEnergy = 0.0;
    float tornadoElapsedSeconds = 0.0f;
    MainMemoryGameObjectStats gameObjectMemory;
};

struct RuntimeRenderServices
{
    Assets::AssetSystem& assets;
    Textures::TextureCollection& textures;
    RuntimeRenderModelFrameView models;
    Environment::WorldEnvironment& world;
    Geometry::Terrain* terrain;
    Environment::CameraCollection& cameras;
    Window& window;
    UI::InGameUI& ui;
    RuntimeTools& runtimeTools;
    ReplayRuntime& replayRuntime;
    const RenderToolOverlayView& toolOverlay;
    const RuntimeRenderFramePolicy& framePolicy;
    Geometry::SkyBox* skyBox;
    // Lifetime: selected once by Run for this render call. Passes use this
    // snapshot instead of asking Run to reopen scene/config state.
    const CinematicRenderConfig& cinematic;
    bool cinematicEnabled = false;
    // Lifetime: this command facet is borrowed from the process-bound backend
    // for exactly this render call; pass code must not store it.
    Rendering::IRenderCommandContext& renderCommands;
    // Lifetime: this factory facet is valid only while the current backend is
    // alive. RuntimeRenderer narrows it into RenderResourceContext for
    // create/rebuild phases; draw code should use renderCommands instead.
    Rendering::IRenderResourceFactory& renderResources;
    // Lifetime: this diagnostics facet is sampled for frame-time feature
    // decisions and draw tracing; passes must not cache capability flags across
    // backend teardown.
    Rendering::IRenderDiagnostics& renderDiagnostics;
    // Optional DXR facet. Null means the active backend did not publish the
    // raytracing capability, even if ordinary raster rendering is ready.
    Rendering::IRenderRayTracing* renderRayTracing = nullptr;
    bool renderReady = false;
};

struct RuntimeRenderInputs
{
    RuntimeRenderServices services;
};
} // namespace Basics
} // namespace SkullbonezCore
