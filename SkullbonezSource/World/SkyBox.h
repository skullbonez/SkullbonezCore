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

class SkyBox
{

  private:
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    Box m_boundaries;                                                     // World-space cube bounds around the scene camera.
    Textures::TextureCollection* m_textures;                              // Borrowed texture registry; scene/runtime owns it.
    const SkullbonezCore::Core::EngineConfig* m_config;                   // Borrowed sky texture/scale settings from the runtime config.
    Assets::AssetSystem* m_assets;                                        // Borrowed asset registry used to resolve shader logical names.
    Rendering::Dx12ResourceBuilder* m_resources;                          // Borrowed cold builder for sky GPU objects.
    std::unique_ptr<Rendering::ShaderDX12> m_shader;                      // Unlit textured shader rebuilt on backend reset.
    std::array<std::unique_ptr<Rendering::MeshDX12>, 6> m_faceMeshes;     // One renderer-owned quad mesh per cube face.
    std::array<uint32_t, 6> m_faceTextures;                               // Texture hash selected for each cube face.

    SkullbonezCore::Core::SbResult LoadTextures( const SkullbonezCore::Core::EngineConfig& config );
    void BuildMeshes( const SkullbonezCore::Core::EngineConfig& config, Assets::AssetSystem& assets,
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
