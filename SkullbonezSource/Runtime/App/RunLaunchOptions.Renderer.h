/*
File: RunLaunchOptions.Renderer.h
Purpose:
  Declares the runtime renderer option table consumed by command-line parsing
  and unit tests.

Summary:
  Runtime startup accepts renderer names before any window or backend exists.
  Keeping the accepted options in one tiny data table lets tests prove the
  DX12-only launch contract without booting the renderer.

Glossary:
  Runtime renderer option: Command-line token accepted by `--renderer` before
    runtime startup binds the production backend.

Invariants:
  - DX12 is the only runtime renderer. Extra entries change the product contract
    and require an owner-reviewed backend implementation and validation matrix.
  - `d3d12` is a compatibility alias for older automation; it still launches
    the same DX12 backend.

Related:
  - Runtime/App/Init.cpp parses `--renderer` with this table.
  - SkullbonezTests/TestDx12OnlyRuntime.cpp proves the table stays DX12-only.
*/
#pragma once

#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{

struct RuntimeRendererOption
{
    const char* name;
    const char* alias;
};

inline constexpr RuntimeRendererOption kRuntimeRendererOptions[] = {
    { "dx12", "d3d12" },
};

inline constexpr std::size_t kRuntimeRendererOptionCount = sizeof( kRuntimeRendererOptions ) /
                                                           sizeof( kRuntimeRendererOptions[0] );

} // namespace Runtime
} // namespace SkullbonezCore
