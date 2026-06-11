#version 330 core

// =============================================================================
// SHADOW DISC FRAGMENT SHADER (shadow.frag)
// =============================================================================
//
// PURPOSE: Cut a circular disc out of the shadow quad and apply a soft radial fade.
//
// --- How the Disc Is Formed ---
//
//  The geometry is a full square quad. The disc shape is created here by
//  discarding any fragment whose UV coordinate falls outside the unit circle:
//
//      dist = length(vUV)   [vUV is XZ in -1..1 space]
//      if (dist > 1.0) discard
//
//  Everything inside becomes a dark blended shadow; everything outside is gone.
//
//       Outside (discard)
//          ___
//         /   \
//        | kept|    <-- disc, radius = 1.0 in quad space
//         \___/
//       Outside (discard)
//
// --- Contact Fade ---
//
//  Alpha has a compact contact core plus a much softer outer skirt. This keeps
//  objects grounded without creating huge muddy pools under stylized scenes.
//
//  The result is blended with the scene:
//    Final = Black * alpha + Scene * (1 - alpha)
//           = Scene darkened proportionally
//
// --- Shoreline Behaviour ---
//
//  Shadow discs render with depth writes disabled (SetDepthWrite(false)).
//  This ensures the water surface — rendered after shadows — always passes
//  the depth test and is never occluded by shadow geometry that sits above
//  the water plane near shorelines.
//
// Docs: https://www.khronos.org/opengl/wiki/Fragment_Shader
// Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/discard.xhtml
//
// =============================================================================

in vec2  vUV;      // XZ disc coords in [-1,1]
in float vAlpha;   // base opacity from instance data (height-based)

out vec4 FragColor;

void main()
{
    float dist = length( vUV );

    // Discard fragments outside the unit circle — turns the square quad into a disc.
    if ( dist > 1.0 )
    {
        discard;
    }

    // A tight core grounds the object; the outer skirt fades quickly so the
    // shadow reads as contact instead of a broad black stain.
    float contact = 1.0 - smoothstep( 0.0, 0.24, dist );
    float skirt = ( 1.0 - smoothstep( 0.20, 0.92, dist ) ) * 0.16;
    float alpha = vAlpha * ( contact * 0.36 + skirt );

    // A cool green-brown tint reads softer than pure black in pastel scenes.
    FragColor = vec4( 0.035, 0.060, 0.025, alpha );
}
