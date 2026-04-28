#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezVector3.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezMesh.h"
#include "SkullbonezShader.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;


namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
} // namespace GameObjects

namespace Environment
{
/* -- World Environment ------------------------------------------------------------------------------------------------------------------------------------------

    Defines the global world properties such as gravity, fluids, density etc.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class WorldEnvironment
{

  public:
    WorldEnvironment();                                                                                    // Default constructor
    WorldEnvironment( float fFluidSurfaceHeight, float fFluidDensity, float fGasDensity, float fGravity ); // Overloaded constructor
    ~WorldEnvironment();                                                                                   // Default destructor
    WorldEnvironment( WorldEnvironment&& ) noexcept = default;                                             // Move constructor
    WorldEnvironment& operator=( WorldEnvironment&& ) noexcept = default;                                  // Move assignment

    void SetTerrainBounds( float xMin, float xMax, float zMin, float zMax );                                                                                                  // Must be called before first render; drives calm/ocean mesh split
    void RenderFluid( const Matrix4& view, const Matrix4& proj, const Matrix4& reflectVP, float time, GLuint reflectionTex, bool flatWater = false, bool noReflect = false ); // Renders the water in the scene
    void ResetGLResources();                                                                                                                                                  // Rebuilds GPU resources after GL context recreation
    float GetFluidSurfaceHeight();                                                                                                                                            // Returns the fluid surface height
    void AddWorldForces( GameObjects::GameModel& target, float changeInTime );                                                                                                // Adds world forces to the referenced game model

  private:
    float m_fluidSurfaceHeight; // scalar
    float m_fluidDensity;       // kg/m^3
    float m_gasDensity;         // kg/m^3
    float m_gravity;            // m/s^2
    float m_terrainXMin = 0.0f; // terrain footprint — calm mesh bounds
    float m_terrainXMax = 0.0f;
    float m_terrainZMin = 0.0f;
    float m_terrainZMax = 0.0f;
    std::unique_ptr<Mesh> m_calmMesh; // Inner water: flat, reflective
    std::unique_ptr<Shader> m_calmShader;
    std::unique_ptr<Mesh> m_oceanMesh; // Outer water: waves + perturbation
    std::unique_ptr<Shader> m_oceanShader;

    void BuildFluidMesh(); // Builds calm and ocean meshes

    float CalculateGravity( float objectMass );                                                                                              // returns Y-component representing Newtons of gravity acting on object
    float CalculateBuoyancy( float submergedObjectVolume );                                                                                  // returns Y-component representing Newtons of buoyancy acting on the object
    Vector3 CalculateViscousDrag( Vector3 velocityVector, float submergedVolumePercent, float dragCoefficient, float projectedSurfaceArea ); // returns a vector representative of the supplied velocity vector after viscous drag has acted upon it
};
} // namespace Environment
} // namespace SkullbonezCore
