/*
File: SkullbonezSource/SkullbonezRuntimeFileWriter.h
Purpose:
  Shares runtime file-output naming, directory creation, and text-file opening.

Mental model:
  Interactive saves should all follow one path policy: create the target
  repo-root folder if needed, choose the next unused numbered name, then hand
  the selected path to the feature-specific serializer.
*/
#pragma once

#include <cstddef>
#include <fstream>

namespace SkullbonezCore
{
namespace Basics
{
class RuntimeFileWriter
{
  public:
    static bool EnsureDirectory( const char* directory );
    static bool EnsureParentDirectory( const char* path );
    static bool OpenTextFile( const char* path, std::ofstream& output );
    static bool NextNumberedPath( char* outPath, std::size_t outPathSize, const char* directory, const char* prefix, const char* extension, int& sequence, int maxTries = 1000 );
};
} // namespace Basics
} // namespace SkullbonezCore
