/*
File: StartupCrashLogging.h
Purpose:
  Publishes debug-process crash logger installation to WinMain.

Summary:
  Debug startup installs one SEH filter and one terminate handler before any
  subsystem construction so fatal process diagnostics survive partial startup.

Glossary:
  Symbolized stack: Instruction addresses resolved to function and source names.

Invariants:
  - Installation occurs before the debug crash-test option can fault.
  - Crash handling flushes the engine log before process termination.

Related:
  - StartupCrashLogging.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Runtime
{
namespace Startup
{
#ifdef _DEBUG
// Installs process-wide debug fault handlers. Call once before any deliberate
// crash probe or subsystem construction; the handlers retain no caller state.
void InstallDebugCrashLogger();
#endif
} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
