/*
File: AtomicTextFileWriter.cpp
Purpose:
  Publishes complete UTF-8 text files through temporary-sibling replacement.

Summary:
  The implementation validates the destination, creates its parent tree, then
  uses exclusive Win32 file creation, durable flush, and same-volume rename.
  Every recoverable filesystem failure returns one bounded diagnostic.

Glossary:
  Atomic replacement: MOVEFILE_REPLACE_EXISTING rename after the temporary
    sibling is fully written and flushed.

Invariants:
  - Temporary names combine process id and a monotonic attempt number.
  - A failed write or rename deletes only the temporary sibling it created.
  - The existing destination remains untouched until the final rename.

Related:
  - SkullbonezSource/Core/AtomicTextFileWriter.h
  - SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp
*/
#include "AtomicTextFileWriter.h"
#include "SbDiagnosticStore.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>

namespace SkullbonezCore::Core
{
namespace
{
std::atomic<uint32_t> s_temporarySequence { 1 };

SbResult Failure( SbDiagnosticStore& diagnostics, const char* owner, const char* action, const char* path,
                  DWORD error ) noexcept
{
    return diagnostics.Failure( owner ? owner : "Core/AtomicTextFileWriter", "%s '%s' failed (win32=%lu).", action,
                                path ? path : "", static_cast<unsigned long>( error ) );
}
} // namespace

SbResult WriteTextFileAtomic( SbDiagnosticStore& diagnostics, const char* owner, const char* path, std::string_view bytes )
{

    if ( !path || path[0] == '\0' )
    {
        return diagnostics.Failure( owner ? owner : "Core/AtomicTextFileWriter", "Atomic text path is empty." );
    }

    std::error_code filesystemError;
    const std::filesystem::path destination( path );
    const std::filesystem::path parent = destination.parent_path();

    if ( !parent.empty() )
    {
        std::filesystem::create_directories( parent, filesystemError );

        if ( filesystemError )
        {
            return diagnostics.Failure( owner ? owner : "Core/AtomicTextFileWriter",
                                        "Create parent directories for '%s' failed (error=%d).", path,
                                        filesystemError.value() );
        }
    }

    // Hazard: CREATE_NEW is essential. Reusing a predictable .tmp path could
    // truncate another writer's in-flight artifact before either rename.
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;

    for ( int attempt = 0; attempt < 16 && file == INVALID_HANDLE_VALUE; ++attempt )
    {
        const uint32_t sequence = s_temporarySequence.fetch_add( 1, std::memory_order_relaxed );
        temporary = destination;
        temporary += L".tmp." + std::to_wstring( GetCurrentProcessId() ) + L"." + std::to_wstring( sequence );
        file = CreateFileW( temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr );

        if ( file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS )
        {
            return Failure( diagnostics, owner, "Create temporary sibling for", path, GetLastError() );
        }
    }

    if ( file == INVALID_HANDLE_VALUE )
    {
        return diagnostics.Failure( owner ? owner : "Core/AtomicTextFileWriter",
                                    "Could not reserve a temporary sibling for '%s'.", path );
    }

    bool wrote = true;
    size_t offset = 0;

    while ( offset < bytes.size() )
    {
        const size_t remaining = bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>( (std::min)( remaining, static_cast<size_t>( MAXDWORD ) ) );
        DWORD written = 0;
        const bool writeCallSucceeded = WriteFile( file, bytes.data() + offset, chunk, &written, nullptr ) != FALSE;
        wrote = writeCallSucceeded && written == chunk;

        if ( !wrote )
        {

            if ( writeCallSucceeded )
            {
                SetLastError( ERROR_WRITE_FAULT );
            }

            break;
        }

        offset += written;
    }

    wrote = wrote && FlushFileBuffers( file );
    const DWORD writeError = wrote ? ERROR_SUCCESS : GetLastError();
    CloseHandle( file );

    if ( !wrote )
    {
        DeleteFileW( temporary.c_str() );
        return Failure( diagnostics, owner, "Write temporary sibling for", path, writeError );
    }

    if ( !MoveFileExW( temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
    {
        const DWORD renameError = GetLastError();
        DeleteFileW( temporary.c_str() );
        return Failure( diagnostics, owner, "Replace destination", path, renameError );
    }

    return SbResult::Success();
}
} // namespace SkullbonezCore::Core
