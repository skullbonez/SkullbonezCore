/*
File: SkullbonezSource/Rendering/IRenderResourceFactory.h
Purpose:
  Declares the narrow render capability used to create and destroy backend
  resources such as shaders, meshes, textures, framebuffers, and transient
  geometry buffers.

Mental model:
  Resource factory callers are in load, rebuild, or teardown phases. They ask
  the active backend for opaque engine handles or interface objects, then later
  hand those same handles back for deletion. They should not mutate draw state,
  present frames, or read diagnostic traces.

Glossary:
  Framebuffer: Off-screen color/depth target used by cinematic, shadow, and
    post-processing passes.
  Dynamic vertex buffer: Backend-owned transient buffer used for text, overlays,
    and other per-frame geometry.
  Instanced mesh: Static mesh plus per-instance data drawn many times in one
    backend call.
  Opaque handle: Integer token whose meaning belongs to the backend that created
    it.

Invariants:
  - Created resources belong to the active backend lifetime.
  - Deletion methods must receive handles from the same backend that created
    them.
  - Factory calls may allocate backend resources; per-frame hot paths should use
    previously created handles and command-context upload methods instead.

Related:
  - SkullbonezSource/Rendering/IFramebuffer.h
  - SkullbonezSource/Rendering/IMesh.h
  - SkullbonezSource/Rendering/IShader.h
  - SkullbonezSource/Rendering/IRenderCommandContext.h
*/
#pragma once

#include <cstdint>
#include <memory>

#include "IFramebuffer.h"
#include "IMesh.h"
#include "IShader.h"

namespace SkullbonezCore
{
namespace Rendering
{

class IRenderResourceFactory
{
  public:
    virtual ~IRenderResourceFactory() = default;

    // baseName is relative to DATA_ROOT with no extension, e.g. "shaders/lit_textured".
    virtual std::unique_ptr<IShader> CreateShader( const char* baseName ) = 0;
    virtual std::unique_ptr<IMesh>
    CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords ) = 0;
    virtual std::unique_ptr<IFramebuffer>
    CreateFramebuffer( int width, int height, FramebufferColorFormat colorFormat = FramebufferColorFormat::RGBA8 ) = 0;

    virtual uint32_t
    CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter ) = 0;
    virtual void DeleteTexture( uint32_t handle ) = 0;

    virtual uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) = 0;
    virtual void DestroyDynamicVB( uint32_t handle ) = 0;

    virtual uint32_t CreateInstancedMesh( const float* staticData,
                                          int staticVertCount,
                                          int staticFloatsPerVert,
                                          int maxInstances,
                                          int instanceFloats,
                                          int instanceStartAttrib,
                                          const int* instanceAttribSizes,
                                          int numInstanceAttribs,
                                          const int* staticAttribSizes = nullptr,
                                          int numStaticAttribs = 0 ) = 0;
    virtual void DestroyInstancedMesh( uint32_t handle ) = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore
