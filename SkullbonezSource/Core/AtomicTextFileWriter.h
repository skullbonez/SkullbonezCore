/*
File: AtomicTextFileWriter.h
Purpose:
  Declares recoverable atomic publication for cold UTF-8 text artifacts.

Summary:
  Filesystem-facing owners can publish a complete text value without exposing
  readers to a truncated target. The helper creates missing parent directories,
  flushes a unique temporary sibling, and replaces the destination atomically.

Glossary:
  Temporary sibling: Exclusively created file beside the final destination so
    Windows can rename it without crossing a volume boundary.

Invariants:
  - Failure never reports the temporary sibling as a completed artifact.
  - A successful return means the final path contains every supplied byte.
  - Diagnostics are bounded by SbDiagnosticStore rather than exceptions.

Related:
  - SkullbonezSource/Core/AtomicTextFileWriter.cpp
  - SkullbonezSource/Core/SbDiagnosticStore.h
*/
#pragma once

#include "SbResult.h"

#include <string_view>

namespace SkullbonezCore::Core
{
class SbDiagnosticStore;

// Publishes bytes only after the temporary sibling is durable. Parent
// directories may be created; an existing destination is replaced at rename.
[[nodiscard]] SbResult WriteTextFileAtomic( SbDiagnosticStore& diagnostics, const char* owner, const char* path,
                                            std::string_view bytes );
} // namespace SkullbonezCore::Core
