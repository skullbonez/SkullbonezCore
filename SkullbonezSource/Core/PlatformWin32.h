/*
File: PlatformWin32.h
Purpose:
  Owns the narrow Windows SDK prelude for engine code that exposes or calls
  Win32 APIs.

Summary:
  Platform-facing headers and translation units include this file explicitly.
  Windows builds receive the narrow SDK prelude and system-call adapters;
  non-Windows builds are redirected to the sibling POSIX contract. Domain
  headers remain independent of platform macros and native handle types.

Glossary:
  Win32 handle: Opaque operating-system token such as HWND or HANDLE.
  Lean SDK surface: Windows declarations with rarely used APIs excluded before
    the SDK headers are parsed.

Invariants:
  - WIN32_LEAN_AND_MEAN and NOMINMAX are defined before windows.h.
  - Common.h must not include this file; platform dependence is explicit.
  - Physics, maths, scene-schema, and UI-layout headers must not include it.
  - Platform adapters preserve the atomic writer's exclusive-create, flush,
    and replace ordering across both implementations.

Related:
  - SkullbonezSource/Core/Common.h owns the platform-free legacy prelude.
  - SkullbonezSource/Core/PlatformPosix.h owns the non-Windows sibling contract.
  - SkullbonezSource/Runtime/App/Window.h owns the application window handle.
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h owns the native render startup boundary.
*/
#pragma once

#if !defined( _WIN32 )
#include "PlatformPosix.h"
#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <intrin.h>

namespace SkullbonezCore::Core::Platform
{
using NativeError = DWORD;
using NativeFileHandle = HANDLE;
using NativePathCharacter = wchar_t;

inline NativeFileHandle InvalidFileHandle() noexcept
{
    return INVALID_HANDLE_VALUE;
}

inline NativeError LastError() noexcept
{
    return GetLastError();
}

inline NativeError ShortWriteError() noexcept
{
    return ERROR_WRITE_FAULT;
}

inline bool IsFileExistsError( NativeError error ) noexcept
{
    return error == ERROR_FILE_EXISTS;
}

inline const char* ErrorDomainName() noexcept
{
    return "win32";
}

inline uint64_t CurrentProcessId() noexcept
{
    return static_cast<uint64_t>( GetCurrentProcessId() );
}

inline NativeFileHandle CreateExclusiveFile( const NativePathCharacter* path, NativeError& error ) noexcept
{
    NativeFileHandle file = CreateFileW( path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr );
    error = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    return file;
}

inline bool WriteFileChunk( NativeFileHandle file, const char* bytes, std::size_t byteCount, std::size_t& written,
                            NativeError& error ) noexcept
{
    const DWORD chunk = static_cast<DWORD>( (std::min)( byteCount, static_cast<std::size_t>( MAXDWORD ) ) );
    DWORD nativeWritten = 0;
    const bool callSucceeded = WriteFile( file, bytes, chunk, &nativeWritten, nullptr ) != FALSE;
    written = nativeWritten;
    error = callSucceeded ? ( nativeWritten == chunk ? ERROR_SUCCESS : ERROR_WRITE_FAULT ) : GetLastError();

    // Invariant: preserve the established Windows contract: a short WriteFile
    // result fails publication instead of silently changing retry behavior.
    return callSucceeded && nativeWritten == chunk;
}

inline bool FlushFile( NativeFileHandle file, NativeError& error ) noexcept
{
    const bool succeeded = FlushFileBuffers( file ) != FALSE;
    error = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded;
}

inline void CloseFile( NativeFileHandle file ) noexcept
{
    CloseHandle( file );
}

inline void DeleteFileIfPresent( const NativePathCharacter* path ) noexcept
{
    DeleteFileW( path );
}

inline bool ReplaceFile( const NativePathCharacter* temporary, const NativePathCharacter* destination,
                         NativeError& error ) noexcept
{
    const bool succeeded = MoveFileExW( temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) !=
                           FALSE;
    error = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded;
}

inline std::size_t ReadEnvironmentVariable( const char* name, char* value, std::size_t capacity ) noexcept
{
    return static_cast<std::size_t>( GetEnvironmentVariableA( name, value, static_cast<DWORD>( capacity ) ) );
}

inline int CompareCaseInsensitive( const char* left, const char* right ) noexcept
{
    return _stricmp( left, right );
}

inline void WriteDebugger( const char* message ) noexcept
{
    OutputDebugStringA( message );
}

inline uintptr_t ProcessImageBase() noexcept
{
    return reinterpret_cast<uintptr_t>( GetModuleHandleW( nullptr ) );
}

inline bool CopyTextToClipboard( const char* text ) noexcept
{
    if ( !text )
    {
        return false;
    }

    if ( OpenClipboard( nullptr ) == FALSE )
    {
        return false;
    }

    EmptyClipboard();
    const std::size_t length = std::strlen( text ) + 1u;
    HGLOBAL memory = GlobalAlloc( GMEM_MOVEABLE, length );

    if ( !memory )
    {
        CloseClipboard();
        return false;
    }

    void* destination = GlobalLock( memory );

    if ( !destination )
    {
        GlobalFree( memory );
        CloseClipboard();
        return false;
    }

    std::memcpy( destination, text, length );
    GlobalUnlock( memory );
    SetClipboardData( CF_TEXT, memory );
    CloseClipboard();
    return true;
}

inline void DebugBreak() noexcept
{
    __debugbreak();
}
} // namespace SkullbonezCore::Core::Platform

#endif
