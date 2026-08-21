/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
Purpose:
  Declares the named runtime render pass contracts.

Summary:
  Runtime render passes are small frame-order units with explicit constructor
  owners and frame input structs. The declarations live outside Run so pass
  ownership stays with RuntimeRenderer instead of growing Run.h. Debug-overlay
  input may borrow generic detached geometry without reopening feature owners.

Glossary:
  Pass: Ordered unit of frame rendering owned by RuntimeRenderer.
  Camera-lighting sample: Per-frame camera matrices, axes, and directional
    light values shared by concrete world passes.
  Pass resources: Backend-owned objects such as framebuffers, shaders, and
  vertex buffers used by a pass.

Invariants:
  - Pass input/output structs borrow data for one frame only.
  - Pass constructors receive named long-lived owners; per-frame runtime data
    travels through explicit pass input structs.
  - Pass order is owned by RuntimeRenderer::RenderPreparedFrame.
  - Debug contact packets are synchronous Rendering values, not Replay or Planning state.
  - RuntimeRenderTargetPreviewSnapshot owns its fixed-catalog append boundary;
    every producer reaches fatal invariant before an overflow can index storage.
  - Sky and Profile UI passes close their lifecycle leases before backend
    resource teardown and reject access outside the active epoch.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/FatalError.h"

#include "../../Core/SbResult.h"
#include "../../Core/Config.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Rendering/DX12/FramebufferDX12.h"
#include "../../Rendering/Shadow.h"
#include "../../Rendering/Text.h"
#include "../../UI/UI.h"
#include "../../UI/UIDrawList.h"
#include "RenderPresentationSettings.h"
#include "UiDrawSubmission.h"
#include "../Interaction/RuntimeInteractionController.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
} // namespace Core
namespace Physics
{
class BroadphaseVisualizer;
class CollisionVisualizer;
class ColliderStore;
class PhysicsDebugVisualizer;
class PhysicsEngine;
class PhysicsBodyStore;
struct PhysicsDebugContact;
struct PhysicsPipelineRecord;
} // namespace Physics

namespace Runtime
{
class SceneWorld;
struct SkyPassTestAccess;
struct UiTextPassTestAccess;
} // namespace Runtime

namespace Environment
{
class WorldEnvironment;
} // namespace Environment

namespace Rendering
{
class RenderGpuTimingOwner;
class Dx12GeometryOwner;
class Dx12FrameOwner;
class Dx12GraphTransientPool;
class Dx12RenderDevice;
class Dx12Diagnostics;
class Dx12RaytracingOwner;
class Dx12ResourceBuilder;
class Dx12TextureOwner;
class RenderInstanceStore;
struct ContactManifoldPresentation;
class ShaderDX12;
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
class InGameUI;
struct InGameUIFrameData;
struct OperatorEditorFrameView;
} // namespace UI
namespace Text
{
class TextBatch;
}

namespace Geometry
{
class SkyBox;
class Terrain;
} // namespace Geometry

namespace Rendering
{
class PrimitiveBatchRenderer;
struct PrimitiveRenderContext;
} // namespace Rendering

namespace UI
{
struct RunSceneBrowserState;
}

namespace Runtime
{
class DiagnosticsRuntime;
class RuntimeTools;
class EditorTracer;
class LauncherLaser;
class RuntimeInputContext;
class SceneTerrain;
struct CinematicScenePassResources;
struct FullscreenPassResources;
struct ReflectionPassResources;
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct CameraControlState;
enum class OverlayMode;
struct OverlayDebugState;
struct RunEditorPlacementState;
struct RunRayCastTestState;
struct RuntimeRenderPassResources;
struct ShadowPassResources;
struct SkyPassResources;
struct TonemapPassResources;
struct VolumetricLightPassResources;
struct ReplayHudStatus;
struct ReplayVisualPacket;
namespace ReplayOverlay
{
struct ReplayOverlayStateView;
}
struct RuntimeRenderModelFrameView;
struct RuntimeViewModel;
struct SceneSessionState;
struct RunTimerState;
struct RunReplayPredictionFrame;

// Concept: these private pass contracts are the extraction boundary.
//
// RuntimeRenderer::RenderPreparedFrame() owns pass order, and each pass receives a named
// input bundle or explicit long-lived resources. Frame references are rebuilt
// each pass, while GPU resources stay in RuntimeRenderPassResources.
enum class SkyPassMode
{
    CubemapOnly,                                     // Force the authored cube-map skybox path.
    CinematicIfEnabled                               // Allow the procedural cinematic sky when the active config requests it.
};

enum class ObjectPassMode
{
    Opaque,                                          // Normal body draw before water.
    Transparent                                      // Debug alpha body draw after water so overlays remain readable.
};

// Concrete renderer diagnostic owner shared by resource-producing passes. It
// borrows the device and scene state for RuntimeRenderer's lifetime and never
// calls back into the application shell.
class RenderResourceLifecycleLog
{
  public:
    RenderResourceLifecycleLog( Rendering::Dx12RenderDevice* renderDevice, const SceneSessionState& scene )
        : m_renderDevice( renderDevice ), m_scene( scene )
    {
    }

