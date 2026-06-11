// =============================================================================
// INSTANCED LIT TEXTURED SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Same as lit_textured.hlsl but with INSTANCED rendering for spheres.
// Instead of a single uModel matrix in the cbuffer, each instance provides its
// own 4×4 model matrix via per-instance vertex attributes (TEXCOORD1-4).
//
// --- Instancing in DirectX ---
//
//  DX instancing works similarly to OpenGL:
//  - Per-vertex data (position, normal, UV) is in one vertex buffer with step rate 0
//  - Per-instance data (model matrix, etc.) is in another buffer with step rate 1
//  - The input assembler automatically advances instance data every N vertices
//
//  The only difference from GL is the semantic labeling:
//  GLSL: layout(location = 3) in mat4 aModel;  (locations 3-6, divisor=1)
//  HLSL: float4 model0-3 : TEXCOORD1-4;        (per-instance step rate in input layout)
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
    int      uObjectStyle;
    int      uPrimitiveShape;
    float2   _objectStylePad;
    float4x4 uShadowViewProj;
    float4   uShadowParams;
    float4   uShadowFlags;
};

Texture2D    uTexture  : register(t0);
Texture2D    uShadowMap : register(t3);
SamplerState sSampler0 : register(s0);
SamplerState sSampler3 : register(s3);

struct VS_IN
{
    float3 position : POSITION;   // Per-vertex: object-space position
    float3 normal   : NORMAL;     // Per-vertex: surface normal
    float2 texCoord : TEXCOORD0;  // Per-vertex: UV
    float4 model0   : TEXCOORD1;  // Per-instance: model matrix column 0
    float4 model1   : TEXCOORD2;  // Per-instance: model matrix column 1
    float4 model2   : TEXCOORD3;  // Per-instance: model matrix column 2
    float4 model3   : TEXCOORD4;  // Per-instance: model matrix column 3
    float4 tint     : TEXCOORD5;  // Per-instance: RGB tint + color override amount
};

struct VS_OUT
{
    float4 position  : SV_POSITION;
    float  clipDist  : SV_ClipDistance0;
    float3 viewPos   : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texCoord  : TEXCOORD2;
    float4 tint      : TEXCOORD3;
    float3 worldPos  : TEXCOORD4;
    nointerpolation float4 sphereShadowInfo : TEXCOORD5;
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
    output.texCoord = input.texCoord;
    output.tint     = input.tint;
    output.worldPos = worldPos.xyz;
    float3 sphereCenter = mul(model, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
    float3 sphereAxis = mul(model, float4(1.0f, 0.0f, 0.0f, 1.0f)).xyz - sphereCenter;
    output.sphereShadowInfo = float4(sphereCenter, length(sphereAxis));

    return output;
}

// Cinematic scenes use a procedural beach-ball color so the red/yellow panels stay crisp.
float3 ProceduralBeachBallColor(float2 uv)
{
    // The original source texture is intentionally low-res, which becomes blurry
    // when a ball is close to the camera. The beachball material now uses mesh
    // UVs plus shader math for the red/yellow panels in every render mode, so the
    // edges stay razor crisp at any resolution.
    uv = frac(uv);

    // uv.x is the wrap around the object. Multiplying by 2 creates two vertical
    // panels around the circumference. uv.y chooses the top/bottom half, and the
    // hemisphere offset flips the color order so the total object reads as two
    // red panels and two yellow panels, not a repeated 4-and-4 pattern.
    float longitudePanel = floor(uv.x * 2.0f);
    float hemisphere = step(0.5f, uv.y);
    float redPanel = fmod(longitudePanel + hemisphere, 2.0f);
    float3 yellow = float3(1.0f, 0.78f, 0.0f);
    float3 red = float3(0.96f, 0.06f, 0.0f);
    float3 color = lerp(yellow, red, redPanel);

    // Add a narrow warm seam on panel borders. It is still generated from UVs,
    // so it stays sharp and does not depend on the texture image resolution.
    float panelCoord = frac(uv.x * 2.0f);
    float seamU = min(panelCoord, 1.0f - panelCoord);
    float seamV = abs(uv.y - 0.5f);
    float seam = 1.0f - smoothstep(0.0f, 0.014f, min(seamU, seamV));
    return lerp(color, float3(1.0f, 0.62f, 0.02f), seam * 0.28f);
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

    float ndotl = max(dot(normalize(normalView), normalize(lightView)), 0.0f);
    float bias = uShadowParams.y + uShadowParams.z * (1.0f - ndotl);
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

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 V = normalize(-input.viewPos);

    float3 L;
    if (uLightPosition.w == 0.0)
        L = normalize(uLightPosition.xyz);
    else
        L = normalize(uLightPosition.xyz - input.viewPos);

    float3 ambient = uLightAmbient.rgb * uMaterialAmbient.rgb;

    float diff = max(dot(N, L), 0.0);
    float3 diffuse = uLightDiffuse.rgb * uMaterialDiffuse.rgb * diff;

    float3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 64.0);
    float3 specular = uLightDiffuse.rgb * spec * 0.1;

    bool cinematicMode = uLightPosition.w == 0.0f;
    int materialMode;
    if (input.tint.a < -0.5f)
        materialMode = 0;
    else if (input.tint.a > 1.25f)
        materialMode = (int)floor(input.tint.a + 0.5f);
    else if (input.tint.a > 0.5f)
        materialMode = 1;
    else
        materialMode = uObjectStyle;

    float3 materialColor = input.tint.rgb;
    if (materialMode == 0)
    {
        materialColor = ProceduralBeachBallColor(input.texCoord) * input.tint.rgb;
    }

    if (cinematicMode)
    {
        // The cinematic ball lighting is warmer and softer than the normal Phong
        // path: wrap light fills the shadow side, rim light outlines the silhouette,
        // and glint gives glossy sunset highlights.
        float warmWrap = saturate(dot(N, L) * 0.5f + 0.5f);
        float shadowFactor = ShadowVisibility(SphereShadowReceiverWorldPos(input.worldPos, input.sphereShadowInfo), N, L);
        float rim = pow(1.0f - saturate(dot(N, V)), 2.25f) * (0.35f + warmWrap * 0.65f);
        float glint = pow(max(dot(V, R), 0.0f), 96.0f);
        float3 warmAmbient = materialColor * uLightAmbient.rgb * 1.15f;
        float3 directSun = materialColor * uLightDiffuse.rgb * (diff * 0.62f + warmWrap * 0.18f) * shadowFactor;
        float3 rimLight = uLightDiffuse.rgb * rim * 0.18f;
        float3 specularSun = uLightDiffuse.rgb * glint * 0.24f * shadowFactor;
        float3 styled = ApplyMaterialMode(materialMode, materialColor, N, V, L, uLightDiffuse.rgb, diff, glint, input.texCoord);
        styled *= shadowFactor;
        float3 beachBall = warmAmbient + directSun + rimLight + specularSun;
        return float4(materialMode == 0 ? beachBall : styled + rimLight * 0.35f, 1.0f);
    }

    float shadowFactor = ShadowVisibility(SphereShadowReceiverWorldPos(input.worldPos, input.sphereShadowInfo), N, L);
    float3 litColor = materialMode == 0 ? (ambient + diffuse) * materialColor + specular
                                        : ApplyMaterialMode(materialMode, materialColor, N, V, L, uLightDiffuse.rgb, diff, spec, input.texCoord);
    litColor *= shadowFactor;
    return float4(litColor, 1.0);
}
