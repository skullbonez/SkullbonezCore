/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h
Purpose:
  Names value-only frame policy and model data consumed by frame rendering.

Summary:
  Run publishes model/store borrows and presentation policy for one frame.
  RuntimeRenderer supplies its own persistent resources rather than receiving
  them back through a broad service packet.

Glossary:
  Frame policy: Value-only presentation choices sampled after input.

Invariants:
  - Model-frame references and spans are consumed synchronously and never stored.
  - Persistent assets, cameras, window, terrain, and backend resources come
    from RuntimeRenderer's concrete owners, not this frame view.

Related:
  - SkullbonezSource/Runtime/App/Run.h
  - SkullbonezSource/Runtime/App/RunRender.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>
#include <span>

#include "../../Core/MainMemoryStats.h"

namespace SkullbonezCore
{
namespace Core
{
struct CinematicRenderConfig;
} // namespace Core
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
class Dx12GeometryOwner;
class Dx12Diagnostics;
class Dx12FrameOwner;
class Dx12GraphTransientPool;
class Dx12RaytracingOwner;
class Dx12ResourceBuilder;
class Dx12TextureOwner;
class RenderInstanceStore;
class WorldRenderExtensionRegistration;
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

namespace Runtime
{
class Window;
class RuntimeTools;
struct ReplayRenderFrameView;
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

inline bool ShouldUseDxrReflection( bool capabilityAvailable, const RuntimeRenderFramePolicy& policy,
                                    bool collisionStateColorsVisible, bool transparentBodyPass )
{
    return capabilityAvailable && policy.waterRTReflect && !policy.waterNoReflect && !collisionStateColorsVisible &&
           !transparentBodyPass;
}

struct RuntimeRenderModelFrameView
{
    Rendering::RenderInstanceStore& renderInstances;
    const Physics::ColliderStore& colliders;
    const Physics::PhysicsBodyStore& bodyStore;
    Physics::PhysicsEngine& physicsEngine;
    std::span<const float> worldExtensionDebugLines;
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords;
    std::span<const uint8_t> collisionVisualContacts;
    std::span<const uint8_t> sleepStates;
    std::span<const int> sleepIslandVisualIds;
    std::span<const uint8_t> sleepSupportedStates;
    std::span<const uint8_t> sleepInhibitedStates;
    std::span<const Physics::PhysicsDebugContact> physicsDebugContacts;
    std::span<const Physics::PhysicsPipelineRecord> physicsPipelineTrace;
    Threading::WorkerPool* renderWorkerPool;
    int modelCount = 0;
    bool renderCollisionVolumes = false;
    bool shadowParallelPrep = false;
    double sceneKineticEnergy = 0.0;
    SkullbonezCore::Core::MainMemoryGameObjectStats gameObjectMemory;
};

} // namespace Runtime
} // namespace SkullbonezCore
