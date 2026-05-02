#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezTextureCollection.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezIShader.h"
#include "SkullbonezIMesh.h"
#include "SkullbonezMatrix4.h"
#include <memory>
#include <array>


// --- Usings ---
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
    inline static SkyBox* pInstance = nullptr;
    Box m_boundaries;                                   // Boundaries of sky box
    TextureCollection* m_textures;                      // Textures of the sky box
    std::unique_ptr<IShader> m_shader;                  // Unlit textured m_shader
    std::array<std::unique_ptr<IMesh>, 6> m_faceMeshes; // VBO mesh per face
    std::array<uint32_t, 6> m_faceTextures;             // Texture hash per face

    SkyBox( int xMin, int xMax, int yMin, int yMax, int zMin, int zMax ); // Overloaded constructor
    ~SkyBox() = default;                                                  // Destructor
    void LoadTextures();                                                  // Load sky textures into TextureCollection
    void BuildMeshes();                                                   // Build VBO meshes for each face

  public:
    static SkyBox* Instance( int xMin, int xMax, int yMin, int yMax, int zMin, int zMax ); // Request for singleton instance
    static void Destroy();                                                                 // Destroy singleton instance
    void Render( const Matrix4& view, const Matrix4& proj );                               // Render the sky box
    void ResetGLResources();                                                               // Rebuild meshes/shader after GL context recreated
};
} // namespace Geometry
} // namespace SkullbonezCore
