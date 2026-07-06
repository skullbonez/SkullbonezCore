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
  Frame context: Per-frame camera, projection, lighting, water, and store-backed
  render inputs shared by passes.
  Resource context: Creation/rebuild-only render factory bundle used by
  EnsureGpuResources methods, not by draw methods.
  Pass resources: Backend-owned objects such as framebuffers, shaders, and
  vertex buffers used by a pass.
  DXR (DirectX Raytracing): Optional render capability used for hardware ray
  traversal and reflection dispatch when the active backend supports it.

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
#include "../../Rendering/Shadow.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace SkullbonezCore
{
namespace Physics
{
class CollisionVisualizer;
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
struct PhysicsDebugContact;
struct PhysicsPipelineRecord;
} // namespace Physics

namespace Environment
{
class WorldEnvironment;
} // namespace Environment

namespace Rendering
{
class IRenderCommandContext;
class IRenderDiagnostics;
class IRenderRayTracing;
class IRenderResourceFactory;
class RenderInstanceStore;
struct RenderInstancePresentationRecord;
struct RenderGraphTextureBinding;
} // namespace Rendering

namespace Threading
{
class WorkerPool;
}

namespace Textures
{
class TextureCollection;
}

namespace Assets
{
class AssetSystem;
} // namespace Assets

namespace UI
{
struct UIRenderContext;
} // namespace UI

namespace Geometry
{
class SkyBox;
class Terrain;
} // namespace Geometry

namespace Basics
{
class DiagnosticsRuntime;
class EngineConfig;
struct CinematicScenePassResources;
struct FullscreenPassResources;
struct ShadowPassResources;
struct SkyPassResources;
struct TonemapPassResources;
struct VolumetricLightPassResources;
class ReplayRuntime;
class RuntimeRenderHost;
struct RuntimeRenderModelFrameView;
struct RenderHelperContext;

// Concept: these private pass contracts are the extraction boundary.
//
// RuntimeRenderer::RenderFrame() owns pass order, and each pass receives a named
// input bundle or explicit long-lived resources. Frame references are rebuilt
// each pass, while GPU resources stay in RunRenderPassResources.
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

using RenderResourceLifecycleLogFn = void ( * )( void* user, const char* phase, const char* step );

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

    // Store-backed render inputs prepared once before the pass chain. Object,
    // shadow, and reflection passes must consume these instead of reopening the
    // scene/model collection.
    const Rendering::RenderInstanceStore* renderInstances = nullptr;
    const Physics::ColliderStore* colliders = nullptr;
    const Physics::PhysicsBodyStore* bodyStore = nullptr;
    Physics::PhysicsEngine* physicsEngine = nullptr;
    const std::vector<Rendering::RenderInstancePresentationRecord>* presentationRecords = nullptr;
    const std::vector<uint8_t>* collisionVisualContacts = nullptr;
    const std::vector<uint8_t>* sleepStates = nullptr;
    const std::vector<int>* sleepIslandVisualIds = nullptr;
    const std::vector<uint8_t>* sleepSupportedStates = nullptr;
    const std::vector<uint8_t>* sleepInhibitedStates = nullptr;
    const std::vector<Physics::PhysicsDebugContact>* physicsDebugContacts = nullptr;
    const std::vector<Physics::PhysicsPipelineRecord>* physicsPipelineTrace = nullptr;
    Threading::WorkerPool* renderWorkerPool = nullptr;
    int modelCount = 0;
    bool renderCollisionVolumes = false;
    bool shadowParallelPrep = false;
    double sceneKineticEnergy = 0.0;
    float tornadoElapsedSeconds = 0.0f;

    // Lifetime: borrowed from RuntimeRenderInputs for this frame only. It is
    // non-null after RuntimeRenderer::BuildRenderFrameContext(), and pass code
    // must not store it beyond the current RenderFrame call.
    Assets::AssetSystem* assets = nullptr;
    // Lifetime: borrowed texture binding service for this frame only. Pass code
    // uses it to resolve legacy texture handles without reopening RuntimeRenderHost.
    Textures::TextureCollection* textures = nullptr;
    // Lifetime: borrowed from RuntimeRenderInputs for lazy debug resource
    // creation in this frame only.
    Rendering::IRenderResourceFactory* renderResources = nullptr;
    // Lifetime: borrowed from RuntimeRenderInputs for this frame only. It is
    // non-null after RuntimeRenderer::BuildRenderFrameContext(), and pass code
    // must not store it beyond the current RenderFrame call.
    Rendering::IRenderCommandContext* renderCommands = nullptr;
    // Lifetime: borrowed from RuntimeRenderInputs for capability checks and
    // tracing decisions in this frame only.
    Rendering::IRenderDiagnostics* renderDiagnostics = nullptr;
    // Lifetime: optional DXR capability borrowed for this frame only. It stays
    // nullable so the reflection pass can fall back to planar rendering when
    // raytracing is unavailable.
    Rendering::IRenderRayTracing* renderRayTracing = nullptr;
    int windowWidth = 1;                    // Active render-target width sampled from the runtime window service.
    int windowHeight = 1;                   // Active render-target height sampled from the runtime window service.
};

struct RenderResourceContext
{
    // Creation/rebuild-only contract. Passes receive this context from
    // RuntimeRenderer::EnsureFrameResources() or explicit ensure calls, while
    // draw methods keep using RenderFrameContext.
    bool cinematicEnabled = false;
    Assets::AssetSystem& assets;
    Rendering::IRenderResourceFactory& renderResources;
    int windowWidth = 1;                    // Active render-target width used for resize-sensitive GPU objects.
    int windowHeight = 1;                   // Active render-target height used for resize-sensitive GPU objects.
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
    bool terrainHidden;                     // Frame snapshot of the debug/scene visibility flag.
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
    float liveWaterTime;                    // Current simulation time used when water animation is not frozen.
};

struct ReplayOverlayFrameState
{
    // Replay overlay draw code needs UI/window policy, not the full runtime host.
    // Run samples these values once for the late UI frame so scrubber rendering
    // cannot observe a different scene/UI state from the rest of the pass.
    bool editorModeEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    bool scenePhysicsEnabled = false;
    int screenW = 1;
    int screenH = 1;
    double nowSeconds = 0.0;
};

struct UiTextPassInputs
{
    // UI/text can run even when text-only mode skips RuntimeRenderer::RenderFrame(),
    // so it borrows only the narrow render facets sampled by overlays.
    Rendering::IRenderDiagnostics& renderDiagnostics;
    const UI::UIRenderContext& uiRender;
    const RuntimeRenderModelFrameView& models;
    DiagnosticsRuntime& diagnosticsRuntime;
    ReplayRuntime& replayRuntime;
    const ReplayOverlayFrameState& replayOverlay;
    // Lifetime: selected by Run for this UI frame. UI text can render without
    // world passes, so it receives its own snapshot instead of reopening host state.
    const CinematicRenderConfig& cinematic;
    bool cinematicRendering = false;
    Rendering::IRenderRayTracing* renderRayTracing;
    double secondsPerFrame = 0.0;
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
    bool terrainHidden;                     // Frame snapshot of debug/scene terrain visibility.
    bool collisionVisualizerVisible;        // Collision-color mode disables object shadow casters.
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
    explicit FullscreenQuadPass( FullscreenPassResources& resources ) : m_resources( resources )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources( Rendering::IRenderResourceFactory* renderResources );
    uint32_t QuadVB() const;

  private:
    FullscreenPassResources& m_resources;
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
    SkyPass( SkyPassResources& skyResources,
             FullscreenPassResources& fullscreenResources,
             Geometry::SkyBox*& skyBox,
             const EngineConfig& config )
        : m_skyResources( skyResources ), m_fullscreenResources( fullscreenResources ), m_skyBox( skyBox ),
          m_config( config )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    void Render( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view, SkyPassMode mode );

  private:
    void RenderCinematicSky( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view );

    SkyPassResources& m_skyResources;
    FullscreenPassResources& m_fullscreenResources;
    // Lifetime: this aliases RunSubsystemState::skyBox because RuntimeRenderer
    // is constructed before Initialise wires the owned SkyBox pointer.
    Geometry::SkyBox*& m_skyBox;
    const EngineConfig& m_config;
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
    explicit SceneTargetPass( CinematicScenePassResources& resources ) : m_resources( resources )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    bool IsReady() const;
    void Begin( const RenderFrameContext& frame, SkyPass& skyPass );

  private:
    CinematicScenePassResources& m_resources;
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
    ShadowPass( ShadowPassResources& resources,
                std::unique_ptr<Geometry::Terrain>& terrain,
                const EngineConfig& config,
                RenderResourceLifecycleLogFn lifecycleLog,
                void* lifecycleLogUser )
        : m_resources( resources ), m_terrain( terrain ), m_config( config ), m_lifecycleLog( lifecycleLog ),
          m_lifecycleLogUser( lifecycleLogUser )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources, const CinematicRenderConfig& cinematic );
    void ReleaseGpuResources();
    ShadowPassOutput Render( const ShadowPassInputs& inputs );

  private:
    void LogResourceLifecycleStep( const char* phase, const char* step ) const;
    Rendering::ShadowFrameData BuildTerrainFrameData( const CinematicRenderConfig& cinematic,
                                                      const Math::Vector::Vector3& lightDirectionWorld ) const;
    Rendering::ShadowFrameData BuildObjectFrameData( const CinematicRenderConfig& cinematic,
                                                     const Math::Vector::Vector3& lightDirectionWorld,
                                                     const Math::Vector::Vector3& focusHint,
                                                     const Rendering::RenderInstanceStore& renderInstances,
                                                     Threading::WorkerPool* renderWorkerPool,
                                                     bool shadowParallelPrep );
    void RenderShadowMap( Rendering::IFramebuffer& target,
                          const RenderHelperContext& helperContext,
                          const Rendering::ShadowFrameData& shadowFrame,
                          const CinematicRenderConfig& cinematic,
                          Rendering::IRenderCommandContext& renderCommands,
                          bool renderTerrain,
                          bool renderObjects,
                          const Rendering::RenderInstanceStore& renderInstances,
                          const Physics::ColliderStore& colliders,
                          Threading::WorkerPool* renderWorkerPool,
                          bool shadowParallelPrep,
                          const Rendering::ShadowCasterBatches* objectCasters );

    ShadowPassResources& m_resources;
    std::unique_ptr<Geometry::Terrain>& m_terrain;
    const EngineConfig& m_config;
    RenderResourceLifecycleLogFn m_lifecycleLog = nullptr;
    void* m_lifecycleLogUser = nullptr;
    bool m_activeTerrainHidden = false;
    bool m_activeCollisionVisualizerVisible = false;
    int m_activeWindowWidth = 1;
    int m_activeWindowHeight = 1;
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

    void EnsureGpuResources( const RenderResourceContext& resources );
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
    ObjectPass( Physics::CollisionVisualizer& collisionVisualizer, const EngineConfig& config )
        : m_collisionVisualizer( collisionVisualizer ), m_config( config )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    void Render( const ObjectPassInputs& inputs );

  private:
    Physics::CollisionVisualizer& m_collisionVisualizer;
    const EngineConfig& m_config;
};

