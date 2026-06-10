// =============================================================================
// LIT TEXTURED SHADER — HLSL 5.0 (Combined Vertex + Pixel Shader)
// =============================================================================
//
// PURPOSE: Transform geometry with MVP matrices and apply Phong lighting + texture.
// This is the HLSL (Direct3D) equivalent of lit_textured.vert + lit_textured.frag.
//
// --- HLSL vs GLSL Key Differences ---
//
//  | Concept             | GLSL                     | HLSL                           |
//  |---------------------|--------------------------|--------------------------------|
//  | Constant data       | uniform mat4 uModel;     | cbuffer { float4x4 uModel; }  |
//  | Texture sampling    | texture(sampler, uv)     | tex.Sample(sampler, uv)        |
//  | Input/output        | layout(location=N) in    | struct with SEMANTICS          |
//  | Position output     | gl_Position              | SV_POSITION semantic           |
//  | Clip distance       | gl_ClipDistance[0]       | SV_ClipDistance0 semantic      |
//  | Matrix multiply     | mat * vec (operator*)    | mul(mat, vec) intrinsic        |
//
// --- cbuffer (Constant Buffer) ---
//
//  In DirectX, shader constants are grouped into "constant buffers" (cbuffers).
//  These are blocks of GPU memory that are updated once per draw call (or less).
//  register(b0) means "bind this cbuffer to slot 0".
//
//  Think of it as a struct in GPU memory that the CPU fills before each draw call.
//
// --- Semantics (POSITION, NORMAL, TEXCOORD0, etc.) ---
//
//  HLSL uses "semantics" to label what each input/output MEANS.
//  This tells the GPU pipeline how to wire data between stages:
//  - POSITION → vertex position (input assembler provides this)
//  - NORMAL → vertex normal vector
//  - TEXCOORD0..N → generic interpolated data passed between stages
//  - SV_POSITION → final screen-space position (System Value)
//  - SV_TARGET → pixel shader color output
//  - SV_ClipDistance0 → hardware clip plane (fragments behind are discarded)
//
// --- #pragma pack_matrix(column_major) ---
//
//  Tells the HLSL compiler to store matrices in column-major order in memory.
//  This matches our C++ Matrix4 layout, so we can upload matrices directly
//  without transposing. Without this, HLSL defaults to row-major and you'd
//  need to transpose every matrix on the CPU side.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl
// =============================================================================

#pragma pack_matrix(column_major)

// Constant buffer: all uniform data for this shader, uploaded by the CPU once per draw.
// register(b0) = bind to constant buffer slot 0.
cbuffer Uniforms : register(b0)
{
    float4x4 uModel;            // Object-to-world transform
    float4x4 uView;             // World-to-camera transform
    float4x4 uProjection;       // Camera-to-screen (perspective) transform
    float4   uClipPlane;        // Water reflection clip plane (discard below/above water)
    float4   uLightPosition;    // Light position (w=0: directional, w=1: point light)
    float4   uLightAmbient;     // Light ambient color
    float4   uLightDiffuse;     // Light diffuse color
    float4   uMaterialAmbient;  // Material ambient response
    float4   uMaterialDiffuse;  // Material diffuse response
    float4   uCinematicTerrain; // enable, relief, basin depth, rim lift
    float4   uCinematicBasin;   // center x/z, radius x/z
    float4   uStyleModes;       // sky, terrain, object, water
    float4   uTerrainTint;      // rgb tint
    float4   uTerrainAccent;    // rgb accent
    float4   uTerrainGrid;      // scale, strength, unused, unused
};

// Texture + sampler (equivalent to GLSL's uniform sampler2D).
// In DX, textures and samplers are SEPARATE objects bound to different slots.
// register(t0) = texture slot 0; register(s0) = sampler slot 0.
Texture2D    uTexture  : register(t0);
SamplerState sSampler0 : register(s0);

// Vertex shader input — each field maps to a vertex buffer element via its semantic.
struct VS_IN
{
    float3 position : POSITION;   // Vertex position in object space
    float3 normal   : NORMAL;     // Surface normal for lighting
    float2 texCoord : TEXCOORD0;  // UV coordinates for texture lookup
};

// Vertex → Pixel interpolated data (the GPU interpolates these across triangles).
struct VS_OUT
{
    float4 position  : SV_POSITION;     // Final screen position (consumed by rasterizer)
    float  clipDist  : SV_ClipDistance0; // Hardware clip plane distance
    float3 viewPos   : TEXCOORD0;       // Position in view/camera space (for lighting)
    float3 normal    : TEXCOORD1;       // Normal in view space (for lighting)
    float2 texCoord  : TEXCOORD2;       // UV passed through to pixel shader
    float3 worldPos  : TEXCOORD3;       // World-space position for cinematic terrain shaping
};