    void Write( const char* phase, const char* step ) const;

  private:
    Rendering::Dx12RenderDevice* m_renderDevice = nullptr;
    const SceneSessionState& m_scene;
};

struct RenderCameraLighting
{
    // Cohesive value sampled after SetCamera(). It carries no owner or resource
    // pointer and cannot grant a pass access to unrelated runtime state.
    Math::Transformation::Matrix4 baseView;
    Math::Transformation::Matrix4 projection;
    Math::Transformation::Matrix4 viewProjection;
    Math::Vector::Vector3 eye;
    Math::Vector::Vector3 viewCenter;
    Math::Vector::Vector3 up;

    // Invariant: ordinary and cinematic lighting both use a directional sun
    // so direct-light shading and shadow maps agree on the same vector.
    float lightPosition[4] = { 200.0f, 400.0f, 1200.0f, 0.0f };
};

struct RenderResourceContext
{
    // Creation/rebuild-only contract. Passes receive this context from
    // RuntimeRenderer::EnsureFrameResources() or explicit ensure calls; draw
    // phases borrow only their concrete resources.
    bool cinematicEnabled = false;
    Assets::AssetSystem& assets;
    Rendering::Dx12ResourceBuilder& renderResources;
    Rendering::Dx12TextureOwner& renderTextures;
    Rendering::Dx12GeometryOwner& renderGeometry;
    int windowWidth = 1;                             // Active render-target width used for resize-sensitive GPU objects.
    int windowHeight = 1;                            // Active render-target height used for resize-sensitive GPU objects.
};

struct ObjectPassInputs
{
    // Object pass view of the body collection. It can act as the opaque
    // pass or the transparent debug pass, but target binding stays with the
    // caller.
    const RenderCameraLighting& camera;
    const RuntimeRenderModelFrameView& models;
    const Rendering::PrimitiveRenderContext& primitive;
    Assets::AssetSystem& assets;
    Textures::TextureCollection& textures;
    Rendering::Dx12TextureOwner& renderTextures;
    Rendering::Dx12Diagnostics& renderDiagnostics;
    Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;

    // Documents where this object pass sits in the frame ordering.
    ObjectPassMode mode;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic;
    const Rendering::ShadowFrameData* shadow;
    bool collisionStateColorsVisible;                // Route bodies through collision-state visualization instead of materials.
    float collisionVisualizerAlphaOverride;          // -1 keeps visualizer defaults; otherwise overrides debug alpha.
    float bodyAlpha;                                 // 1 for opaque bodies; debug alpha for the transparent object pass.
    const std::vector<uint8_t>* modelMask;           // Optional replay focus mask for split opaque/faded body rendering.
    bool drawMaskedModels;                           // True draws only masked bodies; false draws everything outside the mask.
};

struct TerrainPassInputs
{
    // Terrain reads the same camera/light contract as objects, plus the
    // terrain shadow frame when shadows were built for the current frame.
    const RenderCameraLighting& camera;
    Textures::TextureCollection& textures;
    Rendering::Dx12TextureOwner& renderTextures;
    Rendering::Dx12Diagnostics& renderDiagnostics;
    Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic;
    const Rendering::ShadowFrameData* shadow;
    const Rendering::ShadowFrameData* detailShadow;
    const float* clipPlane = nullptr;                // Borrowed from PrimitiveBatchRenderer for this terrain draw.
    bool terrainHidden;                              // Frame snapshot of the debug/scene visibility flag.
};

struct ReflectionPassInputs
{
    // Produces the texture sampled by water. The pass may choose the DXR
    // raytraced path or the mirrored-camera render-target path, but both
    // must return a texture handle and matching sample transform.
    const RenderCameraLighting& camera;
    const RuntimeRenderModelFrameView& models;
    const Rendering::PrimitiveRenderContext& primitive;
    Assets::AssetSystem& assets;
    Textures::TextureCollection& textures;
    Rendering::Dx12TextureOwner& renderTextures;
    Rendering::Dx12FrameOwner& renderFrame;
    Rendering::Dx12Diagnostics& renderDiagnostics;
    Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;
    Rendering::Dx12RaytracingOwner& rayTracing;
    bool useDxrReflection = false;                   // Composition capability and frame policy resolved by RuntimeRenderer.
    Math::Transformation::Matrix4 reflectionView;
    Math::Transformation::Matrix4 reflectionViewProjection;
    float waterY = 0.0f;
    int windowWidth = 1;
    int windowHeight = 1;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic;
    const Rendering::ShadowFrameData* objectShadow;
    bool collisionStateColorsVisible;                // Reflection must match the selected body visualization mode.
    float collisionVisualizerAlphaOverride;          // Forwarded to reflected collision-state geometry.
    float bodyAlpha;                                 // Forwarded to reflected production body rendering.
    float simulationTimeSeconds;                     // Timer sample consumed by the DXR reflection shader.
};

struct ReflectionPassOutput
{
    uint32_t reflectionTextureHandle = 0;            // Engine texture handle consumed by WorldEnvironment::RenderFluid.

    // Matrix used by water to project the current surface pixel into the
    // reflection texture returned by this pass.
    Math::Transformation::Matrix4 reflectionSampleViewProjection;
    bool usedDxr = false;                            // True when the texture came from the DXR dispatch instead of the planar target.
};

struct WaterPassInputs
{
    // Water is deliberately downstream of reflection. It must not rebuild
    // reflection itself; it only receives the texture/sample transform that
    // the reflection pass produced for this frame.
    const RenderCameraLighting& camera;
    Rendering::Dx12TextureOwner& renderTextures;
    Rendering::Dx12Diagnostics& renderDiagnostics;
    Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;
    const ReflectionPassOutput& reflection;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic;
    bool cinematicEnabled = false;
    bool waterHidden;                                // Caller-controlled debug visibility; reflection resources stay outside this flag.
    bool flatWater;                                  // Debug water style: flat shading instead of animated waves.
    bool noReflection;                               // Debug override: keep water visible but force the no-reflection shader path.
    bool freezeTime;                                 // Debug override: hold wave animation at frozenTime.
    float frozenTime;                                // Simulation time captured when water animation was frozen.
    float liveWaterTime;                             // Current simulation time used when water animation is not frozen.
};

struct RuntimeRenderTargetPreview
{
    const char* label = "";
    uint32_t textureHandle = 0;
    int width = 0;
    int height = 0;
    bool available = false;
    bool depth = false;
    bool hdr = false;
};

struct RuntimeRenderTargetPreviewSnapshot
{
    // Renderer-owned frame snapshot. Runtime/Render projects only label,
    // dimensions, format flags, and availability into UI; texture handles stay
    // here and are resolved from the recorded catalog identity at submission.
    std::array<RuntimeRenderTargetPreview, 12> targets {};
    int count = 0;

    // Invariant: both production producers share this fixed-catalog boundary;
    // the base lifecycle contributes ten rows and UI may append one DXR row.
    void AppendCatalogTarget( const RuntimeRenderTargetPreview& preview )
    {
        AppendBounded( preview );
    }
    void AppendOptionalDxrTarget( const RuntimeRenderTargetPreview& preview )
    {
        AppendBounded( preview );
    }

  private:
    void AppendBounded( const RuntimeRenderTargetPreview& preview )
    {
        if ( count < 0 || count >= static_cast<int>( targets.size() ) )
        {
            SB_FATAL( "Runtime/Render/RenderTargetPreviewSnapshot",
                      "Render-target preview capacity exceeded. count=%d capacity=%zu", count, targets.size() );
        }

        targets[static_cast<std::size_t>( count++ )] = preview;
    }
};

// Value: the viewport is shared by focused draw operations that place UI.
struct UiTextViewport
{
    int screenW = 1;
    int screenH = 1;
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

struct DebugOverlaySnapshot
{
    // Frame-level overlay decisions sampled before graph callback execution.
    // The pass may draw multiple overlay families, but it should not reopen
    // broad runtime debug/tool/replay state while drawing them.
    bool broadphaseOverlayVisible = false;
    std::span<const float> worldExtensionDebugLines; // position.xyz + color.rgb line vertices.
    bool editorOverlayWorkVisible = false;
    uint32_t physicsDebugFlags = 0u;
    int physicsDebugPipelineStageCursor = 0;
};

struct DebugOverlayPassInputs
{
    // Debug overlays draw after production geometry and use the final world
    // view-projection. They do not participate in material or pass-resource
    // ownership.
    const RenderCameraLighting& camera;
    const RuntimeRenderModelFrameView& models;
    Assets::AssetSystem& assets;
    Rendering::Dx12ResourceBuilder& renderResources;
    Rendering::Dx12GeometryOwner& renderGeometry;
    Rendering::Dx12Diagnostics& renderDiagnostics;
    Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;
    const DebugOverlaySnapshot& snapshot;
    RuntimeTools& runtimeTools;
    const ReplayVisualPacket& replayVisualPacket;
    const Rendering::RetainedGeometryPacket& retainedOverlay;
    const Rendering::ContactManifoldPresentation& contactPresentation;
};

struct ShadowPassInputs
{
    // Shadows are optional. A null cinematic pointer means no shadow maps
    // should be built and receivers should get null shadow outputs.
    const RenderCameraLighting& camera;
    const RuntimeRenderModelFrameView& models;
    const Rendering::PrimitiveRenderContext& primitive;
    Rendering::Dx12FrameOwner& renderFrame;
    Rendering::Dx12TextureOwner& renderTextures;
    Rendering::Dx12Diagnostics& renderDiagnostics;
    Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;
    int windowWidth = 1;
    int windowHeight = 1;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic;
    bool terrainHidden;                              // Frame snapshot of debug/scene terrain visibility.
    bool collisionVisualizerVisible;                 // Collision-color mode disables object shadow casters.
};

struct ShadowPassOutput
{
    // Borrowed pointers into ShadowPassResources. Receivers must consume
    // them during the same RuntimeRenderer::RenderPreparedFrame() call; ShadowPass resource
    // release and the next frame both invalidate them.
    const Rendering::ShadowFrameData* terrainShadow = nullptr;
    const Rendering::ShadowFrameData* objectShadow = nullptr;
};

/*
Concept: FullscreenQuadPass

    Shared two-triangle draw surface for generated sky, volumetric light,
    and tonemap. It owns only the dynamic vertex buffer; shader meaning is
    owned by the pass that uses it.
*/
class FullscreenQuadPass
{
  public:
    explicit FullscreenQuadPass( FullscreenPassResources& resources ) : m_resources( resources )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources( Rendering::Dx12GeometryOwner* renderGeometry );

