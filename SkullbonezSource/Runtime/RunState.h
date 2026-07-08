/*
File: SkullbonezSource/Runtime/RunState.h
Purpose:
  Defines the remaining Run-owned subsystem aggregate shared by split runtime files.

Mental model:
  Run remains the composition root, but this staging header is shrinking toward
  empty as narrower owners claim their state shelves. New feature state should
  go to the owner that mutates it instead of growing this shared boundary.

Glossary:
  State shelf: Run-owned aggregate that groups related fields while split
  implementation files are being decomposed.
  Borrowed subsystem pointer: Non-owning pointer to state owned elsewhere in
    the Run composition root.
  Render pass resources: Long-lived GPU/pass owner storage lazily recreated by
    the render host when size or shader contracts change.

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
#include "../World/SkyBox.h"
#include "CameraCollection.h"
#include "Render/RuntimeRenderResources.h"

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

    Environment::CameraCollection* cameras = nullptr; // Borrowed alias of cameraCollection after Initialise wires services.
    Textures::TextureCollection* textures = nullptr;  // Borrowed alias of textureCollection after Initialise wires services.
    const EngineConfig* config = nullptr;             // Borrowed process config sampled through the Run composition root.
    Threading::WorkerPool* workerPool = nullptr;      // Borrowed worker service initialised and shut down by Runtime/Init.cpp.
    Window* window = nullptr;
    Geometry::SkyBox* skyBox = nullptr;               // Borrowed alias of skyBoxOwner after Initialise wires services.

    void BindStartupServices(
        Window& windowOwner,
        Threading::WorkerPool& workerPoolOwner,
        const EngineConfig& configOwner );            // Binds process-start services and config-derived camera policy.
};

} // namespace Basics
} // namespace SkullbonezCore
