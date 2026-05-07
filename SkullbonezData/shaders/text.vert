#version 330 core

// =============================================================================
// TEXT VERTEX SHADER (text.vert)
// =============================================================================
//
// PURPOSE: Position 2D text quads on screen using an orthographic projection.
//
// --- How Font Rendering Works ---
//
//  1. At init, all 96 ASCII characters are rasterized into a single "font atlas"
//     texture (a big image containing every letter arranged in a grid).
//
//  2. For each character to display, we create a quad (2 triangles = 4 vertices)
//     positioned at the right screen location with UV coordinates pointing at
//     that character's rectangle in the atlas.
//
//  Font Atlas Texture (one big image):
//  +---+---+---+---+---+---+---+---+
//  | ! | " | # | $ | % | & | ' | ( |
//  +---+---+---+---+---+---+---+---+
//  | A | B | C | D | E | F | G | H |
//  +---+---+---+---+---+---+---+---+
//  | a | b | c | d | e | f | g | h |
//  +---+---+---+---+---+---+---+---+
//
//  To render "Hi":
//  - Create quad at (x=100, y=50) with UVs pointing at 'H' in atlas
//  - Create quad at (x=108, y=50) with UVs pointing at 'i' in atlas
//
// --- Orthographic Projection ---
//
//  Unlike 3D objects that use perspective projection (distant = smaller),
//  text uses ORTHOGRAPHIC projection (no perspective — things are pixel-exact).
//  This makes text always appear at the exact pixel position we specify.
//
// =============================================================================

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uProjection;

out vec2 vTexCoord;

void main()
{
    // Transform 2D position using orthographic projection matrix.
    // The Z is 0.0 (flat on the screen), W is 1.0 (homogeneous coordinate).
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
    vTexCoord   = aTexCoord;
}