  private:
    FullscreenPassResources& m_resources;
};

/*
Concept: SkyPass

    Draws the current sky into whichever render target the caller has bound.
    The cube-map path samples authored face textures; the cinematic path
    owns a generated-atmosphere shader and uses FullscreenQuadPass.
*/
class SkyPass
{
  public:
    SkyPass( SkyPassResources& skyResources, FullscreenPassResources& fullscreenResources,
             std::unique_ptr<Geometry::SkyBox>& skyBox, const SkullbonezCore::Core::EngineConfig& config,
             SkullbonezCore::Core::Profiler* profiler )
        : m_skyResources( skyResources ), m_fullscreenResources( fullscreenResources ), m_skyBox( skyBox ),
          m_config( config ), m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    void Render( const RenderCameraLighting& camera, const Math::Transformation::Matrix4& view,
                 const SkullbonezCore::Core::CinematicRenderConfig* cinematic, Rendering::Dx12GeometryOwner& renderGeometry,
                 Rendering::Dx12TextureOwner& renderTextures, SkyPassMode mode );

  private:
    friend struct SkyPassTestAccess;

    class WorldViewLease
    {
      public:
        void Open( Geometry::SkyBox* skyBox )
        {
            m_skyBox = skyBox;
        }
        void Close()
        {
            m_skyBox = nullptr;
        }
        Geometry::SkyBox* Require( const char* operation ) const
        {
            if ( !m_skyBox )
            {
                SB_FATAL( "Runtime/Render/SkyPass", "%s requires the live world-view sky owner.", operation );
            }

            return m_skyBox;
        }

      private:
        Geometry::SkyBox* m_skyBox = nullptr;
    };

    static bool UsesCinematicAtmosphere( const SkullbonezCore::Core::CinematicRenderConfig* cinematic, SkyPassMode mode )
    {
        return mode == SkyPassMode::CinematicIfEnabled && cinematic && cinematic->skyAtmosphereEnabled;
    }
    Geometry::SkyBox& RequireWorldView( const char* operation );
    void RenderCinematicSky( const RenderCameraLighting& camera, const Math::Transformation::Matrix4& view,
                             const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                             Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12TextureOwner& renderTextures );

    SkyPassResources& m_skyResources;
    FullscreenPassResources& m_fullscreenResources;

    // Lifetime: this aliases the composition root's unique owner so startup and
    // backend teardown can replace the object without rebinding the pass.
    std::unique_ptr<Geometry::SkyBox>& m_skyBox;
    const SkullbonezCore::Core::EngineConfig& m_config;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
    WorldViewLease m_worldViewLease;
};

/*
Concept: SceneTargetPass

    Owns the HDR scene target used by cinematic rendering. Begin() binds and
    clears the target, then asks SkyPass to draw the background before world
    geometry is rendered into the target.
*/
class SceneTargetPass
{
  public:
    SceneTargetPass( CinematicScenePassResources& resources, SkyPass& skyPass, SkullbonezCore::Core::Profiler* profiler )
        : m_resources( resources ), m_skyPass( skyPass ), m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    bool IsReady() const;
    void Begin( const RenderCameraLighting& camera, const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12GeometryOwner& renderGeometry,
                Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12Diagnostics& renderDiagnostics,
                Rendering::RenderGpuTimingOwner* gpuTiming );