/* -- TerrainPass
-------------------------------------------------------------------------------------------------------------------------------------------

    Draws the terrain mesh with its material texture, cinematic style
    uniforms, and optional shadow receiver payload.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TerrainPass
{
  public:
    TerrainPass( std::unique_ptr<Geometry::Terrain>& terrain, const EngineConfig& config )
        : m_terrain( terrain ), m_config( config )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    void Render( const TerrainPassInputs& inputs );

  private:
    // Lifetime: aliases RunSubsystemState::terrain because terrain is scene-owned
    // and may be replaced after RuntimeRenderer construction.
    std::unique_ptr<Geometry::Terrain>& m_terrain;
    const EngineConfig& m_config;
};

/* -- WaterPass
---------------------------------------------------------------------------------------------------------------------------------------------

    Draws calm/ocean water after reflection has produced its texture. Water
    samples only the reflection slot and never rebuilds reflection itself.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class WaterPass
{
  public:
    WaterPass( Environment::WorldEnvironment& world, const EngineConfig& config ) : m_world( world ), m_config( config )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    void Render( const WaterPassInputs& inputs );
    const WaterPassDebugInfo& LastDebugInfo() const
    {
        return m_debugInfo;
    }

  private:
    Environment::WorldEnvironment& m_world;
    const EngineConfig& m_config;
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

    void EnsureGpuResources( const RenderResourceContext& resources );
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

    void EnsureGpuResources( const RenderResourceContext& resources );
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
    VolumetricPass( CinematicScenePassResources& sceneResources,
                    VolumetricLightPassResources& volumetricResources,
                    FullscreenPassResources& fullscreenResources,
                    const EngineConfig& config )
        : m_sceneResources( sceneResources ), m_volumetricResources( volumetricResources ),
          m_fullscreenResources( fullscreenResources ), m_config( config )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    bool CanRender( const RenderFrameContext& frame ) const;
    bool Render( const RenderFrameContext& frame, const Rendering::RenderGraphTextureBinding* graphOutput = nullptr );

  private:
    CinematicScenePassResources& m_sceneResources;
    VolumetricLightPassResources& m_volumetricResources;
    FullscreenPassResources& m_fullscreenResources;
    const EngineConfig& m_config;
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
    TonemapPass( CinematicScenePassResources& sceneResources,
                 VolumetricLightPassResources& volumetricResources,
                 TonemapPassResources& tonemapResources,
                 FullscreenPassResources& fullscreenResources,
                 const EngineConfig& config )
        : m_sceneResources( sceneResources ), m_volumetricResources( volumetricResources ),
          m_tonemapResources( tonemapResources ), m_fullscreenResources( fullscreenResources ), m_config( config )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    void Render( const RenderFrameContext& frame,
                 bool sceneAlreadyUnbound,
                 bool volumetricReady,
                 const Rendering::RenderGraphTextureBinding* graphVolumetric = nullptr );

  private:
    CinematicScenePassResources& m_sceneResources;
    VolumetricLightPassResources& m_volumetricResources;
    TonemapPassResources& m_tonemapResources;
    FullscreenPassResources& m_fullscreenResources;
    const EngineConfig& m_config;
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

    void EnsureGpuResources( Rendering::IRenderResourceFactory& renderResources,
                             const Assets::AssetSystem& assets,
                             int screenW,
                             int screenH );
    void ReleaseGpuResources( Rendering::IRenderResourceFactory* renderResources );
    bool ShouldRender() const;
    void Render( const UiTextPassInputs& inputs );

  private:
    RuntimeRenderHost& m_host;
};

} // namespace Basics
} // namespace SkullbonezCore
