// =============================================================================
// IMPULSE SOLVER (SkullbonezImpulseSolver.cpp)
// =============================================================================
//
// PURPOSE: Unified sequential impulse solver for terrain and object-object
//          collisions.  Handles both spheres and oriented boxes.
//
// --- Algorithm (Erin Catto / Box2D / Bullet) ---
//
//  1. Build contact manifold:
//       Sphere  → 1 contact at the bottom pole (center - radius * normal)
//       Box     → up to 8 vertex contacts, filtered by proximity to deepest vertex
//  2. Pre-compute per-contact effective mass (K = 1/m + n·((I⁻¹(r×n))×r))
//  3. Compute Baumgarte bias for resting contacts (penetration correction)
//  4. Iteratively solve normal + two friction constraints (20 iterations)
//       Normal:  accumulated impulse clamped ≥ 0 (push only, not pull)
//       Friction: Coulomb cone |λ_t| ≤ μ·λ_n with gravity-floor warm-start
//  5. Direct position correction (project out remaining penetration × 0.4)
//  6. Rolling friction torque (opposes ω, decays to rest)
//  7. Sleep eligibility rejects under-constrained terrain contact manifolds
//  8. Sleep (zero velocity when both linear and angular fall below threshold)
//  9. Visual pole alignment (spheres only, cosmetic)
//
// --- World-Space Inertia for Boxes ---
//
//  I_world_inv * v = R · (I_body_inv ∘ (Rᵀ · v))
//
//  Where R is the current orientation matrix.  Spheres use body-space (isotropic).
//
// --- References ---
//  Erin Catto, "Iterative Dynamics with Temporal Coherence", GDC 2005
//    Local copy: Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf
//    https://box2d.org/files/ErinCatto_IterativeDynamics_GDC2005.pdf
//
// --- Detailed Catto Provenance ---
//  CATTO-DERIVED SYSTEMS USED IN THIS FILE:
//    - Contact points with a normal and penetration depth:
//      Catto 2005, PDF p. 9, Section 4 "Contact Model"; normal constraint
//      Equations 16-19. Reason: turn continuous contact into a small finite set
//      of velocity constraints that can be solved iteratively.
//    - Point velocity at a contact:
//      Catto 2005, PDF p. 6, Section 3.4, Equations 9-11. Reason: v + omega x r
//      is the velocity of the material point touched by the constraint; using
//      center velocity alone misses rotational effects.
//    - Constraint force/impulse direction:
//      Catto 2005, PDF p. 5, Section 3.3, Equation 7 (Fc = J^T lambda).
//      Reason: each scalar multiplier becomes an impulse along the contact row's
//      Jacobian, which is why we apply both linear impulse and r x impulse torque.
//    - One-way contact bounds:
//      Catto 2005, PDF p. 8, Section 3.5, Equation 14. Reason: normal contacts
//      can push bodies apart but cannot pull them together.
//    - Baumgarte/contact bias:
//      Catto 2005, PDF p. 8, Section 3.6, Equation 15, and PDF p. 10,
//      Section 4.2, Equation 20. Reason: residual overlap decays gradually
//      instead of being corrected with an unstable one-frame snap.
//    - Two tangent friction constraints:
//      Catto 2005, PDF pp. 11-12, Section 4.3, Equations 21-25. Reason:
//      tangential slide is constrained separately from normal separation.
//    - World-space box inertia:
//      Catto 2005, PDF p. 12, Section 5, unnumbered inertia transform directly
//      before Equations 26-28: I_world^-1 = R * I_body^-1 * R^T. Reason:
//      oriented boxes do not have world-axis-aligned inverse inertia.
//    - Projected Gauss-Seidel iteration:
//      Catto 2005, PDF pp. 16-17, Section 7.2, Algorithm 4, solving the
//      time-stepped system from PDF p. 14, Section 6, Equations 34-35. Reason:
//      linear-time iterative convergence is a good game-physics tradeoff.
//
//  ENGINE-SPECIFIC / NOVEL SYSTEMS IN THIS FILE:
//    - Terrain manifold construction is custom to this engine: spheres use one
//      bottom-pole contact and boxes use OBB vertices against the terrain plane.
//      Catto supplies the constraint model, not this terrain manifold generator.
//    - High-speed terrain manifold collapse is a stability policy for this
//      engine. It intentionally uses a centroid contact on impacts to avoid
//      multi-point angular feedback from vertex manifolds.
//    - Terrain friction warm start is not Catto contact caching. It computes a
//      deterministic normal-load estimate from mass, gravity, slope normal, and
//      dt so terrain contacts have static-friction budget on the first iteration.
//    - Direct post-solve projection, rolling friction damping, sleep eligibility,
//      sleep velocity zeroing, and sphere visual pole alignment are engine policy,
//      documented inline where they appear.
//  Bullet Physics — btSequentialImpulseConstraintSolver
//  Box2D — b2ContactSolver
//
// =============================================================================


// --- Includes ---
#include "SkullbonezImpulseSolver.h"
#include "SkullbonezVector3.h"
#include "SkullbonezCollisionShape.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezObjectContactManifold.h"
#if SKULLBONEZ_INTRINSICS
#include <immintrin.h> // SSE/SSE2/SSE4.1 intrinsics for solver inner loop
#endif


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::CollisionDetection;


// =============================================================================
// SSE HELPERS — Inline intrinsics for the solver's hot inner loop.
//
// Vector3 is stored as {x, y, z} (12 bytes). We load into __m128 with the 4th
// lane zeroed. All operations below treat the 4th lane as don't-care (zeroed
// where it matters for correctness, e.g. dot product).
//
// Hardware target: SSE 4.1 (dpps instruction) — available on all x64 CPUs
// since Intel Penryn (2008) and AMD Bulldozer (2011).
// =============================================================================

#if SKULLBONEZ_INTRINSICS

// Load Vector3 {x,y,z} into __m128 {x,y,z,0}
static __forceinline __m128 sse_load3( const Vector3& v )
{
    // Load x,y as a 64-bit pair, then insert z. Avoids reading past struct.
    __m128 xy = _mm_castpd_ps( _mm_load_sd( reinterpret_cast<const double*>( &v.x ) ) );
    return _mm_insert_ps( xy, _mm_load_ss( &v.z ), 0x20 ); // insert z into lane 2
}

// Store __m128 {x,y,z,?} back to Vector3
static __forceinline void sse_store3( Vector3& out, __m128 v )
{
    _mm_store_sd( reinterpret_cast<double*>( &out.x ), _mm_castps_pd( v ) ); // x, y
    _mm_store_ss( &out.z, _mm_movehl_ps( v, v ) );                           // z
}

// Dot product of two 3-component vectors (uses dpps with mask 0x71: lanes 0,1,2 → lane 0)
static __forceinline float sse_dot3( __m128 a, __m128 b )
{
    return _mm_cvtss_f32( _mm_dp_ps( a, b, 0x71 ) );
}

// Cross product: a × b
//   result.x = a.y*b.z - a.z*b.y
//   result.y = a.z*b.x - a.x*b.z
//   result.z = a.x*b.y - a.y*b.x
static __forceinline __m128 sse_cross3( __m128 a, __m128 b )
{
    // Standard SSE cross product: a × b
    // Shuffle both vectors to yzx order, then:
    //   c = a * b_yzx - a_yzx * b  (in yzx layout)
    //   result = shuffle c back to xyz
    __m128 a_yzx = _mm_shuffle_ps( a, a, _MM_SHUFFLE( 3, 0, 2, 1 ) );
    __m128 b_yzx = _mm_shuffle_ps( b, b, _MM_SHUFFLE( 3, 0, 2, 1 ) );
    __m128 c = _mm_sub_ps( _mm_mul_ps( a, b_yzx ), _mm_mul_ps( a_yzx, b ) );
    return _mm_shuffle_ps( c, c, _MM_SHUFFLE( 3, 0, 2, 1 ) );
}

// Component-wise multiply: {a.x*b.x, a.y*b.y, a.z*b.z, 0}
static __forceinline __m128 sse_vmul3( __m128 a, __m128 b )
{
    return _mm_mul_ps( a, b );
}

// Matrix-vector multiply: R * v (3×3 rotation matrix, row-major in __m128 rows)
//   result = { dot(row0, v), dot(row1, v), dot(row2, v), 0 }
static __forceinline __m128 sse_matvec3( __m128 row0, __m128 row1, __m128 row2, __m128 v )
{
    __m128 d0 = _mm_dp_ps( row0, v, 0x71 ); // dot(row0, v) in lane 0
    __m128 d1 = _mm_dp_ps( row1, v, 0x72 ); // dot(row1, v) in lane 1
    __m128 d2 = _mm_dp_ps( row2, v, 0x74 ); // dot(row2, v) in lane 2
    return _mm_or_ps( _mm_or_ps( d0, d1 ), d2 );
}

#endif // SKULLBONEZ_INTRINSICS


