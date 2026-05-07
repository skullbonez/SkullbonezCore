#version 330 core

// =============================================================================
// SHADOW DISC FRAGMENT SHADER (shadow.frag)
// =============================================================================
//
// PURPOSE: Output a flat black color with variable alpha for soft shadow edges.
//
// The vertex shader calculates per-vertex alpha based on:
//  - Distance from disc center (edge fade)
//  - Height of the object above ground (distant objects cast weaker shadows)
//
// This shader just passes that alpha through as the output opacity.
// The result is blended with the scene behind it using alpha blending:
//   Final = Shadow.rgb * Shadow.a + Scene.rgb * (1 - Shadow.a)
//         = Black * alpha + Scene * (1 - alpha)
//         = Scene darkened by alpha amount
//
// =============================================================================

in float vAlpha;

out vec4 FragColor;

void main()
{
    // Pure black with variable opacity = darkens whatever is underneath.
    FragColor = vec4(0.0, 0.0, 0.0, vAlpha);
}
