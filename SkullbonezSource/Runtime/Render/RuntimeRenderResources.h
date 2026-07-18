/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
Purpose:
  Names backend resources owned by RuntimeRenderer's ordered render passes.

Summary:
  These structs are ownership records, not rendering behavior. RuntimeRenderer
  and the individual pass classes create, reset, and consume these resources
  while Run still owns the broader backend teardown order.

Glossary:
  Pass resource: Backend-owned framebuffer, shader, or vertex buffer attached
  to a named render pass.
  Shadow frame data: Borrowed per-frame receiver payload containing light
  matrices and texture handles.
  Backend teardown: Ordered release of GPU resources before the renderer is
  destroyed or rebuilt.

Invariants:
  - RuntimeRenderPassResources owns backend/device resources and must be reset
    while the renderer backend is still alive.
  - Shadow receiver pointers are valid only until the next shadow reset or the
    next frame rebuilds ShadowPassResources.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunPasses.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
*/
#pragma once

#include "../../Core/Common.h"
#include "../Scene/SceneCapacity.h"
#include "../../Rendering/IFramebuffer.h"
#include "../../Rendering/IShader.h"
#include "../../Rendering/Shadow.h"

#include <cstdint>
#include <memory>

namespace SkullbonezCore
{
namespace Runtime
{
// Concept: pass resource structs name ownership before the frame graph exists.
//
// These structs are intentionally small. They do not implement rendering; they
// make lifetime and ownership visible so each pass can later move behind a
// cleaner interface without rediscovering which framebuffer, shader, or
// per-frame payload belongs to it.
struct ReflectionPassResources
{
    // Lifetime: this render target is backend/device-owned. Resize and backend
    // teardown must reset it before the renderer releases the underlying GPU
    // resource memory.
    std::unique_ptr<Rendering::IFramebuffer> target;
};

struct SkyPassResources
{
    // The cube-map skybox is owned by the shared skybox subsystem. This pass
    // owns only the procedural cinematic atmosphere shader used when cinematic
    // mode asks the sky pass to draw a generated background.
    std::unique_ptr<Rendering::IShader> atmosphereShader;
};

struct CinematicScenePassResources
{
    // Full-resolution floating-point scene color/depth. World geometry renders
    // here first so volumetric light and tonemap can sample the completed scene.
    std::unique_ptr<Rendering::IFramebuffer> hdrTarget;
};

struct VolumetricLightPassResources
{
    // Half-resolution because light shafts are intentionally soft. The shader
    // samples the HDR scene and depth, then writes a texture the tonemap pass can
    // composite over the final image.
    std::unique_ptr<Rendering::IFramebuffer> target;
    std::unique_ptr<Rendering::IShader> shader;
};

struct TonemapPassResources
{
    // Final full-screen resolve from HDR scene color to the window backbuffer.
    // This is also where fog, bloom, grade, and optional volumetric light meet.
    std::unique_ptr<Rendering::IShader> shader;
};

struct FullscreenPassResources
{
    // Shared dynamic vertex buffer for two-triangle full-screen passes. It
    // stores only clip-space position and UV; each shader decides what to sample.
    uint32_t quadVB = 0;
};

struct ShadowPassResources
{
    ShadowPassResources()
    {
        // Why: object shadow caster streams are frame-rebuilt, but their
        // capacity is a startup/runtime-resource contract sized to the maximum
        // scene model pool. Exhaustion means the scene capacity budget changed,
        // not that render should grow during the shadow pass.
        objectCasterBatches.ReserveForModelCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    // Terrain target: broad map centered on terrain bounds. Object target:
    // tighter map centered near the camera so nearby body shadows keep detail.
    std::unique_ptr<Rendering::IFramebuffer> terrainTarget;
    std::unique_ptr<Rendering::IFramebuffer> objectTarget;

    // Lifetime: these payloads borrow texture handles from the targets above.
    // Reset both payloads whenever either target is destroyed, and rebuild them
    // every frame before terrain/object receivers read the pointers.
    Rendering::ShadowFrameData terrainFrame;
    Rendering::ShadowFrameData objectFrame;
    Rendering::ShadowCasterBatches objectCasterBatches;
};

struct RuntimeRenderPassResources
{
    // Ownership map for pass-owned renderer resources. Runtime subsystems keep
    // long-lived world state elsewhere; this aggregate is only for resources
    // created by named render passes and released through pass reset hooks.
    ReflectionPassResources reflection;
    SkyPassResources sky;
    CinematicScenePassResources cinematicScene;
    VolumetricLightPassResources volumetricLight;
    TonemapPassResources tonemap;
    FullscreenPassResources fullscreen;
    ShadowPassResources shadows;
};
} // namespace Runtime
} // namespace SkullbonezCore
