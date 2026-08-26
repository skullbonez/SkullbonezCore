/*
File: SkullbonezSource/World/SkyBox.h
Purpose:
  Builds and renders the skybox or sky backdrop for scene rendering.

Summary:
  SkyBox builds six face meshes and texture bindings for one backend epoch;
  RenderResourceLifecycle owns the instance while render passes borrow it.

Glossary:
  Face mesh: One quad for a side of the cube; each face binds a different sky
  texture hash.

Invariants:
  - RenderResourceLifecycle owns the active SkyBox and releases its GPU
    resources before the concrete backend owners die.
  - Texture, asset, resource-builder, and config owners are borrowed for the
    backend epoch and must be rebound before a render-resource rebuild.
  - Release clears every rebuild borrow; reset validates the complete set before
    loading textures or creating a mesh/shader.
  - Authored face textures are square and include three pixels of edge padding
    so cube seams do not sample unrelated texels.

Related:
  - SkullbonezSource/World/SkyBox.cpp
  - SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Core/SbResult.h"
#include "../Core/FatalError.h"
#include "../Assets/TextureCollection.h"
#include "../Maths/Vector3.h"
#include "../Maths/GeometricStructures.h"
#include "../Rendering/DX12/ShaderDX12.h"
#include "../Rendering/DX12/MeshDX12.h"
#include "../Maths/Matrix4.h"
#include <memory>
#include <array>


namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}

namespace Rendering
{
class Dx12ResourceBuilder;
}

namespace Geometry
{
struct SkyBoxRenderLifecycleTestAccess;

// Lifetime: sky rebuild borrows form one backend-epoch lease. Release closes
// the complete lease so a later reset must bind all owners again.
class SkyBoxRenderRebuildLease
{
  public:
    void BindTextures( const void* textures ) noexcept
    {
        m_textures = textures;
    }
    void BindContexts( const void* config, const void* assets, const void* resources ) noexcept
    {
        m_config = config;
        m_assets = assets;
        m_resources = resources;
    }
    void Release() noexcept
    {
        m_textures = nullptr;
        m_config = nullptr;
        m_assets = nullptr;
        m_resources = nullptr;
    }
    bool Complete() const noexcept
    {
        return m_textures && m_config && m_assets && m_resources;
    }
    void Require( const char* operation ) const
    {
        if ( !Complete() )
        {
            SB_FATAL( "World/SkyBox",
                      "Skybox render-resource operation requires complete backend-epoch bindings. operation=%s "
                      "textures=%d config=%d assets=%d resources=%d",
                      operation ? operation : "unknown", m_textures ? 1 : 0, m_config ? 1 : 0, m_assets ? 1 : 0,
                      m_resources ? 1 : 0 );
        }
    }

  private:
    const void* m_textures = nullptr;
    const void* m_config = nullptr;
    const void* m_assets = nullptr;
    const void* m_resources = nullptr;
};

class SkyBox
{

  private:
    friend struct SkyBoxRenderLifecycleTestAccess;
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    Box m_boundaries;                                                     // World-space cube bounds around the scene camera.
    Textures::TextureCollection* m_textures;                              // Borrowed texture registry; scene/runtime owns it.
    const SkullbonezCore::Core::EngineConfig* m_config;                   // Borrowed sky texture/scale settings from the runtime config.
    Assets::AssetSystem* m_assets;                                        // Borrowed asset registry used to resolve shader logical names.
    Rendering::Dx12ResourceBuilder* m_resources;                          // Borrowed cold builder for sky GPU objects.
    SkyBoxRenderRebuildLease m_renderLease;                               // Authoritative rebuild-borrow lifecycle.
    std::unique_ptr<Rendering::ShaderDX12> m_shader;                      // Unlit textured shader rebuilt on backend reset.
    std::array<std::unique_ptr<Rendering::MeshDX12>, 6> m_faceMeshes;     // One renderer-owned quad mesh per cube face.
    std::array<uint32_t, 6> m_faceTextures;                               // Texture hash selected for each cube face.

    void RequireRenderBindings( const char* operation ) const;
    SkullbonezCore::Core::SbResult LoadTextures( const SkullbonezCore::Core::EngineConfig& config );
    static SkullbonezCore::Core::SbResult
    RequiredRenderResourcesResult( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                   const std::array<bool, 6>& meshesReady, bool shaderReady );

    SkullbonezCore::Core::SbResult BuildMeshes( const SkullbonezCore::Core::EngineConfig& config,
                                                Assets::AssetSystem& assets,
                                                Rendering::Dx12ResourceBuilder& resources );

  public:
    SkyBox( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, int xMin, int xMax, int yMin, int yMax, int zMin,
            int zMax );
    ~SkyBox() = default;
    SkyBox( const SkyBox& ) = delete;
    SkyBox& operator=( const SkyBox& ) = delete;

    void BindTextures( Textures::TextureCollection& textures );           // Borrow Run-owned texture registry for sky faces.
    void BindRenderContexts( const SkullbonezCore::Core::EngineConfig& config, Assets::AssetSystem& assets,
                             Rendering::Dx12ResourceBuilder& resources ); // Borrow rebuild-only services for sky resources.
    void ReleaseRenderResources();                                        // Releases backend-owned sky meshes/shader and clears service borrows.
    SkullbonezCore::Core::SbResult Render( const Math::Transformation::Matrix4& view,
                                           const Math::Transformation::Matrix4& proj );
    SkullbonezCore::Core::SbResult ResetRenderResources();                // Rebuild meshes/shader after renderer reset/switch
};
} // namespace Geometry
} // namespace SkullbonezCore
