// Two independently rendered views share one sample grid. Difference mode
// compares their unmodified RGB values before any operator overlay is drawn.
cbuffer Uniforms : register(b0) { float4 uPair; float4 uTexel; };
cbuffer BindlessTextureIndices : register(b1)
{
    uint4 _textureDescriptorIndices0;
    uint2 _textureDescriptorIndices1;
};
SamplerState sClamp : register(s1);
struct Vertex { float2 position : POSITION; float2 uv : TEXCOORD0; };
struct Pixel { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Pixel main_vs(Vertex input)
{
    Pixel output;
    output.position = float4(input.position, 0, 1);
    output.uv = input.uv;
    return output;
}
float4 main_ps(Pixel input) : SV_TARGET
{
    Texture2D<float4> first = ResourceDescriptorHeap[_textureDescriptorIndices0.x];
    Texture2D<float4> second = ResourceDescriptorHeap[_textureDescriptorIndices0.y];
    Texture2D<float4> outline = ResourceDescriptorHeap[_textureDescriptorIndices1.x];
    float2 uv = input.uv;
    int mode = (int)uPair.x;
    if (mode == 0)
    {
        if (uTexel.z > 0.5)
            return float4(uv.y < 0.5 ? first.Sample(sClamp, float2(uv.x, uv.y * 2)).rgb
                                    : second.Sample(sClamp, float2(uv.x, (uv.y - 0.5) * 2)).rgb, 1);
        return float4(uv.x < 0.5 ? first.Sample(sClamp, float2(uv.x * 2, uv.y)).rgb
                                : second.Sample(sClamp, float2((uv.x - 0.5) * 2, uv.y)).rgb, 1);
    }
    float3 a = first.Sample(sClamp, uv).rgb;
    float3 b = second.Sample(sClamp, uv).rgb;
    if (mode == 2) return float4(a, 1);
    if (mode == 3) return float4(b, 1);
    if (mode == 4) return float4(saturate(abs(a - b) * uPair.y), 1);
    // Geometric edges already checked both depths in their own shader. Their
    // antialiased fringe may extend beyond A's silhouette into background pixels.
    float coverage = outline.Sample(sClamp, uv).r;
    return float4(lerp(b, float3(0.1, 0.9, 1), coverage * uPair.z), 1);
}
