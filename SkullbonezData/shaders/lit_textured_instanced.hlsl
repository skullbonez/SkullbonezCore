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
};

Texture2D    uTexture  : register(t0);
SamplerState sSampler0 : register(s0);

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
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // Reconstruct 4×4 model matrix from per-instance columns.
    // float4x4() takes ROW arguments, but we pass COLUMNs → transpose fixes it.
    float4x4 model = transpose(float4x4(input.model0, input.model1, input.model2, input.model3));

    float4x4 modelView = mul(uView, model);
    float4 viewPos     = mul(modelView, float4(input.position, 1.0));
    output.position    = mul(uProjection, viewPos);

    output.clipDist = dot(mul(model, float4(input.position, 1.0)), uClipPlane);

    output.viewPos  = viewPos.xyz;
    output.normal   = mul((float3x3)modelView, input.normal);
    output.texCoord = input.texCoord;
    output.tint     = input.tint;

    return output;
}

// Cinematic scenes use a procedural beach-ball color so the red/yellow panels stay crisp.
float3 ProceduralBeachBallColor(float2 uv)
{
    // The original source texture is intentionally low-res, which becomes blurry
    // when a ball is close to the camera. In cinematic mode we still use the mesh
    // UVs, but we choose the red/yellow color in shader math so the edges stay
    // razor crisp at any resolution.
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

    float4 texColor = uTexture.Sample(sSampler0, input.texCoord);
    bool cinematicMode = uLightPosition.w == 0.0f;
    if (uLightPosition.w == 0.0f)
    {
        // Directional light means cinematic sun mode, so replace the sampled
        // texture with the procedural red/yellow panel color described above.
        texColor.rgb = ProceduralBeachBallColor(input.texCoord);
    }
    float3 materialColor = lerp(texColor.rgb * input.tint.rgb, input.tint.rgb, saturate(input.tint.a));

    if (cinematicMode)
    {
        // The cinematic ball lighting is warmer and softer than the normal Phong
        // path: wrap light fills the shadow side, rim light outlines the silhouette,
        // and glint gives glossy sunset highlights.
        float warmWrap = saturate(dot(N, L) * 0.5f + 0.5f);
        float rim = pow(1.0f - saturate(dot(N, V)), 2.25f) * (0.35f + warmWrap * 0.65f);
        float glint = pow(max(dot(V, R), 0.0f), 96.0f);
        float3 warmAmbient = materialColor * uLightAmbient.rgb * 1.15f;
        float3 directSun = materialColor * uLightDiffuse.rgb * (diff * 0.62f + warmWrap * 0.18f);
        float3 rimLight = uLightDiffuse.rgb * rim * 0.18f;
        float3 specularSun = uLightDiffuse.rgb * glint * 0.24f;
        return float4(warmAmbient + directSun + rimLight + specularSun, 1.0f);
    }

    float3 litColor = (ambient + diffuse) * materialColor + specular;
    return float4(litColor, 1.0);
}
