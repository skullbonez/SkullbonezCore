/*
File: SkullbonezSource/SkullbonezShaderGL.h
Purpose:
  Compiles and binds shaders for the OpenGL parity renderer.

Mental model:
  OpenGL is a legacy parity renderer. It provides a reference path for visual
  comparison while DX12 remains the production renderer.

Glossary:
  OpenGL: Legacy parity renderer used as a reference path for visual output.
  GL (OpenGL): Legacy parity renderer path.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Parity renderer output should stay visually aligned with the DX12
  production path while these backends remain.

Related:
  - SkullbonezSource/SkullbonezShaderGL.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <glad/gl.h>
#pragma comment( lib, "opengl32.lib" )
#include "SkullbonezCommon.h"
#include "SkullbonezIShader.h"
#include <unordered_map>
#include <string>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- ShaderGL ----------------------------------------------------------------------------------------------------------------------------------------------------

    OpenGL 3.3 implementation of IShader. Loads, compiles, and links a GLSL ShaderGL program
    from vertex and fragment ShaderGL source files.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ShaderGL : public IShader
{

  private:
    GLuint m_programID; // OpenGL ShaderGL program handle
    mutable std::unordered_map<std::string, GLint> m_uniformCache;

    static GLuint CompileShader( const char* path, GLenum type ); // Compile a single ShaderGL stage from file
    static std::string LoadShaderSource( const char* path );      // Read ShaderGL source from file
    GLint GetUniformLocation( const char* name ) const;           // Cached uniform location lookup

  public:
    ShaderGL( const char* vertPath, const char* fragPath ); // Constructor: compile and link from files
    ~ShaderGL() override;                                   // Destructor: delete program

    void Use() const override;   // Bind this ShaderGL program
    GLuint GetProgramID() const; // Get the OpenGL program handle

    void SetInt( const char* name, int value ) const override;                                 // Set int uniform
    void SetFloat( const char* name, float value ) const override;                             // Set float uniform
    void SetVec3( const char* name, const Math::Vector::Vector3& v ) const override;           // Set vec3 uniform
    void SetVec3( const char* name, float x, float y, float z ) const override;                // Set vec3 uniform (components)
    void SetVec4( const char* name, float x, float y, float z, float w ) const override;       // Set vec4 uniform
    void SetMat4( const char* name, const Math::Transformation::Matrix4& mat ) const override; // Set mat4 uniform
};
} // namespace Rendering
} // namespace SkullbonezCore
