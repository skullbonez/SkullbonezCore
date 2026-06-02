#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uView;
    float4x4 uProjection;
    float4   uClipPlane;
    float4   uLightPosition;
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 model0   : TEXCOORD1;
    float4 model1   : TEXCOORD2;
    float4 model2   : TEXCOORD3;
    float4 model3   : TEXCOORD4;
    float4 color    : TEXCOORD5;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float  clipDist : SV_ClipDistance0;
    float3 viewPos  : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float4 color    : TEXCOORD2;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    float4x4 model = transpose(float4x4(input.model0, input.model1, input.model2, input.model3));
    float4x4 modelView = mul(uView, model);
    float4 viewPos = mul(modelView, float4(input.position, 1.0));

    output.position = mul(uProjection, viewPos);
    output.clipDist = dot(mul(model, float4(input.position, 1.0)), uClipPlane);
    output.viewPos = viewPos.xyz;
    output.normal = mul((float3x3)modelView, input.normal);
    output.color = input.color;

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    float3 v = normalize(-input.viewPos);

    float3 l;
    if (uLightPosition.w == 0.0)
        l = normalize(uLightPosition.xyz);
    else
        l = normalize(uLightPosition.xyz - input.viewPos);

    float diff = max(dot(n, l), 0.0);
    float3 r = reflect(-l, n);
    float spec = pow(max(dot(v, r), 0.0), 96.0);

    float3 base = input.color.rgb;
    float3 ambient = base * 0.22;
    float3 diffuse = base * diff * 0.72;
    float3 metallicSheen = float3(0.85, 0.92, 1.0) * spec * 0.38;

    return float4(ambient + diffuse + metallicSheen, input.color.a);
}