float BasinDistance(float2 xz)
{
    // Normalize world x/z into an oval basin space. A return value near 0 is the
    // basin center; near 1 is the rim. This is visual-only and does not affect
    // CPU-side terrain collision.
    float2 radius = max(uCinematicBasin.zw, float2(1.0f, 1.0f));
    return length((xz - uCinematicBasin.xy) / radius);
}

float CinematicTerrainOffset(float2 xz)
{
    if (uCinematicTerrain.x < 0.5f || uCinematicTerrain.y <= 0.0f)
    {
        // Relief defaults to off. Returning 0 here means the rendered terrain
        // matches the real terrain exactly.
        return 0.0f;
    }

    // Visual-only morph: lower the middle of an oval basin, raise a soft rim,
    // and add a little roughness on the slopes. Physics data is not changed.
    float d = BasinDistance(xz);
    float bowl = 1.0f - smoothstep(0.10f, 0.94f, d);
    float rim = exp(-pow((d - 1.04f) * 3.1f, 2.0f));
    float slopeTexture = smoothstep(0.32f, 0.92f, d) * (1.0f - smoothstep(1.02f, 1.55f, d));
    float rough = (sin(xz.x * 0.045f + xz.y * 0.011f) + sin(xz.y * 0.052f - xz.x * 0.017f)) * 0.5f;
    return uCinematicTerrain.y * (-uCinematicTerrain.z * bowl + uCinematicTerrain.w * rim + rough * 1.6f * slopeTexture);
}

float GridLine(float2 xz, float scale)
{
    float2 g = abs(frac(xz / max(scale, 0.001f)) - 0.5f);
    float lineDistance = min(g.x, g.y);
    return 1.0f - smoothstep(0.470f, 0.498f, lineDistance);
}

float3 FacetNormalFromDerivatives(float3 viewPos, float3 fallbackNormal)
{
    float3 dx = ddx(viewPos);
    float3 dy = ddy(viewPos);
    float3 faceN = normalize(cross(dy, dx));
    return dot(faceN, fallbackNormal) < 0.0f ? -faceN : faceN;
}

float3 TerrainModeColor(int mode, float3 texColor, float3 N, float3 worldPos)
{
    float3 base = texColor * max(uTerrainTint.rgb, float3(0.001f, 0.001f, 0.001f));
    if (mode == 1)
    {
        float wear = sin(worldPos.x * 0.035f) * sin(worldPos.z * 0.041f) * 0.08f;
        base = float3(0.34f, 0.35f, 0.34f) + wear + texColor * float3(0.14f, 0.14f, 0.14f);
    }
    else if (mode == 2)
    {
        base = float3(0.58f, 0.60f, 0.61f) + texColor * float3(0.08f, 0.08f, 0.08f);
    }
    else if (mode == 3)
    {
        float grid = GridLine(worldPos.xz, max(uTerrainGrid.x, 8.0f));
        base = float3(0.006f, 0.012f, 0.020f) + uTerrainAccent.rgb * grid * max(uTerrainGrid.y, 0.0f);
    }
    else if (mode == 4)
    {
        float veins = sin(worldPos.x * 0.026f + sin(worldPos.z * 0.021f) * 2.0f) * 0.5f + 0.5f;
        base = lerp(float3(0.18f, 0.08f, 0.24f), uTerrainTint.rgb, veins * 0.55f) + uTerrainAccent.rgb * pow(veins, 5.0f) * 0.45f;
    }
    else if (mode == 5)
    {
        base = texColor * uTerrainTint.rgb + float3(0.20f, 0.10f, 0.02f) * (1.0f - max(N.y, 0.0f));
    }
    else if (mode == 6)
    {
        base = floor((texColor * uTerrainTint.rgb + uTerrainAccent.rgb * 0.08f) * 5.0f) / 5.0f;
    }
    else if (mode == 7)
    {
        float heightT = saturate((worldPos.y - 28.0f) / 115.0f);
        float terrace = floor(heightT * 5.0f) / 5.0f;
        float3 lowColor = lerp(float3(0.30f, 0.46f, 0.18f), uTerrainAccent.rgb, 0.18f);
        float3 midColor = lerp(float3(0.58f, 0.68f, 0.28f), uTerrainTint.rgb, 0.35f);
        float3 highColor = float3(0.92f, 0.80f, 0.42f);
        base = lerp(lowColor, midColor, smoothstep(0.08f, 0.58f, heightT));
        base = lerp(base, highColor, smoothstep(0.58f, 1.0f, heightT));
        float slope = saturate(1.0f - max(N.y, 0.0f));
        float3 slopeColor = lerp(float3(0.38f, 0.30f, 0.16f), uTerrainAccent.rgb, 0.25f);
        base = lerp(base, slopeColor, slope * 0.38f);
        float2 patchCell = floor(worldPos.xz / 64.0f);
        float patchSeed = frac(sin(dot(patchCell, float2(12.9898f, 78.233f))) * 43758.5453f);
        float patchBand = floor(patchSeed * 4.0f) / 3.0f;
        float3 coolPatch = lerp(float3(0.23f, 0.38f, 0.10f), uTerrainAccent.rgb, 0.08f);
        float3 warmPatch = float3(0.70f, 0.62f, 0.24f);
        base = lerp(base, lerp(coolPatch, warmPatch, patchBand), 0.22f);
        float basinT = saturate(1.0f - BasinDistance(worldPos.xz));
        base *= 1.0f - basinT * 0.055f;
        float facet = floor(max(N.y, 0.0f) * 4.0f) / 4.0f;
        base *= 0.78f + facet * 0.30f + terrace * 0.10f;
    }
    else if (mode == 8)
    {
        base = lerp(float3(0.08f, 0.09f, 0.10f), uTerrainTint.rgb, 0.55f) + texColor * 0.05f;
    }
    else if (mode == 9)
    {
        base = lerp(float3(0.80f, 0.84f, 0.88f), float3(0.35f, 0.42f, 0.48f), saturate(1.0f - N.y)) + texColor * 0.06f;
    }
    else if (mode == 10)
    {
        float grid = GridLine(worldPos.xz, max(uTerrainGrid.x, 18.0f));
        base = float3(0.055f, 0.060f, 0.066f) + grid * uTerrainAccent.rgb * max(uTerrainGrid.y, 0.0f);
    }
    else if (mode == 11)
    {
        base = lerp(float3(0.76f, 0.82f, 0.88f), float3(0.96f, 0.98f, 1.0f), max(N.y, 0.0f)) + uTerrainAccent.rgb * 0.05f;
    }
    else if (mode == 12)
    {
        base = texColor * uTerrainTint.rgb * float3(0.92f, 1.02f, 0.88f);
    }
    else if (mode == 13)
    {
        float3 bands = 0.5f + 0.5f * cos(float3(0.0f, 2.1f, 4.2f) + worldPos.x * 0.018f + worldPos.z * 0.023f);
        base = lerp(uTerrainTint.rgb, uTerrainAccent.rgb, bands);
    }
    else if (mode == 14)
    {
        base = lerp(uTerrainTint.rgb, float3(0.90f, 0.92f, 0.84f), 0.35f + max(N.y, 0.0f) * 0.25f);
    }
    float authoredGrid = GridLine(worldPos.xz, max(uTerrainGrid.x, 8.0f)) * max(uTerrainGrid.y, 0.0f);
    base += uTerrainAccent.rgb * authoredGrid;
    return base;
}

// VERTEX SHADER: transform vertices and prepare lighting data.
VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // Combine Model and View into one matrix, then transform position to view space.
    float4x4 modelView = mul(uView, uModel);
    float4 worldPos    = mul(uModel, float4(input.position, 1.0));
    worldPos.y += CinematicTerrainOffset(worldPos.xz);
    float4 viewPos     = mul(uView, worldPos);
    // Apply perspective projection to get final screen position.
    output.position    = mul(uProjection, viewPos);

    // Clip distance: dot product with clip plane in WORLD space.
    // Positive = keep fragment; Negative = discard (used for water reflection clipping).
    output.clipDist = dot(worldPos, uClipPlane);

    // Pass view-space position and transformed normal for per-pixel lighting.
    output.viewPos  = viewPos.xyz;
    output.worldPos = worldPos.xyz;
    if (uCinematicTerrain.x > 0.5f && uCinematicTerrain.y > 0.0f)
    {
        // When we visually bend the terrain, the original mesh normals no longer
        // describe the apparent surface. Sample nearby offsets to estimate a new
        // slope normal so lighting follows the morphed basin.
        float eps = 8.0f;
        float dx = CinematicTerrainOffset(worldPos.xz + float2(eps, 0.0f)) -
                   CinematicTerrainOffset(worldPos.xz - float2(eps, 0.0f));
        float dz = CinematicTerrainOffset(worldPos.xz + float2(0.0f, eps)) -
                   CinematicTerrainOffset(worldPos.xz - float2(0.0f, eps));
        float3 reliefNormal = normalize(float3(-dx, eps * 2.0f, -dz));
        float3 baseWorldNormal = normalize(mul((float3x3)uModel, input.normal));
        float3 worldNormal = normalize(lerp(baseWorldNormal, reliefNormal, saturate(uCinematicTerrain.y)));
        output.normal = mul((float3x3)uView, worldNormal);
    }
    else
    {
        output.normal = mul((float3x3)modelView, input.normal);
    }
    output.texCoord = input.texCoord;

    return output;
}

