/*
File: SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp
Purpose:
  Implements shared runtime file-output and path-identity helpers.

Summary:
  Runtime features ask for safe paths here before they write artifacts. The
  helpers recognize aliases, create missing folders, keep numbered saves
  collision-free, and leave serialization to the caller.

Invariants:
  - Helpers choose paths but do not serialize feature-specific data.
  - Directory creation treats an already-existing directory as success.
  - Existing hard links and normalized platform path aliases compare as one file.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "RuntimeFileWriter.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "../../Core/PlatformWin32.h"

using namespace SkullbonezCore::Runtime;

namespace
{
bool ExistingDirectory( const char* directory )
{
    const DWORD attributes = GetFileAttributesA( directory );
    return attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
}

bool TryResolveComparablePath( const char* value, std::filesystem::path& output )
{
    if ( !value || value[0] == '\0' )
    {
        return false;
    }

    std::error_code error;
    output = std::filesystem::weakly_canonical( std::filesystem::path( value ), error );

    if ( error )
    {
        error.clear();
        output = std::filesystem::absolute( std::filesystem::path( value ), error ).lexically_normal();
    }

    return !error && !output.empty();
}

bool ComparablePathsEqual( const std::filesystem::path& first, const std::filesystem::path& second )
{
#if defined( _WIN32 )
    const std::wstring& firstNative = first.native();
    const std::wstring& secondNative = second.native();
    return CompareStringOrdinal( firstNative.c_str(), static_cast<int>( firstNative.size() ), secondNative.c_str(),
                                 static_cast<int>( secondNative.size() ), TRUE ) == CSTR_EQUAL;
#else
    return first == second;
#endif
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

bool RuntimeFileWriter::PathsResolveToSameFile( const char* first, const char* second )
{
    if ( !first || first[0] == '\0' || !second || second[0] == '\0' )
    {
        return false;
    }

    std::error_code error;

    if ( std::filesystem::equivalent( std::filesystem::path( first ), std::filesystem::path( second ), error ) )
    {
        return true;
    }

    std::filesystem::path resolvedFirst;
    std::filesystem::path resolvedSecond;
    return TryResolveComparablePath( first, resolvedFirst ) && TryResolveComparablePath( second, resolvedSecond ) &&
           ComparablePathsEqual( resolvedFirst, resolvedSecond );
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

bool RuntimeFileWriter::NextNumberedPath( char* outPath, std::size_t outPathSize, const char* directory, const char* prefix,
                                          const char* extension, int& sequence, int maxTries )
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
