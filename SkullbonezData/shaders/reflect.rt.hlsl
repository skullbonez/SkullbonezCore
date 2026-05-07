// DXR Reflection Shader — raygen + closest_hit + miss
// Compiled with DXC: -T lib_6_3

// --- Global Resources ---

RaytracingAccelerationStructure gScene : register(t0, space1);
RWTexture2D<float4> gOutput : register(u0);

Texture2D gSphereTex  : register(t0);  // sphere diffuse texture
Texture2D gTerrainTex : register(t1);  // terrain diffuse texture
Texture2D gSkyUp      : register(t2);  // skybox face: +Y
Texture2D gSkyDown    : register(t3);  // skybox face: -Y
Texture2D gSkyRight   : register(t4);  // skybox face: -X (west, x=xMin)
Texture2D gSkyLeft    : register(t5);  // skybox face: +X (east, x=xMax)
Texture2D gSkyFront   : register(t6);  // skybox face: +Z
Texture2D gSkyBack    : register(t7);  // skybox face: -Z

SamplerState gSampler : register(s0);  // linear wrap sampler

cbuffer RTConstants : register(b1)
{
    float4x4 gInvViewProj;
    float3 gCameraPos;
    float gWaterY;
    float3 gLightPos;
    float gTime;
    float3 gSkyColorTop;
    float _pad0;
    float3 gSkyColorBottom;
    float _pad1;
};


// --- Payload ---

struct RayPayload
{
    float3 color;
    float hitT;
};


// --- Helper: reconstruct world-space ray from pixel ---

void GetCameraRay( uint2 pixel, uint2 dims, out float3 origin, out float3 direction )
{
    // Convert pixel to NDC [-1,1]
    float2 uv = ( (float2)pixel + 0.5f ) / (float2)dims;
    float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );

    // Unproject near and far points
    float4 nearH = mul( gInvViewProj, float4( ndc, 0.0f, 1.0f ) );
    float4 farH = mul( gInvViewProj, float4( ndc, 1.0f, 1.0f ) );
    float3 nearPt = nearH.xyz / nearH.w;
    float3 farPt = farH.xyz / farH.w;

    origin = nearPt;
    direction = normalize( farPt - nearPt );
}


// --- Helper: sample skybox from a direction vector ---
// UVs derived exactly from SkyBox.cpp vertex data per face.
// All faces: v=0 at yMax (top), v=1 at yMin (bottom) for side faces.

float3 SampleSkybox( float3 d )
{
    float ax = abs( d.x ), ay = abs( d.y ), az = abs( d.z );

    if ( ay >= ax && ay >= az )
    {
        // Y dominant
        // UP  (y=yMax): corner (xn,zp)→(0,1), (xp,zn)→(1,0)  → u=+x, v=+z
        // DOWN(y=yMin): corner (xp,zp)→(1,0), (xn,zn)→(0,1)  → u=+x, v=-z
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
        // X dominant
        // RIGHT (x=xMin, -X): (zp,yn)→(1,1), (zn,yp)→(0,0)  → u=+z, v=-y
        // LEFT  (x=xMax, +X): (zn,yn)→(1,1), (zp,yp)→(0,0)  → u=-z, v=-y
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
        // Z dominant
        // FRONT (+Z): (xp,yn)→(1,1), (xn,yp)→(0,0)  → u=+x, v=-y
        // BACK  (-Z): (xn,yn)→(1,1), (xp,yp)→(0,0)  → u=-x, v=-y
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


// --- Raygen Shader ---

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    float3 origin, direction;
    GetCameraRay( pixel, dims, origin, direction );

    // Intersect ray with water plane (Y = gWaterY) analytically
    float denom = direction.y;
    float3 finalColor = float3( 0, 0, 0 );

    if ( abs( denom ) > 1e-6f )
    {
        float t = ( gWaterY - origin.y ) / denom;

        if ( t > 0.0f )
        {
            float3 hitPoint = origin + direction * t;

            // Use flat upward water normal — DXR gives a clean still reflection.
            float3 reflected = reflect( direction, float3( 0.0f, 1.0f, 0.0f ) );

            // Trace reflected ray
            RayDesc ray;
            ray.Origin = hitPoint + float3( 0, 0.01f, 0 ); // Offset to avoid self-intersection
            ray.Direction = reflected;
            ray.TMin = 0.001f;
            ray.TMax = 10000.0f;

            RayPayload payload;
            payload.color = float3( 0, 0, 0 );
            payload.hitT = -1.0f;

            TraceRay( gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload );
            finalColor = payload.color;
        }
        else
        {
            // Ray goes away from water — sample sky
            finalColor = SampleSkybox( direction );
        }
    }
    else
    {
        // Parallel to water — sample sky
        finalColor = SampleSkybox( direction );
    }

    gOutput[pixel] = float4( finalColor, 1.0f );
}


// --- Closest Hit Shader ---

[shader("closesthit")]
void ClosestHit( inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs )
{
    // Compute world-space hit position
    float3 hitWorld = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    payload.hitT = RayTCurrent();

    // Compute barycentric-interpolated normal approximation
    // For vertex-only geometry without explicit normals, use the geometric face normal
    float3 objOrigin = float3( ObjectToWorld3x4()[0][3], ObjectToWorld3x4()[1][3], ObjectToWorld3x4()[2][3] );

    // Determine if this is a sphere or terrain based on instance ID
    uint instID = InstanceID();
    float3 normal;
    float3 baseColor;

    if ( instID > 0 )
    {
        // Sphere: normal = direction from instance origin to hit point
        normal = normalize( hitWorld - objOrigin );

        // Spherical UV mapping: longitude (atan2) for U, latitude (asin) for V
        float u = atan2( normal.x, normal.z ) * ( 1.0f / ( 2.0f * 3.14159265f ) ) + 0.5f;
        float v = asin( clamp( normal.y, -1.0f, 1.0f ) ) * ( 1.0f / 3.14159265f ) + 0.5f;
        baseColor = gSphereTex.SampleLevel( gSampler, float2( u, v ), 0 ).rgb;
    }
    else
    {
        // Terrain: approximate up-facing normal
        normal = float3( 0.0f, 1.0f, 0.0f );

        // Planar world-space UV — matches BuildMesh tiling:
        // textureWrap=15, postsPerSide=32, stepSize=8, terrainScale=5 → tile every 1280/15 world units
        float2 terrainUV = hitWorld.xz * ( 15.0f / 1280.0f );
        baseColor = gTerrainTex.SampleLevel( gSampler, terrainUV, 0 ).rgb;
    }

    // Compute diffuse lighting
    float3 lightDir = normalize( gLightPos - hitWorld );
    float ndotl = saturate( dot( normal, lightDir ) );
    float ambient = 0.3f;

    payload.color = baseColor * ( ambient + ndotl * 0.7f );
}


// --- Miss Shader ---

[shader("miss")]
void Miss( inout RayPayload payload )
{
    payload.color = SampleSkybox( WorldRayDirection() );
    payload.hitT = -1.0f;
}
