/*
File: SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp
Purpose:
  Implements renderer backend-epoch resource ownership.

Summary:
  Cold process and scene setup use the concrete owners captured at startup.
  Teardown commands clear only lifecycle-owned state and leave frame/pass order
  to RuntimeRenderer.

Glossary:
  DXR: DirectX Raytracing, used for optional hardware reflections.
  BLAS: Bottom-Level Acceleration Structure built from one mesh's vertices.
  Source asset: Stable authored record used to rebuild a backend texture handle.

Invariants:
  - Allocation-prone setup executes only in backend-init or scene-load phases.
  - Capability publication and required concrete backend owners agree.
  - Resource release leaves handles null or zero for a later rebuild.

Related:
  - SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
*/
#include "RenderResourceLifecycle.h"

#include "../Scene/SceneTerrain.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/FatalError.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../../World/SkyBox.h"
#include "../../World/Terrain.h"

#include <cassert>
#include <cstdio>

using namespace SkullbonezCore::Runtime;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;
namespace Rendering = SkullbonezCore::Rendering;

RenderResourceLifecycle::RenderResourceLifecycle( RuntimeRenderBackendView backend,
                                                  const RenderWorldView& world,
                                                  const RunSceneState& scene )
    : m_backend{ backend.renderFrame,
                 backend.renderGraph,
                 backend.renderResources,
                 backend.renderTextures,
                 backend.renderGeometry,
                 backend.renderDiagnostics,
                 backend.raytracing },
      m_lifecycleLog( backend.renderDevice, scene ), m_assets( world.assets ), m_terrain( world.terrain ),
      m_config( world.config ),
      m_primitiveBatches( std::in_place, backend.renderResources, backend.renderTextures, backend.renderGeometry ),
      m_gpuTiming( world.profiler, backend.renderDiagnostics ), m_uiTextPass( world.profiler, m_gpuTiming )
{
}


RenderResourceLifecycle::~RenderResourceLifecycle() = default;


SkullbonezCore::Core::SbResult RenderResourceLifecycle::InitialiseProcessResources( bool dumpTextureAssets )
{
    Rendering::Dx12ResourceBuilder& renderResources = *m_backend.renderResources;
    Rendering::Dx12TextureOwner& renderTextures = *m_backend.renderTextures;
    Rendering::Dx12GeometryOwner& renderGeometry = *m_backend.renderGeometry;

    m_textures.BindAssetSystem( &m_assets );
    m_textures.BindRenderContexts( &renderTextures, &renderTextures );
    enum class RebuildStep
    {
        RecreateHelperOwner,
        RegisterBuiltInSources,
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
        case RebuildStep::RebuildTextures:
        {
            const SkullbonezCore::Core::SbResult textureResult = m_textures.RebuildTexturesFromSourceAssets();
            if ( !textureResult.ok )
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

    m_skyBox = std::make_unique<Geometry::SkyBox>( -250, 300, -300, 300, -250, 300 );
    m_skyBox->BindTextures( m_textures );
    m_skyBox->BindRenderContexts( m_config, m_assets, renderResources );
    return m_skyBox->ResetRenderResources();
}


SkullbonezCore::Core::SbResult RenderResourceLifecycle::EnsureUiTextResources( int screenW, int screenH )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
    return m_uiTextPass.EnsureGpuResources( *m_backend.renderResources,
                                            *m_backend.renderTextures,
                                            *m_backend.renderGeometry,
                                            m_assets,
                                            screenW,
                                            screenH );
}


SkullbonezCore::Core::SbResult RenderResourceLifecycle::InitialiseSceneRayTracing( int modelCapacity )
{
    Rendering::Dx12RaytracingOwner* rayTracing = m_backend.raytracing;
    Rendering::Dx12Diagnostics* renderDiagnostics = m_backend.renderDiagnostics;
    const bool supported =
        renderDiagnostics && renderDiagnostics->GetCapabilities().supportsDxrReflection && rayTracing;
    if ( !supported )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    Rendering::PrimitiveMeshGeometryView sphereGeometry = PrimitiveBatches().SphereGeometry();
    if ( sphereGeometry.instancedMeshHandle == 0 )
    {
        if ( !m_backend.renderResources || !m_backend.renderTextures || !m_backend.renderGeometry )
        {
            // Lane F: capability publication without the resource facets needed
            // to build the renderer-owned primitive mesh is invalid wiring.
            SB_FATAL( "RenderResourceLifecycle",
                      "DXR reflection initialization requires concrete resource owners. resources=%d geometry=%d",
                      m_backend.renderResources ? 1 : 0,
                      m_backend.renderGeometry ? 1 : 0 );
        }
        const Rendering::PrimitiveRenderContext primitiveContext{ *m_backend.renderResources,
                                                                  *m_backend.renderTextures,
                                                                  *m_backend.renderGeometry,
                                                                  *renderDiagnostics,
                                                                  m_assets,
                                                                  m_config,
                                                                  PrimitiveBatches() };
        PrimitiveBatches().EnsureSphereMesh( primitiveContext );
        sphereGeometry = PrimitiveBatches().SphereGeometry();
    }

    if ( !m_terrain.Get() || !m_terrain.Get()->GetMesh() )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }
    Rendering::MeshDX12* terrainMesh = m_terrain.Get()->GetMesh();
    const uint64_t terrainVBVA = terrainMesh->GetVertexBufferGPUVA();
    const uint32_t sphereHandle = sphereGeometry.instancedMeshHandle;
    const uint64_t sphereVBVA = rayTracing->GetInstancedMeshStaticVBVA( sphereHandle );
    if ( terrainVBVA == 0 || sphereVBVA == 0 )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Lane R: device resource creation and shader bytecode failures remain a
    // recoverable renderer result reported through the scene-load transaction.
    const Rendering::RaytracingSetupDesc setup{
        { terrainVBVA, terrainMesh->GetVertexCount(), terrainMesh->GetStride() },
        { sphereVBVA, sphereGeometry.vertexCount, rayTracing->GetInstancedMeshStaticStride( sphereHandle ) },
        modelCapacity,
    };
    return rayTracing->InitDXR( setup );
}


