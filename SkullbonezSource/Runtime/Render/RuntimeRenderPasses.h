/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
Purpose:
  Declares the named runtime render pass contracts.

Mental model:
  Runtime render passes are small frame-order units that borrow the explicit
  RuntimeRenderHost service view. The declarations live outside Run so pass
  ownership stays with RuntimeRenderer instead of growing Run.h.

Glossary:
  Pass: Ordered unit of frame rendering owned by RuntimeRenderer.
  Frame context: Per-frame camera, projection, lighting, water, and scene view
  bundle shared by passes.
  Pass resources: Backend-owned objects such as framebuffers, shaders, and
  vertex buffers used by a pass.

Invariants:
  - Pass input/output structs borrow data for one frame only.
  - Pass constructors receive RuntimeRenderHost so non-render dependencies stay named.
  - Pass order is owned by RuntimeRenderer::RenderFrame.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
  - SkullbonezSource/Runtime/RunPasses.cpp
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/TornadoField.h"
#include "../../Rendering/IFramebuffer.h"
#include "../../Rendering/RenderSceneView.h"
#include "../../Rendering/Shadow.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
class RuntimeRenderHost;

// Concept: these private pass contracts are the extraction boundary.
//
// RuntimeRenderer::RenderFrame() owns pass order, and each pass receives a named
// input bundle and returns only the data later passes need. References and
// pointers here are borrowed for one frame; long-lived GPU resources live in
// RunRenderPassResources instead.
enum class SkyPassMode
{
    CubemapOnly,                            // Force the authored cube-map skybox path.
    CinematicIfEnabled                      // Allow the procedural cinematic sky when the active config requests it.
};

enum class ObjectPassMode
{
    Opaque,                                 // Normal body draw before water.
    Transparent                             // Debug alpha body draw after water so overlays remain readable.
};

struct RenderFrameContext
{
    // Shared inputs for the ordered world-render passes. This is a borrowed
    // per-frame contract: every value is rebuilt after SetCamera(), consumed
    // during RuntimeRenderer::RenderFrame(), and discarded before the next frame.
    Math::Transformation::Matrix4 baseView;
    Math::Transformation::Matrix4 projection;
    Math::Transformation::Matrix4 viewProjection;
    Math::Transformation::Matrix4 reflectionView;
    Math::Transformation::Matrix4 reflectionViewProjection;
    Math::Vector::Vector3 eye;
    Math::Vector::Vector3 viewCenter;
    Math::Vector::Vector3 up;
    Math::Vector::Vector3 reflectionEye;
    Math::Vector::Vector3 reflectionCenter;
    Math::Vector::Vector3 reflectionUp;

    // Invariant: ordinary and cinematic lighting both use a directional sun
    // so direct-light shading and shadow maps agree on the same vector.
    float lightPosition[4] = { 200.0f, 400.0f, 1200.0f, 0.0f };
    float waterY = 0.0f;                    // World-space fluid surface height used by reflection clipping and water shading.

    // Non-null only when cinematic rendering wraps this frame. Passes use
    // the pointer as an opt-in contract, not as ownership.
    bool cinematicEnabled = false;
    const CinematicRenderConfig* cinematic = nullptr;

    // Renderer-facing scene adapter for model draws, shadow casters, DXR
    // transforms, and debug scene overlays. The adapter is owned by Run;
    // passes borrow it only for this frame.
    Rendering::IRenderSceneView* scene = nullptr;
};

struct ObjectPassInputs
{
    // Object pass view of the body collection. It can act as the opaque
    // pass or the transparent debug pass, but target binding stays with the
    // caller.
    const RenderFrameContext& frame;
    // Documents where this object pass sits in the frame ordering.
    ObjectPassMode mode;
    const CinematicRenderConfig* cinematic;
    const Rendering::ShadowFrameData* shadow;
    bool collisionStateColorsVisible;       // Route bodies through collision-state visualization instead of materials.
    float collisionVisualizerAlphaOverride; // -1 keeps visualizer defaults; otherwise overrides debug alpha.
    float bodyAlpha;                        // 1 for opaque bodies; debug alpha for the transparent object pass.
    const std::vector<uint8_t>* modelMask;  // Optional replay focus mask for split opaque/faded body rendering.
    bool drawMaskedModels;                  // True draws only masked bodies; false draws everything outside the mask.
};

struct TerrainPassInputs
{
    // Terrain reads the same camera/light contract as objects, plus the
    // terrain shadow frame when shadows were built for the current frame.
    const RenderFrameContext& frame;
    const CinematicRenderConfig* cinematic;
    const Rendering::ShadowFrameData* shadow;
};

struct ReflectionPassInputs
{
    // Produces the texture sampled by water. The pass may choose the DXR
    // raytraced path or the mirrored-camera render-target path, but both
    // must return a texture handle and matching sample transform.
    const RenderFrameContext& frame;
    const CinematicRenderConfig* cinematic;
    const Rendering::ShadowFrameData* objectShadow;
    bool collisionStateColorsVisible;       // Reflection must match the selected body visualization mode.
    // Disables DXR reflection because the mirrored raster path can honor
    // debug alpha and collision-state rendering.
    bool transparentBodyPass;
    float collisionVisualizerAlphaOverride; // Forwarded to reflected collision-state geometry.
    float bodyAlpha;                        // Forwarded to reflected production body rendering.
};