  private:
    CinematicScenePassResources& m_resources;
    SkyPass& m_skyPass;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
};

/*
Concept: ShadowPass

    Builds terrain/object shadow maps before receiver passes run. It owns
    the shadow targets and the per-frame receiver payloads that terrain and
    object shaders borrow for the rest of RuntimeRenderer::RenderPreparedFrame().
*/
class ShadowPass
{
  public:
    ShadowPass( ShadowPassResources& resources, SceneTerrain& terrain, const SkullbonezCore::Core::EngineConfig& config,
                RenderResourceLifecycleLog& lifecycleLog, SkullbonezCore::Core::Profiler* profiler )
        : m_resources( resources ), m_terrain( terrain ), m_config( config ), m_lifecycleLog( lifecycleLog ),
          m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources,
                             const SkullbonezCore::Core::CinematicRenderConfig& cinematic );
    void ReleaseGpuResources();

    // Clears last-frame receiver payloads without scheduling a render pass.
    // Disabled space scenes use this path so "shadows off" means no graph work.
    ShadowPassOutput ResetFrameOutputs();
    ShadowPassOutput Render( const ShadowPassInputs& inputs );

  private:
    void LogResourceLifecycleStep( const char* phase, const char* step ) const;
    Rendering::ShadowFrameData BuildTerrainFrameData( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                                      const Math::Vector::Vector3& lightDirectionWorld ) const;
    Rendering::ShadowFrameData BuildObjectFrameData( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                                     const Math::Vector::Vector3& lightDirectionWorld,
                                                     const Math::Vector::Vector3& focusHint,
                                                     const Rendering::RenderInstanceStore& renderInstances,
                                                     Threading::WorkerPool* renderWorkerPool, bool shadowParallelPrep );
    void RenderShadowMap( Rendering::FramebufferDX12& target, const Rendering::PrimitiveRenderContext& primitiveContext,
                          const Rendering::ShadowFrameData& shadowFrame,
                          const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                          Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12TextureOwner& renderTextures,
                          const Rendering::RenderInstanceStore& renderInstances, const Physics::ColliderStore& colliders,
                          Threading::WorkerPool* renderWorkerPool, bool renderTerrain, bool shadowParallelPrep,
                          const Rendering::ShadowCasterBatches* objectCasters );

    ShadowPassResources& m_resources;
    SceneTerrain& m_terrain;
    const SkullbonezCore::Core::EngineConfig& m_config;
    RenderResourceLifecycleLog& m_lifecycleLog;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
    bool m_activeTerrainHidden = false;
    bool m_activeCollisionVisualizerVisible = false;
    int m_activeWindowWidth = 1;
    int m_activeWindowHeight = 1;
};

/*
Concept: ReflectionPass

    Produces the reflection texture consumed by WaterPass. It chooses DXR
    reflection when possible, otherwise it renders a mirrored scene into the
    planar reflection target.
*/
class ReflectionPass
{
  public:
    ReflectionPass( ReflectionPassResources& resources, Physics::CollisionVisualizer& collisionVisualizer, SkyPass& skyPass,
                    const SkullbonezCore::Core::EngineConfig& config, Math::Transformation::Matrix4* dxrReflectionTransforms,
                    int dxrReflectionTransformCapacity, RenderResourceLifecycleLog& lifecycleLog,
                    SkullbonezCore::Core::Profiler* profiler )
        : m_resources( resources ), m_collisionVisualizer( collisionVisualizer ), m_skyPass( skyPass ), m_config( config ),
          m_dxrReflectionTransforms( dxrReflectionTransforms ),
          m_dxrReflectionTransformCapacity( dxrReflectionTransformCapacity ), m_lifecycleLog( lifecycleLog ),
          m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    ReflectionPassOutput Render( const ReflectionPassInputs& inputs );

  private:
    void LogResourceLifecycleStep( const char* phase, const char* step ) const;

    ReflectionPassResources& m_resources;
    Physics::CollisionVisualizer& m_collisionVisualizer;
    SkyPass& m_skyPass;
    const SkullbonezCore::Core::EngineConfig& m_config;
    Math::Transformation::Matrix4* m_dxrReflectionTransforms = nullptr;
    int m_dxrReflectionTransformCapacity = 0;
    RenderResourceLifecycleLog& m_lifecycleLog;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
};

/*
Concept: ObjectPass

    Draws production bodies or collision-state solids into the current
    target. The caller chooses whether this is the opaque or transparent
    body pass; this class owns the object shader texture-slot contract.
*/
class ObjectPass
{
  public:
    ObjectPass( Physics::CollisionVisualizer& collisionVisualizer, const SkullbonezCore::Core::EngineConfig& config,
                SkullbonezCore::Core::Profiler* profiler )
        : m_collisionVisualizer( collisionVisualizer ), m_config( config ), m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void Render( const ObjectPassInputs& inputs );

