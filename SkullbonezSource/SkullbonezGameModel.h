/*
File: SkullbonezSource/SkullbonezGameModel.h
Purpose:
  Defines one renderable and optionally simulated object in the scene.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezGameModel.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>
#include "SkullbonezWorldEnvironment.h"
#include "SkullbonezCommon.h"
#include "SkullbonezRenderMaterial.h"
#include "SkullbonezRigidBody.h"
#include "SkullbonezCollisionShape.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezResponseInformation.h"


namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
} // namespace Environment
namespace Physics
{
// A TerrainContactPoint is one exact place where a moving model is touching
// the terrain. Think of it as a thumbtack on the object: the solver will push
// at this thumbtack, not at the object's center. rA is the lever arm from the
// body center to the thumbtack, so the same push can also create spin.
struct TerrainContactPoint
{
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    uint32_t featureId = 0;
};

// A TerrainContactManifold is the full "touching terrain" report for one body
// during one physics tick. Spheres usually have one point; boxes can have
// several corners touching at once. This struct is only geometry and policy
// metadata. The actual velocity response happens later in GameModelCollection's
// shared contact-row solver.
struct TerrainContactManifold
{
    int bodyA = -1;
    int bodyB = -1; // Static terrain sentinel.
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    TerrainContactPoint points[8];
    uint8_t pointCount = 0;
    float timeOfImpact = 0.0f;
    bool sweptHit = false;
    bool supportsRestingPolicy = true;
    bool inhibitsSleep = false;
    uint32_t terrainCellId = 0;
    uint32_t materialId = 0;
};
} // namespace Physics

namespace GameObjects
{
/* -- Game Model -------------------------------------------------------------------------------------------------------------------------------------------------

    Represents the highest level object in the library: a renderable mesh
    with collision bounds and physics information.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class GameModel
{
  private:
    struct BallPhysicsCache
    {
        // This cache is named for the original sphere-only code path, but it now
        // stores the hot scalar properties for both spheres and boxes. For boxes,
        // radius means conservative bounding radius: the distance from center to
        // the farthest corner, used for cheap broadphase checks.
        float radius;                               // Sphere radius
        float radiusSq;                             // Sphere radius squared
        float volume;                               // Sphere volume
        float invVolume;                            // 1 / sphere volume
        float projectedSurfaceArea;                 // Cached circle area used by drag
        float dragCoefficient;                      // Cached sphere drag coefficient
        float mass;                                 // Immutable ball mass
        float invMass;                              // 1 / mass
        Math::Vector::Vector3 rotationalInertia;    // Immutable inertia tensor diagonal
        Math::Vector::Vector3 invRotationalInertia; // Component-wise 1 / inertia
    };

    // Authoritative collision shape. Broadphase may use cached radii for speed,
    // but narrowphase and solver-row setup come back to this variant so boxes,
    // spheres, and future shapes can each provide their real contact geometry.
    Math::CollisionDetection::CollisionShape m_boundingVolume; // Bounding volume (variant, inline)
    BallPhysicsCache m_ballPhysics;                            // Immutable per-ball physics cache for hot loops
    Physics::RigidBody m_physicsInfo;                          // Physics information for the game object
    Environment::WorldEnvironment* m_worldEnvironment;         // Pointer to the world environment settings
    Geometry::Terrain* m_terrain;                              // Pointer to the world m_terrain
    // Temporary terrain-hit mailbox. CollisionDetectTerrain writes where and
    // when a terrain hit happened; BuildTerrainContactManifold reads it and
    // converts it into solver-neutral contact points. It is not the solver.
    Physics::ResponseInformation m_responseInformation; // Information regarding a collision response that needs to be reacted to
    float m_projectedSurfaceArea;                       // 2d surface area approximation based on dynamics object list
    float m_dragCoefficient;                            // Calculated based on the average drag coefficient of all dynamics objects
    float m_fixedContactHighlightSeconds;               // Seconds remaining for fixed-body red contact feedback
    float m_renderTintR;                                // Per-instance render tint red channel
    float m_renderTintG;                                // Per-instance render tint green channel
    float m_renderTintB;                                // Per-instance render tint blue channel
    float m_renderColorOverride;                        // 1 = render with tint as material color, 0 = material tint multiplier
    Rendering::RenderMaterial m_renderMaterial;         // Render-only material intent, mirrored to the current tint bridge
    bool m_isResponseRequired;                          // Terrain detection handoff flag; shared terrain rows consume generated manifolds
    bool m_isFixed;                                     // True for immovable collision bodies such as floating ramps
    char m_name[64];                                    // Optional name for logging (empty = unnamed)

    void BuildSpherePhysicsCache( float radius );                                  // Precompute immutable sphere data used in hot paths
    const Math::CollisionDetection::BoundingSphere& GetBoundingSphere() const;     // Sphere-only fast path accessor (variant-backed)
    Math::CollisionDetection::BoundingSphere& GetBoundingSphere();                 // Mutable sphere-only fast path accessor (variant-backed)
    void CalculateVolume();                                                        // Calculates the volume of the model
    void ApplyWorldForces( float changeInTime );                                   // Apply forces on the body from the world environment
    void UpdateModelInfo();                                                        // Perform this operation every time the model has objects added or removed from its object list
    float GetTerrainCollisionTime( float changeInTime );                           // Gets the time of collision between the current GameModel instance and the terrain
    float GetModelCollisionTime( GameModel& collisionTarget, float changeInTime ); // Gets the time of collision between the current GameModel instance and collisionTarget

    // Box/terrain contact must be measured from the actual oriented box vertices,
    // not from the model center plus a vertical support extent. On uneven terrain,
    // the center-based shortcut can say a box is supported while every real vertex
    // is still visibly above the surface. This helper returns the closest true
    // vertex, the terrain sample under that vertex, and the signed vertical gap.
    bool GetClosestBoxTerrainVertex( Math::Vector::Vector3& outVertex, float& outTerrainHeight, Geometry::Plane& outPlane, float& outGap );
    bool GetClosestHullTerrainVertex( Math::Vector::Vector3& outVertex, float& outTerrainHeight, Geometry::Plane& outPlane, float& outGap );
    void ClampToTerrainSurface(); // Keep residual terrain penetration from leaking past integration

  public:
    struct ObjectSweepResult
    {
        bool hit = false;
        float collisionTime = 0.0f;
    };

    GameModel( Environment::WorldEnvironment* pWorldEnv, const Math::Vector::Vector3& vPosition, const Math::Vector::Vector3& vRotationalInertia, float fMass ); // Overloaded constructor
    ~GameModel() = default;
    GameModel( GameModel&& ) noexcept = default;            // Move constructor
    GameModel& operator=( GameModel&& ) noexcept = default; // Move assignment

    Math::Transformation::Matrix4 GetModelMatrix();                                                                                   // Returns the model matrix for rendering (T*R*T*S)
    bool IsResponseRequired();                                                                                                        // Indicates whether terrain/deprecated response is required
    void ClearResponseRequired();                                                                                                     // Clears the terrain/deprecated response flag after the owner consumes it
    float GetSubmergedVolumePercent();                                                                                                // Returns the percentage of the game model submerged in fluid
    float GetMass();                                                                                                                  // Returns the mass of the game model
    float GetInvertedMass();                                                                                                          // Returns inverted mass (cached immutable)
    float GetVolume();                                                                                                                // Returns the volume of the game model
    void CalculateProjectedSurfaceArea();                                                                                             // Calculates the sum of the surface area of the game model
    void CalculateDragCoefficient();                                                                                                  // Calculates the drag coefficient of the model
    float GetProjectedSurfaceArea();                                                                                                  // Returns the projected surface area of the model
    float GetDragCoefficient();                                                                                                       // Returns the drag coefficient of the model
    const Math::Vector::Vector3& GetPosition();                                                                                       // Returns the position of the game model
    const Math::Vector::Vector3& GetPosition() const;                                                                                 // Const read for manifold row rA/rB setup
    const Math::Vector::Vector3& GetVelocity();                                                                                       // Returns the velocity of the model
    const Math::Vector::Vector3& GetAngularVelocity();                                                                                // Returns the angular velocity of the model
    void ApplyForces( float changeInTime );                                                                                           // Update the models velocity based on its current physicsInfo
    void UpdatePosition( float changeInTime );                                                                                        // Update the models position based on its current physicsInfo
    void SetTerrain( Geometry::Terrain* pTerrain );                                                                                   // Sets the terrain pointer
    float CollisionDetectTerrain( float changeInTime );                                                                               // Collision detect model against terrain
    bool BuildTerrainContactManifold( int bodyIndex, float timeOfImpact, float availableTime, Physics::TerrainContactManifold& out ); // Builds terrain contact geometry for the shared row solver
    void SetImpulseForce( const Math::Vector::Vector3& vForce, const Math::Vector::Vector3& vApplicationPoint );                      // Sets an impulse force for the model
    void SetCoefficientRestitution( float fCoefficientRestitution );                                                                  // Sets the coefficient of restitution for the game model
    void SetWorldForce( const Math::Vector::Vector3& vWorldForce, const Math::Vector::Vector3& vWorldTorque );                        // Sets the worlds forces acting on the model
    void SetInitialOrientation( float fEulerXDeg, float fEulerYDeg, float fEulerZDeg );                                               // Sets the initial orientation from euler angles (degrees)
    void SetName( const char* name );                                                                                                 // Sets the ball's log name (up to 63 chars)
    const char* GetName() const;                                                                                                      // Returns the ball's log name
    void SetRenderTint( float tintR, float tintG, float tintB, float colorOverride );                                                 // Sets per-instance render tint/override
    void GetRenderTint( float& tintR, float& tintG, float& tintB, float& colorOverride ) const;                                       // Returns per-instance render tint/override
    void SetRenderMaterial( const Rendering::RenderMaterial& material );                                                              // Sets render-only material intent
    const Rendering::RenderMaterial& GetRenderMaterial() const;                                                                       // Returns render-only material intent
    void AddBoundingSphere( float fRadius );                                                                                          // Add a bounding sphere to the game model
    void AddBoundingBox( const Math::Vector::Vector3& halfExtents );                                                                  // Add a bounding box to the game model
    void AddConvexHull( const Math::CollisionDetection::ConvexHullShape& hull );                                                      // Add an authored convex hull to the game model
    bool ScaleCollisionShapeAxisFromBase( const Math::CollisionDetection::CollisionShape& baseShape, int axis, float factor );        // Rebuilds this model shape from a base copy scaled along one axis
    bool IsSphere() const;                                                                                                            // True if bounding volume is a BoundingSphere
    bool IsBox() const;                                                                                                               // True if bounding volume is a BoundingBox
    bool IsConvexHull() const;                                                                                                        // True if bounding volume is a ConvexHullShape
    bool UsesWorldInertia() const;                                                                                                    // True if angular inertia must rotate with the body
    const char* GetShapeName() const;                                                                                                 // Stable diagnostic shape name
    void SetFixed( bool isFixed );                                                                                                    // Make this model immovable while still participating in contacts
    bool IsFixed() const;                                                                                                             // True if the model is an immovable collision body
    void NotifyFixedContact( float highlightSeconds );                                                                                // Refresh fixed-body contact highlight timer
    void TickFixedContactHighlight( float dt );                                                                                       // Decay fixed-body contact highlight timer
    float GetFixedContactHighlightAlpha() const;                                                                                      // 0=no contact tint, 1=full red contact tint
    ObjectSweepResult SweepGameModel( GameModel& collisionTarget, float changeInTime );                                               // Swept object/object query with explicit hit state
    float GetBoundingRadius();                                                                                                        // Returns the radius of the bounding sphere
    Math::Vector::Vector3 GetOrientationUp();                                                                                         // Returns local Y axis (0,1,0) rotated into world space by the visual orientation
    const Math::Orientation::Quaternion& GetOrientation() const;                                                                      // Returns the orientation quaternion (passthrough to RigidBody)
    const Math::Vector::Vector3& GetRotationalInertia();                                                                              // Returns the rotational inertia (passthrough to RigidBody)
    const Math::Vector::Vector3& GetInvertedRotationalInertia();                                                                      // Returns component-wise inverse rotational inertia (cached immutable)
    float GetCoefficientRestitution();                                                                                                // Returns the coefficient of restitution (passthrough to RigidBody)
    const Math::CollisionDetection::CollisionShape& GetCollisionShape() const;                                                        // Const shape variant for narrowphase manifold dispatch
    void SetLinearVelocity( const Math::Vector::Vector3& v );                                                                         // Sets the linear velocity (passthrough to RigidBody)
    void SetAngularVelocity( const Math::Vector::Vector3& v );                                                                        // Sets the angular velocity (passthrough to RigidBody)
    void SetPosition( const Math::Vector::Vector3& pos );                                                                             // Teleports the model to a world position (passthrough to RigidBody)
    void SetOrientation( const Math::Orientation::Quaternion& q );                                                                    // Sets the orientation quaternion (passthrough to RigidBody)
};
} // namespace GameObjects
} // namespace SkullbonezCore
