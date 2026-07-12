/*
File: IRenderShaderDevelopment.h
Purpose:
  Declares the opt-in developer capability for offline shader rebake and live
  raster-pipeline adoption.

Summary:
  Runtime input may request one cold developer transaction. The active backend
  runs the repository's pinned bake, verifies every replacement shader, drains
  GPU work, then atomically replaces live bytecode and dependent PSOs.

Glossary:
  Hot reload: Developer-only replacement of verified shader bytecode without
    restarting the process.
  Transactional adoption: All live shader replacements validate before any
    current bytecode or pipeline state is released.

Invariants:
  - The capability is disabled unless the exact developer launch option is set.
  - A failed bake or contract check leaves every live shader and PSO unchanged.

Related:
  - Rendering/DX12/ShaderDX12.cpp
  - tools/bake_shaders.bat
*/
#pragma once

#include "../Core/SbResult.h"

namespace SkullbonezCore::Rendering
{
class IRenderShaderDevelopment
{
  public:
    virtual ~IRenderShaderDevelopment() = default;
    virtual bool ShaderHotReloadEnabled() const = 0;
    virtual Basics::SbResult ReloadShadersFromSource() = 0;
};
} // namespace SkullbonezCore::Rendering
