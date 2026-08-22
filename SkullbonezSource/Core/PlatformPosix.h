/*
File: PlatformPosix.h
Purpose:
  Implements the narrow non-Windows platform contract used by portable CPU code.

Summary:
  POSIX builds translate the engine's exclusive temporary-file publication,
  environment lookup, debugger notification, and process diagnostics into
  fixed system calls. Higher layers keep one ordering and error contract.

Glossary:
  POSIX file descriptor: Small process-local integer naming an open file.
  Signal trap: SIGTRAP notification that stops in an attached debugger before
    the fatal owner proceeds to abort.

Invariants:
  - Temporary siblings use O_EXCL and cannot truncate another writer's file.
  - Successful file publication fsyncs the complete temporary file before the
    same-directory rename.
  - Error values are captured from errno at the failing call boundary.

Related:
  - SkullbonezSource/Core/PlatformWin32.h
  - SkullbonezSource/Core/AtomicTextFileWriter.cpp
*/
#pragma once

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

namespace SkullbonezCore::Core::Platform
{
using NativeError = int;
using NativeFileHandle = int;
using NativePathCharacter = char;

inline NativeFileHandle InvalidFileHandle() noexcept
{
    return -1;
}

inline NativeError LastError() noexcept
{
    return errno;
}

inline NativeError ShortWriteError() noexcept
{
    return EIO;
}

inline bool IsFileExistsError( NativeError error ) noexcept
{
    return error == EEXIST;
}

inline const char* ErrorDomainName() noexcept
{
    return "posix";
}

inline uint64_t CurrentProcessId() noexcept
{
    return static_cast<uint64_t>( getpid() );
}

inline NativeFileHandle CreateExclusiveFile( const NativePathCharacter* path, NativeError& error ) noexcept
{
    const NativeFileHandle file = open( path, O_WRONLY | O_CREAT | O_EXCL, 0666 );
    error = file < 0 ? errno : 0;
    return file;
}

inline bool WriteFileChunk( NativeFileHandle file, const char* bytes, std::size_t byteCount, std::size_t& written,
                            NativeError& error ) noexcept
{
    const std::size_t maximum = static_cast<std::size_t>( ( std::numeric_limits<ssize_t>::max )() );
    const std::size_t chunk = (std::min)( byteCount, maximum );
    const ssize_t result = ::write( file, bytes, chunk );

    if ( result < 0 )
    {
        written = 0u;
        error = errno;
        return false;
    }

    written = static_cast<std::size_t>( result );
    error = 0;
    return true;
}

inline bool FlushFile( NativeFileHandle file, NativeError& error ) noexcept
{
    const bool succeeded = ::fsync( file ) == 0;
    error = succeeded ? 0 : errno;
    return succeeded;
}

inline void CloseFile( NativeFileHandle file ) noexcept
{
    ::close( file );
}

inline void DeleteFileIfPresent( const NativePathCharacter* path ) noexcept
{
    ::unlink( path );
}

inline bool ReplaceFile( const NativePathCharacter* temporary, const NativePathCharacter* destination,
                         NativeError& error ) noexcept
{
    const bool succeeded = ::rename( temporary, destination ) == 0;
    error = succeeded ? 0 : errno;
    return succeeded;
}

inline std::size_t ReadEnvironmentVariable( const char* name, char* value, std::size_t capacity ) noexcept
{
    const char* source = std::getenv( name );

    if ( !source )
    {
        return 0u;
    }

    const std::size_t length = std::strlen( source );

    if ( length >= capacity )
    {
        return length + 1u;
    }

    std::memcpy( value, source, length + 1u );
    return length;
}

inline int CompareCaseInsensitive( const char* left, const char* right ) noexcept
{
    return strcasecmp( left, right );
}

inline void WriteDebugger( const char* ) noexcept
{
}

inline uintptr_t ProcessImageBase() noexcept
{
    return 0u;
}

inline bool CopyTextToClipboard( const char* ) noexcept
{
    return false;
}

inline void DebugBreak() noexcept
{
    raise( SIGTRAP );
}
} // namespace SkullbonezCore::Core::Platform