  private:
    Physics::CollisionVisualizer& m_collisionVisualizer;
    const SkullbonezCore::Core::EngineConfig& m_config;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
};

/*
Concept: TerrainPass

    Draws the terrain mesh with its material texture, cinematic style
    uniforms, and optional shadow receiver payload.
*/
class TerrainPass
{
  public:
    TerrainPass( SceneTerrain& terrain, const SkullbonezCore::Core::EngineConfig& config,
                 SkullbonezCore::Core::Profiler* profiler )
        : m_terrain( terrain ), m_config( config ), m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    void Render( const TerrainPassInputs& inputs );

  private:

    // Lifetime: borrows the stable scene terrain owner and resolves its current
    // terrain after each scene activation.
    SceneTerrain& m_terrain;
    const SkullbonezCore::Core::EngineConfig& m_config;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
};

/*
Concept: WaterPass

    Draws calm/ocean water after reflection has produced its texture. Water
    samples only the reflection slot and never rebuilds reflection itself.
*/
class WaterPass
{
  public:
    WaterPass( Environment::WorldEnvironment& world, const SkullbonezCore::Core::EngineConfig& config,
               SkullbonezCore::Core::Profiler* profiler )
        : m_world( world ), m_config( config ), m_profiler( profiler )
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
    const SkullbonezCore::Core::EngineConfig& m_config;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
    WaterPassDebugInfo m_debugInfo;
};

/*
Concept: DebugOverlayPass

    Draws non-production world overlays after the main scene. These overlays
    are intentionally separate from ObjectPass so debug visuals do not leak
    into material or shadow contracts.
*/
class DebugOverlayPass
{
  public:
    DebugOverlayPass( Physics::BroadphaseVisualizer& broadphaseVisualizer,
                      Physics::PhysicsDebugVisualizer& physicsDebugVisualizer, SceneTerrain& terrain,
                      Assets::AssetSystem& assets, SkullbonezCore::Core::Profiler* profiler )
        : m_broadphaseVisualizer( broadphaseVisualizer ), m_physicsDebugVisualizer( physicsDebugVisualizer ),
          m_terrain( terrain ), m_assets( assets ), m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    bool Render( const DebugOverlayPassInputs& inputs );

  private:
    bool HasOverlayWork( const DebugOverlayPassInputs& inputs ) const;

    Physics::BroadphaseVisualizer& m_broadphaseVisualizer;
    Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer;

    // Lifetime: borrows the stable scene terrain owner and resolves its current
    // terrain after each scene load.
    SceneTerrain& m_terrain;
    Assets::AssetSystem& m_assets;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
};

/*
Concept: VolumetricPass

    Reads the completed HDR scene color/depth target and writes a
    half-resolution light-shaft texture for TonemapPass to composite.
*/
class VolumetricPass
{
  public:
    VolumetricPass( CinematicScenePassResources& sceneResources, VolumetricLightPassResources& volumetricResources,
                    FullscreenPassResources& fullscreenResources, const SkullbonezCore::Core::EngineConfig& config,
                    SkullbonezCore::Core::Profiler* profiler )
        : m_sceneResources( sceneResources ), m_volumetricResources( volumetricResources ),
          m_fullscreenResources( fullscreenResources ), m_config( config ), m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    bool CanRender( bool cinematicEnabled, const SkullbonezCore::Core::CinematicRenderConfig* cinematic ) const;
    bool Render( const RenderCameraLighting& camera, const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                 Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12TextureOwner& renderTextures,
                 Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12GraphTransientPool& renderGraph,
                 Rendering::Dx12Diagnostics& renderDiagnostics, Rendering::RenderGpuTimingOwner* gpuTiming, int windowWidth,
                 int windowHeight, const Rendering::RenderGraphTextureBinding* graphOutput = nullptr );

  private:
    CinematicScenePassResources& m_sceneResources;
    VolumetricLightPassResources& m_volumetricResources;
    FullscreenPassResources& m_fullscreenResources;
    const SkullbonezCore::Core::EngineConfig& m_config;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
};

/*
Concept: TonemapPass

    Resolves the HDR scene target back to the window backbuffer. It owns the
    final post shader contract: scene color, scene depth, optional
    volumetric light, and cinematic grading uniforms.
*/
class TonemapPass
{
  public:
    TonemapPass( CinematicScenePassResources& sceneResources, VolumetricLightPassResources& volumetricResources,
                 TonemapPassResources& tonemapResources, FullscreenPassResources& fullscreenResources,
                 const SkullbonezCore::Core::EngineConfig& config, SkullbonezCore::Core::Profiler* profiler )
        : m_sceneResources( sceneResources ), m_volumetricResources( volumetricResources ),
          m_tonemapResources( tonemapResources ), m_fullscreenResources( fullscreenResources ), m_config( config ),
          m_profiler( profiler )
    {
    }

