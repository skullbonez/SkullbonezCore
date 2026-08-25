/*
File: SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h
Purpose:
  Shares runtime file-output naming, directory creation, identity checks, and text-file opening.

Summary:
  Interactive saves follow one path policy: resolve aliases before destructive
  opens, create the target repo-root folder if needed, choose the next unused
  numbered name, then hand the selected path to the feature-specific serializer.

Invariants:
  - Path helpers own naming and directory policy only.
  - Input/output identity checks resolve existing links and platform case rules.
  - Callers remain responsible for the bytes written to the chosen file.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <fstream>

namespace SkullbonezCore
{
namespace Runtime
{
class RuntimeFileWriter
{
  public:
    static bool EnsureDirectory( const char* directory );
    static bool EnsureParentDirectory( const char* path );
    // Resolves relative components, existing links, and platform case rules so
    // an output owner can reject aliases of an input before opening with truncation.
    static bool PathsResolveToSameFile( const char* first, const char* second );
    static bool OpenTextFile( const char* path, std::ofstream& output );
    static bool NextNumberedPath( char* outPath, std::size_t outPathSize, const char* directory, const char* prefix,
                                  const char* extension, int& sequence, int maxTries = 1000 );
};
} // namespace Runtime
} // namespace SkullbonezCore
