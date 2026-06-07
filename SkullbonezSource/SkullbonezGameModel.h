#pragma once


// --- Includes ---
#include "SkullbonezWorldEnvironment.h"
#include "SkullbonezCommon.h"
#include "SkullbonezRigidBody.h"
#include "SkullbonezCollisionShape.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezResponseInformation.h"


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;


namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
} // namespace Environment
namespace Physics
{
class ImpulseSolver;
} // namespace Physics

namespace GameObjects
{
/* -- Game Model -------------------------------------------------------------------------------------------------------------------------------------------------

    Represents the highest level object in the library - a renderable mesh
    with collision bounds and physics information.

    TODO: 3DS MODEL mesh BELONGS HERE!
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class GameModel
{
    friend class ImpulseSolver; // Unified sequential impulse solver needs private access to cached shape/rigid-body state

  private:
    struct BallPhysicsCache
    {
        float radius;                 // Sphere radius
        float radiusSq;               // Sphere radius squared
        float volume;                 // Sphere volume
        float invVolume;              // 1 / sphere volume
        float projectedSurfaceArea;   // Cached circle area used by drag
        float dragCoefficient;        // Cached sphere drag coefficient
        float mass;                   // Immutable ball mass
        float invMass;                // 1 / mass
        Vector3 rotationalInertia;    // Immutable inertia tensor diagonal
        Vector3 invRotationalInertia; // Component-wise 1 / inertia
    };

    CollisionShape m_boundingVolume;                    // Bounding volume (variant, inline)
    BallPhysicsCache m_ballPhysics;                     // Immutable per-ball physics cache for hot loops
    RigidBody m_physicsInfo;                            // Physics information for the game object
    Environment::WorldEnvironment* m_worldEnvironment;  // Pointer to the world environment settings
    Geometry::Terrain* m_terrain;                       // Pointer to the world m_terrain
    Physics::ResponseInformation m_responseInformation; // Information regarding a collision response that needs to be reacted to
    float m_projectedSurfaceArea;                       // 2d surface area approximation based on dynamics object list
    float m_dragCoefficient;                            // Calculated based on the average drag coefficient of all dynamics objects
    float m_fixedContactHighlightSeconds;               // Seconds remaining for fixed-body red contact feedback
    bool m_isResponseRequired;                          // Indicates whether a response is required or not
    bool m_isFixed;                                     // True for immovable collision bodies such as floating ramps
    char m_name[64];                                    // Optional name for logging (empty = unnamed)

    void BuildSpherePhysicsCache( float radius );                                  // Precompute immutable sphere data used in hot paths
    const BoundingSphere& GetBoundingSphere() const;                               // Sphere-only fast path accessor (variant-backed)
    BoundingSphere& GetBoundingSphere();                                           // Mutable sphere-only fast path accessor (variant-backed)
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
    bool GetClosestBoxTerrainVertex( Vector3& outVertex, float& outTerrainHeight, Plane& outPlane, float& outGap );
    void DEBUG_SetSphereToTerrain(); // Debug routine - ensure sphere does not go through terrain

  public:
    GameModel( Environment::WorldEnvironment* pWorldEnv, const Vector3& vPosition, const Vector3& vRotationalInertia, float fMass ); // Overloaded constructor
    ~GameModel() = default;
    GameModel( GameModel&& ) noexcept = default;            // Move constructor
    GameModel& operator=( GameModel&& ) noexcept = default; // Move assignment

    Matrix4 GetModelMatrix();                                                           // Returns the model matrix for rendering (T*R*T*S)
    bool IsResponseRequired();                                                          // Indicates whether a collision response is required
    void ClearResponseRequired();                                                       // Clears the response-required flag after an inline collision response
    float GetSubmergedVolumePercent();                                                  // Returns the percentage of the game model submerged in fluid
    float GetMass();                                                                    // Returns the mass of the game model
    float GetInvertedMass();                                                            // Returns inverted mass (cached immutable)
    float GetVolume();                                                                  // Returns the volume of the game model
    void CalculateProjectedSurfaceArea();                                               // Calculates the sum of the surface area of the game model
    void CalculateDragCoefficient();                                                    // Calculates the drag coefficient of the model
    float GetProjectedSurfaceArea();                                                    // Returns the projected surface area of the model
    float GetDragCoefficient();                                                         // Returns the drag coefficient of the model
    const Vector3& GetPosition();                                                       // Returns the position of the game model
    const Vector3& GetPosition() const;                                                 // Const read for manifold row rA/rB setup
    const Vector3& GetVelocity();                                                       // Returns the velocity of the model
    const Vector3& GetAngularVelocity();                                                // Returns the angular velocity of the model
    void ApplyForces( float changeInTime );                                             // Update the models velocity based on its current physicsInfo
    void UpdatePosition( float changeInTime );                                          // Update the models position based on its current physicsInfo
    void SetTerrain( Geometry::Terrain* pTerrain );                                     // Sets the terrain pointer
    float CollisionDetectTerrain( float changeInTime );                                 // Collision detect model against terrain
    bool CollisionResponseTerrain( float changeInTime );                                // Collision response model against terrain; false means contact should stay awake
    void SetImpulseForce( const Vector3& vForce, const Vector3& vApplicationPoint );    // Sets an impulse force for the model
    void SetCoefficientRestitution( float fCoefficientRestitution );                    // Sets the coefficient of restitution for the game model
    void SetWorldForce( const Vector3& vWorldForce, const Vector3& vWorldTorque );      // Sets the worlds forces acting on the model
    void SetInitialOrientation( float fEulerXDeg, float fEulerYDeg, float fEulerZDeg ); // Sets the initial orientation from euler angles (degrees)
    void SetName( const char* name );                                                   // Sets the ball's log name (up to 63 chars)
    const char* GetName() const;                                                        // Returns the ball's log name
    void AddBoundingSphere( float fRadius );                                            // Add a bounding sphere to the game model
    void AddBoundingBox( const Vector3& halfExtents );                                  // Add a bounding box to the game model
    bool IsBox() const;                                                                 // True if bounding volume is a BoundingBox
    void SetFixed( bool isFixed );                                                      // Make this model immovable while still participating in contacts
    bool IsFixed() const;                                                               // True if the model is an immovable collision body
    void NotifyFixedContact( float highlightSeconds );                                  // Refresh fixed-body contact highlight timer
    void TickFixedContactHighlight( float dt );                                         // Decay fixed-body contact highlight timer
    float GetFixedContactHighlightAlpha() const;                                        // 0=no contact tint, 1=full red contact tint
    float CollisionDetectGameModel( GameModel& collisionTarget, float changeInTime );   // Collision detect model against model
    void CollisionResponseGameModel( GameModel& responseTarget );                       // Collision response model against model (velocity-only)
    void StaticOverlapResponseGameModel( GameModel& overlapTarget );                    // Check for static overlap and push apart if overlapping
    float GetBoundingRadius();                                                          // Returns the radius of the bounding sphere
    Vector3 GetOrientationUp();                                                         // Returns local Y axis (0,1,0) rotated into world space by the visual orientation
    const Quaternion& GetOrientation() const;                                           // Returns the orientation quaternion (passthrough to RigidBody)
    const Vector3& GetRotationalInertia();                                              // Returns the rotational inertia (passthrough to RigidBody)
    const Vector3& GetInvertedRotationalInertia();                                      // Returns component-wise inverse rotational inertia (cached immutable)
    float GetCoefficientRestitution();                                                  // Returns the coefficient of restitution (passthrough to RigidBody)
    const CollisionShape& GetCollisionShape() const;                                    // Const shape variant for narrowphase manifold dispatch
    void SetLinearVelocity( const Vector3& v );                                         // Sets the linear velocity (passthrough to RigidBody)
    void SetAngularVelocity( const Vector3& v );                                        // Sets the angular velocity (passthrough to RigidBody)
    void SetPosition( const Vector3& pos );                                             // Teleports the model to a world position (passthrough to RigidBody)
    void SetOrientation( const Quaternion& q );                                         // Sets the orientation quaternion (passthrough to RigidBody)
};
} // namespace GameObjects
} // namespace SkullbonezCore