struct ReflectionPassOutput
{
    uint32_t reflectionTextureHandle = 0;   // Engine texture handle consumed by WorldEnvironment::RenderFluid.
    // Matrix used by water to project the current surface pixel into the
    // reflection texture returned by this pass.
    Math::Transformation::Matrix4 reflectionSampleViewProjection;
    bool usedDxr = false;                   // True when the texture came from the DXR dispatch instead of the planar target.
};

struct WaterPassInputs
{
    // Water is deliberately downstream of reflection. It must not rebuild
    // reflection itself; it only receives the texture/sample transform that
    // the reflection pass produced for this frame.
    const RenderFrameContext& frame;
    const ReflectionPassOutput& reflection;
    const CinematicRenderConfig* cinematic;
    bool waterHidden;                       // Caller-controlled debug visibility; reflection resources stay outside this flag.
    bool flatWater;                         // Debug water style: flat shading instead of animated waves.
    bool noReflection;                      // Debug override: keep water visible but force the no-reflection shader path.
    bool freezeTime;                        // Debug override: hold wave animation at frozenTime.
    float frozenTime;                       // Simulation time captured when water animation was frozen.
};

struct WaterPassDebugInfo
{
    bool rendered = false;
    bool skippedHidden = false;
    bool skippedModeOff = false;
    bool reflectionValid = false;
    bool reflectionRaytraced = false;
    bool noReflection = false;
    bool flatWater = false;
    bool freezeTime = false;
    uint32_t reflectionTextureHandle = 0;
    float waterTime = 0.0f;
    int styleWaterMode = -1;
};

struct TornadoVisualPassInputs
{
    // Production tornado art uses the final world view/depth after opaque
    // objects, terrain, and water. Physics field state is read-only shape input.
    const RenderFrameContext& frame;
};

struct DebugOverlayPassInputs
{
    // Debug overlays draw after production geometry and use the final world
    // view-projection. They do not participate in material or pass-resource
    // ownership.
    const RenderFrameContext& frame;
};

struct ShadowPassInputs
{
    // Shadows are optional. A null cinematic pointer means no shadow maps
    // should be built and receivers should get null shadow outputs.
    const RenderFrameContext& frame;
    const CinematicRenderConfig* cinematic;
};

struct ShadowPassOutput
{
    // Borrowed pointers into ShadowPassResources. Receivers must consume
    // them during the same RuntimeRenderer::RenderFrame() call; ShadowPass resource
    // release and the next frame both invalidate them.
    const Rendering::ShadowFrameData* terrainShadow = nullptr;
    const Rendering::ShadowFrameData* objectShadow = nullptr;
};

/* -- FullscreenQuadPass
------------------------------------------------------------------------------------------------------------------------------------

    Shared two-triangle draw surface for generated sky, volumetric light,
    and tonemap. It owns only the dynamic vertex buffer; shader meaning is
    owned by the pass that uses it.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class FullscreenQuadPass
{
  public:
    explicit FullscreenQuadPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    uint32_t QuadVB() const;

  private:
    RuntimeRenderHost& m_host;
};

/* -- SkyPass
-----------------------------------------------------------------------------------------------------------------------------------------------

    Draws the current sky into whichever render target the caller has bound.
    The cube-map path samples authored face textures; the cinematic path
    owns a generated-atmosphere shader and uses FullscreenQuadPass.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkyPass
{
  public:
    explicit SkyPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    void Render( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view, SkyPassMode mode );

  private:
    void RenderCinematicSky( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view );

    RuntimeRenderHost& m_host;
};

/* -- SceneTargetPass
---------------------------------------------------------------------------------------------------------------------------------------

    Owns the HDR scene target used by cinematic rendering. Begin() binds and
    clears the target, then asks SkyPass to draw the background before world
    geometry is rendered into the target.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SceneTargetPass
{
  public:
    explicit SceneTargetPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    bool IsReady() const;
    void Begin( const RenderFrameContext& frame, SkyPass& skyPass );

  private:
    RuntimeRenderHost& m_host;
};

/* -- ShadowPass
--------------------------------------------------------------------------------------------------------------------------------------------

    Builds terrain/object shadow maps before receiver passes run. It owns
    the shadow targets and the per-frame receiver payloads that terrain and
    object shaders borrow for the rest of RuntimeRenderer::RenderFrame().
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ShadowPass
{
  public:
    explicit ShadowPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame, const CinematicRenderConfig& cinematic );
    void ReleaseGpuResources();
    ShadowPassOutput Render( const ShadowPassInputs& inputs );

  private:
    Rendering::ShadowFrameData BuildTerrainFrameData( const CinematicRenderConfig& cinematic,
                                                      const Math::Vector::Vector3& lightDirectionWorld ) const;
    Rendering::ShadowFrameData BuildObjectFrameData( const CinematicRenderConfig& cinematic,
                                                     const Math::Vector::Vector3& lightDirectionWorld,
                                                     const Math::Vector::Vector3& focusHint,
                                                     Rendering::IRenderSceneView& scene );
    void RenderShadowMap( Rendering::IFramebuffer& target,
                          const Rendering::ShadowFrameData& shadowFrame,
                          const CinematicRenderConfig& cinematic,
                          bool renderTerrain,
                          bool renderObjects,
                          Rendering::IRenderSceneView& scene,
                          const Rendering::ShadowCasterBatches* objectCasters );

    RuntimeRenderHost& m_host;
};

/* -- ReflectionPass
----------------------------------------------------------------------------------------------------------------------------------------

    Produces the reflection texture consumed by WaterPass. It chooses DXR
    reflection when possible, otherwise it renders a mirrored scene into the
    planar reflection target.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ReflectionPass
{
  public:
    explicit ReflectionPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    ReflectionPassOutput Render( const ReflectionPassInputs& inputs, SkyPass& skyPass );

  private:
    RuntimeRenderHost& m_host;
};

/* -- ObjectPass
--------------------------------------------------------------------------------------------------------------------------------------------

    Draws production bodies or collision-state solids into the current
    target. The caller chooses whether this is the opaque or transparent
    body pass; this class owns the object shader texture-slot contract.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ObjectPass
{
  public:
    explicit ObjectPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    void Render( const ObjectPassInputs& inputs );

  private:
    RuntimeRenderHost& m_host;
};

/* -- TerrainPass
-------------------------------------------------------------------------------------------------------------------------------------------

    Draws the terrain mesh with its material texture, cinematic style
    uniforms, and optional shadow receiver payload.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TerrainPass
{
  public:
    explicit TerrainPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    void Render( const TerrainPassInputs& inputs );

  private:
    RuntimeRenderHost& m_host;
};

/* -- WaterPass
---------------------------------------------------------------------------------------------------------------------------------------------

    Draws calm/ocean water after reflection has produced its texture. Water
    samples only the reflection slot and never rebuilds reflection itself.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class WaterPass
{
  public:
    explicit WaterPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    void Render( const WaterPassInputs& inputs );
    const WaterPassDebugInfo& LastDebugInfo() const
    {
        return m_debugInfo;
    }

  private:
    RuntimeRenderHost& m_host;
    WaterPassDebugInfo m_debugInfo;
};

/* -- TornadoVisualPass
-------------------------------------------------------------------------------------------------------------------------------------

    Draws sparse production tornado ribbons and dust after opaque world
    depth exists, while leaving debug field vectors in DebugOverlayPass.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TornadoVisualPass
{
  public:
    explicit TornadoVisualPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    bool Render( const TornadoVisualPassInputs& inputs );

  private:
    RuntimeRenderHost& m_host;
    std::vector<float> m_vertices;
    std::vector<Physics::TornadoActiveVortex> m_activeVisualVortices;
    float m_liveVisualTimeSeconds = 0.0f;
    double m_lastLiveVisualSourceSeconds = 0.0;
    bool m_hasLiveVisualTime = false;
};

/* -- DebugOverlayPass
--------------------------------------------------------------------------------------------------------------------------------------

    Draws non-production world overlays after the main scene. These overlays
    are intentionally separate from ObjectPass so debug visuals do not leak
    into material or shadow contracts.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class DebugOverlayPass
{
  public:
    explicit DebugOverlayPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    void Render( const DebugOverlayPassInputs& inputs );

  private:
    bool HasOverlayWork( const DebugOverlayPassInputs& inputs ) const;

    RuntimeRenderHost& m_host;
};

/* -- VolumetricPass
----------------------------------------------------------------------------------------------------------------------------------------

    Reads the completed HDR scene color/depth target and writes a
    half-resolution light-shaft texture for TonemapPass to composite.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class VolumetricPass
{
  public:
    explicit VolumetricPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    bool Render( const RenderFrameContext& frame );

  private:
    RuntimeRenderHost& m_host;
};

/* -- TonemapPass
-------------------------------------------------------------------------------------------------------------------------------------------

    Resolves the HDR scene target back to the window backbuffer. It owns the
    final post shader contract: scene color, scene depth, optional
    volumetric light, and cinematic grading uniforms.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TonemapPass
{
  public:
    explicit TonemapPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources( const RenderFrameContext& frame );
    void ReleaseGpuResources();
    void Render( const RenderFrameContext& frame, bool sceneAlreadyUnbound, bool volumetricReady );

  private:
    RuntimeRenderHost& m_host;
};

/* -- UiTextPass
--------------------------------------------------------------------------------------------------------------------------------------------

    Named 2D pass for the existing HUD, in-game UI, and SDF text renderer.
    It leaves UI layout code in the UI subsystem but gives the frame loop a
    pass-level Render/Release contract like the 3D passes.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class UiTextPass
{
  public:
    explicit UiTextPass( RuntimeRenderHost& host ) : m_host( host )
    {
    }

    void EnsureGpuResources();
    void ReleaseGpuResources();
    bool ShouldRender() const;
    void Render( double secondsPerFrame );

  private:
    RuntimeRenderHost& m_host;
};

} // namespace Basics
} // namespace SkullbonezCore
