/*
File: SkullbonezSource/World/SkyBox.h
Purpose:
  Builds and renders the skybox or sky backdrop for scene rendering.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  Face mesh: One quad for a side of the cube; each face binds a different sky
  texture hash.

  Invariants:
  - SkyBox is runtime-owned by Run in normal paths; the legacy singleton entry
    point remains only as a compatibility shell.
  - The texture registry is borrowed from Run and must be rebound before any
    render-resource rebuild.

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
#include "../Maths/Matrix4.h"
#include <memory>
#include <array>


namespace SkullbonezCore
{
namespace Geometry
{
/* -- Sky Box
----------------------------------------------------------------------------------------------------------------------------------------------------

    A singleton class to represent a skybox.  Textures must be square and contain 3 pixels of padding around the edges.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkyBox
{

  private:
    inline static SkyBox* pInstance = nullptr;
    Box m_boundaries;                                              // World-space cube bounds around the scene camera.
    Textures::TextureCollection* m_textures;                       // Borrowed texture registry; scene/runtime owns it.
    std::unique_ptr<Rendering::IShader> m_shader;                  // Unlit textured shader rebuilt on backend reset.
    std::array<std::unique_ptr<Rendering::IMesh>, 6> m_faceMeshes; // One renderer-owned quad mesh per cube face.
    std::array<uint32_t, 6> m_faceTextures;                        // Texture hash selected for each cube face.

    void LoadTextures();
    void BuildMeshes();

  public:
    SkyBox( int xMin, int xMax, int yMin, int yMax, int zMin, int zMax );
    ~SkyBox() = default;
    SkyBox( const SkyBox& ) = delete;
    SkyBox& operator=( const SkyBox& ) = delete;

    static SkyBox* Instance( int xMin,
                             int xMax,
                             int yMin,
                             int yMax,
                             int zMin,
                             int zMax );                           // Lazy singleton access for scene-owned sky bounds.
    static void Destroy();
    void BindTextures( Textures::TextureCollection& textures );    // Borrow Run-owned texture registry for sky faces.
    void ReleaseRenderResources();                                 // Releases backend-owned sky meshes/shader and clears service borrows.
    void Render( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj );
    void ResetRenderResources();                                   // Rebuild meshes/shader after renderer reset/switch
};
} // namespace Geometry
} // namespace SkullbonezCore
