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
};

// VERTEX SHADER: transform vertices and prepare lighting data.
VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // Combine Model and View into one matrix, then transform position to view space.
    float4x4 modelView = mul(uView, uModel);
    float4 viewPos     = mul(modelView, float4(input.position, 1.0));
    // Apply perspective projection to get final screen position.
    output.position    = mul(uProjection, viewPos);

    // Clip distance: dot product with clip plane in WORLD space.
    // Positive = keep fragment; Negative = discard (used for water reflection clipping).
    output.clipDist = dot(mul(uModel, float4(input.position, 1.0)), uClipPlane);

    // Pass view-space position and transformed normal for per-pixel lighting.
    output.viewPos  = viewPos.xyz;
    output.normal   = mul((float3x3)modelView, input.normal);
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
    return float4((ambient + diffuse) * texColor.rgb + specular, 1.0);
}