// PIXEL SHADER: compute Phong lighting and apply texture.
// (Identical logic to the GLSL version — see lit_textured.frag for detailed explanation.)
float4 main_ps(VS_OUT input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 V = normalize(-input.viewPos);

    // Light direction: directional (w=0) vs point light (w=1).
    float3 L;
    if (uLightPosition.w == 0.0)
        L = normalize(uLightPosition.xyz);
    else
        L = normalize(uLightPosition.xyz - input.viewPos);

    // Ambient: constant minimum light.
    float3 ambient = uLightAmbient.rgb * uMaterialAmbient.rgb;

    // Diffuse: Lambert's cosine law.
    float diff = max(dot(N, L), 0.0);
    float3 diffuse = uLightDiffuse.rgb * uMaterialDiffuse.rgb * diff;

    // Specular: mirror highlight (Phong model).
    float3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 64.0);
    float3 specular = uLightDiffuse.rgb * spec * 0.1;

    // tex.Sample() is the HLSL equivalent of GLSL's texture().
    float4 texColor = uTexture.Sample(sSampler0, input.texCoord);

    if (uLightPosition.w == 0.0f)
    {
        // w=0 is how the C++ render path tells this shader "cinematic sun mode".
        // The terrain then gets a warmer, more photographic grade instead of the
        // neutral gameplay Phong lighting.
        int terrainMode = (int)floor(uStyleModes.y + 0.5f);
        float3 terrainN = N;
        if (terrainMode == 7)
        {
            terrainN = normalize(lerp(N, FacetNormalFromDerivatives(input.viewPos, N), 0.86f));
        }
        float terrainDot = dot(terrainN, L);
        float terrainDiff = max(terrainDot, 0.0f);
        float warmWrap = saturate(terrainDot * 0.5f + 0.5f);
        float grazing = pow(saturate(1.0f - abs(terrainDot)), 1.5f);
        float rim = pow(1.0f - saturate(dot(terrainN, V)), 3.0f) * (0.25f + warmWrap * 0.75f);
        float3 earthBase = TerrainModeColor(terrainMode, texColor.rgb, terrainN, input.worldPos);
        if (uCinematicTerrain.x > 0.5f)
        {
            // If visual terrain relief is enabled, darken the basin center and
            // add a subtle warm lift near the rim. This helps the bowl read even
            // before the height exaggeration slider is turned up high.
            float relief = clamp(uCinematicTerrain.y, 0.0f, 1.5f);
            float d = BasinDistance(input.worldPos.xz);
            float bowlShade = (1.0f - smoothstep(0.16f, 0.88f, d)) * relief;
            float rimShade = exp(-pow((d - 1.03f) * 3.2f, 2.0f)) * relief;
            earthBase = lerp(earthBase, earthBase * float3(0.62f, 0.47f, 0.34f), bowlShade * 0.55f);
            earthBase += float3(0.20f, 0.09f, 0.02f) * rimShade * 0.10f;
        }
        if (terrainMode == 7)
        {
            // Low-poly art mode: no texture dependency, just height-colored
            // terrain, hemisphere ambient, warm sun bands, and readable facets.
            float hemiT = saturate(terrainN.y * 0.5f + 0.5f);
            float3 skyAmbient = float3(0.45f, 0.65f, 1.0f);
            float3 groundAmbient = float3(0.35f, 0.25f, 0.15f);
            float3 hemiAmbient = lerp(groundAmbient, skyAmbient, hemiT);
            float sunBand = terrainDiff > 0.72f ? 1.0f : (terrainDiff > 0.34f ? 0.62f : 0.30f);
            float softFill = warmWrap * 0.12f;
            float3 warmSun = uLightDiffuse.rgb * float3(1.04f, 0.94f, 0.72f);
            float3 color = earthBase * (hemiAmbient * 0.58f + warmSun * (sunBand * 0.34f + softFill));
            color += warmSun * (rim * 0.055f + grazing * 0.030f);
            return float4(color, 1.0f);
        }

        // Grade the texture into a sunset palette: brown shadow tone, orange lit
        // tone, and a tiny ridge highlight where the view/light angle catches.
        float3 shadowTone = earthBase * float3(0.42f, 0.28f, 0.18f);
        float3 litTone = earthBase * float3(1.28f, 0.72f, 0.34f);
        float3 gradedBase = lerp(shadowTone, litTone, saturate(terrainDiff * 0.85f + warmWrap * 0.12f));
        float3 warmAmbient = gradedBase * uLightAmbient.rgb * (0.95f + max(terrainN.y, 0.0f) * 0.35f);
        float3 directSun = gradedBase * uLightDiffuse.rgb * (terrainDiff * 0.42f + grazing * 0.08f);
        float3 ridgeLight = uLightDiffuse.rgb * (rim * 0.045f + spec * 0.035f);
        return float4(warmAmbient + directSun + ridgeLight, 1.0f);
    }

    return float4((ambient + diffuse) * texColor.rgb + specular, 1.0);
}