    void EnsureGpuResources( const RenderResourceContext& resources );
    void ReleaseGpuResources();
    void Render( const SkullbonezCore::Core::CinematicRenderConfig& cinematic, Rendering::Dx12GeometryOwner& renderGeometry,
                 Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12FrameOwner& renderFrame,
                 Rendering::Dx12Diagnostics& renderDiagnostics, Rendering::RenderGpuTimingOwner* gpuTiming, int windowWidth,
                 int windowHeight, bool sceneAlreadyUnbound, bool volumetricReady,
                 const Rendering::RenderGraphTextureBinding* graphVolumetric = nullptr );

  private:
    CinematicScenePassResources& m_sceneResources;
    VolumetricLightPassResources& m_volumetricResources;
    TonemapPassResources& m_tonemapResources;
    FullscreenPassResources& m_fullscreenResources;
    const SkullbonezCore::Core::EngineConfig& m_config;
    SkullbonezCore::Core::Profiler* m_profiler;      // Startup-bound diagnostics borrow; null when profiling is disabled.
};

/*
Concept: UiTextPass

    Cohesive 2D owner for the existing HUD, in-game UI, and signed-distance-field
    (SDF) text renderer.
    It owns text-batch state and process-lifetime presentation capabilities;
    RuntimeRenderer owns only the graph scheduling edge.
*/
class UiTextPass
{
  public:
    UiTextPass( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, SkullbonezCore::Core::Profiler* profiler,
                Rendering::RenderGpuTimingOwner& gpuTiming )
        : m_resultDiagnostics( resultDiagnostics ), m_profilerLifecycle( profiler ), m_gpuTiming( &gpuTiming )
    {
    }

    SkullbonezCore::Core::SbResult EnsureGpuResources( Rendering::Dx12ResourceBuilder& renderResources,
                                                       Rendering::Dx12TextureOwner& renderTextures,
                                                       Rendering::Dx12GeometryOwner& renderGeometry,
                                                       const Assets::AssetSystem& assets, int screenW, int screenH );
    void ReleaseGpuResources( Rendering::Dx12TextureOwner* renderTextures, Rendering::Dx12GeometryOwner* renderGeometry );
    bool ShouldRender( const OverlayDebugState& debug, const SceneSessionState& scene, bool crossScenePauseLocked,
                       const CameraControlState& camera, const UI::InGameUI& ui, bool replayScrubberVisible,
                       bool replayPathVisualizerHasTarget ) const;
    void SetDxrReflectionPreviewTexture( uint32_t textureHandle );
    float BeginFrame( RunTimerState& timers, const RuntimeRenderModelFrameView& models, double secondsPerFrame, int screenW,
                      int screenH );
    void RenderChromeStatus( const UiTextViewport& viewport, const OverlayDebugState& debug, bool crossScenePauseLocked,
                             const SceneSessionState& scene, const CameraControlState& camera, int sceneQueueSize,
                             const char* cameraModeLabel, Rendering::Dx12TextureOwner& renderTextures,
                             Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12Diagnostics& renderDiagnostics );
    void RenderChromeTail( const OverlayDebugState& debug, const ReplayHudStatus& replayHud, bool launcherCameraMode,
                           const char* launcherFireModeLabel, double reproMessageAgeSeconds,
                           Rendering::Dx12GeometryOwner& renderGeometry );
    void PrepareOperatorFrame( UI::InGameUIFrameData& uiData, const UiTextViewport& viewport, bool drawTestPattern,
                               Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12GeometryOwner& renderGeometry,
                               Rendering::Dx12Diagnostics& renderDiagnostics );
    void ProjectOperatorDiagnostics( UI::InGameUIFrameData& uiData, const ReplayHudStatus& replayHud, RunTimerState& timers,
                                     const RuntimeRenderModelFrameView& models, DiagnosticsRuntime& diagnosticsRuntime,
                                     UI::InGameUI& ui, Threading::WorkerPool* workerPool, double secondsPerFrame,
                                     Rendering::Dx12Diagnostics& renderDiagnostics );
    void ProjectOperatorSettings( UI::InGameUIFrameData& uiData, const OverlayDebugState& debug,
                                  const RenderPresentationSettings& renderPresentation, const SceneWorld& world,
                                  const SkullbonezCore::Core::EngineConfig& config,
                                  const SkullbonezCore::Core::CinematicRenderConfig& cinematic, bool cinematicRendering );
    void ProjectOperatorInteraction( UI::InGameUIFrameData& uiData, const RunRayCastTestState& rayCastTest,
                                     const RunEditorPlacementState& editor, const RuntimeInputContext& runtimeInput,
                                     const CameraControlState& camera, const UI::InGameUI& ui,
                                     uint32_t cameraModeEnabledMask, const char* cameraModeLabel );
    void ProjectOperatorPresentation( UI::InGameUIFrameData& uiData, const SceneSessionState& scene,
                                      const RuntimeViewModel& runtimeViewModel,
                                      const SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser,
                                      const UI::OperatorEditorFrameView& operatorEditorView, bool sceneHasCurrentEntry,
                                      const char* currentScenePath, int currentSceneBrowserIndex,
                                      float sceneEnergyForDisplay );
    void SubmitOperatorFrame( UI::InGameUIFrameData& uiData, UI::InGameUI& ui,
                              const RuntimeRenderTargetPreviewSnapshot& renderTargetPreviews, Assets::AssetSystem& assets,
                              Rendering::Dx12ResourceBuilder& renderResources, Rendering::Dx12TextureOwner& renderTextures,
                              Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12Diagnostics& renderDiagnostics,
                              int uiPassDrawCallStart );
    void RenderOverlayContent( const UiTextViewport& viewport, OverlayMode mode, int modelCount, float rollingFpsTime,
                               float sceneEnergyForDisplay, Rendering::Dx12TextureOwner& renderTextures,
                               Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12Diagnostics& renderDiagnostics );
    void RenderReplay( const ReplayOverlay::ReplayOverlayStateView& overlay, Core::Profiler* profiler,
                       bool legacySurfaceActive, bool scenePhysicsEnabled, RuntimeInteractionGestureKind gesture,
                       const UiTextViewport& viewport, double nowSeconds, Rendering::Dx12TextureOwner& renderTextures,
                       Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12Diagnostics& renderDiagnostics );
    void FinalizeOverlay( OverlayMode mode, Rendering::Dx12TextureOwner& renderTextures,
                          Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12Diagnostics& renderDiagnostics );
    void ReportRetainedDrawStats();

  private:
    friend struct UiTextPassTestAccess;

    class ProfilerLifecycle
    {
      public:
        explicit ProfilerLifecycle( SkullbonezCore::Core::Profiler* profiler ) : m_profiler( profiler )
        {
        }

        void Activate()
        {
            m_active = m_profiler != nullptr;
        }
        void Close()
        {
            m_active = false;
        }
        SkullbonezCore::Core::Profiler& Require( const char* operation ) const
        {
            if ( !m_active || !m_profiler )
            {
                SB_FATAL( "Runtime/Render/UiTextPass", "%s requires an active startup-bound profiler. active=%d profiler=%p",
                          operation, m_active ? 1 : 0, static_cast<void*>( m_profiler ) );
            }

            return *m_profiler;
        }

      private:
        SkullbonezCore::Core::Profiler* m_profiler = nullptr;
        bool m_active = false;
    };

    static bool PrepareMemoryTabProjection( bool sourceValid, SkullbonezCore::Core::MainMemoryStats& projected )
    {
        if ( !sourceValid )
        {
            projected = {};
            return false;
        }

        return true;
    }
    template <typename SampleMemory>
    static SkullbonezCore::Core::MainMemoryStats ProjectMemoryTabStats( bool sourceValid, SampleMemory&& sampleMemory )
    {
        SkullbonezCore::Core::MainMemoryStats projected;

        if ( !PrepareMemoryTabProjection( sourceValid, projected ) )
        {
            return projected;
        }

        return sampleMemory();
    }

    float UpdateFrameMetrics( RunTimerState& timers, const RuntimeRenderModelFrameView& models, double secondsPerFrame );
    static SkullbonezCore::Core::MainMemoryStats
    ProjectMemoryTabStats( DiagnosticsRuntime& diagnosticsRuntime, const ReplayHudStatus& replayHud,
                           const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects, double nowSeconds );

    // Lifetime: font vertices/projection and optional render capabilities share
    // this pass's process lifetime and are cleared before backend teardown.
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    Text::TextBatch m_textBatch;

    // Owns draw replay and the preview-only GPU objects. UI provides values and
    // never sees this renderer capability.
    UiDrawSubmission m_uiDrawSubmission;

    // Fixed scratch streams are retained by the pass so the 2,048-command
    // capacity never consumes nested Windows stack frames or grows at runtime.
    UI::UIDrawList m_testPatternDrawList;
    UI::UIDrawList m_badgeDrawList;
    UI::UIDrawList m_replayDrawList;
    UI::UIDrawList m_profilerDrawList;

    // Runtime owns the detached label/value snapshot. The frame packet lends
    // this fixed storage to UI only for the synchronous Memory-tab draw.
    UI::UIRuntimeReserveCapacityRow m_reserveCapacityRows[UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX];

    // Lifetime: backend resource release closes profile reads before the pass's
    // retained draw/GPU state is torn down; a successful rebuild reopens them.
    ProfilerLifecycle m_profilerLifecycle;
    Rendering::RenderGpuTimingOwner* m_gpuTiming = nullptr;
    uint32_t m_dxrReflectionPreviewTexture = 0;
};

} // namespace Runtime
} // namespace SkullbonezCore
