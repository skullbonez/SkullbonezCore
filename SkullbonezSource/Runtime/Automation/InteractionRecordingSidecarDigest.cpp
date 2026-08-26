/*
File: SkullbonezSource/Runtime/Automation/InteractionRecordingSidecarDigest.cpp
Purpose:
  Authenticates one interaction-recording sidecar with SHA-256.

Summary:
  The digest owner reads the exact caller-selected path to EOF and compares its
  lowercase SHA-256 bytes with the manifest value. It owns no manifest parsing
  or later Scene/Replay publication policy.

Invariants:
  - A stream error cannot authenticate a successfully read prefix.
  - BCrypt handles are released on every exit after acquisition.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionRecordingSidecarDigest.h
*/
#include "InteractionRecordingSidecarDigest.h"
#include "../../Core/PlatformWin32.h"

#include <array>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#pragma comment( lib, "bcrypt.lib" )

bool SkullbonezCore::Runtime::InteractionRecordingSidecarDigestMatches(
    const std::filesystem::path& path, std::string_view expectedSha256 )
{
    if ( expectedSha256.size() != 64u )
    {
        return false;
    }

    std::ifstream input( path, std::ios::binary );

    if ( !input )
    {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    std::vector<UCHAR> object;
    std::array<UCHAR, 32> digest = {};
    bool ok = BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) >= 0;

    if ( ok )
    {
        ok = BCryptGetProperty( algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>( &objectBytes ),
                                sizeof( objectBytes ), &resultBytes, 0 ) >= 0;
    }

    if ( ok )
    {
        object.resize( objectBytes );
        ok = BCryptCreateHash( algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0 ) >= 0;
    }

    std::array<char, 64 * 1024> buffer = {};

    while ( ok && input )
    {
        input.read( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
        const std::streamsize count = input.gcount();

        if ( count > 0 )
        {
            ok = BCryptHashData( hash, reinterpret_cast<PUCHAR>( buffer.data() ), static_cast<ULONG>( count ), 0 ) >= 0;
        }
    }

    // A normal EOF sets failbit after the last short read; badbit proves the
    // digest observed only a prefix and must fail closed.
    ok = ok && !input.bad();

    if ( ok )
    {
        ok = BCryptFinishHash( hash, digest.data(), static_cast<ULONG>( digest.size() ), 0 ) >= 0;
    }

    if ( hash )
    {
        BCryptDestroyHash( hash );
    }

    if ( algorithm )
    {
        BCryptCloseAlgorithmProvider( algorithm, 0 );
    }

    if ( !ok )
    {
        return false;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill( '0' );

    for ( const UCHAR byte : digest )
    {
        stream << std::setw( 2 ) << static_cast<unsigned int>( byte );
    }

    return stream.str() == expectedSha256;
}
