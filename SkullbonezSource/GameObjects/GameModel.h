/*
File: SkullbonezSource/GameObjects/GameModel.h
Purpose:
  Defines one renderable and optionally simulated object in the scene.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Center of buoyancy: World-space average location of displaced water. Applying
  lift through this point creates the roll/pitch torque that makes floating
  bodies settle.
  Wet sample: Fixed point inside a box-like volume used to approximate how much
  water is under that part of the body.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/GameObjects/GameModel.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>
#include "../World/WorldEnvironment.h"
#include "../Core/Common.h"
#include "../Rendering/RenderMaterial.h"
#include "../Physics/RigidBody.h"
#include "../Physics/CollisionShape.h"
#include "../World/Terrain.h"
#include "../Physics/ResponseInformation.h"


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
    int bodyB = -1;                                                         // -1 marks terrain, which is static and not stored in the body array.
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    TerrainContactPoint points[8];
    uint8_t pointCount = 0;
    float timeOfImpact = 0.0f;
    bool sweptHit = false;
    bool supportsRestingPolicy = true;
    bool allowsTangentFriction = true;
    bool inhibitsSleep = false;
    uint32_t terrainCellId = 0;
    uint32_t materialId = 0;
};
} // namespace Physics

namespace GameObjects
{
/* -- Game Model
-------------------------------------------------------------------------------------------------------------------------------------------------

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
        float radius;                                                       // Conservative bounding radius in meters for broadphase and terrain support.
        float radiusSq;                                                     // Squared radius avoids sqrt in hot distance checks.
        float volume;                                                       // Collision volume in cubic meters, used for buoyancy scaling.
        float invVolume;                                                    // Reciprocal volume; cache builders require nonzero shape volume.
        float projectedSurfaceArea;                                         // Drag-facing area in square meters.
        float dragCoefficient;                                              // Shape drag coefficient cached before force integration.
        float mass;                                                         // Authoring mass in kilograms; fixed bodies still keep this for reports.
        float invMass;                                                      // Solver inverse mass; fixed bodies use zero.
        Math::Vector::Vector3 rotationalInertia;                            // Local-space inertia tensor diagonal for principal axes.
        Math::Vector::Vector3 invRotationalInertia;                         // Component-wise reciprocal used by angular solver rows.
    };

    // Authoritative collision shape. Broadphase may use cached radii for speed,
    // but narrowphase and solver-row setup come back to this variant so boxes,
    // spheres, and future shapes can each provide their real contact geometry.
    Math::CollisionDetection::CollisionShape
        m_boundingVolume;                                                   // Inline shape variant; narrowphase dispatch owns exact geometry here.
    BallPhysicsCache m_ballPhysics;                                         // Immutable scalar cache read by broadphase, buoyancy, and drag loops.
    Physics::RigidBody m_physicsInfo;                                       // Integrator state: position, velocity, orientation, forces, and impulses.
    Environment::WorldEnvironment* m_worldEnvironment;                      // Borrowed world force/fluid settings; owning scene keeps it alive.
    Geometry::Terrain* m_terrain;                                           // Borrowed terrain used for height samples and terrain contacts.
    // Temporary terrain-hit mailbox. CollisionDetectTerrain writes where and
    // when a terrain hit happened; BuildTerrainContactManifold reads it and
    // converts it into solver-neutral contact points. It is not the solver.
    Physics::ResponseInformation
        m_responseInformation;                                              // Legacy terrain-response mailbox consumed before shared manifold solving.
    float m_projectedSurfaceArea;                                           // Drag area in square meters after collision-shape updates.
    float m_dragCoefficient;                                                // Effective drag coefficient averaged from attached dynamics data.
    float m_fixedContactHighlightSeconds;                                   // Seconds remaining for fixed-body red contact feedback
    float m_renderTintR;                                                    // Per-instance render tint red channel
    float m_renderTintG;                                                    // Per-instance render tint green channel
    float m_renderTintB;                                                    // Per-instance render tint blue channel
    float m_renderColorOverride;                                            // 1 = render with tint as material color, 0 = material tint multiplier
    Rendering::RenderMaterial m_renderMaterial;                             // Render-only material intent, mirrored to the current tint bridge
    bool m_isResponseRequired;                                              // Terrain detection handoff flag; shared terrain rows consume generated manifolds
    bool m_isFixed;                                                         // True for immovable collision bodies such as floating ramps
    bool m_releasesFromFixedOnContact;                                      // Fixed decorative pieces can become dynamic after a real hit.
    float m_contactReleaseImpulseThreshold;                                 // Minimum solved normal impulse before fixed-contact release.
    uint32_t m_replayBodyId;                                                // Stable replay-facing id assigned by GameModelCollection.
    char m_name[64];                                                        // Optional name for logging (empty = unnamed)

    void BuildSpherePhysicsCache(
        float radius );                                                     // Hot-path cache built from authoring radius before broadphase and drag sampling.
    const Math::CollisionDetection::BoundingSphere&
    GetBoundingSphere() const;                                              // Precondition: m_boundingVolume currently holds BoundingSphere.
    Math::CollisionDetection::BoundingSphere&
    GetBoundingSphere();                                                    // Mutable sphere fast path; caller owns the shape-kind precondition.
    void CalculateVolume();                                                 // Refreshes cached collision volume after shape changes.
    void ApplyWorldForces( float changeInTime );
    void UpdateModelInfo();                                                 // Refreshes derived physics/render scalars after object-list mutations.
    float GetTerrainCollisionTime( float changeInTime );                    // Swept terrain time-of-impact over changeInTime seconds.
    float GetModelCollisionTime( GameModel& collisionTarget,
                                 float changeInTime );                      // Swept object/object time-of-impact over changeInTime seconds.

    // Box/terrain contact must be measured from the actual oriented box vertices,
    // not from the model center plus a vertical support extent. On uneven terrain,
    // the center-based shortcut can say a box is supported while every real vertex
    // is still visibly above the surface. This helper returns the closest true
    // vertex, the terrain sample under that vertex, and the signed vertical gap.
    bool GetClosestBoxTerrainVertex( Math::Vector::Vector3& outVertex,
                                     float& outTerrainHeight,
                                     Geometry::Plane& outPlane,
                                     float& outGap );
    bool GetClosestHullTerrainVertex( Math::Vector::Vector3& outVertex,
                                      float& outTerrainHeight,
                                      Geometry::Plane& outPlane,
                                      float& outGap );
    void ClampToTerrainSurface();                                           // Corrects tiny post-solve terrain overlap before it accumulates visibly.

  public:
    struct BuoyancySample
    {
        // Invariant: this report is part of deterministic physics. Keep the
        // sample storage fixed-size and caller-owned so buoyancy never allocates
        // while the frame is being simulated.
        static constexpr uint8_t MAX_WET_POINTS = 32;

        float submergedVolumePercent = 0.0f;                                // 0..1 fraction of collision volume below usable water.
        Math::Vector::Vector3 centerOfBuoyancy = Math::Vector::ZERO_VECTOR; // World-space lift point.

        // Wet points are world-space samples used by water damping and
        // righting. Weights are relative water exposure and are normalized by
        // wetWeightTotal before callers share damping across the sample set.
        Math::Vector::Vector3 wetPoints[MAX_WET_POINTS] = {};
        float wetWeights[MAX_WET_POINTS] = {};
        float wetWeightTotal = 0.0f;
        uint8_t wetPointCount = 0;
    };

    struct ObjectSweepResult
    {
        bool hit = false;
        float collisionTime = 0.0f;
    };

    GameModel( Environment::WorldEnvironment* pWorldEnv,
               const Math::Vector::Vector3& vPosition,
               const Math::Vector::Vector3& vRotationalInertia,
               float fMass );                                               // Scene construction path; world pointer is borrowed.
    ~GameModel() = default;
    GameModel( GameModel&& ) noexcept = default;
    GameModel& operator=( GameModel&& ) noexcept = default;

    Math::Transformation::Matrix4
    GetModelMatrix();                                                       // World transform sent to rendering; order is translate * rotate * terrain-offset * scale.
    bool IsResponseRequired();                                              // Legacy terrain-response mailbox has data waiting for the owner.
    void ClearResponseRequired();                                           // Owner has consumed the terrain-response mailbox for this tick.
    float GetSubmergedVolumePercent();                                      // Fraction in [0,1] used by buoyancy and fluid drag.
    BuoyancySample CalculateBuoyancySample();                               // Submerged fraction plus the world-space center of displaced water.
    Math::Vector::Vector3
    CalculateBuoyancyRightingTorque( float buoyancyForce,
                                     float submergedVolumePercent );        // Water stability torque from principal inertia.
    float GetMass();
    float GetInvertedMass();                                                // Immutable inverse mass cache; fixed bodies use zero.
    float GetVolume();
    void CalculateProjectedSurfaceArea();                                   // Refreshes fluid-drag area from the current collision shape.
    void CalculateDragCoefficient();                                        // Refreshes fluid-drag coefficient from the current collision shape.
    float GetProjectedSurfaceArea();
    float GetDragCoefficient();
    const Math::Vector::Vector3& GetPosition();
    const Math::Vector::Vector3& GetPosition() const;                       // Const position access for manifold lever-arm setup.
    const Math::Vector::Vector3& GetVelocity();
    const Math::Vector::Vector3& GetVelocity() const;
    const Math::Vector::Vector3& GetAngularVelocity();
    const Math::Vector::Vector3& GetAngularVelocity() const;
    void ApplyForces( float changeInTime );
    void UpdatePosition( float changeInTime );
    void SetTerrain( Geometry::Terrain* pTerrain );                         // Borrowed scene terrain; caller keeps it alive for this model.
    float CollisionDetectTerrain(
        float changeInTime );                                               // Swept terrain query that fills the response mailbox but applies no impulse.
    bool BuildTerrainContactManifold(
        int bodyIndex,
        float timeOfImpact,
        float availableTime,
        Physics::TerrainContactManifold& out );                             // Emits solver-row terrain geometry from the terrain
                                                // mailbox; false when mailbox has no hit.
    void SetImpulseForce( const Math::Vector::Vector3& vForce,
                          const Math::Vector::Vector3&
                              vApplicationPoint );                          // Stages a one-shot impulse at a world-space application point.
    void SetCoefficientRestitution( float fCoefficientRestitution );
    void SetWorldForce(
        const Math::Vector::Vector3& vWorldForce,
        const Math::Vector::Vector3& vWorldTorque );                        // Continuous environment force/torque consumed during integration.
    void SetInitialOrientation( float fEulerXDeg,
                                float fEulerYDeg,
                                float fEulerZDeg );                         // Input angles are degrees in the engine's Euler order.
    void SetName( const char* name );                                       // Diagnostic name is capped at 63 bytes for deterministic logs.
    const char* GetName() const;
    void SetReplayBodyId( uint32_t id );                                    // Replay ids are scene-local and assigned once by the owning collection.
    uint32_t GetReplayBodyId() const;
    void SetRenderTint( float tintR,
                        float tintG,
                        float tintB,
                        float colorOverride );                              // Render-only color override; physics state is unaffected.
    void GetRenderTint( float& tintR,
                        float& tintG,
                        float& tintB,
                        float& colorOverride ) const;                       // Mirrors the shader-facing tint payload.
    void SetRenderMaterial(
        const Rendering::RenderMaterial& material );                        // Render intent only; collision material stays elsewhere.
    const Rendering::RenderMaterial& GetRenderMaterial() const;
    void AddBoundingSphere( float fRadius );
    void AddBoundingBox( const Math::Vector::Vector3& halfExtents );
    void AddConvexHull( const Math::CollisionDetection::ConvexHullShape& hull );
    bool ScaleCollisionShapeAxisFromBase(
        const Math::CollisionDetection::CollisionShape& baseShape,
        int axis,
        float factor );                                                     // Rebuilds this model shape from a base copy scaled along one axis.
    bool IsSphere() const;
    bool IsBox() const;
    bool IsConvexHull() const;
    bool UsesWorldInertia() const;                                          // Non-spherical inertia must rotate with the body before solving.
    const char* GetShapeName() const;                                       // Stable diagnostic spelling for logs and SkullScope output.
    void SetFixed( bool isFixed );                                          // Immovable models still contribute contacts to dynamic bodies.
    bool IsFixed() const;
    void SetContactReleaseOnImpact( bool enabled,
                                    float impulseThreshold );               // Optional fixed-to-dynamic release for hit foliage.
    bool ReleasesFromFixedOnContact() const;
    float GetContactReleaseImpulseThreshold() const;
    void NotifyFixedContact(
        float highlightSeconds );                                           // Restarts red contact feedback when a dynamic body hits fixed geometry.
    void TickFixedContactHighlight( float dt );                             // dt is seconds; saturates at no-contact tint.
    float GetFixedContactHighlightAlpha() const;                            // 0=no contact tint, 1=full red contact tint.
    float GetFixedContactHighlightSeconds() const;                          // Raw highlight timer for replay/prediction state restore.
    void SetFixedContactHighlightSeconds(
        float seconds );                                                    // Restores the raw contact-highlight timer after speculative physics.
    ObjectSweepResult SweepGameModel( GameModel& collisionTarget,
                                      float changeInTime );                 // Swept object/object query with explicit hit state.
    float GetBoundingRadius();                                              // Conservative broadphase radius; convex hulls and boxes are not sphere geometry.
    Math::Vector::Vector3 GetOrientationUp();                               // Local +Y axis rotated into world space by the visual orientation.
    const Math::Orientation::Quaternion& GetOrientation() const;
    const Math::Vector::Vector3& GetRotationalInertia();
    const Math::Vector::Vector3& GetInvertedRotationalInertia();            // Component-wise inverse inertia cache for solver rows.
    float GetCoefficientRestitution();
    const Math::CollisionDetection::CollisionShape&
    GetCollisionShape() const;                                              // Authoritative shape variant for narrowphase manifold dispatch.
    void SetLinearVelocity( const Math::Vector::Vector3& v );
    void SetAngularVelocity( const Math::Vector::Vector3& v );
    void SetPosition( const Math::Vector::Vector3& pos );                   // Teleports the model; bypasses force accumulation.
    void SetOrientation( const Math::Orientation::Quaternion& q );
};
} // namespace GameObjects
} // namespace SkullbonezCore
