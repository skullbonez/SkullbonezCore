/*
File: SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp
Purpose:
  Implements renderer backend-epoch resource ownership.

Summary:
  Cold process and scene setup use the concrete owners captured at startup.
  Teardown commands clear only lifecycle-owned state and leave frame/pass order
  to RuntimeRenderer.

Invariants:
  - Allocation-prone setup executes only in backend-init or scene-load phases.
  - Capability publication and required concrete backend owners agree.
  - Resource release leaves handles null or zero for a later rebuild.
  - Preview publication appends through the snapshot owner's fixed-capacity
    boundary; lifecycle code never indexes the catalog directly.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
*/
#include "RenderResourceLifecycle.h"

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/FatalError.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../../World/SkyBox.h"
#include "../../World/Terrain.h"

#include <cassert>
#include <cstdio>
#include <utility>

using namespace SkullbonezCore::Runtime;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;
namespace Rendering = SkullbonezCore::Rendering;

RenderResourceLifecycle::RenderResourceLifecycle( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                  Rendering::RenderBackendDX12& backend, Assets::AssetSystem& assets,
                                                  SkullbonezCore::Core::EngineConfig& config,
                                                  SkullbonezCore::Core::Profiler* profiler, int sceneIndex,
                                                  int sceneLoadCount )
    : m_resultDiagnostics( resultDiagnostics ), m_renderDevice( backend.RenderDevice() ), m_renderFrame( backend.Frame() ),
      m_renderGraph( backend.GraphTransients() ), m_renderResources( backend.ResourceBuilder() ),
      m_renderTextures( backend.Textures() ), m_renderGeometry( backend.Geometry() ),
      m_renderDiagnostics( backend.Diagnostics() ), m_raytracing( backend.Raytracing() ),
      m_raytracingAvailable( backend.Diagnostics().GetCapabilities().supportsDxrReflection ),
      m_lifecycleLog( &backend.RenderDevice(), sceneIndex, sceneLoadCount ), m_assets( assets ),
      m_textures( resultDiagnostics ), m_config( config ),
      m_primitiveBatches( std::in_place, &backend.ResourceBuilder(), &backend.Textures(), &backend.Geometry() ),
      m_gpuTiming( profiler, &backend.Diagnostics() ), m_uiTextPass( resultDiagnostics, profiler, m_gpuTiming )
{
}


RenderResourceLifecycle::~RenderResourceLifecycle() = default;


