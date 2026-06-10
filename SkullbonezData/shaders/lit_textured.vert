#version 330 core

// =============================================================================
// LIT TEXTURED VERTEX SHADER (lit_textured.vert)
// =============================================================================
//
// PURPOSE: Transform each vertex from 3D model space → screen space, and prepare
// data for per-pixel lighting in the fragment shader.
//
// --- What this shader does in the rendering pipeline ---
//
//  3D Model Space       View (Camera) Space         Screen Space (2D)
//  (object local)       (camera-relative)           (final pixels)
//      |                       |                         |
//      |   uModel matrix       |   uView matrix          |  uProjection matrix
//      |   (position/rotate    |   (camera look-at)      |  (perspective/FOV)
//      |    the object)        |                         |
//      v                       v                         v
//  [aPosition] -----> [world pos] -----> [viewPos] -----> [gl_Position]
//
// The vertex shader runs ONCE PER VERTEX (e.g. 7500 times for a 25×25 sphere).
// Its job is to:
//   1. Transform position: model space → clip space (gl_Position)
//   2. Transform normal: rotate to match camera orientation (for lighting)
//   3. Pass texture coordinates through unchanged
//   4. Calculate clip distance (for water reflection clipping)
//
// --- Clip Distance (Water Reflection) ---
//
//  When rendering the reflection pass for water, we need to clip everything
//  BELOW the water surface. The clip plane (0, 1, 0, -waterHeight) means:
//  "keep vertices where dot(worldPos, plane) >= 0" (i.e. above the water).
//
//       Camera
//         |
//   ------+------------ water surface (y = waterHeight)
//         |
//    (CLIPPED - discarded by GPU when gl_ClipDistance[0] < 0)
//
// =============================================================================

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uClipPlane;    // world-space clip plane; default (0,1,0,1e9) = always pass
uniform vec4 uCinematicTerrain; // enable, relief, basin depth, rim lift
uniform vec4 uCinematicBasin;   // center x/z, radius x/z

out vec3 vViewPos;
out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vTexCoord;

float BasinDistance(vec2 xz)
{
    // Normalize world x/z into an oval basin space. A return value near 0 is the
    // basin center; near 1 is the rim. This is only used for the cinematic visual
    // deformation, not for physics collision.
    vec2 radius = max(uCinematicBasin.zw, vec2(1.0));
    return length((xz - uCinematicBasin.xy) / radius);
}

float CinematicTerrainOffset(vec2 xz)
{
    if (uCinematicTerrain.x < 0.5 || uCinematicTerrain.y <= 0.0)
    {
        // Relief defaults to off. Returning 0 here means the rendered terrain
        // matches the real terrain exactly.
        return 0.0;
    }

    // This is a visual-only morph. The vertex shader lowers the middle of an
    // oval basin, raises a soft rim, and adds a little roughness on the slopes.
    // The terrain mesh/physics data on the CPU are not changed by this.
    float d = BasinDistance(xz);
    float bowl = 1.0 - smoothstep(0.10, 0.94, d);
    float rim = exp(-pow((d - 1.04) * 3.1, 2.0));
    float slopeTexture = smoothstep(0.32, 0.92, d) * (1.0 - smoothstep(1.02, 1.55, d));
    float rough = (sin(xz.x * 0.045 + xz.y * 0.011) + sin(xz.y * 0.052 - xz.x * 0.017)) * 0.5;
    return uCinematicTerrain.y * (-uCinematicTerrain.z * bowl + uCinematicTerrain.w * rim + rough * 1.6 * slopeTexture);
}

void main()
{
    mat4 modelView = uView * uModel;
    vec4 worldPos  = uModel * vec4(aPosition, 1.0);
    worldPos.y += CinematicTerrainOffset(worldPos.xz);
    vec4 viewPos   = uView * worldPos;

    // Apply the projection matrix to get clip space coordinates.
    // The GPU uses this to determine where on the 2D screen this vertex appears.
    // Perspective projection makes distant objects appear smaller (vanishing point effect).
    gl_Position    = uProjection * viewPos;

    // Clip distance: the GPU discards fragments where this is negative.
    // We transform the vertex to world space and dot it with the clip plane equation.
    // Positive = above the plane (keep), negative = below the plane (discard).
    gl_ClipDistance[0] = dot(worldPos, uClipPlane);

    // Pass view-space position to fragment shader (used for light direction calculation).
    vViewPos  = viewPos.xyz;
    vWorldPos = worldPos.xyz;

    // Transform the normal vector. We use the "normal matrix" (transpose of inverse of
    // the model-view matrix's upper 3x3). This correctly handles non-uniform scaling —
    // if an object is squished, its normals must rotate differently than its vertices.
    if (uCinematicTerrain.x > 0.5 && uCinematicTerrain.y > 0.0)
    {
        // When we visually bend the terrain, the original mesh normals no longer
        // describe the apparent surface. Sample nearby offsets to estimate a new
        // slope normal so lighting follows the morphed basin.
        float eps = 8.0;
        float dx = CinematicTerrainOffset(worldPos.xz + vec2(eps, 0.0)) -
                   CinematicTerrainOffset(worldPos.xz - vec2(eps, 0.0));
        float dz = CinematicTerrainOffset(worldPos.xz + vec2(0.0, eps)) -
                   CinematicTerrainOffset(worldPos.xz - vec2(0.0, eps));
        vec3 reliefNormal = normalize(vec3(-dx, eps * 2.0, -dz));
        vec3 baseWorldNormal = normalize(mat3(uModel) * aNormal);
        vec3 worldNormal = normalize(mix(baseWorldNormal, reliefNormal, clamp(uCinematicTerrain.y, 0.0, 1.0)));
        vNormal = mat3(uView) * worldNormal;
    }
    else
    {
        vNormal = transpose(inverse(mat3(modelView))) * aNormal;
    }

    // Texture coordinates pass through unchanged — they're defined in the mesh data.
    vTexCoord = aTexCoord;
}
