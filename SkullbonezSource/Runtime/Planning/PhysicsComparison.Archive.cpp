#include "PhysicsComparison.Archive.h"
#include "PhysicsComparison.h"
#include <windows.h>
#include <compressapi.h>
#include <array>
#include <fstream>
#include <vector>
#pragma comment( lib, "cabinet.lib" )

using namespace SkullbonezCore::Runtime;
namespace fs = std::filesystem;

namespace
{
constexpr uint32_t BLOCK_BYTES = 1024 * 1024;
constexpr uint64_t MAX_DIAGNOSTIC_BYTES = 2ull * 1024 * 1024 * 1024;

struct DiagnosticDecoder
{
    // Invariant: this owner closes its decompressor and bounds each raw/packed
    // block to 1/2 MiB, with at most 2 GiB of reconstructed evidence in total.
    DECOMPRESSOR_HANDLE handle = nullptr;
    std::vector<char> packed = std::vector<char>( BLOCK_BYTES * 2 );
    std::vector<char> raw = std::vector<char>( BLOCK_BYTES );
    uint64_t bytes = 0;

    ~DiagnosticDecoder()
    {
        if ( handle )
        {
            CloseDecompressor( handle );
        }
    }

    bool ReadBlock( std::istream& input, std::ostream& output, const std::array<uint32_t, 2>& sizes )
    {
        if ( !sizes[0] || !sizes[1] || sizes[0] > raw.size() || sizes[1] > packed.size() ||
             bytes + sizes[0] > MAX_DIAGNOSTIC_BYTES )
        {
            return false;
        }
        input.read( packed.data(), sizes[1] );
        SIZE_T written = 0;
        if ( !input || !Decompress( handle, packed.data(), sizes[1], raw.data(), sizes[0], &written ) ||
             written != sizes[0] )
        {
            return false;
        }
        output.write( raw.data(), static_cast<std::streamsize>( written ) );
        bytes += written;
        return static_cast<bool>( output );
    }

    bool ReadPart( const fs::path& path, std::ostream& output, ComparisonLoadProgress& progress )
    {
        std::error_code error;
        if ( fs::file_size( path, error ) > 48ull * 1024 * 1024 || error )
        {
            return false;
        }
        std::ifstream input( path, std::ios::binary );
        std::array<char, 8> magic {};
        input.read( magic.data(), magic.size() );
        if ( !input || magic != std::array<char, 8> { 'S', 'K', 'D', 'I', 'A', 'G', '1', '\n' } )
        {
            return false;
        }
        while ( !progress.cancelled.load( std::memory_order_relaxed ) )
        {
            std::array<uint32_t, 2> sizes {};
            input.read( reinterpret_cast<char*>( sizes.data() ), sizeof( sizes ) );
            if ( !input )
            {
                return false;
            }
            if ( sizes[0] == 0 && sizes[1] == 0 )
            {
                return input.peek() == std::char_traits<char>::eof() && !input.bad();
            }
            if ( !ReadBlock( input, output, sizes ) )
            {
                return false;
            }
        }
        return false;
    }
};
} // namespace

bool SkullbonezCore::Runtime::RestoreComparisonDiagnostics( const fs::path& directory, std::span<const std::string> parts,
                                                            const fs::path& output, ComparisonLoadProgress& progress )
{
    if ( parts.empty() || parts.size() > 64 )
    {
        return false;
    }
    DiagnosticDecoder decoder;
    if ( !CreateDecompressor( COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decoder.handle ) )
    {
        return false;
    }
    std::ofstream stream( output, std::ios::binary | std::ios::trunc );
    std::size_t completedParts = 0;
    for ( const auto& name : parts )
    {
        const fs::path part( name );
        // Invariant: manifests can name sibling archive parts, never arbitrary
        // files outside their A/B evidence directory.
        if ( part.has_parent_path() || part.has_root_path() || part.extension() != ".skdiag" ||
             !decoder.ReadPart( directory / part, stream, progress ) )
        {
            stream.close();
            std::error_code error;
            fs::remove( output, error );
            return false;
        }
        progress.Update( ++completedParts, parts.size() );
    }
    stream.close();
    return static_cast<bool>( stream );
}
