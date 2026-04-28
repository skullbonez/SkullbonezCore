/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_SKY_BOX_H
#define SKULLBONEZ_SKY_BOX_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezTextureCollection.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezShader.h"
#include "SkullbonezMesh.h"
#include "SkullbonezMatrix4.h"
#include <memory>
#include <array>

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Textures;
using namespace SkullbonezCore::Rendering;

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
    static SkyBox* pInstance;                          // Singleton instance pointer
    Box m_boundaries;                                  // Boundaries of sky box
    TextureCollection* m_textures;                     // Textures of the sky box
    std::unique_ptr<Shader> m_shader;                  // Unlit textured m_shader
    std::array<std::unique_ptr<Mesh>, 6> m_faceMeshes; // VBO mesh per face
    std::array<uint32_t, 6> m_faceTextures;            // Texture hash per face

    SkyBox( int xMin, int xMax, int yMin, int yMax, int zMin, int zMax ); // Overloaded constructor
    ~SkyBox( void ) = default;                                            // Destructor
    void LoadTextures( void );                                            // Load sky textures into TextureCollection
    void BuildMeshes( void );                                             // Build VBO meshes for each face

  public:
    static SkyBox* Instance( int xMin, int xMax, int yMin, int yMax, int zMin, int zMax ); // Request for singleton instance
    static void Destroy( void );                                                           // Destroy singleton instance
    void Render( const Matrix4& view, const Matrix4& proj );                               // Render the sky box
    void ResetGLResources( void );                                                         // Rebuild meshes/shader after GL context recreated
};
} // namespace Geometry
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
