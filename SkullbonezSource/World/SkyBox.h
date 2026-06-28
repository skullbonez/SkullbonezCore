/*
File: SkullbonezSource/World/SkyBox.h
Purpose:
  Builds and renders the skybox or sky backdrop for scene rendering.

Mental model:
  Run owns one skybox shell and injects the texture registry, asset registry,
  config, and render resource factory used to rebuild backend-facing resources.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  Command context: Borrowed frame capability used here to bind sky-face textures
  without reacquiring the renderer singleton.
  Render resource factory: Borrowed renderer capability used to build face
  meshes and upload sky textures.
  Face mesh: One quad for a side of the cube; each face binds a different sky
  texture hash.

Invariants:
  - Run owns the skybox shell and passes borrowed texture/assets/config services
    into it during startup.
  - Backend-owned meshes and shaders are released before renderer teardown and
    rebuilt from the borrowed services after renderer reset.

Related:
  - SkullbonezSource/World/SkyBox.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Assets/TextureCollection.h"
#include "../Maths/Vector3.h"
#include "../Maths/GeometricStructures.h"
#include "../Rendering/IShader.h"
#include "../Rendering/IMesh.h"
#include "../Rendering/IRenderCommandContext.h"
#include "../Rendering/IRenderResourceFactory.h"
#include "../Maths/Matrix4.h"
#include <memory>
#include <array>


namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}

namespace Geometry
{
class SkyBox
{

  private:
    Box m_boundaries;                                              // World-space cube bounds around the scene camera.
    Textures::TextureCollection& m_textures;                       // Borrowed texture registry; scene/runtime owns it.
    Assets::AssetSystem& m_assets;                                 // Borrowed source registry for shader creation.
    const Basics::EngineConfig& m_config;                          // Borrowed startup/runtime config sampled by Run.
    std::unique_ptr<Rendering::IShader> m_shader;                  // Unlit textured shader rebuilt on backend reset.
    std::array<std::unique_ptr<Rendering::IMesh>, 6> m_faceMeshes; // One renderer-owned quad mesh per cube face.
    std::array<uint32_t, 6> m_faceTextures;                        // Texture hash selected for each cube face.

    void LoadTextures( Rendering::IRenderResourceFactory& renderResources );
    void BuildMeshes( Rendering::IRenderResourceFactory& renderResources );

  public:
    SkyBox( Textures::TextureCollection& textures,
            Assets::AssetSystem& assets,
            const Basics::EngineConfig& config,
            int xMin,
            int xMax,
            int yMin,
            int yMax,
            int zMin,
            int zMax );
    ~SkyBox() = default;
    void Render( Rendering::IRenderCommandContext& renderCommands,
                 const Math::Transformation::Matrix4& view,
                 const Math::Transformation::Matrix4& proj );
    void ResetRenderResources( Rendering::IRenderResourceFactory& renderResources ); // Rebuild after backend reset/switch.
    void ReleaseRenderResources();                                             // Release backend-owned meshes/shader.
};
} // namespace Geometry
} // namespace SkullbonezCore
