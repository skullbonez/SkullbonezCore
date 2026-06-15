/*
File: SkullbonezData/shaders/reflect.rt.hlsl
Purpose:
  Runs the reflect.rt HLSL shader program used by the renderer.

Mental model:
  Shaders are GPU programs. Constant buffers provide per-frame data, shader
  stages transform or shade inputs, and CPU-side renderer bindings must match
  the declarations in this file.

Glossary:
  HLSL (High Level Shader Language): Shader language compiled for Direct3D
  render, compute, and raytracing stages.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - CPU-side root signatures, input layouts, and descriptor bindings must
  match this shader exactly.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
// =============================================================================
// DXR REFLECTION SHADER — HLSL Library 6.3 (Raytracing)
// =============================================================================
//
// PURPOSE: Render real-time raytraced reflections on the water surface using
// DirectX Raytracing (DXR). This is the most advanced shader in the engine.
//
// --- What is DXR (DirectX Raytracing)? ---
//
//  Traditional rendering (rasterization) draws triangles one by one onto the screen.
//  Raytracing works BACKWARDS: for each pixel, shoot a ray into the scene and see
//  what it hits. This naturally produces accurate reflections, shadows, and lighting.
//
//  DXR is Microsoft's API for hardware-accelerated raytracing (RTX GPUs).
//
// --- How This Shader Works (Conceptual Flow) ---
//
//  For EACH pixel on screen:
//
//  1. [RayGen] Generate a camera ray through this pixel
//  2. [RayGen] Intersect ray with water plane (Y = gWaterY) analytically
//  3. [RayGen] If ray hits water, compute reflection direction (bounce ray upward)
//  4. [RayGen] Trace the reflected ray against ALL scene geometry (BLAS/TLAS)
//  5. [ClosestHit] If reflected ray hits something → compute lit color with texture
//  6. [Miss] If reflected ray hits nothing → sample the skybox
//
//  ASCII diagram of the reflection process:
//
//        Camera
//          |  \  (primary ray)
//          |   \
//          |    \ hit water surface
//          |     *───────── water plane (Y = gWaterY)
//          |    /
//          |   /  (reflected ray bounces UP)
//          |  /
//          | /
//          |/ hits sphere
//          * ← color this pixel with sphere's lit texture color
//
// --- Acceleration Structure (BLAS/TLAS) ---
//
//  To make raytracing fast, geometry is organized into a tree structure:
//
//  TLAS (Top-Level Acceleration Structure)
//    ├── Instance 0: Terrain BLAS (position, transform)
//    ├── Instance 1: Sphere BLAS (position, transform)
//    ├── Instance 2: Sphere BLAS (different transform)
//    └── ...
//
//  BLAS = Bottom-Level Acceleration Structure (the actual triangles)
//  TLAS = Top-Level (instances of BLASes with transforms)
//
//  TraceRay() traverses this tree in hardware (BVH traversal) — O(log N) instead
//  of testing every triangle.
//
// --- Shader Types in DXR ---
//
//  [shader("raygeneration")] — Called once per pixel. Generates rays.
//  [shader("closesthit")]    — Called when a ray hits geometry. Computes color.
//  [shader("miss")]          — Called when a ray hits nothing. Returns sky color.
//
// --- Compiled With ---
//
//  DXC (DirectX Shader Compiler): dxc -T lib_6_3 reflect.rt.hlsl
//  Target: Shader Model 6.3 (first version supporting DXR)
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-raytracing
// =============================================================================


// --- Global Resources ---

// The acceleration structure containing ALL scene geometry (spheres + terrain).
// TraceRay() uses this to find ray-geometry intersections in hardware.
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-in-hlsl#acceleration-structure
RaytracingAccelerationStructure gScene : register(t0, space1);

// Output texture — we write the final color for each pixel here.
// RWTexture2D = Read-Write texture (UAV: Unordered Access View).
RWTexture2D<float4> gOutput : register(u0);

// Scene textures used in ClosestHit to color the surfaces we hit.
Texture2D gSphereTex  : register(t0);  // sphere diffuse texture
Texture2D gTerrainTex : register(t1);  // terrain diffuse texture
Texture2D gSkyUp      : register(t2);  // skybox face: +Y (up)
Texture2D gSkyDown    : register(t3);  // skybox face: -Y (down)
Texture2D gSkyRight   : register(t4);  // skybox face: -X (west)
Texture2D gSkyLeft    : register(t5);  // skybox face: +X (east)
Texture2D gSkyFront   : register(t6);  // skybox face: +Z (north)
Texture2D gSkyBack    : register(t7);  // skybox face: -Z (south)

SamplerState gSampler : register(s0);  // linear wrap sampler for all textures

// Constant buffer with per-frame camera and scene data.
cbuffer RTConstants : register(b1)
{
    float4x4 gInvViewProj;     // Inverse View×Projection (to reconstruct world rays from pixels)
    float3 gCameraPos;         // Camera world position
    float gWaterY;             // Water surface Y height (ray-plane intersection target)
    float3 gLightPos;          // Light position for shading hit surfaces
    float gTime;               // Time (unused currently, reserved for animated effects)
    float3 gSkyColorTop;       // Sky gradient top (unused — using texture instead)
    float _pad0;
    float3 gSkyColorBottom;    // Sky gradient bottom (unused — using texture instead)
    float _pad1;
};


// --- Payload ---
// Data carried by a ray as it travels. ClosestHit/Miss write into this.

struct RayPayload
{
    float3 color;  // Final color computed by hit/miss shader
    float hitT;    // Distance along ray to hit point (-1 = no hit)
};


// =============================================================================
// HELPER: Reconstruct a world-space ray from a screen pixel
// =============================================================================
//
// Given a pixel coordinate, this function figures out which direction
// a ray from the camera passes through that pixel in 3D world space.
//
// How it works:
// 1. Convert pixel (e.g., 400,300) to NDC (Normalized Device Coords) [-1,+1]
// 2. "Unproject" NDC near-plane and far-plane points using inverse VP matrix
// 3. Ray direction = far point - near point (normalized)
//
//  Screen pixels:          NDC:              World space:
//  (0,0)─────(800,0)      (-1,+1)──(+1,+1)     Camera ──→ ray direction
//    |    pixel    |        |              |
//  (0,600)──(800,600)     (-1,-1)──(+1,-1)
//

void GetCameraRay( uint2 pixel, uint2 dims, out float3 origin, out float3 direction )
{
    // Convert pixel to normalized [0,1] coordinates, centering on the pixel.
    float2 uv = ( (float2)pixel + 0.5f ) / (float2)dims;
    // Remap to NDC [-1,+1] with Y flipped (screen Y goes down, NDC Y goes up).
    float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );

    // Unproject near plane (Z=0) and far plane (Z=1) through inverse View-Projection.
    // This gives us two world-space points that define the ray through this pixel.
    float4 nearH = mul( gInvViewProj, float4( ndc, 0.0f, 1.0f ) );
    float4 farH = mul( gInvViewProj, float4( ndc, 1.0f, 1.0f ) );
    // Perspective divide: homogeneous → Cartesian coordinates.
    float3 nearPt = nearH.xyz / nearH.w;
    float3 farPt = farH.xyz / farH.w;

    origin = nearPt;
    direction = normalize( farPt - nearPt );
}


// =============================================================================
// HELPER: Sample the 6-face skybox cubemap from a direction vector
// =============================================================================
//
// Given a 3D direction, determine which skybox face it points at and compute
// the UV coordinates to sample that face's texture.
//
// The skybox is 6 separate 2D textures (not a DX cubemap) to match the
// rasterized renderer's layout. We determine the dominant axis of the direction
// to pick the face, then compute UV from the other two axes.
//
//  Direction vector → which face?
//  +Y dominant → top face (gSkyUp)
//  -Y dominant → bottom face (gSkyDown)
//  +X dominant → left face (gSkyLeft)    [named from inside-out view]
//  -X dominant → right face (gSkyRight)
//  +Z dominant → front face (gSkyFront)
//  -Z dominant → back face (gSkyBack)
//

float3 SampleSkybox( float3 d )
{
    float ax = abs( d.x ), ay = abs( d.y ), az = abs( d.z );

    if ( ay >= ax && ay >= az )
    {
        // Y dominant — looking up or down
        float u = ( d.x / ay ) * 0.5f + 0.5f;
        if ( d.y > 0.0f )
        {
            float v = ( d.z / ay ) * 0.5f + 0.5f;
            return gSkyUp.SampleLevel( gSampler, float2( u, v ), 0 ).rgb;
        }
        else
        {
            float v = -( d.z / ay ) * 0.5f + 0.5f;
            return gSkyDown.SampleLevel( gSampler, float2( u, v ), 0 ).rgb;
        }
    }
    else if ( ax >= az )
    {
        // X dominant — looking left or right
        float v = -( d.y / ax ) * 0.5f + 0.5f;
        if ( d.x < 0.0f )
        {
            float u = ( d.z / ax ) * 0.5f + 0.5f;
            return gSkyRight.SampleLevel( gSampler, float2( u, v ), 0 ).rgb;
        }
        else
        {
            float u = -( d.z / ax ) * 0.5f + 0.5f;
            return gSkyLeft.SampleLevel( gSampler, float2( u, v ), 0 ).rgb;
        }
    }
    else
    {
        // Z dominant — looking forward or backward
        float v = -( d.y / az ) * 0.5f + 0.5f;
        if ( d.z > 0.0f )
        {
            float u = ( d.x / az ) * 0.5f + 0.5f;
            return gSkyFront.SampleLevel( gSampler, float2( u, v ), 0 ).rgb;
        }
        else
        {
            float u = -( d.x / az ) * 0.5f + 0.5f;
            return gSkyBack.SampleLevel( gSampler, float2( u, v ), 0 ).rgb;
        }
    }
}


// =============================================================================
// RAY GENERATION SHADER
// =============================================================================
//
// Called once per pixel. This is the "entry point" of the raytracing pipeline.
//
// Algorithm:
// 1. Compute camera ray for this pixel
// 2. Analytically intersect with water plane (Y = gWaterY)
//    - Cheaper than tracing against water geometry
//    - Uses: t = (waterY - origin.y) / direction.y
// 3. If ray hits water: reflect direction around water normal (0,1,0)
//    then trace the reflected ray against scene geometry
// 4. If ray misses water (or parallel): sample skybox directly
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/ray-generation-shader
//

[shader("raygeneration")]
void RayGen()
{
    // DispatchRaysIndex() = which pixel this thread is processing.
    uint2 pixel = DispatchRaysIndex().xy;
    // DispatchRaysDimensions() = total output resolution.
    uint2 dims = DispatchRaysDimensions().xy;

    // Reconstruct world-space ray from pixel coordinates.
    float3 origin, direction;
    GetCameraRay( pixel, dims, origin, direction );

    // Analytical ray-plane intersection: plane at Y = gWaterY.
    // t = (planeY - rayOrigin.y) / rayDirection.y
    float denom = direction.y;
    float3 finalColor = float3( 0, 0, 0 );

    if ( abs( denom ) > 1e-6f )  // Not parallel to water surface
    {
        float t = ( gWaterY - origin.y ) / denom;

        if ( t > 0.0f )  // Water is in front of camera (not behind)
        {
            float3 hitPoint = origin + direction * t;

            // Reflect the incoming direction around the water normal (flat, pointing up).
            // reflect(I, N) = I - 2*dot(I,N)*N — mirrors the ray off the surface.
            float3 reflected = reflect( direction, float3( 0.0f, 1.0f, 0.0f ) );

            // Build a ray descriptor for the reflected ray.
            RayDesc ray;
            ray.Origin = hitPoint + float3( 0, 0.01f, 0 ); // Offset slightly above surface to avoid self-intersection
            ray.Direction = reflected;
            ray.TMin = 0.001f;    // Minimum travel distance (avoids hitting the origin point)
            ray.TMax = 10000.0f;  // Maximum travel distance (effectively infinite)

            RayPayload payload;
            payload.color = float3( 0, 0, 0 );
            payload.hitT = -1.0f;

            // TraceRay() — the core DXR intrinsic. Traverses the acceleration structure
            // and calls ClosestHit or Miss depending on what the ray encounters.
            // Parameters:
            //   gScene           = acceleration structure to traverse
            //   RAY_FLAG_CULL_BACK_FACING_TRIANGLES = skip back faces (optimization)
            //   0xFF             = instance mask (0xFF = test all instances)
            //   0, 0, 0          = hit group index offsets (we only have one hit group)
            //   ray              = ray origin + direction + min/max T
            //   payload          = data passed to/from hit/miss shaders
            // Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/traceray-function
            TraceRay( gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload );
            finalColor = payload.color;
        }
        else
        {
            // Ray goes away from water (looking up) — just show sky.
            finalColor = SampleSkybox( direction );
        }
    }
    else
    {
        // Ray is parallel to water plane — show sky.
        finalColor = SampleSkybox( direction );
    }

    // Write final color to the output texture at this pixel.
    gOutput[pixel] = float4( finalColor, 1.0f );
}


// =============================================================================
// CLOSEST HIT SHADER
// =============================================================================
//
// Called when a traced ray hits geometry (the closest intersection along the ray).
// Our job: compute the color at the hit point (texture + lighting).
//
// --- Built-in DXR Intrinsics Used ---
//
//  WorldRayOrigin()     — where the ray started (world space)
//  WorldRayDirection()  — ray direction (world space)
//  RayTCurrent()        — distance along ray to this hit point
//  InstanceID()         — which instance was hit (0=terrain, >0=sphere)
//  ObjectToWorld3x4()   — transform matrix of the hit instance
//
// --- Sphere vs Terrain Detection ---
//
//  We use InstanceID() to distinguish what was hit:
//  - Instance 0 = terrain (planar UV mapping from world XZ)
//  - Instance >0 = sphere (spherical UV mapping from surface normal)
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/closest-hit-shader
//

[shader("closesthit")]
void ClosestHit( inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs )
{
    // Compute world-space hit position: origin + direction * distance.
    float3 hitWorld = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    payload.hitT = RayTCurrent();

    // Extract the instance's world-space origin (translation column of its transform).
    float3 objOrigin = float3( ObjectToWorld3x4()[0][3], ObjectToWorld3x4()[1][3], ObjectToWorld3x4()[2][3] );

    // Determine what we hit based on instance ID.
    uint instID = InstanceID();
    float3 normal;
    float3 baseColor;

    if ( instID > 0 )
    {
        // --- SPHERE HIT ---
        // Surface normal = direction from sphere center to hit point (normalized).
        normal = normalize( hitWorld - objOrigin );

        // Transform normal to OBJECT space so UVs rotate with the ball's orientation.
        // This makes the texture appear to rotate as the ball rolls (physics-driven).
        float3x4 o2w = ObjectToWorld3x4();
        float3x3 rotInv = transpose( float3x3( o2w[0].xyz, o2w[1].xyz, o2w[2].xyz ) );
        float3 localNormal = normalize( mul( rotInv, normal ) );

        // Spherical UV mapping: convert normal direction to longitude (U) and latitude (V).
        //   U = atan2(x, z) / 2π + 0.5  → wraps around the equator
        //   V = asin(y) / π + 0.5        → wraps from pole to pole
        float u = atan2( localNormal.x, localNormal.z ) * ( 1.0f / ( 2.0f * 3.14159265f ) ) + 0.5f;
        float v = asin( clamp( localNormal.y, -1.0f, 1.0f ) ) * ( 1.0f / 3.14159265f ) + 0.5f;
        baseColor = gSphereTex.SampleLevel( gSampler, float2( u, v ), 0 ).rgb;
    }
    else
    {
        // --- TERRAIN HIT ---
        // Approximate normal as straight up (terrain is mostly flat).
        normal = float3( 0.0f, 1.0f, 0.0f );

        // Planar UV mapping: world XZ → texture UV with tiling factor matching rasterizer.
        // The magic number 15.0/1280.0 matches the rasterized terrain's texture tiling.
        float2 terrainUV = hitWorld.xz * ( 15.0f / 1280.0f );
        baseColor = gTerrainTex.SampleLevel( gSampler, terrainUV, 0 ).rgb;
    }

    // Simple diffuse lighting: N·L (Lambert) with ambient floor.
    float3 lightDir = normalize( gLightPos - hitWorld );
    float ndotl = saturate( dot( normal, lightDir ) );  // saturate = clamp to [0,1]
    float ambient = 0.3f;

    payload.color = baseColor * ( ambient + ndotl * 0.7f );
}


// =============================================================================
// MISS SHADER
// =============================================================================
//
// Called when a traced ray doesn't hit ANY geometry in the acceleration structure.
// We return the skybox color in the ray's direction (the ray "missed" everything
// and flew off into infinity — so it sees the sky).
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/miss-shader
//

[shader("miss")]
void Miss( inout RayPayload payload )
{
    // Sample skybox in the direction the ray was traveling.
    payload.color = SampleSkybox( WorldRayDirection() );
    payload.hitT = -1.0f;  // Signal "no geometry hit"
}
