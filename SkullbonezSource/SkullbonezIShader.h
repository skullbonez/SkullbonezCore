/*
File: SkullbonezSource/SkullbonezIShader.h
Purpose:
  Declares the renderer-neutral shader interface.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  HLSL (High Level Shader Language): Shader language compiled for Direct3D
  render, compute, and raytracing stages.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezMatrix4.h"


namespace SkullbonezCore
{
namespace Rendering
{
/* -- IShader ----------------------------------------------------------------------------------------------------------------------------------------------------

    Engine-facing shader interface. Uniform setters write shader constants; the
    DX12 backend handles how those constants are uploaded and bound.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IShader
{

  public:
    virtual ~IShader() = default;

    virtual void Use() const = 0;
    virtual void SetInt( const char* name, int value ) const = 0;
    virtual void SetFloat( const char* name, float value ) const = 0;
    virtual void SetVec3( const char* name, const Math::Vector::Vector3& v ) const = 0;
    virtual void SetVec3( const char* name, float x, float y, float z ) const = 0;
    virtual void SetVec4( const char* name, float x, float y, float z, float w ) const = 0;
    virtual void SetMat4( const char* name, const Math::Transformation::Matrix4& mat ) const = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore
