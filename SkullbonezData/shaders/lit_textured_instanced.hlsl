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

// Pixel shader is identical to the non-instanced version (same Phong lighting).
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
    float3 materialColor = lerp(texColor.rgb * input.tint.rgb, input.tint.rgb, saturate(input.tint.a));
    float3 litColor = (ambient + diffuse) * materialColor + specular;
    return float4(litColor, 1.0);
}
