/*
File: SkullbonezSource/Core/Allocation/DevelopmentToolsCapability.h
Purpose:
  Enforces the single compile-time boundary shared by development-only tools.

Summary:
  Dear ImGui, Tracy, and their engine-side allocation seam are compiled only
  when the development-tools property sheet defines one capability. Release
  and Profile-WPO exclude every source that includes this header.

Glossary:
  Development-tools capability: The SKULLBONEZ_DEVELOPMENT_TOOLS build flag
  that admits both ImGui and Tracy code into one development configuration.

Invariants:
  - There is no per-tool fallback flag that can accidentally ship one tool.
  - A source that requires a development dependency fails compilation when the
    shared capability is absent.

Related:
  - SkoreDevelopmentThirdParty.props
  - SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h
  - Agentic/Reports/2026-07-21/imgui-tracy-editor-campaign-closure.md (E2)
*/
#pragma once

#if !defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#error Development tool source compiled without SKULLBONEZ_DEVELOPMENT_TOOLS.
#endif
