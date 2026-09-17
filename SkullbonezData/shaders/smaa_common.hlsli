// SMAA 1x High, official algorithm with the engine's bindless textures and clamp samplers.
cbuffer Uniforms : register(b0) { float4 uSmaaMetrics; };
cbuffer BindlessTextureIndices : register(b1) { uint4 _textureDescriptorIndices0; uint2 _textureDescriptorIndices1; };
SamplerState LinearSampler : register(s1);
SamplerState PointSampler : register(s3);
#define SMAA_RT_METRICS uSmaaMetrics
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL
// The texture uploader expands two-channel lookup bytes as luminance/alpha.
#define SMAA_AREATEX_SELECT(sample) sample.ra
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(LinearSampler, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(PointSampler, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(LinearSampler, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#include "../../ThirdPtySource/SMAA/SMAA.hlsl"
struct VS_IN { float2 position : POSITION; float2 texCoord : TEXCOORD0; };
struct VS_OUT { float4 position : SV_POSITION; float2 texCoord : TEXCOORD0; float2 pixcoord : TEXCOORD1; float4 offset[3] : TEXCOORD2; };
