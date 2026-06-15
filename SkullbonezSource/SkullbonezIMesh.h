/*
File: SkullbonezSource/SkullbonezIMesh.h
Purpose:
  Declares the renderer-neutral mesh interface.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  DX11 (DirectX 11): Legacy parity renderer used to compare output while the
  engine migrates to DX12.
  OpenGL: Legacy parity renderer used as a reference path for visual output.
  GL (OpenGL): Legacy parity renderer path.
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

    Abstract mesh interface. Concrete implementations handle VAO/VBO (OpenGL) or ID3D11Buffer (DirectX).
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
    // acceleration structures. Legacy GL/DX11 meshes do not expose DX12 GPU
    // addresses, so they return 0.
    virtual uint64_t GetVertexBufferGPUVA() const = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore
