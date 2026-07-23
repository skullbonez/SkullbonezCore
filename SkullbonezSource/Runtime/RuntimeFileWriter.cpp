/*
File: SkullbonezSource/Runtime/RuntimeFileWriter.cpp
Purpose:
  Implements shared runtime file-output helpers.

Summary:
  Runtime features ask for safe paths here before they write artifacts. The
  helpers create missing folders, keep numbered saves collision-free, and leave
  serialization to the caller.

Glossary:
  Artifact: File written by runtime tools, diagnostics, captures, or saves.
  Parent directory: Folder portion of a requested output path.
  Numbered path: Prefix plus sequence number chosen to avoid overwriting an
    existing artifact.

Invariants:
  - Helpers choose paths but do not serialize feature-specific data.
  - Directory creation treats an already-existing directory as success.

Related:
  - SkullbonezSource/Runtime/RuntimeFileWriter.h
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
*/
#include "RuntimeFileWriter.h"

#include <cstdio>
#include <cstring>

#include "../Core/PlatformWin32.h"

using namespace SkullbonezCore::Runtime;

namespace
{
bool ExistingDirectory( const char* directory )
{
    const DWORD attributes = GetFileAttributesA( directory );
    return attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
}
} // namespace

bool RuntimeFileWriter::EnsureDirectory( const char* directory )
{
    if ( !directory || directory[0] == '\0' )
    {
        return true;
    }

    if ( CreateDirectoryA( directory, nullptr ) )
    {
        return true;
    }

    return GetLastError() == ERROR_ALREADY_EXISTS && ExistingDirectory( directory );
}

bool RuntimeFileWriter::EnsureParentDirectory( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        return false;
    }

    char directory[MAX_PATH] = {};
    strcpy_s( directory, sizeof( directory ), path );

    char* slash = strrchr( directory, '/' );
    char* backslash = strrchr( directory, '\\' );
    char* separator = slash;
    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }

    if ( !separator )
    {
        return true;
    }

    *separator = '\0';
    return EnsureDirectory( directory );
}

bool RuntimeFileWriter::OpenTextFile( const char* path, std::ofstream& output )
{
    if ( !EnsureParentDirectory( path ) )
    {
        return false;
    }

    output.open( path, std::ios::out | std::ios::trunc );
    return output.is_open();
}

bool RuntimeFileWriter::NextNumberedPath( char* outPath,
                                          std::size_t outPathSize,
                                          const char* directory,
                                          const char* prefix,
                                          const char* extension,
                                          int& sequence,
                                          int maxTries )
{
    if ( !outPath || outPathSize == 0 || !directory || !prefix || !extension || maxTries <= 0 )
    {
        return false;
    }

    outPath[0] = '\0';
    if ( !EnsureDirectory( directory ) )
    {
        return false;
    }

    int candidateSequence = sequence;
    for ( int tries = 0; tries < maxTries; ++tries )
    {
        if ( sprintf_s( outPath, outPathSize, "%s\\%s%04d%s", directory, prefix, candidateSequence, extension ) < 0 )
        {
            outPath[0] = '\0';
            return false;
        }
        ++candidateSequence;

        if ( GetFileAttributesA( outPath ) == INVALID_FILE_ATTRIBUTES )
        {
            sequence = candidateSequence;
            return true;
        }
    }

    sequence = candidateSequence;
    outPath[0] = '\0';
    return false;
}
