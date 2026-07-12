/*
File: SkullbonezSource/Rendering/IMesh.h
Purpose:
  Declares the renderer-neutral mesh interface.

Summary:
  IMesh.h declares the renderer-neutral mesh interface. As a public header,
  keep edits anchored on render submission and resource lifetime and on the
  glossary/invariants below.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Mesh implementations own GPU buffers; callers may draw or query metadata but
    must not assume native buffer layout.
  - Vertex-buffer GPU VA is exposed only for acceleration-structure builds.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- IMesh
------------------------------------------------------------------------------------------------------------------------------------------------------

    Engine-facing mesh interface. The active DX12 implementation owns GPU
    buffers; callers only ask the mesh to draw or expose data needed by DXR.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IMesh
{

    // Inheritance retention: rendering owns this test seam. Production draw and
    // metadata calls occur once per mesh submission or setup query; unit tests
    // use NullMesh without native resources. A value wrapper would add another
    // forwarding allocation. Retention depends on the 2026-07-12 measured
    // dispatch/perf evidence remaining neutral.

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
