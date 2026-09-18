// One instance is a rooted surface patch. Its four world-field samples are
// bilinearly interpolated on the GPU; eight rooted crowns each carry four leaves.
#pragma pack_matrix(column_major)
#include "procedural_turf.hlsli"
cbuffer Uniforms : register(b0)
{
    float4x4 uViewProj;
};
struct VS_IN
{
    float3 root : POSITION;
    float4 normalAndSpacing : TEXCOORD0;
    float4 compression : TEXCOORD1;
    float2 bend : TEXCOORD2;
    float3 shape : TEXCOORD3; // height, width, continuous density/LOD coverage
    float3 light : TEXCOORD4;
    float4 heights : TEXCOORD5;
    float3 lightTint : TEXCOORD6;
    float4 heightsSecond : TEXCOORD7;
};
struct VS_OUT
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
    float coverage : TEXCOORD0;
};
uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float Random01(uint value) { return (Hash(value) & 65535u) / 65535.0; }
VS_OUT main_vs(VS_IN input, uint vertexId : SV_VertexID)
{
    uint leaf = vertexId / 12;
    uint blade = leaf / 4;
    uint fan = leaf % 4;
    uint segment = (vertexId % 12) / 6;
    uint corner = vertexId % 6;
    float endpoint = (corner == 2 || corner == 4 || corner == 5) ? 1.0 : 0.0;
    float side = (corner == 1 || corner == 2 || corner == 4) ? 1.0 : -1.0;
    float t = (segment + endpoint) / 2.0;
    uint seed = Hash(asuint(input.root.x) ^ Hash(asuint(input.root.z)) ^ (blade * 131u));
    float2 uv = (float2(blade & 3u, blade >> 2u) + 0.1 + 0.8 * float2(Random01(seed), Random01(seed + 17u))) / float2(4,2);
    float3 normal = normalize(input.normalAndSpacing.xyz);
    float3 tangent = normalize(float3(1,0,0) - normal * normal.x);
    float3 bitangent = cross(tangent, normal);
    float2 horizontalOffset = uv * input.normalAndSpacing.w;
    // CPU samples these exact seeded roots on the real terrain. This remains
    // correct across triangle edges and nonintegral heightfield grid spacing.
    float rootY = blade < 4 ? input.heights[blade] : input.heightsSecond[blade-4];
    float3 root = float3(input.root.x+horizontalOffset.x,rootY,input.root.z+horizontalOffset.y);
    float compression = lerp(lerp(input.compression.x, input.compression.y, uv.x),
                             lerp(input.compression.z, input.compression.w, uv.x), uv.y);
    uint leafSeed = Hash(seed + fan * 977u);
    float angle = (fan + Random01(seed + 47u)) * 1.57079633;
    float3 across = tangent * cos(angle) + bitangent * sin(angle);
    float3 direction = float3(input.bend.x, 0, input.bend.y);
    direction -= normal * dot(direction, normal);
    float coverage = saturate(input.shape.z * 8.0 - blade);
    // Distant crowns represent a wider turf clump instead of subpixel leaves.
    float height = input.shape.x * (0.78 + Random01(leafSeed + 83u) * 0.35) * coverage;
    // Quadratic tip displacement keeps roots fixed and creates a curved blade.
    float3 curve = normal * (height * t * (1.0 - 0.88 * compression * t));
    curve += direction * (height * compression * t * t * 0.95);
    float3 naturalLean = (tangent*sin(angle)+bitangent*cos(angle)) * .30;
    naturalLean += tangent * (TurfMowBand(root.xz) * 2.0 - 1.0) * .07;
    curve += naturalLean * height * t*t * (1.0-compression);
    float width = input.shape.y * (1.0 - t * 0.78) * coverage;
    float3 position = root + curve + across * side * width;
    float3 derivative = normal * (1.0 - 1.76 * compression * t) + direction * (1.9 * compression * t);
    derivative += naturalLean * (2.0 * t * (1.0-compression));
    float3 normalCross = cross(across, derivative);
    float normalLengthSquared = dot(normalCross,normalCross);
    float3 deformedNormal = normalLengthSquared > 1e-10 ? normalCross * rsqrt(max(normalLengthSquared,1e-10)) : normal;
    float lighting = 0.70 + 0.30 * abs(dot(deformedNormal, normalize(input.light)));
    VS_OUT output;
    output.position = mul(uViewProj, float4(position, 1.0));
    output.color = TurfColor(root.xz) * lerp(0.38, 1.28, sqrt(t)) * lighting;
    output.color *= (0.86 + Random01(leafSeed + 101u) * 0.28) * TurfLightTint(input.lightTint);
    output.coverage = coverage;
    return output;
}
float4 main_ps(VS_OUT input) : SV_TARGET
{
    clip(input.coverage - 0.001);
    return float4(input.color, 1.0);
}
