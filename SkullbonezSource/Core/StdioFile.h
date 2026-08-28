#pragma once

// Cross-CRT ownership and opening for the narrow set of engine files that use FILE.

#include <cerrno>
#include <cstdio>
#include <memory>

namespace SkullbonezCore::Core
{
struct StdioFileCloser
{
    void operator()( FILE* file ) const noexcept
    {
        if ( file )
        {
            std::fclose( file );
        }
    }
};

using StdioFile = std::unique_ptr<FILE, StdioFileCloser>;

inline int OpenStdioFile( FILE*& outFile, const char* path, const char* mode ) noexcept
{
    outFile = nullptr;
#if defined( _WIN32 )
    return ::fopen_s( &outFile, path, mode );
#else
    errno = 0;
    outFile = std::fopen( path, mode );
    return outFile ? 0 : errno;
#endif
}

inline int CreateTemporaryStdioFile( FILE*& outFile ) noexcept
{
    outFile = nullptr;
#if defined( _WIN32 )
    return ::tmpfile_s( &outFile );
#else
    errno = 0;
    outFile = std::tmpfile();
    return outFile ? 0 : errno;
#endif
}
} // namespace SkullbonezCore::Core

#if !defined( _WIN32 )
// Portable builds force-include this header so unchanged MSVC-era loaders keep
// their secure-CRT call shape while the actual open uses the host C runtime.
inline int fopen_s( FILE** outFile, const char* path, const char* mode ) noexcept
{
    if ( !outFile )
    {
        return EINVAL;
    }

    return SkullbonezCore::Core::OpenStdioFile( *outFile, path, mode );
}
#endif
