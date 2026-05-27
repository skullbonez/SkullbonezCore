#version 330 core

// =============================================================================
// SOLID COLOR BATCH FRAGMENT SHADER (solid_color_batch.frag)
// =============================================================================
//
// PURPOSE: Output the per-vertex colour interpolated across the quad.
//
// Because the geometry is always axis-aligned rectangles and there is no
// vertex at the centre, all four corners share the same colour and the
// interpolated result is flat — exactly what we want for profiler bar segments.
//
// Alpha is blended via glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
//
// =============================================================================

in vec4 vColor;

out vec4 FragColor;

void main()
{
    // Pass the per-vertex colour straight through.
    // The rasteriser has already interpolated it, but since all quad corners
    // carry the same colour, every pixel in the quad gets exactly vColor.
    FragColor = vColor;
}
