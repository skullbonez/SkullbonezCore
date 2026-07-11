/*
File: ShaderBytecodeManifest.h
Purpose:
  Declares the startup boundary that verifies and loads baked shader bytecode.

Mental model:
  Authored HLSL and executable DXIL are a matched pair. The bake manifest
  records their hashes; startup accepts bytecode only while both hashes match.

Glossary:
  Freshness manifest: Checked-in JSON map from compiler inputs to baked bytes.
  Dev fallback: Explicit cold-start source compilation enabled only by
    `--dev-compile-shaders`.

Invariants:
  - Shipping startup never invokes a shader compiler.
  - A stale source or bytecode hash is a recoverable startup failure.

Related:
  - tools/bake_shaders.py
  - ShaderDX12.cpp
*/
#pragma once

#include <d3d12shader.h>
#include <d3dcommon.h>
#include <wrl/client.h>
#include <string>

namespace SkullbonezCore::Rendering
{
bool DevShaderSourceCompileEnabled();

bool LoadManifestCurrentShaderBytecode( const char* hlslPath,
                                        const char* stage,
                                        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
                                        std::string& outError );

bool ReflectShaderBytecode( ID3DBlob* blob,
                            Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& outReflection,
                            HRESULT& outResult );
} // namespace SkullbonezCore::Rendering
