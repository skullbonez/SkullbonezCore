/*
File: SkullbonezData/shaders/grid_line.hlsl
Purpose:
  Runs the grid_line HLSL shader program used by the renderer.

Mental model:
  Shaders are GPU programs. Constant buffers provide per-frame data, shader
  stages transform or shade inputs, and CPU-side renderer bindings must match
  the declarations in this file.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  HLSL (High Level Shader Language): Shader language compiled for Direct3D
  render, compute, and raytracing stages.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - CPU-side root signatures, input layouts, and descriptor bindings must
  match this shader exactly.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
// =============================================================================
// GRID LINE SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Draw per-vertex colored line segments in 3D world space.
// This is the canonical DX12 grid-line shader.
//
// Used by the broadphase spatial grid visualizer to render cell boundaries
// with colors indicating occupancy and collision state.
//
// Vertex layout: [position (float3), color (float3)] = 6 floats per vertex.
// Topology: LINE_LIST (pairs of vertices form independent line segments).
//
// Cell color encoding:
//  - White (1,1,1):  empty cell
//  - Yellow (1,1,0): ball just entered (fading to blue)
//  - Blue (0,0,1):   occupied (steady state)
//  - Red→Black:      active collision (intensity deepens with collision count)
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uViewProj;  // Combined view-projection matrix (world → clip space)
};

struct VS_IN
{
    float3 position : POSITION;   // World-space line endpoint
    float3 color    : TEXCOORD0;  // Per-vertex RGB color
};

struct VS_OUT
{
    float4 position : SV_POSITION;  // Clip-space output
    float3 color    : COLOR0;       // Interpolated color for fragment shader
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    // Transform world-space position to clip space via the combined view-projection matrix.
    output.position = mul(uViewProj, float4(input.position, 1.0));
    // Pass per-vertex color through for interpolation along the line segment.
    output.color = input.color;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Output the interpolated vertex color at full opacity.
    return float4(input.color, 1.0);
}
