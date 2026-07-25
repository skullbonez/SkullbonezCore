/*
File: ShaderBytecodeManifest.h
Purpose:
  Declares the startup/hot-reload boundary that verifies and loads baked shader
  bytecode.

Summary:
  Authored HLSL and executable DXIL are a matched pair. The bake manifest
  records their hashes; startup and explicit reload accept bytecode only while
  both hashes match.

Glossary:
  Freshness manifest: Checked-in JSON map from compiler inputs to baked bytes.
  Hot reload: Explicit developer action that reruns the offline bake, then asks
    live shader owners to adopt hash-verified bytes transactionally.

Invariants:
  - Shipping startup never invokes a shader compiler.
  - A stale source or bytecode hash is a recoverable startup failure.

Related:
  - tools/bake_shaders.py
  - ShaderDX12.cpp
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include <d3d12shader.h>
#include <d3dcommon.h>
#include <wrl/client.h>
#include <string>

namespace SkullbonezCore::Rendering
{
bool DevShaderHotReloadEnabled();

bool LoadManifestCurrentShaderBytecode(
    const char* hlslPath,
    const char* stage,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
    std::string& outError
);

bool ReflectShaderBytecode(
    ID3DBlob* blob,
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& outReflection,
    HRESULT& outResult
);
} // namespace SkullbonezCore::Rendering
