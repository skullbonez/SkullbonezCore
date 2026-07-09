/*
File: SkullbonezData/shaders/lit_textured_instanced.hlsl
Purpose:
  Runs the lit_textured_instanced HLSL shader program used by the renderer.

Mental model:
  lit_textured_instanced.hlsl is shader source for the renderer's
  lit_textured_instanced pass. Keep edits anchored on shader inputs, bindings,
  and render-output contracts and on the glossary/invariants below.

Glossary:
  SRV (Shader Resource View): Descriptor row used when shaders read textures or
  buffers.
  TEXCOORD semantic: Named vertex/interpolator channel shared between the input
  layout and shader stages.
  Material table: Fixed t4 texture that stores default material response values
  by material kind.
  Material payload: Four per-instance float4 rows named material0 through
  material3.
  Contact flash alpha: material3.w blend that pushes the final lit color toward
  white for short render-only feedback.
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
// INSTANCED LIT TEXTURED SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Same as lit_textured.hlsl but with INSTANCED rendering for spheres.
// Instead of a single uModel matrix in the cbuffer, each instance provides its
// own 4×4 model matrix via per-instance vertex attributes (TEXCOORD1-4).
//
// --- Instancing In DX12 ---
//
//  - Per-vertex data (position, normal, UV) is in one vertex buffer with step rate 0
//  - Per-instance data (model matrix, etc.) is in another buffer with step rate 1
//  - The input assembler automatically advances instance data every N vertices
//
//  HLSL names those streams with semantics:
//  float4 model0-3 : TEXCOORD1-4; (per-instance step rate in the input layout)
//
// --- Matrix Transpose Issue ---
//
//  Our C++ Matrix4 stores data column-major. When we upload 4 float4s as instance
//  data (model0=col0, model1=col1, etc.), HLSL's float4x4() constructor treats its
//  arguments as ROWS. So we must transpose() to fix the orientation.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-buffers-intro
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uView;
    float4x4 uProjection;
    float4   uClipPlane;
    float4   uLightPosition;
    float4   uLightAmbient;
    float4   uLightDiffuse;
    float4   uMaterialAmbient;
    float4   uMaterialDiffuse;
    int      uObjectStyle;      // >=0 ordinary style, <0 encoded cinematic style
    int      uPrimitiveShape;
    float    uMaterialAlpha;
    float    _objectStylePad;
    float4x4 uShadowViewProj;
    float4   uShadowParams;
    float4   uShadowFlags;
};

Texture2D    uTexture  : register(t0);
Texture2D    uShadowMap : register(t3);
Texture2D    uMaterialTable : register(t4);
SamplerState sSampler0 : register(s0);
SamplerState sSampler3 : register(s3);

// Concept: object material data arrives through two small channels.
//
// The per-instance stream carries draw-local color and response values in
// material0/material1/material2/material3. The t4 material table carries
// defaults by material kind, sampled through material2.w. Keeping both channels
// lets legacy tint/colorOverride authoring keep working while the shader gains
// typed roughness, specular, emissive controls, and per-object alpha.
struct VS_IN
{
    float3 position : POSITION;   // Per-vertex: object-space position
    float3 normal   : NORMAL;     // Per-vertex: surface normal
    float2 texCoord : TEXCOORD0;  // Per-vertex: UV
    float4 model0   : TEXCOORD1;  // Per-instance: model matrix column 0
    float4 model1   : TEXCOORD2;  // Per-instance: model matrix column 1
    float4 model2   : TEXCOORD3;  // Per-instance: model matrix column 2
    float4 model3   : TEXCOORD4;  // Per-instance: model matrix column 3
    float4 material0 : TEXCOORD5; // base rgb + legacy mode bridge
    float4 material1 : TEXCOORD6; // roughness, metallic, specular, emissive strength
    float4 material2 : TEXCOORD7; // emissive rgb + material-table row
    float4 material3 : TEXCOORD8; // alpha, transmission, flags, contact flash
};

struct VS_OUT
{
    float4 position  : SV_POSITION;
    float  clipDist  : SV_ClipDistance0;
    float3 viewPos   : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texCoord  : TEXCOORD2;
    float4 material0 : TEXCOORD3;
    float4 material1 : TEXCOORD4;
    float4 material2 : TEXCOORD5;
    float4 material3 : TEXCOORD10;
    float3 worldPos  : TEXCOORD6;
    nointerpolation float4 sphereShadowInfo : TEXCOORD7;
    float3 worldNormal : TEXCOORD8;
    float3 localDir : TEXCOORD9;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // Reconstruct 4×4 model matrix from per-instance columns.
    // float4x4() takes ROW arguments, but we pass COLUMNs → transpose fixes it.
    float4x4 model = transpose(float4x4(input.model0, input.model1, input.model2, input.model3));

    float4x4 modelView = mul(uView, model);
    float4 worldPos    = mul(model, float4(input.position, 1.0));
    float4 viewPos     = mul(uView, worldPos);
    output.position    = mul(uProjection, viewPos);

    output.clipDist = dot(worldPos, uClipPlane);

    output.viewPos  = viewPos.xyz;
    output.normal   = mul((float3x3)modelView, input.normal);
    output.worldNormal = mul((float3x3)model, input.normal);
    output.localDir = normalize(input.position);
    output.texCoord = input.texCoord;
    output.material0 = input.material0;
    output.material1 = input.material1;
    output.material2 = input.material2;
    output.material3 = input.material3;
    output.worldPos = worldPos.xyz;
    float3 sphereCenter = mul(model, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
    float3 sphereAxis = mul(model, float4(1.0f, 0.0f, 0.0f, 1.0f)).xyz - sphereCenter;
    output.sphereShadowInfo = float4(sphereCenter, length(sphereAxis));

    return output;
}

float4 SampleMaterialTable(float materialRow)
{
    // Material rows are stored in a 16x1 texture. Sample at texel centers and
    // clamp out-of-range kinds to keep bad authoring data from reading a
    // neighboring descriptor or wrap-filtered value.
    float row = clamp(floor(materialRow + 0.5f), 0.0f, 15.0f);
    return uMaterialTable.SampleLevel(sSampler3, float2((row + 0.5f) / 16.0f, 0.5f), 0.0f);
}

int DecodeObjectStyle(int objectStyle)
{
    return objectStyle < 0 ? -(objectStyle + 1) : objectStyle;
}

int ResolveMaterialMode(float legacyMode, int objectStyle)
{
    // Compatibility contract: material0.w still follows the old tint.a rules.
    // Negative means textured beachball, large positive values are explicit
    // material kinds, small positive values mean matte, and zero falls back to
    // the current object style.
    if (legacyMode < -0.5f)
        return 0;
    if (legacyMode > 1.25f)
        return (int)floor(legacyMode + 0.5f);
    if (legacyMode > 0.5f)
        return 1;
    return DecodeObjectStyle(objectStyle);
}

float3 ProceduralBeachBallPalette(float redPanel, float seam)
{
    float3 yellow = float3(1.0f, 0.78f, 0.0f);
    float3 red = float3(0.96f, 0.06f, 0.0f);
    float3 color = lerp(yellow, red, redPanel);
    return lerp(color, float3(1.0f, 0.62f, 0.02f), seam * 0.34f);
}

float3 ProceduralBeachBallColorFromUv(float2 uv)
{
    uv = frac(uv);
    float longitudePanel = floor(uv.x * 2.0f);
    float hemisphere = step(0.5f, uv.y);
    float redPanel = fmod(longitudePanel + hemisphere, 2.0f);

    float panelCoord = frac(uv.x * 2.0f);
    float seamU = min(panelCoord, 1.0f - panelCoord);
    float seamV = abs(uv.y - 0.5f);
    float panelFootprint = max(abs(ddx(uv.x * 2.0f)) + abs(ddy(uv.x * 2.0f)), abs(ddx(uv.y)) + abs(ddy(uv.y)));
    float seamWidth = max(0.014f, panelFootprint * 1.25f);
    float seam = 1.0f - smoothstep(0.0f, seamWidth, min(seamU, seamV));
    return ProceduralBeachBallPalette(redPanel, seam);
}

// Procedural beach-ball panels stay crisp without relying on low-res texture pixels.
float3 ProceduralBeachBallColorFromSphereDir(float3 localDir)
{
    // UV interpolation wraps at the sphere seam and can wobble on close-up balls.
    // Object-local direction keeps the color split attached to the mesh surface.
    float3 dir = normalize(localDir);
    float redPanel = abs(step(0.0f, -dir.x) - step(0.0f, -dir.y));

    float seamMetric = min(abs(dir.x), abs(dir.y));
    float seamFootprint = max(fwidth(dir.x), fwidth(dir.y));
    float seamWidth = max(0.014f, seamFootprint * 1.35f);
    float seam = 1.0f - smoothstep(0.0f, seamWidth, seamMetric);
    return ProceduralBeachBallPalette(redPanel, seam);
}

float3 QuantizedLowPolyNormal(float3 N)
{
    float3 qN = floor(N * 2.5f + 0.5f) / 2.5f;
    return normalize(lerp(N, qN, 0.88f));
}

float LowPolySunBand(float lightAmount)
{
    return lightAmount > 0.72f ? 1.0f : (lightAmount > 0.34f ? 0.62f : 0.30f);
}

float ShadowVisibility(float3 worldPos, float3 normalView, float3 lightView)
{
    if (uShadowFlags.x < 0.5f || uShadowFlags.y < 0.5f || uShadowParams.x <= 0.0f)
    {
        return 1.0f;
    }

    float3 receiverN = normalize(normalView);
    float3 receiverL = normalize(lightView);
    float ndotl = dot(receiverN, receiverL);
    // Why: the shadow map only knows whether a caster is between this point and
    // the sun. Back-side fill and rim lighting should not be darkened by a
    // silhouette that belongs on the opposite face of a thin wall.
    if (ndotl <= 0.001f)
    {
        return 1.0f;
    }

    float4 shadowClip = mul(uShadowViewProj, float4(worldPos, 1.0f));
    if (shadowClip.w <= 0.0f)
    {
        return 1.0f;
    }

    float3 ndc = shadowClip.xyz / shadowClip.w;
    float2 uv = ndc.xy * 0.5f + 0.5f;
    if (uShadowFlags.w > 0.5f)
    {
        uv.y = 1.0f - uv.y;
    }
    float receiverDepth = uShadowFlags.w > 0.5f ? ndc.z : ndc.z * 0.5f + 0.5f;

    if (uv.x <= 0.0f || uv.x >= 1.0f || uv.y <= 0.0f || uv.y >= 1.0f || receiverDepth <= 0.0f || receiverDepth >= 1.0f)
    {
        return 1.0f;
    }

    float bias = uShadowParams.y + uShadowParams.z * (1.0f - saturate(ndotl));
    int radius = (int)floor(uShadowFlags.z + 0.5f);
    float texel = max(uShadowParams.w, 0.00001f);
    float visible = 0.0f;
    float samples = 0.0f;
    for (int y = -3; y <= 3; ++y)
    {
        if (abs(y) > radius)
        {
            continue;
        }
        for (int x = -3; x <= 3; ++x)
        {
            if (abs(x) > radius)
            {
                continue;
            }
            float shadowDepth = uShadowMap.SampleLevel(sSampler3, uv + float2((float)x, (float)y) * texel, 0.0f).r;
            visible += receiverDepth - bias <= shadowDepth ? 1.0f : 0.0f;
            samples += 1.0f;
        }
    }

    float visibility = samples > 0.0f ? visible / samples : 1.0f;
    return lerp(1.0f - uShadowParams.x, 1.0f, visibility);
}

float3 SphereShadowReceiverWorldPos(float3 worldPos, float4 sphereShadowInfo)
{
    if (uPrimitiveShape != 1 || sphereShadowInfo.w <= 0.0f)
    {
        return worldPos;
    }

    float3 radial = worldPos - sphereShadowInfo.xyz;
    float lenSq = dot(radial, radial);
    if (lenSq <= 0.000001f)
    {
        return worldPos;
    }

    return sphereShadowInfo.xyz + radial * (sphereShadowInfo.w * rsqrt(lenSq));
}

float3 ApplyMaterialMode(int mode, float3 materialColor, float3 N, float3 V, float3 L, float3 lightColor, float diff, float spec, float2 uv)
{
    float3 R = reflect(-L, N);
    if (mode == 2)
    {
        float metalSpec = pow(max(dot(V, R), 0.0f), 140.0f);
        float3 reflected = lerp(float3(0.10f, 0.12f, 0.14f), lightColor * 1.4f, metalSpec);
        return materialColor * (0.12f + diff * 0.32f) + reflected * (0.35f + metalSpec * 0.75f);
    }
    if (mode == 3)
    {
        float pulse = 0.70f + 0.30f * sin(uv.x * 24.0f + uv.y * 18.0f);
        return materialColor * (1.5f + pulse * 1.2f) + lightColor * spec * 0.22f;
    }
    if (mode == 4)
    {
        float fresnel = pow(1.0f - saturate(dot(N, V)), 2.6f);
        return materialColor * (0.18f + diff * 0.22f) + lightColor * (fresnel * 0.85f + spec * 0.48f);
    }
    if (mode == 5)
    {
        float bands = diff > 0.72f ? 1.0f : (diff > 0.34f ? 0.58f : 0.24f);
        float rim = pow(1.0f - saturate(dot(N, V)), 3.0f);
        return materialColor * (0.18f + bands * 0.95f) + lightColor * rim * 0.16f;
    }
    if (mode == 6)
    {
        float3 qN = QuantizedLowPolyNormal(N);
        float qDiff = max(dot(qN, L), 0.0f);
        float sunBand = LowPolySunBand(qDiff);
        float hemiT = saturate(qN.y * 0.5f + 0.5f);
        float3 skyAmbient = float3(0.34f, 0.46f, 0.72f);
        float3 groundAmbient = float3(0.34f, 0.24f, 0.13f);
        float3 hemiAmbient = lerp(groundAmbient, skyAmbient, hemiT);
        float3 poster = lerp(materialColor, floor(materialColor * 4.0f + 0.5f) / 4.0f, 0.35f);
        float rim = pow(1.0f - saturate(dot(qN, V)), 2.2f);
        float3 warmSun = lightColor * float3(1.03f, 0.92f, 0.68f);
        return poster * (hemiAmbient * 0.50f + warmSun * (0.22f + sunBand * 0.42f)) + warmSun * rim * 0.055f;
    }
    if (mode == 8)
    {
        float3 qN = QuantizedLowPolyNormal(N);
        float qDiff = max(dot(qN, L), 0.0f);
        float sunBand = LowPolySunBand(qDiff);
        float hemiT = saturate(qN.y * 0.5f + 0.5f);
        float leafTier = floor(saturate(uv.y + qN.y * 0.22f) * 4.0f) / 4.0f;
        float3 topLeaf = lerp(materialColor, float3(0.50f, 0.66f, 0.18f), 0.42f);
        float3 underside = lerp(materialColor * float3(0.40f, 0.58f, 0.34f), float3(0.06f, 0.22f, 0.06f), 0.36f);
        float3 leaf = lerp(underside, topLeaf, hemiT);
        leaf *= 0.78f + leafTier * 0.20f + sunBand * 0.10f;
        float3 skyAmbient = float3(0.26f, 0.38f, 0.52f);
        float3 groundAmbient = float3(0.14f, 0.24f, 0.07f);
        float3 hemiAmbient = lerp(groundAmbient, skyAmbient, hemiT);
        float3 warmSun = lightColor * float3(1.02f, 0.88f, 0.54f);
        float rim = pow(1.0f - saturate(dot(qN, V)), 2.1f);
        return leaf * (hemiAmbient * 0.52f + warmSun * (0.18f + sunBand * 0.38f)) + warmSun * rim * 0.034f;
    }
    if (mode == 13)
    {
        float3 qN = QuantizedLowPolyNormal(N);
        float qDiff = max(dot(qN, L), 0.0f);
        float sunBand = LowPolySunBand(qDiff);
        float hemiT = saturate(qN.y * 0.5f + 0.5f);
        float heightBand = floor(saturate(uv.y) * 3.0f) / 3.0f;
        float3 litNeedle = lerp(materialColor, float3(0.48f, 0.64f, 0.18f), 0.38f);
        float3 shadowNeedle = lerp(materialColor * float3(0.36f, 0.54f, 0.34f), float3(0.040f, 0.16f, 0.045f), 0.42f);
        float3 leaf = lerp(shadowNeedle, litNeedle, hemiT);
        leaf *= 0.74f + heightBand * 0.16f + sunBand * 0.18f;
        float3 skyAmbient = float3(0.22f, 0.33f, 0.48f);
        float3 groundAmbient = float3(0.10f, 0.18f, 0.07f);
        float3 hemiAmbient = lerp(groundAmbient, skyAmbient, hemiT);
        float3 warmSun = lightColor * float3(1.02f, 0.86f, 0.50f);
        float rim = pow(1.0f - saturate(dot(qN, V)), 2.0f);
        return leaf * (hemiAmbient * 0.58f + warmSun * (0.12f + sunBand * 0.36f)) + warmSun * rim * 0.024f;
    }
    if (mode == 9)
    {
        float3 qN = QuantizedLowPolyNormal(N);
        float sunBand = LowPolySunBand(max(dot(qN, L), 0.0f));
        float grain = 0.5f + 0.5f * sin(uv.x * 24.0f + uv.y * 7.0f);
        float3 bark = lerp(materialColor * float3(0.72f, 0.58f, 0.42f), materialColor * float3(1.22f, 0.96f, 0.62f) + float3(0.05f, 0.025f, 0.0f), grain * 0.28f);
        float hemiT = saturate(qN.y * 0.5f + 0.5f);
        float3 hemiAmbient = lerp(float3(0.30f, 0.20f, 0.11f), float3(0.44f, 0.44f, 0.32f), hemiT);
        float3 warmSun = lightColor * float3(1.02f, 0.86f, 0.55f);
        return bark * (hemiAmbient * 0.46f + warmSun * (0.14f + sunBand * 0.34f));
    }
    if (mode == 10)
    {
        float3 qN = QuantizedLowPolyNormal(N);
        float sunBand = LowPolySunBand(max(dot(qN, L), 0.0f));
        float facet = floor(max(qN.y, 0.0f) * 4.0f) / 4.0f;
        float3 stone = lerp(materialColor * float3(0.90f, 0.94f, 1.04f), float3(0.44f, 0.48f, 0.56f), 0.42f);
        stone *= 0.68f + facet * 0.26f + sunBand * 0.05f;
        float hemiT = saturate(qN.y * 0.5f + 0.5f);
        float3 hemiAmbient = lerp(float3(0.22f, 0.22f, 0.26f), float3(0.46f, 0.50f, 0.56f), hemiT);
        float3 warmSun = lightColor * float3(0.86f, 0.78f, 0.58f);
        float rim = pow(1.0f - saturate(dot(qN, V)), 2.4f);
        return stone * (hemiAmbient * 0.54f + warmSun * (0.12f + sunBand * 0.24f)) + warmSun * rim * 0.014f;
    }
    if (mode == 11)
    {
        float3 qN = QuantizedLowPolyNormal(N);
        float sunBand = LowPolySunBand(max(dot(qN, L), 0.0f));
        float hemiT = saturate(qN.y * 0.5f + 0.5f);
        float3 ridge = lerp(materialColor, float3(0.70f, 0.76f, 0.48f), 0.38f);
        ridge *= 0.82f + floor(max(qN.y, 0.0f) * 3.0f) * 0.08f;
        float3 hazeAmbient = lerp(float3(0.40f, 0.44f, 0.30f), float3(0.62f, 0.70f, 0.66f), hemiT);
        float3 warmSun = lightColor * float3(0.88f, 0.80f, 0.58f);
        return ridge * (hazeAmbient * 0.58f + warmSun * (0.10f + sunBand * 0.20f));
    }
    if (mode == 12)
    {
        float3 qN = QuantizedLowPolyNormal(N);
        float sunBand = LowPolySunBand(max(dot(qN, L), 0.0f));
        float band = floor(saturate(uv.y) * 3.0f) / 3.0f;
        float3 sand = lerp(materialColor, float3(0.84f, 0.76f, 0.48f), 0.44f);
        sand *= 0.86f + band * 0.12f;
        float hemiT = saturate(qN.y * 0.5f + 0.5f);
        float3 hemiAmbient = lerp(float3(0.42f, 0.34f, 0.20f), float3(0.62f, 0.66f, 0.52f), hemiT);
        float3 warmSun = lightColor * float3(1.06f, 0.94f, 0.64f);
        return sand * (hemiAmbient * 0.50f + warmSun * (0.18f + sunBand * 0.34f));
    }
    if (mode == 7)
    {
        float rim = pow(1.0f - saturate(dot(N, V)), 1.8f);
        return float3(0.006f, 0.010f, 0.018f) + materialColor * rim * 0.16f + lightColor * spec * 0.05f;
    }
    return materialColor * (0.16f + diff * 0.92f) + lightColor * spec * 0.12f;
}

float3 OrdinaryHemisphereAmbient(float3 worldN)
{
    float hemiT = saturate(worldN.y * 0.5f + 0.5f);
    return lerp(uMaterialAmbient.rgb, uLightAmbient.rgb, hemiT) * max(uLightAmbient.a, 0.0f);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    float x = 1.0f - saturate(cosTheta);
    float x2 = x * x;
    return f0 + (1.0f - f0) * x2 * x2 * x;
}

float DistributionGGX(float ndoth, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = ndoth * ndoth * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(3.14159265f * denom * denom, 0.00001f);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return ndotv / max(ndotv * (1.0f - k) + k, 0.00001f);
}

float3 OrdinaryMaterialBRDF(float3 materialColor,
                            float3 emissive,
                            float roughness,
                            float metallic,
                            float materialSpecular,
                            float3 worldN,
                            float3 N,
                            float3 V,
                            float3 L,
                            float shadowFactor)
{
    float3 H = normalize(V + L);
    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V));
    float ndoth = saturate(dot(N, H));
    float vdoth = saturate(dot(V, H));

    float3 ambient = materialColor * OrdinaryHemisphereAmbient(worldN);
    roughness = clamp(roughness, 0.04f, 1.0f);
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f) * max(materialSpecular, 0.0f), materialColor, saturate(metallic));
    float3 F = FresnelSchlick(vdoth, f0);
    float D = DistributionGGX(ndoth, roughness);
    float G = GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
    float3 specular = (D * G * F) / max(4.0f * ndotv * ndotl, 0.0001f);
    float3 diffuse = (1.0f - F) * (1.0f - saturate(metallic)) * materialColor * (1.0f / 3.14159265f);
    float3 direct = (diffuse + specular) * uLightDiffuse.rgb * ndotl * shadowFactor;
    return ambient + direct + emissive;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 worldN = normalize(input.worldNormal);
    float3 V = normalize(-input.viewPos);

    float3 L;
    if (uLightPosition.w == 0.0)
        L = normalize(uLightPosition.xyz);
    else
        L = normalize(uLightPosition.xyz - input.viewPos);

    float diff = max(dot(N, L), 0.0);

    float3 R = reflect(-L, N);
    float4 tableParams = SampleMaterialTable(input.material2.w);
    // Blend mostly per-instance values with a small table contribution. The
    // instance payload remains authoritative for scene-specific overrides, while
    // the table keeps material-kind defaults visible to the shader contract.
    float roughness = saturate(input.material1.x * 0.80f + tableParams.x * 0.20f);
    float metallic = saturate(input.material1.y * 0.80f + tableParams.y * 0.20f);
    float materialSpecular = saturate(input.material1.z * 0.80f + tableParams.z * 0.20f);
    if (uPrimitiveShape == 1)
    {
        roughness = saturate(roughness * max(uMaterialAmbient.a, 0.01f));
        materialSpecular = saturate(materialSpecular * max(uMaterialDiffuse.a, 0.0f));
    }
    else
    {
        roughness = saturate(roughness * max(uLightDiffuse.a, 0.01f));
        materialSpecular = saturate(materialSpecular * max(_objectStylePad, 0.0f));
    }

    bool cinematicMode = uObjectStyle < 0;
    int materialMode = ResolveMaterialMode(input.material0.a, uObjectStyle);

    float3 materialColor = input.material0.rgb;
    float materialAlpha = saturate(uMaterialAlpha * input.material3.x);
    float contactFlash = saturate(input.material3.w);
    if (materialMode == 0)
    {
        float3 proceduralColor = uPrimitiveShape == 1 ? ProceduralBeachBallColorFromSphereDir(input.localDir)
                                                      : ProceduralBeachBallColorFromUv(input.texCoord);
        if (input.material0.a < -1.5f)
        {
            proceduralColor = 1.0f - proceduralColor;
        }
        materialColor = proceduralColor * input.material0.rgb;
    }
    float3 emissive = input.material2.rgb * max(input.material1.w, 0.0f);

    if (cinematicMode)
    {
        // The cinematic ball lighting is warmer and softer than the normal Phong
        // path: wrap light fills the shadow side, rim light outlines the silhouette,
        // and glint gives glossy sunset highlights.
        float warmWrap = saturate(dot(N, L) * 0.5f + 0.5f);
        float shadowFactor = ShadowVisibility(SphereShadowReceiverWorldPos(input.worldPos, input.sphereShadowInfo), N, L);
        float rim = pow(1.0f - saturate(dot(N, V)), 2.25f) * (0.35f + warmWrap * 0.65f);
        float glint = pow(max(dot(V, R), 0.0f), 72.0f);
        float3 warmAmbient = materialColor * uLightAmbient.rgb * 1.15f;
        float3 directSun = materialColor * uLightDiffuse.rgb * (diff * 0.62f + warmWrap * 0.18f) * shadowFactor;
        float3 rimLight = uLightDiffuse.rgb * rim * 0.18f;
        float3 specularSun = uLightDiffuse.rgb * glint * 0.30f * shadowFactor;
        float3 styled = ApplyMaterialMode(materialMode, materialColor, N, V, L, uLightDiffuse.rgb, diff, glint, input.texCoord);
        styled *= shadowFactor;
        float3 beachBall = warmAmbient + directSun + rimLight + specularSun;
        float3 color = (materialMode == 0 ? beachBall : styled + rimLight * 0.35f) + emissive;
        color = lerp(color, float3(1.0f, 1.0f, 1.0f), contactFlash);
        return float4(color, materialAlpha);
    }

    float shadowFactor = ShadowVisibility(SphereShadowReceiverWorldPos(input.worldPos, input.sphereShadowInfo), N, L);
    float3 litColor = OrdinaryMaterialBRDF(materialColor, emissive, roughness, metallic, materialSpecular, worldN, N, V, L, shadowFactor);
    litColor = lerp(litColor, float3(1.0f, 1.0f, 1.0f), contactFlash);
    return float4(litColor, materialAlpha);
}