// =============================================================================
// TERRAIN COLLISION RESPONSE — Unified Sequential Impulse Solver
// =============================================================================
bool ImpulseSolver::RespondCollisionTerrain( GameModel& gameModel, float changeInTime )
{
    PROFILE_SCOPED( "Frame/Physics/Terrain/Impulse" );

    // --- Common state ---
    // CATTO REF:
    //   Catto 2005, local PDF Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf,
    //   PDF p. 3, Section 3.1, Equations 1-3 define the velocity state used by
    //   the solver: linear velocity v, angular velocity omega, and stacked body
    //   velocity V. PDF p. 12, Section 5, Equations 26-28 define how mass and
    //   inertia convert forces/impulses into velocity changes.
    // REASON:
    //   The terrain solver works at velocity level. We copy the body's state to
    //   locals, solve the contact rows, then write back once, matching Catto's
    //   "solve constraints over V" structure instead of mutating the body during
    //   row setup.
    float invMass = gameModel.GetInvertedMass();
    Vector3 invInertia = gameModel.GetInvertedRotationalInertia();
    Vector3 inertia = gameModel.GetRotationalInertia();
    Vector3 velocity = gameModel.m_physicsInfo.GetVelocity();
    Vector3 omega = gameModel.m_physicsInfo.GetAngularVelocity();
    float e = gameModel.m_physicsInfo.GetCoefficientRestitution();
    float mu = gameModel.m_physicsInfo.GetFrictionCoefficient();
    Vector3 position = gameModel.m_physicsInfo.GetPosition();
    float mass = gameModel.GetMass();

    // --- World-space inverse inertia ---
    // CATTO REF:
    //   Catto 2005, PDF p. 12, Section 5, unnumbered inertia transform directly
    //   before Equations 26-28:
    //       I_world^-1 = R * I_body^-1 * R^T
    // REASON:
    //   Spheres are isotropic, so body-space inverse inertia is valid in world
    //   space. Oriented boxes are not; their inverse inertia tensor rotates with
    //   orientation. Without this transform, tilted boxes respond to contact
    //   torque as if their inertia axes were still world-aligned.
    // For spheres (isotropic inertia), body-space = world-space.
    // For boxes, we must transform: I_world_inv * v = R * (I_body_inv ∘ (R^T * v))
    // where R is the orientation matrix and ∘ is component-wise multiply.
    Transformation::RotationMatrix orientMat = gameModel.m_physicsInfo.GetOrientationMatrix();
    bool useWorldInertia = std::visit( []( const auto& s ) -> bool
                                       {
        using S = std::decay_t<decltype( s )>;
        return std::is_same_v<S, BoundingBox>; },
                                       gameModel.m_boundingVolume );

    // Lambda: compute I_world_inv * v.
    // CATTO REF:
    //   Same Section 5 inertia transform as above. This helper is the local
    //   implementation of M^-1 for angular rows in Catto's J*M^-1*J^T terms.
    // REASON:
    //   Keeping the helper local avoids repeating shape checks and makes every
    //   effective-mass and impulse application use the exact same inertia path.
    auto applyInvInertia = [&]( const Vector3& v ) -> Vector3
    {
        if ( !useWorldInertia )
        {
            return Vector::VectorMultiply( invInertia, v );
        }
        // Rotate v into body space, scale by body-space invI, rotate back
        Vector3 bodyV = orientMat.TransposeMultiply( v );
        Vector3 scaled = Vector::VectorMultiply( invInertia, bodyV );
        return orientMat * scaled;
    };

    // ENGINE NOTE:
    //   Catto starts with generated contact points; this engine first collides
    //   against cached terrain planes. Reusing the plane from detection keeps
    //   "did we collide?" and "which plane do we solve?" consistent.
    // Collision plane from the detection pass - guarantees consistency
    // between "did we collide?" and "where are the contacts?"
    Geometry::Plane colPlane = gameModel.m_responseInformation.collidedPlane;
    Vector3 planeNormal = colPlane.m_normal;

    // Per-contact data for the sequential impulse solver.
    // CATTO REF:
    //   r and normal come from the normal contact model in Catto 2005, PDF p. 9,
    //   Section 4.1, Equations 16-19. normalMass/tangentMass are the row
    //   denominators from PDF p. 16, Section 7.2, Algorithm 4, expanded from the
    //   time-stepped system on PDF p. 14, Section 6, Equations 34-35. bias is
    //   Catto's constraint bias from PDF p. 8, Section 3.6, Equation 15 and
    //   contact stabilization from PDF p. 10, Section 4.2, Equation 20. accN and
    //   accT* are Algorithm 4's accumulated lambda values.
    // REASON:
    //   Each Contact is one sparse constraint row: build it once, then visit it
    //   repeatedly during Projected Gauss-Seidel.
    // Fields map directly to the Catto iterative constraint formulation.
    struct Contact
    {
        Vector3 r;          // Vector from body CoM to contact point (world space); used in r×n torque terms
        Vector3 normal;     // Outward surface normal at contact (pointing away from the surface)
        float penetration;  // Signed overlap depth (positive = penetrating into surface)
        float normalMass;   // Effective (reduced) mass for the normal constraint:
                            //   K_n = 1/m + n · (I⁻¹(r×n) × r)   →   normalMass = 1/K_n
        float tangentMass1; // Effective mass for friction along tangent1 (same structure as normalMass)
        float tangentMass2; // Effective mass for friction along tangent2
        Vector3 tangent1;   // First tangent direction (perpendicular to normal, Gram-Schmidt orthonormalised)
        Vector3 tangent2;   // Second tangent direction = normal × tangent1  (completing the orthonormal frame)
        float bias;         // Per-contact velocity target:
                            //   Resting contact → Baumgarte bias = β*(pen-slop)/dt  (positional error correction)
                            //   Impacting contact → e*v_n/count  (restitution push)
        float accN;         // Accumulated normal impulse this frame; clamped to [0,∞] (push only, never pull).
                            //   Warm-started with expected gravity load so friction has a budget from iteration 0.
        float accT1;        // Accumulated friction impulse along tangent1; clamped to [−μ·accN, +μ·accN] (Coulomb cone)
        float accT2;        // Accumulated friction impulse along tangent2
    };

    Contact contacts[8]; // Up to 8 contacts (one per box vertex; sphere always has 1)
    int contactCount = 0;

    // --- Build contact manifold based on shape type ---
    // CATTO REF:
    //   Catto 2005, PDF p. 9, Section 4 "Contact Model" defines a contact as a
    //   point of overlap and a normal; Equation 16 measures separation along the
    //   normal.
    // ENGINE-SPECIFIC / NOVEL:
    //   This terrain manifold generator is ours. Spheres get one bottom-pole
    //   row. Boxes test all OBB vertices against the terrain plane and retain the
    //   deepest cluster, producing face, edge, or vertex support without a full
    //   clipping manifold.
    // Sphere: single contact at the bottom pole (centre - radius * normal).
    // Box: check all 8 vertices against the collision plane; include those within
    //      a small threshold of the deepest-penetrating vertex.  This gives face
    //      contacts (4 pts) for flat boxes and edge/vertex contacts for tilted ones.
    std::visit( [&]( const auto& shape )
                {
        using ShapeT = std::decay_t<decltype( shape )>;

        if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
        {
            // Sphere: single contact at the bottom pole
            float radius = shape.GetRadius();
            Vector3 contactWorldPos = position - planeNormal * radius;

            // Signed distance from contact point to plane (negative = penetrating)
            // Plane convention: dot(point, normal) = m_distance for points ON the plane
            float signedDist = ( contactWorldPos * planeNormal ) - colPlane.m_distance;
            float penetration = -signedDist;

            Contact& c = contacts[0];
            c.r = contactWorldPos - position;
            c.normal = planeNormal;
            c.penetration = penetration;
            c.accN = 0.0f;
            c.accT1 = 0.0f;
            c.accT2 = 0.0f;
            contactCount = 1;
        }
        else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
        {
            // Marker covers the box-only vertex manifold build. It lets the
            // profiler separate the cost of sampling/caching the eight contact
            // vertices from the shared impulse response below.
            PROFILE_SCOPED( "Frame/Physics/Terrain/BoxVertexManifold" );

            // Box: check all 8 vertices against the collision plane.
            // Use two-pass approach: first find the minimum signed distance,
            // then include all vertices within a relative threshold of the min.
            // This ensures at least 1 contact even on slopes where absolute
            // distances vary widely across vertices.
            const Vector3& he = shape.GetHalfExtents();
            Transformation::RotationMatrix rotMat = gameModel.m_physicsInfo.GetOrientationMatrix();

            Vector3 worldVerts[8];
            float signedDists[8];
            float minSignedDist = 1e10f;

            for ( int v = 0; v < 8; ++v )
            {
                Vector3 local(
                    ( v & 1 ) ? he.x : -he.x,
                    ( v & 2 ) ? he.y : -he.y,
                    ( v & 4 ) ? he.z : -he.z );
                worldVerts[v] = position + ( rotMat * local );
                signedDists[v] = ( worldVerts[v] * planeNormal ) - colPlane.m_distance;
                if ( signedDists[v] < minSignedDist )
                {
                    minSignedDist = signedDists[v];
                }
            }

            // Include all vertices within contactThreshold of the deepest penetration.
            // A small threshold gives edge (2 verts) or vertex (1) contacts for
            // tilted boxes, and face (4) contacts for flush boxes.
            constexpr float contactThreshold = 0.15f;
            float cutoff = minSignedDist + contactThreshold;

            for ( int v = 0; v < 8; ++v )
            {
                if ( signedDists[v] > cutoff )
                {
                    continue;
                }

                float penetration = -signedDists[v];
                Contact& c = contacts[contactCount];
                c.r = worldVerts[v] - position;
                c.normal = planeNormal;
                c.penetration = ( penetration > 0.0f ) ? penetration : 0.0f;
                c.accN = 0.0f;
                c.accT1 = 0.0f;
                c.accT2 = 0.0f;
                ++contactCount;
            }
        } },
                gameModel.m_boundingVolume );

    // ENGINE-SPECIFIC SAFETY:
    //   If detection requested a response but manifold construction produced no
    //   rows, Catto's PGS solve has no valid constraint to process. Cancel only
    //   incoming normal velocity and report that the contact is not sleep-safe.
    // Safety fallback: cancel normal velocity if no contacts found
    if ( contactCount == 0 )
    {
        float vn = velocity * planeNormal;
        if ( vn < 0.0f )
        {
            velocity -= planeNormal * vn;
        }
        gameModel.m_physicsInfo.SetLinearVelocity( velocity );
        return false;
    }

    // --- Collapse manifold to centroid for high-velocity impacts ---
    // ENGINE-SPECIFIC / NOVEL:
    //   Catto supports multi-row contact manifolds, but this terrain vertex
    //   manifold is intentionally approximate. On high-speed impacts, multiple
    //   vertex rows can create excessive angular feedback before the body has a
    //   stable support face. Collapsing only impact-time manifolds to one
    //   centroid row keeps impacts bounded while preserving full multi-point
    //   support for resting contacts.
    // With multiple contacts, per-contact impulses create angular feedback
    // that can amplify beyond physical bounds. Using a single centroid contact
    // for impacts avoids this; full manifold used for resting/low-velocity.
    float preVn = velocity * planeNormal;
    if ( preVn < -Cfg().contactRestitutionThreshold && contactCount > 1 )
    {
        Vector3 centroidR = Math::Vector::ZERO_VECTOR;
        float avgPen = 0.0f;
        for ( int i = 0; i < contactCount; ++i )
        {
            centroidR += contacts[i].r;
            avgPen += contacts[i].penetration;
        }
        float invN = 1.0f / static_cast<float>( contactCount );
        contacts[0].r = centroidR * invN;
        contacts[0].penetration = avgPen * invN;
        contactCount = 1;
    }

    // --- Box terrain support classification ---
    // ENGINE-SPECIFIC / NOVEL:
    //   This block decides whether the current box/terrain manifold is allowed
    //   to behave like a stable resting support. It intentionally runs before
    //   warm-starting, friction, and rolling damping because those policies can
    //   otherwise make an edge/point touch act like a broad support footprint.
    //
    //   The edge/point repro showed exactly that failure mode: the collision
    //   detector correctly found a near-terrain box vertex and the sleep gate
    //   correctly refused to sleep the body, but the solver still gave the edge
    //   manifold a full gravity warm-start and static-friction budget. That made
    //   the box quiet while balanced on only two terrain-near vertices.
    //
    //   Catto's constraint rows can resolve impacts and penetrations for any
    //   contact point count. The extra policy here is only about "resting support"
    //   privileges:
    //
    //     * gravity normal warm-start,
    //     * static-friction floor based on that warm-start,
    //     * rolling damping as a rest-state cleanup,
    //     * sleep support seeding.
    //
    //   Edge/point contacts still solve normal impulses. They just do not get the
    //   artificial support budget that can hold an unstable pose in place.
    bool isBox = std::visit( []( const auto& s ) -> bool
                             {
        using S = std::decay_t<decltype( s )>;
        return std::is_same_v<S, BoundingBox>; },
                             gameModel.m_boundingVolume );

    float bestFaceNormalDot = 1.0f;
    if ( isBox )
    {
        // A box resting on terrain should have one of its local face normals close
        // to the terrain plane normal. This test is cheap: the orientation matrix
        // columns/axes already exist, and orientation does not change inside this
        // terrain response call.
        Vector3 axisX = orientMat * Vector3( 1.0f, 0.0f, 0.0f );
        Vector3 axisY = orientMat * Vector3( 0.0f, 1.0f, 0.0f );
        Vector3 axisZ = orientMat * Vector3( 0.0f, 0.0f, 1.0f );

        bestFaceNormalDot = fabsf( axisX * planeNormal );
        float absDotY = fabsf( axisY * planeNormal );
        if ( absDotY > bestFaceNormalDot )
        {
            bestFaceNormalDot = absDotY;
        }
        float absDotZ = fabsf( axisZ * planeNormal );
        if ( absDotZ > bestFaceNormalDot )
        {
            bestFaceNormalDot = absDotZ;
        }
    }

    bool contactSupportsSleep = true;
    if ( isBox )
    {
        // A one- or two-row terrain manifold can be a genuine transition contact,
        // but it is not automatically a stable rest footprint. First require face
        // alignment so a tilted edge does not receive support just because it is
        // close to a terrain plane.
        if ( contactCount > 0 && contactCount < 4 )
        {
            constexpr float stableFaceNormalDot = 0.95f; // ~18 degrees from the contact plane normal.
            contactSupportsSleep = bestFaceNormalDot >= stableFaceNormalDot;
        }

        if ( contactSupportsSleep )
        {
            // Face-normal alignment alone is not enough on a heightfield. A box
            // can be oriented with a plausible face normal while only one edge is
            // actually close to the real terrain samples. Count the real OBB
            // vertices against their own terrain heights before granting stable
            // support policy.
            PROFILE_SCOPED( "Frame/Physics/Terrain/BoxSupportPolicyVerts" );

            int terrainSupportedVertices = 0;
            std::visit( [&]( const auto& shape )
                        {
                using ShapeT = std::decay_t<decltype( shape )>;
                if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
                {
                    const Vector3& he = shape.GetHalfExtents();
                    constexpr float vertexSupportSlack = 0.15f;
                    float supportGap = Cfg().contactEpsilon + vertexSupportSlack;

                    for ( int v = 0; v < 8; ++v )
                    {
                        Vector3 local(
                            ( v & 1 ) ? he.x : -he.x,
                            ( v & 2 ) ? he.y : -he.y,
                            ( v & 4 ) ? he.z : -he.z );
                        Vector3 worldVertex = position + ( orientMat * local );
                        if ( !gameModel.m_terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
                        {
                            continue;
                        }

                        float terrainHeight = 0.0f;
                        Plane terrainPlane;
                        gameModel.m_terrain->GetTerrainHeightAndPlaneAt( worldVertex.x,
                                                                         worldVertex.z,
                                                                         terrainHeight,
                                                                         terrainPlane );
                        if ( worldVertex.y - terrainHeight <= supportGap )
                        {
                            ++terrainSupportedVertices;
                        }
                    }
                } },
                        gameModel.m_boundingVolume );

            // Three or more supported vertices means the box has a real terrain
            // footprint. A two-vertex heightfield footprint is only accepted when
            // the solver manifold itself has at least three rows and a face is
            // very close to the terrain plane. That keeps uneven-but-flat terrain
            // rests alive while rejecting the edge/point support edge case.
            constexpr float stablePlanePatchDot = 0.99f; // ~8 degrees from the terrain plane normal.
            bool hasHeightfieldFootprint = terrainSupportedVertices >= 3;
            bool hasStablePlanePatch = terrainSupportedVertices >= 2 &&
                                       contactCount >= 3 &&
                                       bestFaceNormalDot >= stablePlanePatchDot;
            contactSupportsSleep = hasHeightfieldFootprint || hasStablePlanePatch;
        }
    }

    // Non-box terrain contacts keep the existing behavior. For boxes, this is
    // the single switch that decides whether rest-support-only solver policy is
    // enabled. Normal collision impulses still run either way.
    const bool useRestingSupportPolicy = !isBox || contactSupportsSleep;

    // --- Gravity-based friction floor ---
    // CATTO REF:
    //   Catto 2005, PDF pp. 11-12, Section 4.3, Equations 24-25 bound tangent
    //   friction by +/-mu*m_c*g rather than coupling it directly to the solved
    //   normal force.
    // ENGINE-SPECIFIC / NOVEL:
    //   Terrain contacts in this function do not have a temporal lambda cache.
    //   A fresh resting contact can therefore start with accN near zero and have
    //   no friction budget. We estimate the normal support impulse from
    //   mass*|gravity|*|planeNormal.y|*dt/contactCount so static friction is
    //   available on the first iteration.
    // For resting contacts sliding tangentially, the solver's accN can be
    // near-zero. But static friction still acts proportional to the normal
    // reaction force (mg·cos(θ)). We compute the per-contact share of the
    // expected gravity load and use it as a minimum friction budget.
    // Only stable resting support gets a gravity warm-start. When a box is on
    // an edge/point manifold, giving every contact row an expected gravity load
    // manufactures support that the real footprint does not have. Leaving this
    // at zero lets any friction budget come only from actual normal impulses
    // generated by impact/penetration rows.
    float gravityNormalImpulse = useRestingSupportPolicy
                                     ? mass * fabsf( Cfg().gravity ) * fabsf( planeNormal.y ) * changeInTime
                                     : 0.0f;
    float warmStartPerContact = gravityNormalImpulse /
                                static_cast<float>( contactCount );

    // --- Pre-compute per-contact effective mass and bias ---
    // CATTO REF:
    //   This is the setup for Catto 2005, PDF p. 16, Section 7.2, Algorithm 4.
    //   Algorithm 4 divides by d_i = J_i*B_i. With B = h*M^-1*J^T from PDF p. 14,
    //   Section 6, Equations 34-35, the scalar point-contact expansion is:
    //       K = invMass + axis dot ((I^-1 * (r cross axis)) cross r)
    //   We store 1/K as normalMass/tangentMass.
    // REASON:
    //   Effective mass turns velocity error into the impulse magnitude needed to
    //   correct that error for this body's mass and rotational inertia.
    // ENGINE-SPECIFIC:
    //   The Baumgarte threshold and max bias cap are local stability policy:
    //   they keep deep terrain vertex penetration from becoming a launch impulse.
    // Baumgarte stabilization is used ONLY for resting contacts (low v_n) with
    // a hard cap on maximum bias to prevent launching objects on impacts.
    constexpr float penetrationSlop = 0.005f;
    constexpr float baumgarteBeta = 0.3f;
    constexpr float maxBaumgarteBias = 2.0f;
    float invDt = ( changeInTime > TOLERANCE ) ? ( 1.0f / changeInTime ) : 120.0f;

    for ( int i = 0; i < contactCount; ++i )
    {
        Contact& c = contacts[i];

        // Build orthonormal tangent frame for friction (Gram-Schmidt).
        // CATTO REF:
        //   Catto 2005, PDF pp. 11-12, Section 4.3, Equations 21-23 define two
        //   tangent directions u1/u2 perpendicular to the contact normal.
        // REASON:
        //   3D friction needs two independent tangent constraints. We build a
        //   stable local basis from the normal so sliding in any direction can be
        //   decomposed into two scalar rows for the PGS solver.
        // We need two directions perpendicular to the contact normal to represent
        // the 2D friction constraint (opposing tangential sliding in any direction).
        //
        // Algorithm:
        //   1. Pick a "seed" vector not parallel to n (world-X, or world-Z if n is near X).
        //   2. Gram-Schmidt: t1 = normalize(seed - (seed·n)*n)  — subtracts the n component.
        //   3. t2 = n × t1  — guaranteed perpendicular to both n and t1.
        //
        // t1 and t2 together span the plane perpendicular to n at the contact point.
        if ( fabsf( c.normal.x ) > 0.9f )
        {
            c.tangent1 = Vector3( 0.0f, 0.0f, 1.0f );
        }
        else
        {
            c.tangent1 = Vector3( 1.0f, 0.0f, 0.0f );
        }
        float d = c.tangent1 * c.normal;
        c.tangent1 = c.tangent1 - c.normal * d;
        float t1Mag = VectorMag( c.tangent1 );
        if ( t1Mag > TOLERANCE )
        {
            c.tangent1 = c.tangent1 / t1Mag;
        }
        c.tangent2 = Vector::CrossProduct( c.normal, c.tangent1 );

        // Normal effective mass: K = 1/m + n . ((I^-1 * (r x n)) x r)
        // CATTO REF:
        //   Catto 2005, PDF p. 9, Section 4.1, Equations 17-19 give the normal
        //   velocity Jacobian. PDF p. 14, Section 6, Equations 34-35 and PDF
        //   p. 16, Algorithm 4 use J*M^-1*J^T as the denominator.
        // REASON:
        //   The dot/cross expression below is the scalar expansion of that
        //   denominator for one body against static ground.
        Vector3 rCrossN = Vector::CrossProduct( c.r, c.normal );
        Vector3 iRCrossN = applyInvInertia( rCrossN );
        float kN = invMass + ( c.normal * Vector::CrossProduct( iRCrossN, c.r ) );
        c.normalMass = ( kN > TOLERANCE ) ? ( 1.0f / kN ) : 0.0f;

        // Tangent effective masses.
        // CATTO REF:
        //   Catto 2005, PDF pp. 11-12, Section 4.3, Equations 21-23 are the two
        //   tangent velocity rows. Their effective masses use the same
        //   J*M^-1*J^T expansion as the normal row, with tangent axes replacing n.
        // REASON:
        //   Tangential velocity correction must respect rotational inertia too;
        //   otherwise friction would stop center-of-mass sliding but miss spin.
        Vector3 rCrossT1 = Vector::CrossProduct( c.r, c.tangent1 );
        Vector3 iRCrossT1 = applyInvInertia( rCrossT1 );
        float kT1 = invMass + ( c.tangent1 * Vector::CrossProduct( iRCrossT1, c.r ) );
        c.tangentMass1 = ( kT1 > TOLERANCE ) ? ( 1.0f / kT1 ) : 0.0f;

        Vector3 rCrossT2 = Vector::CrossProduct( c.r, c.tangent2 );
        Vector3 iRCrossT2 = applyInvInertia( rCrossT2 );
        float kT2 = invMass + ( c.tangent2 * Vector::CrossProduct( iRCrossT2, c.r ) );
        c.tangentMass2 = ( kT2 > TOLERANCE ) ? ( 1.0f / kT2 ) : 0.0f;

        // Bias: Baumgarte only for resting contacts (|v_n| below restitution threshold)
        // CATTO REF:
        //   Catto 2005, PDF p. 8, Section 3.6, Equation 15 adds a bias vector to
        //   the velocity constraint; PDF p. 10, Section 4.2, Equation 20 applies
        //   beta*C_n to contact stabilization.
        // REASON:
        //   Bias turns penetration depth into a target separating velocity. The
        //   restitution branch below is impact response; the Baumgarte branch is
        //   resting-contact drift correction.
        // with a hard cap to prevent launching on slopes where vertex pen can be large.
        Vector3 vAtContact = velocity + Vector::CrossProduct( omega, c.r );
        float vnContact = vAtContact * c.normal;

        // If this is an unstable box edge/point support, do not let a
        // non-penetrating contact-skin row become a resting normal constraint.
        //
        // Why this is separate from the warm-start/friction gate:
        //   Removing the gravity warm-start stops us from manufacturing support
        //   up front, but a slow object under gravity can still accumulate real
        //   normal impulses frame after frame while the contact point is merely
        //   close to the terrain plane. For face contacts that is exactly what
        //   we want. For one- or two-vertex box contacts it creates a numerical
        //   edge balance: the box is not sleep-safe, but the normal row keeps
        //   catching it gently before it can fall/topple.
        //
        // Policy:
        //   * Stable support: solve normal/friction normally.
        //   * Unstable support with real penetration: solve normally so the box
        //     is pushed out of terrain.
        //   * Unstable support with a real impact: solve normally so impacts
        //     still bounce/deflect.
        //   * Unstable support that is only inside the contact skin and moving
        //     slowly: disable this row for this frame. Gravity will carry the
        //     box into a real impact/penetration on a later tick instead of
        //     letting the solver balance it on an edge forever.
        if ( !useRestingSupportPolicy &&
             c.penetration <= penetrationSlop &&
             vnContact > -Cfg().contactRestitutionThreshold )
        {
            c.normalMass = 0.0f;
            c.tangentMass1 = 0.0f;
            c.tangentMass2 = 0.0f;
            c.bias = 0.0f;
            continue;
        }

        // Baumgarte stabilisation for resting contacts (|v_n| < restitution threshold):
        //
        //   bias = β × max(penetration - slop, 0) / dt
        //
        //   β = 0.3  — fraction of penetration error corrected per step (tunable)
        //   slop     — small tolerance band; penetrations smaller than this are ignored
        //              to prevent jitter when objects rest flush against a surface
        //   dt       — frame time (inverted: multiplying by 1/dt converts metres → m/s)
        //
        // Effect: the normal constraint targets a velocity of +bias (pushing the contact
        // point out of the surface) rather than zero.  Each frame the solver adds a small
        // corrective velocity that closes the residual overlap over ≈ 3–10 frames.
        //
        // NOT applied to impacts (|v_n| > threshold) — impacts use the restitution bias
        // instead, which encodes how fast to push back based on approach speed × e.
        c.bias = 0.0f;
        if ( fabsf( vnContact ) < Cfg().contactRestitutionThreshold )
        {
            // Resting contact: gentle Baumgarte to maintain surface contact
            float corrPen = c.penetration - penetrationSlop;
            if ( corrPen < 0.0f )
            {
                corrPen = 0.0f;
            }
            float baumBias = baumgarteBeta * corrPen * invDt;
            if ( baumBias > maxBaumgarteBias )
            {
                baumBias = maxBaumgarteBias;
            }
            c.bias = baumBias;
        }
        else if ( vnContact < -Cfg().contactRestitutionThreshold )
        {
            // Impact: restitution only, no Baumgarte (position correction handles pen)
            c.bias = ( -e * vnContact ) / static_cast<float>( contactCount );
        }
    }

    // --- Sequential impulse solver loop ---
    // CATTO REF:
    //   Catto 2005, PDF pp. 16-17, Section 7.2, Algorithm 4:
    //       delta_lambda = (eta_i - J_i*a) / d_i
    //       lambda_i = clamp(lambda_i + delta_lambda, lower_i, upper_i)
    //       delta_lambda = lambda_i - old_lambda_i
    //       a += delta_lambda * B_i
    //   Our velocity variables are the explicit "a" state. Each applied impulse
    //   updates velocity immediately, so later rows see the latest solution.
    // REASON:
    //   This is Projected Gauss-Seidel. It is approximate but cheap, stable for
    //   game scenes, and matches Catto's linear-time/linear-storage goal.
    //
    // Each iteration visits every contact and applies a small corrective impulse:
    //   λ = effective_mass × (-v_n + bias)
    //
    // The accumulated impulse (acc*) is the key to correctness:
    //   Instead of clamping λ directly, we clamp the ACCUMULATED value: acc += λ, then clamp acc.
    //   The actual applied impulse is (new_acc - old_acc).  This monotone clamping ensures that
    //   even if a single iteration overshoots, the accumulated total stays valid and subsequent
    //   iterations can correct back.  Without accumulation, each iteration would independently
    //   overshoot/undershoot and the solver would not converge.
    //
    // More iterations → closer to the exact constraint solution, but more CPU.
    // 20 iterations is sufficient for the object counts in this engine.
    //
    // Warm-starting: accN is pre-seeded with the expected gravitational load per contact
    // (mg·cos(θ) / contact_count).  This ensures the friction budget (μ·accN) is non-zero
    // from iteration 0, preventing resting objects from sliding a frame before friction kicks in.
    // As in Catto's Algorithm 4, any non-zero initial λ must be applied to the velocity state
    // before the iterations begin; otherwise later negative deltas would "undo" an impulse
    // that was never applied and inject jitter into separating/resting contacts.
    // Plain English: if we remember that gravity should already be pressing this
    // object into the floor, we must actually give the object that upward support
    // push before solving. Remembering the number alone is like balancing the
    // books without moving the money.
    constexpr int solverIterations = 20;

    for ( int i = 0; i < contactCount; ++i )
    {
        Contact& c = contacts[i];
        c.accN = warmStartPerContact;

        if ( warmStartPerContact > 0.0f )
        {
            Vector3 warmImpulse = c.normal * warmStartPerContact;
            velocity += warmImpulse * invMass;
            omega += applyInvInertia( Vector::CrossProduct( c.r, warmImpulse ) );
        }
    }

    // ENGINE-SPECIFIC / NOVEL:
    //   Catto's Algorithm 4 uses a fixed iteration limit. The early-out below is
    //   an optimization: when all lambda deltas are tiny, remaining iterations
    //   are unlikely to change visible motion. The threshold is deterministic and
    //   based on impulse squared, so it avoids sqrt in the hot loop.
    // Adaptive early-out: if the total impulse applied in an iteration is below
    // this threshold, the system is converged and remaining iterations are skipped.
    // Threshold is expressed as impulse-squared to avoid sqrt.
    constexpr float CONVERGENCE_THRESHOLD_SQ = 1.0e-6f;

#if SKULLBONEZ_INTRINSICS
    // ENGINE-SPECIFIC / NOVEL:
    //   This SSE path is an implementation optimization, not part of Catto's
    //   paper. It computes the same scalar PGS equations as the Debug path below
    //   but keeps Vector3 math in registers in Profile/Release builds.
    // Release/Profile: SSE-accelerated inner loop using preloaded __m128 registers.
    // Pre-load orientation matrix rows for applyInvInertia (boxes only).
    // For spheres, useWorldInertia is false and we use component-wise multiply.
    // Row-major: row0 = {m11,m12,m13,0}, row1 = {m21,m22,m23,0}, row2 = {m31,m32,m33,0}
    // Transpose rows (columns of R): col0={m11,m21,m31,0} etc. for R^T * v
    __m128 matRow0, matRow1, matRow2;
    __m128 matCol0, matCol1, matCol2;
    if ( useWorldInertia )
    {
        orientMat.LoadSSE( matRow0, matRow1, matRow2, matCol0, matCol1, matCol2 );
    }
    else
    {
        matRow0 = matRow1 = matRow2 = _mm_setzero_ps();
        matCol0 = matCol1 = matCol2 = _mm_setzero_ps();
    }

    __m128 sseInvInertia = sse_load3( invInertia );
    __m128 sseVelocity = sse_load3( velocity );
    __m128 sseOmega = sse_load3( omega );
    __m128 sseInvMass = _mm_set1_ps( invMass );

    // Lambda: SSE-accelerated I_world_inv * v
    // For spheres: component-wise multiply (invInertia is diagonal = {1/Ix, 1/Iy, 1/Iz})
    // For boxes: R * (invI ∘ (R^T * v)) using preloaded matrix rows/cols
    auto sseApplyInvInertia = [&]( __m128 v ) -> __m128
    {
        if ( !useWorldInertia )
        {
            return sse_vmul3( sseInvInertia, v );
        }
        // R^T * v = { dot(col0, v), dot(col1, v), dot(col2, v) }
        __m128 bodyV = sse_matvec3( matCol0, matCol1, matCol2, v );
        // Component-wise scale by body-space inverse inertia
        __m128 scaled = sse_vmul3( sseInvInertia, bodyV );
        // R * scaled
        return sse_matvec3( matRow0, matRow1, matRow2, scaled );
    };

    for ( int iter = 0; iter < solverIterations; ++iter )
    {
        float iterImpulseSq = 0.0f;

        for ( int i = 0; i < contactCount; ++i )
        {
            Contact& c = contacts[i];

            __m128 sseR = sse_load3( c.r );
            __m128 sseNormal = sse_load3( c.normal );

            // Velocity at contact point: v + omega × r
            __m128 vAtContact = _mm_add_ps( sseVelocity, sse_cross3( sseOmega, sseR ) );

            // --- Normal constraint (non-penetration) ---
            // CATTO REF:
            //   Catto 2005, local PDF p. 9, Section 4.1, Equations 16-19 and
            //   PDF p. 8, Section 3.5, Equation 14. We solve the normal row and
            //   clamp the accumulated multiplier to [0, infinity), so terrain can
            //   push but never pull.
            float vn = sse_dot3( vAtContact, sseNormal );
            float lambdaN = c.normalMass * ( -vn + c.bias );

            float oldAccN = c.accN;
            c.accN = oldAccN + lambdaN;
            if ( c.accN < 0.0f )
            {
                c.accN = 0.0f;
            }
            float deltaN = c.accN - oldAccN;

            __m128 impulseN = _mm_mul_ps( sseNormal, _mm_set1_ps( deltaN ) );
            sseVelocity = _mm_add_ps( sseVelocity, _mm_mul_ps( impulseN, sseInvMass ) );
            sseOmega = _mm_add_ps( sseOmega, sseApplyInvInertia( sse_cross3( sseR, impulseN ) ) );

            // --- Friction constraints (Catto-style tangent bounds) ---
            // CATTO REF:
            //   Catto 2005, PDF pp. 11-12, Section 4.3, Equations 21-25.
            //   Tangential slide is solved as two bounded scalar constraints.
            // ENGINE NOTE:
            //   Terrain keeps Catto's independent tangent clamps here. The
            //   persistent object solver below uses a stronger 2D cone clamp and
            //   documents that as an engine improvement.
            __m128 sseTangent1 = sse_load3( c.tangent1 );
            vAtContact = _mm_add_ps( sseVelocity, sse_cross3( sseOmega, sseR ) );

            float vt1 = sse_dot3( vAtContact, sseTangent1 );
            float lambdaT1 = c.tangentMass1 * ( -vt1 );

            // The warm-start is a friction floor only for stable resting support.
            // For unstable box edge/point contacts warmStartPerContact is zero,
            // so friction can still happen, but only after the normal solve has
            // produced a real contact impulse.
            float frictionBudget = ( c.accN > warmStartPerContact ) ? c.accN : warmStartPerContact;
            float maxFriction = mu * frictionBudget;
            float oldAccT1 = c.accT1;
            c.accT1 = oldAccT1 + lambdaT1;
            if ( c.accT1 > maxFriction )
            {
                c.accT1 = maxFriction;
            }
            if ( c.accT1 < -maxFriction )
            {
                c.accT1 = -maxFriction;
            }
            float deltaT1 = c.accT1 - oldAccT1;

            __m128 impulseT1 = _mm_mul_ps( sseTangent1, _mm_set1_ps( deltaT1 ) );
            sseVelocity = _mm_add_ps( sseVelocity, _mm_mul_ps( impulseT1, sseInvMass ) );
            sseOmega = _mm_add_ps( sseOmega, sseApplyInvInertia( sse_cross3( sseR, impulseT1 ) ) );

            // Tangent axis 2: same Catto Section 4.3 friction row as tangent1,
            // using the second perpendicular basis vector.
            __m128 sseTangent2 = sse_load3( c.tangent2 );
            vAtContact = _mm_add_ps( sseVelocity, sse_cross3( sseOmega, sseR ) );
            float vt2 = sse_dot3( vAtContact, sseTangent2 );
            float lambdaT2 = c.tangentMass2 * ( -vt2 );

            float oldAccT2 = c.accT2;
            c.accT2 = oldAccT2 + lambdaT2;
            if ( c.accT2 > maxFriction )
            {
                c.accT2 = maxFriction;
            }
            if ( c.accT2 < -maxFriction )
            {
                c.accT2 = -maxFriction;
            }
            float deltaT2 = c.accT2 - oldAccT2;

            __m128 impulseT2 = _mm_mul_ps( sseTangent2, _mm_set1_ps( deltaT2 ) );
            sseVelocity = _mm_add_ps( sseVelocity, _mm_mul_ps( impulseT2, sseInvMass ) );
            sseOmega = _mm_add_ps( sseOmega, sseApplyInvInertia( sse_cross3( sseR, impulseT2 ) ) );

            // Track convergence: sum of squared deltas
            iterImpulseSq += deltaN * deltaN + deltaT1 * deltaT1 + deltaT2 * deltaT2;
        }

        // Early-out when converged
        if ( iterImpulseSq < CONVERGENCE_THRESHOLD_SQ )
        {
            break;
        }
    }

    // Store SSE results back to scalar Vector3
    sse_store3( velocity, sseVelocity );
    sse_store3( omega, sseOmega );

#else  // SKULLBONEZ_INTRINSICS
    // Debug: scalar arithmetic; each intermediate value is individually inspectable.
    // CATTO REF:
    //   This is the same Projected Gauss-Seidel solve described above for the
    //   SSE path: Catto 2005, PDF pp. 16-17, Section 7.2, Algorithm 4.
    // Reuses the scalar applyInvInertia lambda defined above for effective-mass pre-compute.
    for ( int iter = 0; iter < solverIterations; ++iter )
    {
        float iterImpulseSq = 0.0f;

        for ( int i = 0; i < contactCount; ++i )
        {
            Contact& c = contacts[i];

            // Velocity at contact point: v + omega × r
            Vector3 vAtContact = velocity + Vector::CrossProduct( omega, c.r );

            // --- Normal constraint (non-penetration) ---
            // CATTO REF:
            //   PDF p. 9, Section 4.1, Equations 16-19; PDF p. 8, Section 3.5,
            //   Equation 14 for the one-way lambda lower bound.
            float vn = vAtContact * c.normal;
            float lambdaN = c.normalMass * ( -vn + c.bias );
            float oldAccN = c.accN;
            c.accN = oldAccN + lambdaN;
            if ( c.accN < 0.0f )
            {
                c.accN = 0.0f;
            }
            float deltaN = c.accN - oldAccN;
            Vector3 impulseN = c.normal * deltaN;
            velocity += impulseN * invMass;
            omega += applyInvInertia( Vector::CrossProduct( c.r, impulseN ) );

            // --- Friction tangent 1 ---
            // CATTO REF:
            //   PDF pp. 11-12, Section 4.3, Equations 21, 23, and 24.
            vAtContact = velocity + Vector::CrossProduct( omega, c.r );
            float vt1 = vAtContact * c.tangent1;
            float lambdaT1 = c.tangentMass1 * ( -vt1 );
            // Same policy as the SSE path: edge/point box contacts are not
            // granted a static-friction floor. They may use only the normal
            // impulse actually accumulated by this contact row.
            float frictionBudget = ( c.accN > warmStartPerContact ) ? c.accN : warmStartPerContact;
            float maxFriction = mu * frictionBudget;
            float oldAccT1 = c.accT1;
            c.accT1 = oldAccT1 + lambdaT1;
            if ( c.accT1 > maxFriction )
            {
                c.accT1 = maxFriction;
            }
            if ( c.accT1 < -maxFriction )
            {
                c.accT1 = -maxFriction;
            }
            float deltaT1 = c.accT1 - oldAccT1;
            Vector3 impulseT1 = c.tangent1 * deltaT1;
            velocity += impulseT1 * invMass;
            omega += applyInvInertia( Vector::CrossProduct( c.r, impulseT1 ) );

            // --- Friction tangent 2 ---
            // CATTO REF:
            //   PDF pp. 11-12, Section 4.3, Equations 22, 23, and 25.
            vAtContact = velocity + Vector::CrossProduct( omega, c.r );
            float vt2 = vAtContact * c.tangent2;
            float lambdaT2 = c.tangentMass2 * ( -vt2 );
            float oldAccT2 = c.accT2;
            c.accT2 = oldAccT2 + lambdaT2;
            if ( c.accT2 > maxFriction )
            {
                c.accT2 = maxFriction;
            }
            if ( c.accT2 < -maxFriction )
            {
                c.accT2 = -maxFriction;
            }
            float deltaT2 = c.accT2 - oldAccT2;
            Vector3 impulseT2 = c.tangent2 * deltaT2;
            velocity += impulseT2 * invMass;
            omega += applyInvInertia( Vector::CrossProduct( c.r, impulseT2 ) );

            // Track convergence: sum of squared deltas
            iterImpulseSq += deltaN * deltaN + deltaT1 * deltaT1 + deltaT2 * deltaT2;
        }

        // Early-out when converged
        if ( iterImpulseSq < CONVERGENCE_THRESHOLD_SQ )
        {
            break;
        }
    }
#endif // SKULLBONEZ_INTRINSICS

    // --- Position correction (direct projection) ---
    // ENGINE-SPECIFIC / NOVEL:
    //   Catto's contact bias handles overlap through velocity-level correction.
    //   This extra partial projection is local terrain policy: it removes tiny
    //   remaining visible penetration after the velocity solve without snapping
    //   the full depth in one frame.
    // The iterative velocity solve removes most sinking, but tiny overlaps can
    // remain. Push the object out by a small fraction of the remaining overlap so
    // it settles without the harsh snap upward that causes visible bouncing.
    float maxPen = 0.0f;
    for ( int i = 0; i < contactCount; ++i )
    {
        float corrPen = contacts[i].penetration - penetrationSlop;
        if ( corrPen > maxPen )
        {
            maxPen = corrPen;
        }
    }
    if ( maxPen > 0.0f )
    {
        position += planeNormal * ( maxPen * 0.4f );
    }

    // --- Sleep support classification ---
    // ENGINE-SPECIFIC / NOVEL:
    //   This is not Catto's body sleeping. It is a Skullbonez policy gate that
    //   prevents a box from sleeping when the terrain manifold is an edge/vertex
    //   support that is not face-stable against the actual terrain plane normal.
    //   This keeps the solver running until the contact becomes a plausible rest
    //   state on the slope, without injecting artificial toppling torque.
    // contactSupportsSleep was classified before solving so the same cheap
    // support decision controls both rest-support impulse policy and sleep
    // eligibility. This keeps the behavior consistent: a contact that is not a
    // believable rest footprint cannot receive warm-start support and also
    // cannot seed sleep.

    // --- Rolling friction ---
    // ENGINE-SPECIFIC / NOVEL:
    //   Catto's paper does not add rolling resistance. This is local damping
    //   policy that opposes angular velocity using a small normal-load-scaled
    //   torque, letting objects come to visual rest after contact constraints
    //   have resolved their main motion.
    // Small torque opposing angular velocity to bring objects to rest.
    float normalForce = mass * fabsf( Cfg().gravity ) * fabsf( planeNormal.y );
    float omegaMagSq = omega * omega;
    if ( useRestingSupportPolicy && omegaMagSq > TOLERANCE * TOLERANCE )
    {
        float omegaMag = sqrtf( omegaMagSq );

        float rEff = std::visit( []( const auto& s ) -> float
                                 {
            using S = std::decay_t<decltype( s )>;
            if constexpr ( std::is_same_v<S, BoundingSphere> )
            {
                return s.GetRadius();
            }
            else
            {
                const Vector3& he = s.GetHalfExtents();
                return ( he.x + he.y + he.z ) / 3.0f;
            } },
                                 gameModel.m_boundingVolume );

        constexpr float muRolling = 0.02f;
        float rollingTorqueMag = muRolling * normalForce * rEff;

        float avgInertia = ( inertia.x + inertia.y + inertia.z ) / 3.0f;
        if ( avgInertia < TOLERANCE )
        {
            avgInertia = 1.0f;
        }
        float deltaOmega = ( rollingTorqueMag / avgInertia ) * changeInTime;

        if ( deltaOmega >= omegaMag )
        {
            omega = Math::Vector::ZERO_VECTOR;
        }
        else
        {
            Vector3 omegaDir = omega / omegaMag;
            omega -= omegaDir * deltaOmega;
        }
    }

    // --- Sleep detection ---
    // ENGINE-SPECIFIC / NOVEL:
    //   Catto's box-stacking result explicitly notes no sleeping was used
    //   (PDF p. 21, Section 9.1). Skullbonez does use sleeping for runtime
    //   stability/perf, but gates it through contactSupportsSleep above so the
    //   sleep plane normal is respected.
    // Angular threshold must be very low so slow contact motion can keep
    // resolving before a body is eligible for sleep.
    float finalSpeedSq = velocity * velocity;
    omegaMagSq = omega * omega;
    constexpr float sleepLinear = 0.05f;
    constexpr float sleepAngular = 0.02f;
    if ( contactSupportsSleep &&
         finalSpeedSq < sleepLinear * sleepLinear &&
         omegaMagSq < sleepAngular * sleepAngular )
    {
        velocity = Math::Vector::ZERO_VECTOR;
        omega = Math::Vector::ZERO_VECTOR;
    }

    // --- Write back ---
    gameModel.m_physicsInfo.SetPosition( position );
    gameModel.m_physicsInfo.SetLinearVelocity( velocity );
    gameModel.m_physicsInfo.SetAngularVelocity( omega );


    return contactSupportsSleep;
}


// =============================================================================
// SPHERE-SPHERE / MIXED GAME MODEL COLLISION RESPONSE
// =============================================================================
//
// CATTO-DERIVED PARTS:
//   - Point velocity v + omega x r:
//     Catto 2005, local PDF p. 6, Section 3.4, Equations 9-11. Reason:
//     object-object impulses must account for rotation at the contact point.
//   - Normal Jacobian/effective mass:
//     PDF p. 9, Section 4.1, Equations 16-19 plus PDF p. 14, Section 6,
//     Equations 34-35. Reason: convert normal velocity error to an impulse
//     using mass and rotational inertia.
//   - Tangent friction:
//     PDF pp. 11-12, Section 4.3, Equations 21-25. Reason: transfer tangential
//     sliding into bounded friction impulse and spin.
//   - World-space inertia:
//     PDF p. 12, Section 5, unnumbered inertia transform before Equations 26-28.
//
// ENGINE-SPECIFIC / NOVEL PARTS:
//   - Object-object narrowphase still uses bounding radii, so contact arms are
//     approximated along the center-to-center normal instead of a true convex
//     manifold. This is an intentional bridge until proper shape-pair manifolds
//     exist.
//   - This path is one-shot impact response, not the persistent Catto cache. Slow
//     resting support is handled in GameModelCollection::SolvePersistentObjectContacts.
//   - Positional correction is mass-weighted push-apart policy for visible
//     overlap cleanup.
//
// =============================================================================
void ImpulseSolver::RespondCollisionGameModels( GameModel& gameModel1,
                                                GameModel& gameModel2 )
{
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/Impulse" );
    if ( gameModel1.IsBox() || gameModel2.IsBox() )
    {
        // ENGINE-SPECIFIC / NOVEL:
        //   Box-involving contacts are delegated to the persistent manifold PGS
        //   pass in GameModelCollection. Catto's iterative solver is the better
        //   fit for box stacks because it can solve several contact rows and
        //   warm start them over time. The one-shot path below is intentionally
        //   kept to sphere/sphere impacts; using a single immediate impulse for
        //   boxes reintroduced stack spin and late creep.
        return;
    }

    std::visit( [&]( const auto&, const auto& )
                {
            // =================================================================
            // OBJECT-OBJECT ANGULAR IMPULSE (Friction-Based Spin Transfer)
            // =================================================================
            //
            // ENGINE-SPECIFIC / NOVEL:
            // This immediate impact path now only runs for sphere/sphere pairs.
            // It still uses BuildObjectContactManifold so the contact point,
            // normal, and r arms are the same row geometry consumed by the
            // persistent Catto-style solver. Box contacts return above because
            // stable stacking needs multi-row temporal coherence, not a single
            // impact impulse.
            //
            // Pipeline:
            //   1. Collision normal and contact arms
            //   2. Full contact velocity (linear + ω × r)
            //   3. Normal impulse with angular effective mass
            //   4. Tangent friction impulse (Coulomb-clamped)
            //   5. Apply Δv and Δω to both bodies
            //   6. Positional correction (mass-weighted push-apart)
            //
            // =================================================================

            Vector3 pos1 = gameModel1.m_physicsInfo.GetPosition();
            Vector3 pos2 = gameModel2.m_physicsInfo.GetPosition();
            ObjectContactManifold manifold;
            if ( !BuildObjectContactManifold( gameModel1, gameModel2, 0, 1, Cfg().contactEpsilon, manifold ) )
            {
                return;
            }

            Vector3 delta = pos2 - pos1;
            float dist = Vector::VectorMag( delta );
            if ( dist < TOLERANCE )
            {
                return;
            }
            Vector3 normal = manifold.normal;

            float e1 = gameModel1.m_physicsInfo.GetCoefficientRestitution();
            float e2 = gameModel2.m_physicsInfo.GetCoefficientRestitution();
            float e = sqrtf( e1 * e2 );

            float invMass1 = gameModel1.GetInvertedMass();
            float invMass2 = gameModel2.GetInvertedMass();
            Vector3 invInertia1 = gameModel1.GetInvertedRotationalInertia();
            Vector3 invInertia2 = gameModel2.GetInvertedRotationalInertia();

            // World-space inverse inertia application.
            // CATTO REF:
            //   Catto 2005, PDF p. 12, Section 5, unnumbered inertia transform
            //   before Equations 26-28. Reason: box angular response must use
            //   the rotated inverse inertia tensor.
            // For boxes: I_world_inv * v = R * (I_body_inv ∘ (R^T * v))
            // For spheres: isotropic, body-space = world-space.
            bool isBox1 = gameModel1.IsBox();
            bool isBox2 = gameModel2.IsBox();
            Transformation::RotationMatrix rot1 = gameModel1.m_physicsInfo.GetOrientationMatrix();
            Transformation::RotationMatrix rot2 = gameModel2.m_physicsInfo.GetOrientationMatrix();

            auto applyInvI1 = [&]( const Vector3& v ) -> Vector3
            {
                if ( !isBox1 )
                {
                    return Vector::VectorMultiply( invInertia1, v );
                }
                Vector3 bodyV = rot1.TransposeMultiply( v );
                return rot1 * Vector::VectorMultiply( invInertia1, bodyV );
            };
            auto applyInvI2 = [&]( const Vector3& v ) -> Vector3
            {
                if ( !isBox2 )
                {
                    return Vector::VectorMultiply( invInertia2, v );
                }
                Vector3 bodyV = rot2.TransposeMultiply( v );
                return rot2 * Vector::VectorMultiply( invInertia2, bodyV );
            };

            // Contact arms: actual shape-pair manifold contact point.
            // CATTO REF:
            //   Contact arms r1/r2 appear in PDF p. 9, Section 4.1, Equations
            //   16-18 and in tangent constraints on PDF pp. 11-12, Equations
            //   21-23.
            // ENGINE NOTE:
            //   Broadphase may still be bounding-radius based, but the contact
            //   point consumed here comes from exact sphere/box/OBB narrowphase.
            const ObjectContactPoint& impactPoint = manifold.points[0];
            Vector3 rContact1 = impactPoint.rA;
            Vector3 rContact2 = impactPoint.rB;

            // Full contact velocity including angular contributions.
            // CATTO REF:
            //   Catto 2005, PDF p. 6, Section 3.4, Equations 9-11.
            Vector3 vel1 = gameModel1.m_physicsInfo.GetVelocity();
            Vector3 vel2 = gameModel2.m_physicsInfo.GetVelocity();
            Vector3 omega1 = gameModel1.m_physicsInfo.GetAngularVelocity();
            Vector3 omega2 = gameModel2.m_physicsInfo.GetAngularVelocity();

            Vector3 vContact1 = vel1 + Vector::CrossProduct( omega1, rContact1 );
            Vector3 vContact2 = vel2 + Vector::CrossProduct( omega2, rContact2 );
            Vector3 vRel = vContact1 - vContact2;
            float vRelNormal = vRel * normal;

            // Only resolve if approaching
            if ( vRelNormal <= 0.0f )
            {
                return;
            }

            // Very slow contacts are resting contacts, not bounces. Turning
            // restitution off here prevents the solver from injecting a tiny
            // rebound every frame, which is the usual "jitters forever" symptom.
            if ( vRelNormal < Cfg().contactRestitutionThreshold )
            {
                e = 0.0f;
            }

            // --- Normal impulse with angular effective mass ---
            // CATTO REF:
            //   Catto 2005, PDF p. 9, Section 4.1, Equations 16-19 for the
            //   normal row, and PDF p. 14, Section 6, Equations 34-35 for the
            //   J*M^-1*J^T effective-mass denominator.
            //
            //  K_n = 1/m1 + 1/m2 + n · (I1^-1(r1×n) × r1) + n · (I2^-1(r2×n) × r2)
            //  j_n = -(1+e) * v_rel_n / K_n
            //
            Vector3 r1CrossN = Vector::CrossProduct( rContact1, normal );
            Vector3 iInv1_r1CrossN = applyInvI1( r1CrossN );
            float angTerm1 = normal * Vector::CrossProduct( iInv1_r1CrossN, rContact1 );

            Vector3 r2CrossN = Vector::CrossProduct( rContact2, normal );
            Vector3 iInv2_r2CrossN = applyInvI2( r2CrossN );
            float angTerm2 = normal * Vector::CrossProduct( iInv2_r2CrossN, rContact2 );

            float kNormal = invMass1 + invMass2 + angTerm1 + angTerm2;
            float jn = ( 1.0f + e ) * vRelNormal / kNormal;

            // Apply normal impulse (linear + angular)
            Vector3 normalImpulse = normal * jn;
            vel1 -= normalImpulse * invMass1;
            vel2 += normalImpulse * invMass2;
            omega1 -= applyInvI1( Vector::CrossProduct( rContact1, normalImpulse ) );
            omega2 += applyInvI2( Vector::CrossProduct( rContact2, normalImpulse ) );

            // --- Tangent friction impulse (Coulomb model) ---
            // CATTO REF:
            //   Catto 2005, PDF pp. 11-12, Section 4.3, Equations 21-25.
            // REASON:
            //   Recompute contact velocity after the normal impulse, remove the
            //   normal component, and solve a bounded tangent impulse that
            //   opposes sliding.
            //
            // Recompute contact velocity after normal impulse, extract tangential slide.
            // j_t = v_tangent_mag / K_t, clamped to μ * j_n (Coulomb cone).
            // Applied opposing the tangential sliding direction.
            //
            vContact1 = vel1 + Vector::CrossProduct( omega1, rContact1 );
            vContact2 = vel2 + Vector::CrossProduct( omega2, rContact2 );
            vRel = vContact1 - vContact2;
            float vRelN_post = vRel * normal;
            Vector3 vTangent = vRel - normal * vRelN_post;
            float vTangentSq = vTangent * vTangent;

            if ( vTangentSq > TOLERANCE * TOLERANCE )
            {
                float vTangentMag = sqrtf( vTangentSq );
                Vector3 tangentDir = vTangent / vTangentMag;

                // Effective mass for tangent direction
                Vector3 r1CrossT = Vector::CrossProduct( rContact1, tangentDir );
                Vector3 iInv1_r1CrossT = applyInvI1( r1CrossT );
                float tAngTerm1 = tangentDir * Vector::CrossProduct( iInv1_r1CrossT, rContact1 );

                Vector3 r2CrossT = Vector::CrossProduct( rContact2, tangentDir );
                Vector3 iInv2_r2CrossT = applyInvI2( r2CrossT );
                float tAngTerm2 = tangentDir * Vector::CrossProduct( iInv2_r2CrossT, rContact2 );

                float kTangent = invMass1 + invMass2 + tAngTerm1 + tAngTerm2;
                if ( kTangent > TOLERANCE )
                {
                    float jt = vTangentMag / kTangent;

                    // Coulomb friction cone: |j_t| ≤ μ * j_n
                    float mu = ( gameModel1.m_physicsInfo.GetFrictionCoefficient() +
                                 gameModel2.m_physicsInfo.GetFrictionCoefficient() ) *
                               0.5f;
                    float maxFriction = mu * jn;
                    if ( jt > maxFriction )
                    {
                        jt = maxFriction;
                    }

                    // Apply friction impulse opposing tangential slide
                    Vector3 frictionImpulse = tangentDir * jt;
                    vel1 -= frictionImpulse * invMass1;
                    vel2 += frictionImpulse * invMass2;
                    omega1 -= applyInvI1( Vector::CrossProduct( rContact1, frictionImpulse ) );
                    omega2 += applyInvI2( Vector::CrossProduct( rContact2, frictionImpulse ) );
                }
            }

            // Write back velocities
            gameModel1.m_physicsInfo.SetLinearVelocity( vel1 );
            gameModel2.m_physicsInfo.SetLinearVelocity( vel2 );
            gameModel1.m_physicsInfo.SetAngularVelocity( omega1 );
            gameModel2.m_physicsInfo.SetAngularVelocity( omega2 );

            // --- Positional correction (mass-weighted push-apart) ---
            // ENGINE-SPECIFIC / NOVEL:
            //   Catto's paper focuses on velocity-level contact constraints and
            //   Baumgarte bias. This immediate overlap cleanup is local policy
            //   for the current bounding-radius object narrowphase.
            float overlap = 0.0f;
            for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
            {
                overlap = (std::max)( overlap, manifold.points[pointIndex].penetration );
            }
            if ( overlap > 0.0f )
            {
                float totalInvMass = invMass1 + invMass2;
                gameModel1.m_physicsInfo.SetPosition( pos1 - normal * ( overlap * invMass1 / totalInvMass ) );
                gameModel2.m_physicsInfo.SetPosition( pos2 + normal * ( overlap * invMass2 / totalInvMass ) );
            } },
                gameModel1.m_boundingVolume,
                gameModel2.m_boundingVolume );
}


// =============================================================================
// SPHERE-SPHERE LINEAR VELOCITY EXCHANGE
// =============================================================================
//
// Uses the 1D elastic collision formula projected along the collision normal:
//
//         (m1 - e·m2)·v1i + (1+e)·m2·v2i
//  v1f = ──────────────────────────────────
//                   m1 + m2
//
// Restitution combined via geometric mean (e = sqrt(e1 * e2)).
// A perfectly inelastic material (e=0) dominates regardless of the other body.
//
// =============================================================================
void ImpulseSolver::SphereVsSphereLinear( GameModel& gameModel1,
                                          GameModel& gameModel2,
                                          const Vector3& collisionNormal )
{
    Vector3 v1 = gameModel1.m_physicsInfo.GetVelocity();
    Vector3 v2 = gameModel2.m_physicsInfo.GetVelocity();

    float v1Proj = v1 * collisionNormal;
    float v2Proj = v2 * collisionNormal;

    float e1 = gameModel1.m_physicsInfo.GetCoefficientRestitution();
    float e2 = gameModel2.m_physicsInfo.GetCoefficientRestitution();
    float e = sqrtf( e1 * e2 );
    float relativeNormalSpeed = ( v2 - v1 ) * collisionNormal;

    // If the two spheres are already separating, this older impact path should
    // leave them alone. The persistent contact solver handles slow resting
    // support separately, so this function only needs to process real impacts.
    if ( relativeNormalSpeed >= 0.0f )
    {
        return;
    }

    // Below the restitution threshold, a contact should become dull support
    // instead of a bounce. This lets a ball eventually settle rather than getting
    // a fresh tiny kick on every low-speed collision.
    if ( fabsf( relativeNormalSpeed ) < Cfg().contactRestitutionThreshold )
    {
        e = 0.0f;
    }

    float m1 = gameModel1.GetMass();
    float m2 = gameModel2.GetMass();
    float totalMass = m1 + m2;

    float v1f = ( ( m1 - e * m2 ) * v1Proj + ( 1.0f + e ) * m2 * v2Proj ) / totalMass;
    float v2f = ( ( m2 - e * m1 ) * v2Proj + ( 1.0f + e ) * m1 * v1Proj ) / totalMass;

    gameModel1.m_physicsInfo.SetLinearVelocity( v1 + collisionNormal * ( v1f - v1Proj ) );
    gameModel2.m_physicsInfo.SetLinearVelocity( v2 + collisionNormal * ( v2f - v2Proj ) );
}


// =============================================================================
// SPHERE-SPHERE ANGULAR IMPULSE (Friction-Based Spin Transfer)
// =============================================================================
//
// For sphere-sphere collisions, the contact point lies along the collision
// normal (r = R*n), making r × n = 0.  This means the NORMAL impulse cannot
// produce angular velocity change — all spin transfer must come from TANGENTIAL
// friction at the contact point.
//
//  1. Compute the tangential (sliding) velocity at the contact point
//  2. Apply a friction impulse opposing this sliding, clamped to the Coulomb
//     friction cone (μ × normal impulse magnitude)
//  3. Δω = I⁻¹(r × J)
//
// =============================================================================
void ImpulseSolver::SphereVsSphereAngular( GameModel& gameModel1,
                                           GameModel& gameModel2,
                                           const Vector3& collisionNormal )
{
    float r1 = GetShapeBoundingRadius( gameModel1.m_boundingVolume );
    float r2 = GetShapeBoundingRadius( gameModel2.m_boundingVolume );

    Vector3 rContact1 = collisionNormal * r1;
    Vector3 rContact2 = collisionNormal * ( -r2 );

    Vector3 vContact1 = gameModel1.m_physicsInfo.GetVelocity() +
                        Vector::CrossProduct( gameModel1.m_physicsInfo.GetAngularVelocity(), rContact1 );
    Vector3 vContact2 = gameModel2.m_physicsInfo.GetVelocity() +
                        Vector::CrossProduct( gameModel2.m_physicsInfo.GetAngularVelocity(), rContact2 );

    // Relative velocity at contact, decomposed into normal and tangential parts.
    // v_tangent = v_rel - (v_rel · n) * n  (the sliding component perpendicular to normal)
    Vector3 vRel = vContact2 - vContact1;
    float vRelNormal = vRel * collisionNormal;
    Vector3 vTangent = vRel - collisionNormal * vRelNormal;
    float vTangentMagSq = vTangent * vTangent;

    if ( vTangentMagSq < TOLERANCE * TOLERANCE )
    {
        gameModel1.m_physicsInfo.SetChangeInAngularVelocity( Math::Vector::ZERO_VECTOR );
        gameModel2.m_physicsInfo.SetChangeInAngularVelocity( Math::Vector::ZERO_VECTOR );
        return;
    }

    float vTangentMag = sqrtf( vTangentMagSq );
    Vector3 tangentDir = vTangent / vTangentMag;

    float e1 = gameModel1.m_physicsInfo.GetCoefficientRestitution();
    float e2 = gameModel2.m_physicsInfo.GetCoefficientRestitution();
    float e = sqrtf( e1 * e2 );

    // Friction gets its strength from the normal impact. For a gentle resting
    // touch, treat that impact as inelastic so the friction calculation does not
    // inherit artificial bounce energy.
    if ( fabsf( vRelNormal ) < Cfg().contactRestitutionThreshold )
    {
        e = 0.0f;
    }

    float invMass1 = gameModel1.GetInvertedMass();
    float invMass2 = gameModel2.GetInvertedMass();
    Vector3 invInertia1 = gameModel1.GetInvertedRotationalInertia();
    Vector3 invInertia2 = gameModel2.GetInvertedRotationalInertia();

    // Magnitude of the normal impulse (used to compute the Coulomb friction cone limit).
    // j_n = -(1+e) * v_rel_n / (1/m1 + 1/m2)   (no angular terms because r × n = 0 for spheres)
    float jnMag = fabsf( -( 1.0f + e ) * vRelNormal / ( invMass1 + invMass2 ) );

    // Effective (reduced) mass for the tangential friction constraint at the contact point.
    // K_t = 1/m1 + 1/m2 + t · (I1⁻¹(r1×t) × r1) + t · (I2⁻¹(r2×t) × r2)
    // This is the same effective-mass formula as for the normal constraint but along t instead of n.
    Vector3 r1CrossT = Vector::CrossProduct( rContact1, tangentDir );
    Vector3 iInv1_r1CrossT = Vector::VectorMultiply( invInertia1, r1CrossT );
    Vector3 term1 = Vector::CrossProduct( iInv1_r1CrossT, rContact1 );

    Vector3 r2CrossT = Vector::CrossProduct( rContact2, tangentDir );
    Vector3 iInv2_r2CrossT = Vector::VectorMultiply( invInertia2, r2CrossT );
    Vector3 term2 = Vector::CrossProduct( iInv2_r2CrossT, rContact2 );

    float kTangent = invMass1 + invMass2 + ( tangentDir * term1 ) + ( tangentDir * term2 );
    if ( kTangent < TOLERANCE )
    {
        gameModel1.m_physicsInfo.SetChangeInAngularVelocity( Math::Vector::ZERO_VECTOR );
        gameModel2.m_physicsInfo.SetChangeInAngularVelocity( Math::Vector::ZERO_VECTOR );
        return;
    }

    // j_t = tangential sliding speed / K_t  (impulse needed to stop sliding)
    float jt = vTangentMag / kTangent;

    // Coulomb friction cone: |j_t| ≤ μ * |j_n|
    // Physical meaning: friction force cannot exceed μ times the normal force.
    // μ averaged between the two surfaces (e.g. 0.5 + 0.7) / 2 = 0.6.
    float mu = ( gameModel1.m_physicsInfo.GetFrictionCoefficient() +
                 gameModel2.m_physicsInfo.GetFrictionCoefficient() ) *
               0.5f;
    float maxFriction = mu * jnMag;
    if ( jt > maxFriction )
    {
        jt = maxFriction;
    }

    // Friction impulse opposes tangential sliding
    Vector3 frictionImpulse = tangentDir * ( -jt );

    // Convert impulse to angular velocity change: Δω = I⁻¹ * (r × J)
    // Body 1 gets the reaction impulse (+frictionImpulse negated) because
    // Newton's 3rd law: the force on body1 is opposite to the force on body2.
    Vector3 deltaOmega1 = Vector::VectorMultiply( invInertia1, Vector::CrossProduct( rContact1, frictionImpulse * ( -1.0f ) ) );
    Vector3 deltaOmega2 = Vector::VectorMultiply( invInertia2, Vector::CrossProduct( rContact2, frictionImpulse ) );

    gameModel1.m_physicsInfo.SetChangeInAngularVelocity( deltaOmega1 );
    gameModel2.m_physicsInfo.SetChangeInAngularVelocity( deltaOmega2 );
}


// Returns the world-space centre of the bounding volume for this game model.
// bounding_center_world = body_position + R * local_offset
// where R is the orientation matrix.  For shapes with zero local offset this
// equals the body position directly; the general form handles offset shapes.
Vector3 ImpulseSolver::GetCollidedObjectWorldPosition( GameModel& gameModel )
{
    return gameModel.m_physicsInfo.GetPosition() +
           ( gameModel.m_physicsInfo.GetOrientationMatrix() * GetShapePosition( gameModel.m_boundingVolume ) );
}


// Computes the collision normal for a sphere-sphere pair.
// For two spheres, the contact point lies on the straight line between their centres.
// The outward contact normal (pointing from body1 toward body2) is:
//   n = normalize(pos2 - pos1)
// Impulses along +n push body2 away; impulses along -n push body1 away.
Vector3 ImpulseSolver::GetCollisionNormalSphereVsSphere( GameModel& gameModel1,
                                                         GameModel& gameModel2 )
{
    Vector3 pos1 = ImpulseSolver::GetCollidedObjectWorldPosition( gameModel1 );
    Vector3 pos2 = ImpulseSolver::GetCollidedObjectWorldPosition( gameModel2 );
    Vector3 normal = pos2 - pos1;
    normal.Normalise();
    return normal;
}
