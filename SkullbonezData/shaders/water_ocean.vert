#version 330 core

// =============================================================================
// OCEAN WATER VERTEX SHADER (water_ocean.vert)
// =============================================================================
//
// PURPOSE: Animate the OUTER water surface with sine-wave displacement.
// This creates the rolling ocean effect for the distant water beyond the terrain.
//
// --- Wave Animation Technique ---
//
//  Each vertex is pushed up/down on the Y axis using layered sine waves:
//
//  Wave 1: sin(x * 0.04 + time * 1.2) * height       — primary swell along X
//  Wave 2: sin(z * 0.06 + time * 0.8) * height * 0.667 — secondary chop along Z
//
//  Side view (showing vertex displacement over time):
//
//  height ─┐
//           │    .·'·.       .·'·.
//    0 ─────┼──'───────'───'───────'───── flat water level
//           │                             
// -height ─┘
//
//  The two waves have different frequencies (0.04 vs 0.06) and speeds (1.2 vs 0.8),
//  which creates an organic, non-repeating look as they drift out of sync.
//
// --- uFlatWater Debug Toggle ---
//
//  When uFlatWater=1 (toggled by debug key 3), displacement is suppressed
//  and the surface renders flat — useful for debugging reflection alignment.
//
// --- Reflection Coordinates ---
//
//  Note: vReflectClipPos uses the ORIGINAL (un-displaced) vertex position.
//  This is intentional — we want the reflection UV perturbation in the fragment
//  shader to be the only source of "shimmer", not the vertex movement.
//
// =============================================================================

layout(location = 0) in vec3 aPosition;

uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProjection;
uniform mat4  uReflectVP;    // Reflection camera View-Projection
uniform float uTime;         // Elapsed time in seconds (drives animation)
uniform float uWaveHeight;   // Wave amplitude from engine.cfg ocean_wave_height
uniform int   uFlatWater;    // 1 = suppress displacement (debug key 3)

out vec4 vReflectClipPos;
out vec2 vWorldXZ;

void main()
{
    vec3 pos = aPosition;
    if (uFlatWater == 0)
    {
        // Layer two sine waves with different frequencies and speeds for organic motion.
        pos.y += sin(pos.x * 0.04 + uTime * 1.2) * uWaveHeight
               + sin(pos.z * 0.06 + uTime * 0.8) * (uWaveHeight * 0.667);
    }

    // Position the displaced vertex on screen.
    gl_Position     = uProjection * uView * uModel * vec4(pos, 1.0);
    // Reflection uses un-displaced position (shimmer comes from frag shader instead).
    vReflectClipPos = uReflectVP  * uModel * vec4(aPosition, 1.0);
    // Pass world XZ for fragment shader's UV perturbation calculation.
    vWorldXZ        = aPosition.xz;
}
