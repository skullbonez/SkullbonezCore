/*
File: SkullbonezSource/SkullbonezIMesh.h
Purpose:
  Declares the renderer-neutral mesh interface.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- IMesh ------------------------------------------------------------------------------------------------------------------------------------------------------

    Engine-facing mesh interface. The active DX12 implementation owns GPU
    buffers; callers only ask the mesh to draw or expose data needed by DXR.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IMesh
{

  public:
    virtual ~IMesh() = default;

    virtual void Draw() const = 0;
    virtual void DrawInstanced( int instanceCount ) const = 0;
    virtual int GetVertexCount() const = 0;
    virtual int GetStride() const = 0;

    // DXR needs the GPU virtual address of mesh vertex data when building
    // acceleration structures.
    virtual uint64_t GetVertexBufferGPUVA() const = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore
