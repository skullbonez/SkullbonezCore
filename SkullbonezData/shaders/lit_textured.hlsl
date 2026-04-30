// Phong lighting + texture shader (combined HLSL)
// Vertex and pixel shader entry points

cbuffer TransformBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4 clipPlane;
};

cbuffer LightingBuffer : register(b1)
{
    float4 lightPosition;
    float4 lightAmbient;
    float4 lightDiffuse;
    float4 materialAmbient;
    float4 materialDiffuse;
};

Texture2D mainTexture : register(t0);
SamplerState mainSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 viewPos : TEXCOORD1;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
};

PS_INPUT main_vs(VS_INPUT input)
{
    PS_INPUT output;

    float4x4 modelView = mul(view, model);
    float4 viewPos = mul(float4(input.position, 1.0f), modelView);
    output.position = mul(viewPos, projection);

    output.viewPos = viewPos.xyz;
    output.normal = mul(input.normal, (float3x3)modelView);
    output.texCoord = input.texCoord;

    return output;
}

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 V = normalize(-input.viewPos);

    float3 L;
    if (lightPosition.w == 0.0f)
        L = normalize(lightPosition.xyz);
    else
        L = normalize(lightPosition.xyz - input.viewPos);

    float3 ambient = lightAmbient.rgb * materialAmbient.rgb;

    float diff = max(dot(N, L), 0.0f);
    float3 diffuse = lightDiffuse.rgb * materialDiffuse.rgb * diff;

    float3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0f), 64.0f);
    float3 specular = lightDiffuse.rgb * spec * 0.1f;

    float4 texColor = mainTexture.Sample(mainSampler, input.texCoord);
    return float4((ambient + diffuse) * texColor.rgb + specular, 1.0f);
}
