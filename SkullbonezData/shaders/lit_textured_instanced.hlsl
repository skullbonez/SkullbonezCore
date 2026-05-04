// Phong lighting + texture: instanced shader (HLSL 5.0, combined VS+PS)
// Per-instance model matrix arrives via vertex attributes.
// Column-major matrices — uploaded directly from engine Matrix4 without transposing.

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
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 model0   : TEXCOORD1;
    float4 model1   : TEXCOORD2;
    float4 model2   : TEXCOORD3;
    float4 model3   : TEXCOORD4;
};

struct VS_OUT
{
    float4 position  : SV_POSITION;
    float  clipDist  : SV_ClipDistance0;
    float3 viewPos   : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texCoord  : TEXCOORD2;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // Instance data arrives as columns (matching GL convention) but float4x4()
    // assembles them as rows — transpose to get the correct orientation.
    float4x4 model = transpose(float4x4(input.model0, input.model1, input.model2, input.model3));

    float4x4 modelView = mul(uView, model);
    float4 viewPos     = mul(modelView, float4(input.position, 1.0));
    output.position    = mul(uProjection, viewPos);

    output.clipDist = dot(mul(model, float4(input.position, 1.0)), uClipPlane);

    output.viewPos  = viewPos.xyz;
    output.normal   = mul((float3x3)modelView, input.normal);
    output.texCoord = input.texCoord;

    return output;
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
    return float4((ambient + diffuse) * texColor.rgb + specular, 1.0);
}
