#version 330 core

// =============================================================================
// SOLID COLOR VERTEX SHADER (solid_color.vert)
// =============================================================================
//
// PURPOSE: Position flat 2D quads on screen using orthographic projection.
// Used for HUD background panels (the dark rectangles behind FPS text, etc).
//
// --- How HUD Backgrounds Work ---
//
//  The engine draws semi-transparent black rectangles behind text so the text
//  is always readable regardless of the 3D scene behind it:
//
//  Screen:
//  +--------------------------------------------------+
//  | [3D Scene visible here]                          |
//  |                                                  |
//  |  +-------------------------------+               |
//  |  | FPS: 60.0  Balls: 5          |  <- dark rect |
//  |  +-------------------------------+               |
//  +--------------------------------------------------+
//
//  These quads are positioned in 2D pixel coordinates and use orthographic
//  projection (no perspective distortion — pixel-exact placement).
//
// =============================================================================

layout(location = 0) in vec2 aPosition;

uniform mat4 uProjection;

void main()
{
    // Transform 2D pixel coordinates to clip space via orthographic projection.
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
