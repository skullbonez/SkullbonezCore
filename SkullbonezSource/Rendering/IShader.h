/*
File: SkullbonezSource/Rendering/IShader.h
Purpose:
  Declares the renderer-neutral shader interface.

Mental model:
  IShader.h declares the renderer-neutral shader interface. As a public
  header, keep edits anchored on render submission and resource lifetime and
  on the glossary/invariants below.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Shader implementations own backend binding details; callers set engine
    constants and texture slots only through this interface.
  - SetConstantBufferBytes returns false when a typed block does not match the
    reflected shader contract, and the dependent draw should be skipped.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Maths/Vector3.h"
#include "../Maths/Matrix4.h"
#include <cstddef>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- IShader
----------------------------------------------------------------------------------------------------------------------------------------------------

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

    // Uploads a complete typed constant-buffer block instead of setting named
    // uniforms one at a time. Callers must pass a struct whose field order and
    // size exactly match the shader's reflected cbuffer; false means the block
    // was rejected and the caller should skip the draw that depends on it.
    virtual bool SetConstantBufferBytes( const void* data, size_t size, const char* debugName ) const = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore
