#version 330 core

// =============================================================================
// LIT TEXTURED INSTANCED VERTEX SHADER (lit_textured_instanced.vert)
// =============================================================================
//
// PURPOSE: Same as lit_textured.vert, but uses INSTANCED RENDERING to draw
// hundreds of identical meshes (spheres) in a single draw call.
//
// --- How Instancing Works on the GPU ---
//
//  Without instancing (300 spheres = 300 draw calls):
//    for each sphere:
//      SetUniform(modelMatrix)    ← CPU → GPU round-trip
//      Draw(sphereMesh)           ← CPU → GPU round-trip
//    Total: 600 CPU→GPU commands (slow!)
//
//  With instancing (300 spheres = 1 draw call):
//    UploadInstanceData([matrix0, matrix1, ..., matrix299])  ← 1 upload
//    DrawInstanced(sphereMesh, 300)                          ← 1 draw call
//    Total: 2 CPU→GPU commands (fast!)
//
// --- Vertex Attribute Layout (per vertex vs per instance) ---
//
//  Location 0: aPosition (vec3)  ← per-VERTEX (from static VBO, shared by all instances)
//  Location 1: aNormal   (vec3)  ← per-VERTEX
//  Location 2: aTexCoord (vec2)  ← per-VERTEX
//  Location 3-6: aModel  (mat4)  ← per-INSTANCE (from instance VBO, one per sphere)
//
//  A mat4 is 4×vec4, so it occupies 4 consecutive attribute locations (3, 4, 5, 6).
//  The GPU reads locations 0-2 from the mesh, advancing per-vertex.
//  The GPU reads locations 3-6 from the instance buffer, advancing per-INSTANCE
//  (thanks to glVertexAttribDivisor(loc, 1) set in C++).
//
// =============================================================================

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in mat4 aModel;          // per-instance model matrix (locations 3-6)
layout(location = 7) in vec4 aTint;           // per-instance RGB tint + color override amount

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uClipPlane;
uniform vec4 uLightPosition;

out vec3 vViewPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec4 vTint;

void main()
{
    // Use the per-INSTANCE model matrix (different for each sphere).
    mat4 modelView = uView * aModel;
    vec4 viewPos   = modelView * vec4(aPosition, 1.0);
    gl_Position    = uProjection * viewPos;

    // Clip distance for water reflection (same concept as non-instanced version).
    gl_ClipDistance[0] = dot(aModel * vec4(aPosition, 1.0), uClipPlane);

    vViewPos  = viewPos.xyz;
    vNormal   = transpose(inverse(mat3(modelView))) * aNormal;
    vTexCoord = aTexCoord;
    vTint     = aTint;
}
