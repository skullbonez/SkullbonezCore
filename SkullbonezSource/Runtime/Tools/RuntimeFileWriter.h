/*
File: SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h
Purpose:
  Shares runtime file-output naming, directory creation, and text-file opening.

Summary:
  Interactive saves should all follow one path policy: create the target
  repo-root folder if needed, choose the next unused numbered name, then hand
  the selected path to the feature-specific serializer.

Glossary:
  Artifact: File written by runtime tools, diagnostics, captures, or saves.
  Parent directory: Folder portion of a requested output path.
  Numbered path: Prefix plus sequence number chosen to avoid overwriting an
    existing artifact.

Invariants:
  - Path helpers own naming and directory policy only.
  - Callers remain responsible for the bytes written to the chosen file.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
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
    static bool OpenTextFile( const char* path, std::ofstream& output );
    static bool NextNumberedPath(
        char* outPath,
        std::size_t outPathSize,
        const char* directory,
        const char* prefix,
        const char* extension,
        int& sequence,
        int maxTries = 1000
    );
};
} // namespace Runtime
} // namespace SkullbonezCore