RuntimeRenderTargetPreviewSnapshot
RenderResourceLifecycle::BuildRenderTargetPreviewSnapshot( bool shadowsAvailable,
                                                           bool cinematicTargetsAvailable,
                                                           bool volumetricAvailable ) const
{
    RuntimeRenderTargetPreviewSnapshot snapshot;
    const auto append = [&]( const char* label, const Rendering::FramebufferDX12* target, bool depth, bool available )
    {
        assert( snapshot.count < static_cast<int>( snapshot.targets.size() ) );
        RuntimeRenderTargetPreview& preview = snapshot.targets[static_cast<size_t>( snapshot.count++ )];
        preview.label = label;
        preview.textureHandle =
            target ? ( depth ? target->GetDepthTextureHandle() : target->GetColorTextureHandle() ) : 0;
        preview.width = target ? target->GetWidth() : 0;
        preview.height = target ? target->GetHeight() : 0;
        preview.available = available && preview.textureHandle != 0 && preview.width > 0 && preview.height > 0;
        preview.depth = depth;
        preview.hdr = target && !depth && target->GetColorFormat() == Rendering::FramebufferColorFormat::RGBA16F;
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


bool RenderResourceLifecycle::ShouldRenderUiText( const UiTextPassState& state, const UI::InGameUI& ui ) const
{
    return m_uiTextPass.ShouldRender( state, ui );
}


void RenderResourceLifecycle::SetUiTextRayTracingCapability( Rendering::Dx12RaytracingOwner* rayTracing )
{
    m_uiTextPass.SetRayTracingCapability( rayTracing );
}


void RenderResourceLifecycle::ReleaseHelperResources()
{
    m_primitiveBatches.reset();
}


void RenderResourceLifecycle::ReleaseUiTextResources()
{
    m_uiTextPass.ReleaseGpuResources( m_backend.renderTextures, m_backend.renderGeometry );
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


const Rendering::PrimitiveBatchRenderer& RenderResourceLifecycle::PrimitiveBatches() const
{
    assert( m_primitiveBatches.has_value() );
    return *m_primitiveBatches;
}