SkullbonezCore::Core::SbResult RenderResourceLifecycle::InitialiseProcessResources( bool dumpTextureAssets )
{
    Rendering::Dx12ResourceBuilder& renderResources = m_renderResources;
    Rendering::Dx12TextureOwner& renderTextures = m_renderTextures;
    Rendering::Dx12GeometryOwner& renderGeometry = m_renderGeometry;

    m_textures.BindAssetSystem( &m_assets );
    m_textures.BindRenderContexts( &renderTextures, &renderTextures );
    enum class RebuildStep
    {
        RecreateHelperOwner,
        RegisterBuiltInSources,
        PrepareInitialRasterShaderBytecode,
        PrepareInitialPrimitiveVisibleShader,
        RebuildTextures
    };
    struct RebuildPhase
    {
        const char* name;
        RebuildStep step;
    };
    const RebuildPhase rebuildSteps[] = {
        { "recreate_helper_owner", RebuildStep::RecreateHelperOwner },
        { "register_builtin_source_records", RebuildStep::RegisterBuiltInSources },
        { "prepare_initial_raster_shader_bytecode", RebuildStep::PrepareInitialRasterShaderBytecode },
        { "prepare_initial_primitive_visible_shader", RebuildStep::PrepareInitialPrimitiveVisibleShader },
        { "rebuild_textures_from_source_assets", RebuildStep::RebuildTextures },
    };

    for ( const RebuildPhase& phase : rebuildSteps )
    {
        m_lifecycleLog.Write( "backend_rebuild", phase.name );

        switch ( phase.step )
        {
        case RebuildStep::RecreateHelperOwner:
            m_primitiveBatches.emplace( &renderResources, &renderTextures, &renderGeometry );
            break;
        case RebuildStep::RegisterBuiltInSources:
            m_assets.RegisterBuiltInSourceAssets( m_config );
            break;
        case RebuildStep::PrepareInitialRasterShaderBytecode:
        {
            // Why: all first-frame shader variants are a finite backend
            // contract even though their pass owners remain lazy. Manifest IO
            // and verification belong to this explicit cold rebuild step.
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
            renderResources.PrepareInitialRasterShaderBytecode();
            break;
        }
        case RebuildStep::PrepareInitialPrimitiveVisibleShader:
        {
            // Why: the primitive owner retains the one visible shader shared by
            // its first sphere, box, and pine batches. Keep only its shader-object
            // construction in cold backend setup; all other resources stay lazy.
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
            m_primitiveBatches->PrepareInitialVisibleShader( m_config.ordinaryRender );
            break;
        }
        case RebuildStep::RebuildTextures:
        {
            const SkullbonezCore::Core::SbResult textureResult = m_textures.RebuildTexturesFromSourceAssets();

            if ( !textureResult.Ok() )
            {
                return textureResult;
            }

            break;
        }
        }
    }

    if ( dumpTextureAssets )
    {
        m_textures.DumpTextureAssets( stdout );
    }

    m_skyBox = std::make_unique<Geometry::SkyBox>( m_resultDiagnostics, -250, 300, -300, 300, -250, 300 );
    m_skyBox->BindTextures( m_textures );
    m_skyBox->BindRenderContexts( m_config, m_assets, renderResources );
    return m_skyBox->ResetRenderResources();
}


SkullbonezCore::Core::SbResult RenderResourceLifecycle::EnsureUiTextResources( int screenW, int screenH )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
    std::unique_ptr<Rendering::ShaderDX12> textShader = m_assets.CreateShader( m_renderResources, "shader.text" );
    std::unique_ptr<Rendering::ShaderDX12> solidShader = m_assets.CreateShader( m_renderResources, "shader.solid_color" );
    std::unique_ptr<Rendering::ShaderDX12> solidBatchShader = m_assets.CreateShader( m_renderResources,
                                                                                     "shader.solid_color_batch" );
    return m_uiTextPass.EnsureGpuResources( m_renderTextures, m_renderGeometry, std::move( textShader ),
                                            std::move( solidShader ), std::move( solidBatchShader ), screenW, screenH );
}


SkullbonezCore::Core::SbResult RenderResourceLifecycle::InitialiseSceneRayTracing( Geometry::Terrain* terrain,
                                                                                   int modelCapacity )
{
    if ( !m_raytracingAvailable )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    Rendering::Dx12RaytracingOwner& rayTracing = m_raytracing;

    Rendering::PrimitiveMeshGeometryView sphereGeometry = PrimitiveBatches().SphereGeometry();

    if ( sphereGeometry.instancedMeshHandle == 0 )
    {

        PrimitiveBatches().EnsureSphereMesh();
        sphereGeometry = PrimitiveBatches().SphereGeometry();
    }

    if ( !terrain || !terrain->GetMesh() )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    Rendering::MeshDX12* terrainMesh = terrain->GetMesh();
    const uint64_t terrainVBVA = terrainMesh->GetVertexBufferGPUVA();
    const uint32_t sphereHandle = sphereGeometry.instancedMeshHandle;
    const uint64_t sphereVBVA = rayTracing.GetInstancedMeshStaticVBVA( sphereHandle );

    if ( terrainVBVA == 0 || sphereVBVA == 0 )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Recoverable error: device resource creation and shader bytecode failures remain a
    // recoverable renderer result reported through the scene-load transaction.
    const Rendering::RaytracingSetupDesc setup {
        { terrainVBVA, terrainMesh->GetVertexCount(), terrainMesh->GetStride() },
        { sphereVBVA, sphereGeometry.vertexCount, rayTracing.GetInstancedMeshStaticStride( sphereHandle ) },
        modelCapacity,
    };

    return rayTracing.InitDXR( setup );
}


