#version 330 core

// Full-screen post-process vertex shader.
//
// The CPU sends six vertices that already form a rectangle covering the whole
// screen in clip space (-1 to +1). This shader does not move a 3D object; it just
// hands those positions to the GPU and passes UVs to the fragment shader so each
// pixel can sample the rendered scene texture.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
