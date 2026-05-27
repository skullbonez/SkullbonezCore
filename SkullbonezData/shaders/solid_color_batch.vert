#version 330 core

// =============================================================================
// SOLID COLOR BATCH VERTEX SHADER (solid_color_batch.vert)
// =============================================================================
//
// PURPOSE: Position 2D quads that each carry their own per-vertex RGBA colour,
// allowing an entire frame's worth of differently-coloured quads to be batched
// into a single draw call.
//
// --- Why per-vertex colour? ---
//
//  The original solid_color.vert uses a cbuffer/uniform for the colour, so one
//  draw call is needed per distinct colour.  For the profiler bar overlay that
//  can mean 30+ draw calls (background + N coloured segments + legend swatches).
//
//  By baking RGBA into the vertex stream every quad gets its own colour at no
//  extra overhead, reducing the entire overlay to exactly one draw call for all
//  quads (plus a separate text draw call).
//
// --- Vertex layout ---
//
//  Floats per vertex: 6
//    attrib 0 (vec2) — 2D position in frustum-unit space (same as text/solid_color)
//    attrib 1 (vec4) — RGBA colour [0, 1] per vertex
//
// =============================================================================

layout(location = 0) in vec2 aPosition;  // 2D screen-space position
layout(location = 1) in vec4 aColor;     // Per-vertex RGBA — baked at batch-build time

uniform mat4 uProjection;  // Orthographic projection (same matrix used by solid_color + text)

out vec4 vColor;

void main()
{
    // Orthographic transform: maps frustum-unit coordinates to clip space.
    // Z = 0 (flat on screen), W = 1 (no perspective division).
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
    vColor = aColor;
}
