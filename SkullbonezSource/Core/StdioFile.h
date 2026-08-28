#pragma once

// Cross-CRT file ownership and bounded compatibility for legacy loaders.

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
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

inline constexpr std::size_t _TRUNCATE = static_cast<std::size_t>( -1 );

inline int strcpy_s( char* destination, std::size_t destinationSize, const char* source ) noexcept
{
    if ( !destination || destinationSize == 0 || !source )
    {
        if ( destination && destinationSize > 0 )
        {
            destination[0] = '\0';
        }
        return EINVAL;
    }

    const std::size_t sourceLength = std::strlen( source );
    if ( sourceLength >= destinationSize )
    {
        destination[0] = '\0';
        return ERANGE;
    }

    std::memcpy( destination, source, sourceLength + 1 );
    return 0;
}

template <std::size_t Size> inline int strcpy_s( char ( &destination )[Size], const char* source ) noexcept
{
    return strcpy_s( destination, Size, source );
}

inline int strncpy_s( char* destination, std::size_t destinationSize, const char* source, std::size_t count ) noexcept
{
    if ( !destination || destinationSize == 0 || !source )
    {
        if ( destination && destinationSize > 0 )
        {
            destination[0] = '\0';
        }
        return EINVAL;
    }

    const std::size_t sourceLength = std::strlen( source );
    std::size_t copyLength = sourceLength < count ? sourceLength : count;
    if ( count == _TRUNCATE )
    {
        copyLength = sourceLength < destinationSize ? sourceLength : destinationSize - 1;
    }
    else if ( copyLength >= destinationSize )
    {
        destination[0] = '\0';
        return ERANGE;
    }

    std::memcpy( destination, source, copyLength );
    destination[copyLength] = '\0';
    return 0;
}

template <std::size_t Size>
inline int strncpy_s( char ( &destination )[Size], const char* source, std::size_t count ) noexcept
{
    return strncpy_s( destination, Size, source, count );
}

inline char* strtok_s( char* text, const char* delimiters, char** context ) noexcept
{
    if ( !delimiters || !context )
    {
        return nullptr;
    }

    return ::strtok_r( text, delimiters, context );
}
#endif
