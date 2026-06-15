/*
File: SkullbonezSource/SkullbonezSkyBox.h
Purpose:
  Builds and renders the skybox or sky backdrop for scene rendering.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/SkullbonezSkyBox.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezTextureCollection.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezIShader.h"
#include "SkullbonezIMesh.h"
#include "SkullbonezMatrix4.h"
#include <memory>
#include <array>


namespace SkullbonezCore
{
namespace Geometry
{
/* -- Sky Box ----------------------------------------------------------------------------------------------------------------------------------------------------

    A singleton class to represent a skybox.  Textures must be square and contain 3 pixels of padding around the edges.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkyBox
{

  private:
    inline static SkyBox* pInstance = nullptr;
    Box m_boundaries;                                              // Boundaries of sky box
    Textures::TextureCollection* m_textures;                       // Textures of the sky box
    std::unique_ptr<Rendering::IShader> m_shader;                  // Unlit textured m_shader
    std::array<std::unique_ptr<Rendering::IMesh>, 6> m_faceMeshes; // VBO mesh per face
    std::array<uint32_t, 6> m_faceTextures;                        // Texture hash per face

    SkyBox( int xMin, int xMax, int yMin, int yMax, int zMin, int zMax ); // Overloaded constructor
    ~SkyBox() = default;                                                  // Destructor
    void LoadTextures();                                                  // Load sky textures into TextureCollection
    void BuildMeshes();                                                   // Build VBO meshes for each face

  public:
    static SkyBox* Instance( int xMin, int xMax, int yMin, int yMax, int zMin, int zMax );               // Request for singleton instance
    static void Destroy();                                                                               // Destroy singleton instance
    void Render( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj ); // Render the sky box
    void ResetRenderResources();                                                                         // Rebuild meshes/shader after renderer reset/switch
};
} // namespace Geometry
} // namespace SkullbonezCore
