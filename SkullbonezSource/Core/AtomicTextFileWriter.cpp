/*
File: AtomicTextFileWriter.cpp
Purpose:
  Publishes complete UTF-8 text files through temporary-sibling replacement.

Summary:
  The implementation validates the destination, creates its parent tree, then
  uses the Core platform contract for exclusive creation, durable flush, and
  same-directory rename. Every recoverable filesystem failure returns one
  bounded diagnostic.

Glossary:
  Atomic replacement: Platform rename after the temporary sibling is fully
    written and flushed.

Invariants:
  - Temporary names combine process id and a monotonic attempt number.
  - A failed write or rename deletes only the temporary sibling it created.
  - The existing destination remains untouched until the final rename.

Related:
  - SkullbonezSource/Core/AtomicTextFileWriter.h
  - SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp
*/
#include "AtomicTextFileWriter.h"
#include "PlatformWin32.h"
#include "SbDiagnosticStore.h"

#include <atomic>
#include <filesystem>
#include <string>

namespace SkullbonezCore::Core
{
namespace
{
std::atomic<uint32_t> s_temporarySequence { 1 };

#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
thread_local AtomicTextFileTestFailure s_testFailure = AtomicTextFileTestFailure::None;

bool ConsumeTestFailure( AtomicTextFileTestFailure failure ) noexcept
{
    if ( s_testFailure != failure )
    {
        return false;
    }

    s_testFailure = AtomicTextFileTestFailure::None;
    return true;
}
#endif

SbResult Failure( SbDiagnosticStore& diagnostics, const char* owner, const char* action, const char* path,
                  Platform::NativeError error ) noexcept
{
    return diagnostics.Failure( owner ? owner : "Core/AtomicTextFileWriter", "%s '%s' failed (%s=%llu).", action,
                                path ? path : "", Platform::ErrorDomainName(), static_cast<unsigned long long>( error ) );
}
} // namespace

#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
void SetAtomicTextFileTestFailure( AtomicTextFileTestFailure failure ) noexcept
{
    s_testFailure = failure;
}
#endif

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

    // Hazard: exclusive creation is essential. Reusing a predictable .tmp
    // path could truncate another writer's in-flight artifact before either
    // rename.
    std::filesystem::path temporary;
    Platform::NativeFileHandle file = Platform::InvalidFileHandle();

    for ( int attempt = 0; attempt < 16 && file == Platform::InvalidFileHandle(); ++attempt )
    {
        const uint32_t sequence = s_temporarySequence.fetch_add( 1, std::memory_order_relaxed );
        temporary = destination;
        temporary += ".tmp." + std::to_string( Platform::CurrentProcessId() ) + "." + std::to_string( sequence );
        Platform::NativeError createError = {};
        file = Platform::CreateExclusiveFile( temporary.c_str(), createError );

        if ( file == Platform::InvalidFileHandle() && !Platform::IsFileExistsError( createError ) )
        {
            return Failure( diagnostics, owner, "Create temporary sibling for", path, createError );
        }
    }

    if ( file == Platform::InvalidFileHandle() )
    {
        return diagnostics.Failure( owner ? owner : "Core/AtomicTextFileWriter",
                                    "Could not reserve a temporary sibling for '%s'.", path );
    }

    bool wrote = true;
    size_t offset = 0;
    Platform::NativeError writeError = {};
    const char* failureAction = "Write temporary sibling for";

#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
    if ( ConsumeTestFailure( AtomicTextFileTestFailure::Write ) )
    {
        wrote = false;
        writeError = Platform::ShortWriteError();
    }
#endif

    while ( wrote && offset < bytes.size() )
    {
        const size_t remaining = bytes.size() - offset;
        size_t written = 0;
        const bool writeCallSucceeded = Platform::WriteFileChunk( file, bytes.data() + offset, remaining, written,
                                                                  writeError );
        wrote = writeCallSucceeded && written > 0u;

        if ( !wrote )
        {
            writeError = writeCallSucceeded ? Platform::ShortWriteError() : writeError;
            break;
        }

        offset += written;
    }

    if ( wrote )
    {
        failureAction = "Flush temporary sibling for";
#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
        if ( ConsumeTestFailure( AtomicTextFileTestFailure::Flush ) )
        {
            wrote = false;
            writeError = Platform::ShortWriteError();
        }
        else
#endif
        {
            wrote = Platform::FlushFile( file, writeError );
        }
    }

    Platform::NativeError closeError = {};
    bool closed = Platform::CloseFile( file, closeError );

#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
    if ( ConsumeTestFailure( AtomicTextFileTestFailure::Close ) )
    {
        closed = false;
        closeError = Platform::ShortWriteError();
    }
#endif

    if ( wrote && !closed )
    {
        wrote = false;
        writeError = closeError;
        failureAction = "Close temporary sibling for";
    }

    if ( !wrote )
    {
        Platform::DeleteFileIfPresent( temporary.c_str() );
        return Failure( diagnostics, owner, failureAction, path, writeError );
    }

    Platform::NativeError renameError = {};
    bool replaced = false;

#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
    if ( ConsumeTestFailure( AtomicTextFileTestFailure::Replace ) )
    {
        renameError = Platform::ShortWriteError();
    }
    else
#endif
    {
        replaced = Platform::ReplaceFile( temporary.c_str(), destination.c_str(), renameError );
    }

    if ( !replaced )
    {
        Platform::DeleteFileIfPresent( temporary.c_str() );
        return Failure( diagnostics, owner, "Replace destination", path, renameError );
    }

    return SbResult::Success();
}
} // namespace SkullbonezCore::Core
