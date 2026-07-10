/*
File: SkullbonezSource/Runtime/RunSubsystemState.h
Purpose:
  Owns Run's process-lifetime subsystem services and render pass resource shelf.

Mental model:
  Run is still the composition root. This aggregate groups the long-lived
  assets, textures, terrain, skybox, and render pass resources that
  split runtime files borrow during frame, scene, and render work. The raw
  pointers are aliases into the owned members or startup-owned services.

Glossary:
  Borrowed subsystem pointer: Non-owning pointer to state owned elsewhere in
    Run or the startup layer; callers must not retain it past the owner.
  Render pass resources: Long-lived GPU/pass owner storage lazily recreated by
    the render host when size or shader contracts change.
  Startup service: Window, worker pool, or config object supplied by Runtime/Init
    and bound once before the frame loop starts.

Invariants:
  - `textureCollection`, `terrain`, `skyBoxOwner`, and `renderPasses` are the
    owning members; pointer fields are aliases only.
  - BindStartupServices must run before frame/update code samples window,
    worker, config, or camera movement policy.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunInternal.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "../Assets/AssetSystem.h"
#include "../Assets/TextureCollection.h"
#include "../Core/Config.h"
#include "../World/SkyBox.h"
#include "../World/Terrain.h"
#include "Render/RuntimeRenderResources.h"

#include <memory>

namespace SkullbonezCore
{
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
    std::unique_ptr<Geometry::Terrain> terrain;
    std::unique_ptr<Geometry::SkyBox> skyBoxOwner;
    bool isFlatSlopeTerrain = false;
    // Lifetime: all pass resources are released before backend teardown/rebuild
    // and lazily recreated by the ensure hooks that own their target size and
    // shader contracts.
    RunRenderPassResources renderPasses;

    Textures::TextureCollection* textures = nullptr; // Borrowed alias of textureCollection after Initialise wires services.
    const EngineConfig* config = nullptr;            // Borrowed process config sampled through the Run composition root.
    Threading::WorkerPool* workerPool = nullptr;     // Borrowed worker service initialised and shut down by Runtime/Init.cpp.
    Window* window = nullptr;
    void BindStartupServices(
        Window& windowOwner,
        Threading::WorkerPool& workerPoolOwner,
        const EngineConfig& configOwner );           // Binds process-start services and config-derived camera policy.
};

} // namespace Basics
} // namespace SkullbonezCore
