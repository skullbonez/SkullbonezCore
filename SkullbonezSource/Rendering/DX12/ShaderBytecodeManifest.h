/*
File: ShaderBytecodeManifest.h
Purpose:
  Declares the startup/hot-reload boundary that verifies and loads baked shader
  bytecode.

Summary:
  Authored HLSL, local includes, and executable DXIL are one matched set. The
  bake manifest records their hashes. A backend-lifetime cache verifies raster
  programs, while the DXR library uses the same source-bound loading boundary.

Invariants:
  - Shipping startup never invokes a shader compiler.
  - A stale source or bytecode hash is a recoverable startup failure.
  - Cache publication requires both raster stages from one verified manifest.

Related:
  - tools/bake_shaders.py
  - ShaderDX12.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include <d3d12shader.h>
#include <d3dcommon.h>
#include <wrl/client.h>
#include <array>
#include <cstddef>
#include <string>

namespace SkullbonezCore::Rendering
{
class Dx12ShaderDevelopment;

// Stores verified raster bytecode by source path for one renderer backend
// lifetime. Shader objects receive shared blobs; pass resources remain lazy.
class ShaderBytecodeManifestCache
{
    friend class Dx12ShaderDevelopment;

  private:
    static constexpr std::size_t PROGRAM_CAPACITY = 32;

    struct Program
    {
        std::string sourcePath;
        Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelBytecode;
    };

    struct ProgramLoadSummary
    {
        bool complete = false;
        bool newlyPublished = false;
        bool cacheHit = false;
        std::size_t stageLoads = 0;
    };

    struct PreparationSummary
    {
        std::size_t attempted = 0;
        std::size_t complete = 0;
        std::size_t newlyPublished = 0;
        std::size_t cacheHits = 0;
        std::size_t stageLoads = 0;
    };

    ProgramLoadSummary LoadProgram( const char* hlslPath, Microsoft::WRL::ComPtr<ID3DBlob>& outVertex,
                                    Microsoft::WRL::ComPtr<ID3DBlob>& outPixel, std::string& outError );
    PreparationSummary PrepareFirstGameplayPrograms( std::string& outError );
    void Reset();

    std::array<Program, PROGRAM_CAPACITY> m_programs;
    std::size_t m_programCount = 0;
};

bool DevShaderHotReloadEnabled();

bool LoadManifestCurrentShaderBytecode( const char* hlslPath, const char* stage, Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
                                        std::string& outError );
bool LoadManifestCurrentShaderLibraryBytecode( const char* hlslPath, const char* stage,
                                               Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError );

bool ReflectShaderBytecode( ID3DBlob* blob, Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& outReflection,
                            HRESULT& outResult );
} // namespace SkullbonezCore::Rendering