RuntimeRenderTargetPreviewSnapshot
RenderResourceLifecycle::BuildRenderTargetPreviewSnapshot( bool shadowsAvailable, bool cinematicTargetsAvailable,
                                                           bool volumetricAvailable ) const
{
    RuntimeRenderTargetPreviewSnapshot snapshot;
    const auto append = [&]( const char* label, const Rendering::FramebufferDX12* target, bool depth, bool available )
    {
        RuntimeRenderTargetPreview preview;
        preview.label = label;
        preview.textureHandle = target ? ( depth ? target->GetDepthTextureHandle() : target->GetColorTextureHandle() ) : 0;
        preview.width = target ? target->GetWidth() : 0;
        preview.height = target ? target->GetHeight() : 0;
        preview.available = available && preview.textureHandle != 0 && preview.width > 0 && preview.height > 0;
        preview.depth = depth;
        preview.hdr = target && !depth && target->GetColorFormat() == Rendering::FramebufferColorFormat::RGBA16F;
        snapshot.AppendCatalogTarget( preview );
    };

    append( "Reflection Color", m_passResources.reflection.target.get(), false, true );
    append( "Reflection Depth", m_passResources.reflection.target.get(), true, true );
    append( "Terrain Shadow Depth", m_passResources.shadows.terrainTarget.get(), true, shadowsAvailable );
    append( "Object Shadow Depth", m_passResources.shadows.objectTarget.get(), true, shadowsAvailable );
    append( "Terrain Shadow Color", m_passResources.shadows.terrainTarget.get(), false, shadowsAvailable );
    append( "Object Shadow Color", m_passResources.shadows.objectTarget.get(), false, shadowsAvailable );
    append( "Cinematic Scene Color", m_passResources.cinematicScene.hdrTarget.get(), false, cinematicTargetsAvailable );
    append( "Cinematic Scene Depth", m_passResources.cinematicScene.hdrTarget.get(), true, cinematicTargetsAvailable );
    append( "Volumetric Color", nullptr, false, volumetricAvailable );
    append( "Volumetric Depth", nullptr, true, volumetricAvailable );
    return snapshot;
}


bool RenderResourceLifecycle::ShouldRenderUiText( const UiTextVisibility& visibility ) const
{
    return m_uiTextPass.ShouldRender( visibility );
}


void RenderResourceLifecycle::SetUiTextDxrReflectionPreviewTexture( uint32_t textureHandle )
{
    m_uiTextPass.SetDxrReflectionPreviewTexture( textureHandle );
}


void RenderResourceLifecycle::ReleaseHelperResources()
{
    m_primitiveBatches.reset();
}


void RenderResourceLifecycle::ReleaseUiTextResources()
{
    m_uiTextPass.ReleaseGpuResources( &m_renderTextures, &m_renderGeometry );
}


void RenderResourceLifecycle::InvalidateProfilerResources()
{
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    // Lifetime: invalidate backend queries while the diagnostics facet is live,
    // then clear Core's value history through the concrete timing owner.
    m_gpuTiming.InvalidateDevice();
#endif
}


void RenderResourceLifecycle::ReleaseTextureResources()
{
    m_textures.DeleteAllTextures();
    m_textures.BindAssetSystem( nullptr );
    m_textures.BindRenderContexts( nullptr, nullptr );
}


void RenderResourceLifecycle::ReleaseSkyResources()
{
    if ( m_skyBox )
    {
        m_skyBox->ReleaseRenderResources();
        m_skyBox.reset();
    }
}


Rendering::PrimitiveBatchRenderer& RenderResourceLifecycle::PrimitiveBatches()
{
    assert( m_primitiveBatches.has_value() );
    return *m_primitiveBatches;
}
