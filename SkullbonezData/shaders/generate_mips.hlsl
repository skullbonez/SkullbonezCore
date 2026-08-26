/*
File: SkullbonezData/shaders/generate_mips.hlsl
Purpose:
  Compute shader for GPU-driven mipmap generation across texture mip levels.

Summary:
  Downsamples the source mip level into destination sublevels using bilinear
  reduction in compute threads, supporting both 2D textures and cubemaps during
  resource loading and asset preparation.

Invariants:
  - CPU-side root signatures and descriptor bindings must match this compute shader exactly.
  - Thread group dimensions (8x8) match standard texture downsampling tiles.
  - Source and destination texture formats and sRGB rules are preserved across levels.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp
*/

// =============================================================================
// GENERATE MIPMAPS COMPUTE SHADER — Shader Model 6.6
// =============================================================================
//
// One 8x8 thread group can write up to 4 consecutive mip levels. The CPU ends
// a batch before an odd intermediate dimension; that level becomes the next
// dispatch source so its final row or column is filtered instead of discarded.
// The first output mip is sampled from the SRV source (handles NPOT via
// multiple samples). Subsequent mips use group shared memory reduction.
//
// Root constants (b0): NumMipLevels, SrcDimension, TexelSizeX, TexelSizeY
//   SrcDimension bits: 0=src width odd, 1=src height odd
// SRV (t0): single-mip view of the source mip level (LinearClamp sampler)
// UAV (u0-u3): output mip levels (4 slots, unused ones padded with null UAVs)
//
// Reference: https://www.3dgep.com/learning-directx-12-4/#Generate_Mipmaps_Compute_Shader
// =============================================================================

cbuffer GenerateMipsCB : register(b0)
{
    uint  NumMipLevels;   // Number of output mips to generate this dispatch (1-4)
    uint  SrcDimension;   // Bit 0: src width is odd; Bit 1: src height is odd
    float TexelSizeX;     // 1.0 / OutMip1.Width
    float TexelSizeY;     // 1.0 / OutMip1.Height
};

SamplerState        LinearClampSampler : register(s0);
Texture2D<float4>   SrcMip             : register(t0);
RWTexture2D<float4> OutMip1            : register(u0);
RWTexture2D<float4> OutMip2            : register(u1);
RWTexture2D<float4> OutMip3            : register(u2);
RWTexture2D<float4> OutMip4            : register(u3);

// Group shared memory: one float4 per thread for inter-thread reduction.
groupshared float4 gs_Color[64];

void StoreColor(uint i, float4 c) { gs_Color[i] = c; }
float4 LoadColor(uint i)          { return gs_Color[i]; }

[numthreads(8, 8, 1)]
void main_cs(
    uint3 DispatchThreadID : SV_DispatchThreadID,
    uint  GroupIndex       : SV_GroupIndex )
{
    float2 TexelSize = float2(TexelSizeX, TexelSizeY);
    float4 Src1;

    // Sample the source mip. TexelSize = 1/OutMip1.Dimensions maps each
    // output texel to a 2x2 source region. The +0.5 centres the sample.
    // NPOT handling: when source dimension is odd, we need extra samples
    // to correctly cover the boundary region.
    switch (SrcDimension)
    {
        // Both even: single bilinear sample covers the exact 2x2 source region.
        case 0:
        {
            float2 UV = TexelSize * (float2(DispatchThreadID.xy) + 0.5);
            Src1 = SrcMip.SampleLevel(LinearClampSampler, UV, 0.0);
        }
        break;

        // Width odd: two samples offset by half a source texel in X, averaged.
        case 1:
        {
            float2 UV1 = TexelSize * (float2(DispatchThreadID.xy) + float2(0.25, 0.5));
            float2 Off = TexelSize * float2(0.5, 0.0);
            Src1 = 0.5 * (SrcMip.SampleLevel(LinearClampSampler, UV1, 0.0)
                        + SrcMip.SampleLevel(LinearClampSampler, UV1 + Off, 0.0));
        }
        break;

        // Height odd: two samples offset by half a source texel in Y, averaged.
        case 2:
        {
            float2 UV1 = TexelSize * (float2(DispatchThreadID.xy) + float2(0.5, 0.25));
            float2 Off = TexelSize * float2(0.0, 0.5);
            Src1 = 0.5 * (SrcMip.SampleLevel(LinearClampSampler, UV1, 0.0)
                        + SrcMip.SampleLevel(LinearClampSampler, UV1 + Off, 0.0));
        }
        break;

        // Both odd: four samples, averaged.
        default:
        {
            float2 UV1 = TexelSize * (float2(DispatchThreadID.xy) + float2(0.25, 0.25));
            float2 Off = TexelSize * 0.5;
            Src1  = SrcMip.SampleLevel(LinearClampSampler, UV1, 0.0);
            Src1 += SrcMip.SampleLevel(LinearClampSampler, UV1 + float2(Off.x, 0.0), 0.0);
            Src1 += SrcMip.SampleLevel(LinearClampSampler, UV1 + float2(0.0, Off.y), 0.0);
            Src1 += SrcMip.SampleLevel(LinearClampSampler, UV1 + Off, 0.0);
            Src1 *= 0.25;
        }
        break;
    }

    OutMip1[DispatchThreadID.xy] = Src1;
    if (NumMipLevels == 1) return;

    // Why: mip 2+ reductions can reuse the mip 1 color from group memory
    // instead of sampling the source texture again.
    StoreColor(GroupIndex, Src1);
    GroupMemoryBarrierWithGroupSync();

    // Mip 2: 4x4 active threads (even x AND even y within group).
    // GroupIndex layout: index = y*8+x. Mask 0x9 = 0b001001 checks bit0 (x even) and bit3 (y even).
    if ((GroupIndex & 0x9) == 0)
    {
        float4 Src2 = LoadColor(GroupIndex + 0x01); // (+1, 0)
        float4 Src3 = LoadColor(GroupIndex + 0x08); // (0, +1)
        float4 Src4 = LoadColor(GroupIndex + 0x09); // (+1, +1)
        Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);
        OutMip2[DispatchThreadID.xy / 2] = Src1;
        StoreColor(GroupIndex, Src1);
    }
    if (NumMipLevels == 2) return;
    GroupMemoryBarrierWithGroupSync();

    // Mip 3: 2x2 active threads (x%4==0 AND y%4==0 within group).
    // Mask 0x1B = 0b011011 checks bits 0,1 (x%4==0) and bits 3,4 (y%4==0).
    if ((GroupIndex & 0x1B) == 0)
    {
        float4 Src2 = LoadColor(GroupIndex + 0x02); // (+2, 0)
        float4 Src3 = LoadColor(GroupIndex + 0x10); // (0, +2)
        float4 Src4 = LoadColor(GroupIndex + 0x12); // (+2, +2)
        Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);
        OutMip3[DispatchThreadID.xy / 4] = Src1;
        StoreColor(GroupIndex, Src1);
    }
    if (NumMipLevels == 3) return;
    GroupMemoryBarrierWithGroupSync();

    // Mip 4: only thread (0,0) in group reads the 4 mip-3 values at stride 4.
    if (GroupIndex == 0)
    {
        float4 Src2 = LoadColor(0x04); // GroupIndex for thread (+4, 0)
        float4 Src3 = LoadColor(0x20); // GroupIndex for thread (0, +4)
        float4 Src4 = LoadColor(0x24); // GroupIndex for thread (+4, +4)
        Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);
        OutMip4[DispatchThreadID.xy / 8] = Src1;
    }
}
