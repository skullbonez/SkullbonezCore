/*
File: SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp
Purpose:
  Loads, addresses, and atomically persists bounded driver PSO cache blobs.

Summary:
  The checked-in shader manifest and serialized root signature select a cache
  file. Each PSO recipe selects one entry inside that file. Drivers may reject
  serialized bytes after an adapter or driver change; rejection is an expected
  recoverable cold start, not an engine invariant failure.

Glossary:
  Canonical hash: Field-by-field digest independent of padding and pointers.
  Cold start: Startup that creates PSOs because no compatible cached blob exists.
  Warm hit: CreateGraphicsPipelineState accepted a mapped cached blob by stable recipe name.

Invariants:
  - Every semantic D3D12 graphics descriptor field that can affect compilation
    enters the recipe digest. Shader bytes enter by content; the driver-produced
    CachedPSO output does not address itself.
  - The fixed blob cap bounds both reads and shutdown serialization.
  - A temporary file is replaced atomically so interruption cannot publish a
    partially written cache as the next launch's input.

Related:
  - Dx12CachedPsoStore.h
  - RenderBackendDX12.Pipeline.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "Dx12CachedPsoStore.h"

#include "../../Core/Log.h"

#include <bcrypt.h>
#include <cstdio>
#include <cstring>

#pragma comment( lib, "bcrypt.lib" )

using Microsoft::WRL::ComPtr;

namespace SkullbonezCore::Rendering
{
namespace
{
constexpr wchar_t CACHE_SCHEMA[] = L"skullbonez-pso-v1";
constexpr char MANIFEST_PATH[] = "SkullbonezData/shaders/shader_manifest.json";
constexpr char FILE_MAGIC[8] = { 'S', 'B', 'P', 'S', 'O', '1', '\0', '\0' };

struct CacheFileHeader
{
    char magic[8];
    std::uint32_t version;
    std::uint32_t entryCount;
    std::uint8_t manifestDigest[Dx12CachedPsoStore::DIGEST_BYTES];
    std::uint8_t rootDigest[Dx12CachedPsoStore::DIGEST_BYTES];
};

struct CacheEntryHeader
{
    std::uint8_t digest[Dx12CachedPsoStore::DIGEST_BYTES];
    std::uint32_t blobBytes;
};

class Sha256Writer
{
  public:
    bool Open()
    {
        DWORD resultBytes = 0;
        NTSTATUS status = BCryptOpenAlgorithmProvider( &m_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 );

        if ( status >= 0 )
        {

            // Why: BCryptGetProperty exposes arbitrary property storage as
            // mutable bytes; the requested property is exactly one DWORD.
            status = BCryptGetProperty( m_algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>( &m_objectBytes ),
                                        sizeof( m_objectBytes ), &resultBytes, 0 );
        }

        if ( status < 0 || m_objectBytes > m_object.size() )
        {
            Close();
            return false;
        }

        status = BCryptCreateHash( m_algorithm, &m_hash, m_object.data(), m_objectBytes, nullptr, 0, 0 );

        if ( status < 0 )
        {
            Close();
            return false;
        }

        return true;
    }

    bool Write( SkullbonezCore::Core::ByteView bytes )
    {

        if ( !m_hash || bytes.size() > ULONG_MAX )
        {
            return false;
        }

        if ( bytes.empty() )
        {
            return true;
        }

        // Why: BCryptHashData's legacy ABI lacks const even though hashing does
        // not mutate input. Remove const only at this synchronous API call.
        return BCryptHashData( m_hash, const_cast<std::uint8_t*>( bytes.data() ), static_cast<ULONG>( bytes.size() ), 0 ) >=
               0;
    }

    template <typename T> bool Value( const T& value )
    {
        return Write( SkullbonezCore::Core::ObjectBytes( value ) );
    }

    bool Text( const char* value )
    {
        const std::uint32_t length = value ? static_cast<std::uint32_t>( std::strlen( value ) ) : 0;
        return Value( length ) && Write( SkullbonezCore::Core::ObjectBytes( std::span<const char>( value, length ) ) );
    }

    bool Finish( std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES>& digest )
    {
        const bool ok = m_hash && BCryptFinishHash( m_hash, digest.data(), static_cast<ULONG>( digest.size() ), 0 ) >= 0;

        Close();
        return ok;
    }

    ~Sha256Writer()
    {
        Close();
    }

  private:
    void Close()
    {

        if ( m_hash )
        {
            BCryptDestroyHash( m_hash );
            m_hash = nullptr;
        }

        if ( m_algorithm )
        {
            BCryptCloseAlgorithmProvider( m_algorithm, 0 );
            m_algorithm = nullptr;
        }
    }

    BCRYPT_ALG_HANDLE m_algorithm = nullptr;
    BCRYPT_HASH_HANDLE m_hash = nullptr;
    DWORD m_objectBytes = 0;
    std::array<std::uint8_t, 1024> m_object = {};
};

bool HashBytes( SkullbonezCore::Core::ByteView bytes, std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES>& digest )
{
    Sha256Writer writer;
    return writer.Open() && writer.Write( bytes ) && writer.Finish( digest );
}

bool HashBoundedFile( const wchar_t* path, std::size_t cap,
                      std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES>& digest )
{
    HANDLE file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

    if ( file == INVALID_HANDLE_VALUE )
    {
        return false;
    }

    LARGE_INTEGER size = {};

    const bool bounded = GetFileSizeEx( file, &size ) && size.QuadPart > 0 &&
                         static_cast<ULONGLONG>( size.QuadPart ) <= static_cast<ULONGLONG>( cap );

    if ( !bounded )
    {
        CloseHandle( file );
        return false;
    }

    HANDLE mapping = CreateFileMappingW( file, nullptr, PAGE_READONLY, 0, 0, nullptr );

    // Why: MapViewOfFile is an untyped Win32 ABI. This cold owner immediately
    // narrows the bounded read-only mapping to bytes and never publishes void.
    const std::uint8_t* bytes = mapping
                                    ? static_cast<const std::uint8_t*>( MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, 0 ) )
                                    : nullptr;

    // Allocation policy: cold identity hashing borrows a bounded read-only file
    // mapping; it never grows an owning container in runtime code.
    const bool hashed = bytes && HashBytes( { bytes, static_cast<std::size_t>( size.QuadPart ) }, digest );

    if ( bytes )
    {
        UnmapViewOfFile( bytes );
    }

    if ( mapping )
    {
        CloseHandle( mapping );
    }

    CloseHandle( file );
    return hashed;
}

bool ReadManifestDigest( std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES>& digest )
{
    wchar_t path[MAX_PATH] = {};

    if ( MultiByteToWideChar( CP_UTF8, 0, MANIFEST_PATH, -1, path, MAX_PATH ) == 0 ||
         !HashBoundedFile( path, 4u * 1024u * 1024u, digest ) )
    {
        return false;
    }

    return true;
}

void DigestHex( const std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES>& digest, wchar_t ( &text )[65] )
{
    static constexpr wchar_t digits[] = L"0123456789abcdef";

    for ( std::size_t index = 0; index < digest.size(); ++index )
    {
        text[index * 2] = digits[digest[index] >> 4];
        text[index * 2 + 1] = digits[digest[index] & 0x0f];
    }
}

template <typename T> bool HashValue( Sha256Writer& writer, const T& value )
{
    return writer.Value( value );
}

bool HashShaderBytecode( Sha256Writer& writer, const D3D12_SHADER_BYTECODE& bytecode )
{
    std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES> digest = {};

    // Why: D3D12_SHADER_BYTECODE carries its immutable buffer through a native
    // void pointer. Narrow it at this driver descriptor seam only.
    const SkullbonezCore::Core::ByteView bytes = { static_cast<const std::uint8_t*>( bytecode.pShaderBytecode ),
                                                   bytecode.BytecodeLength };

    if ( !HashBytes( bytes, digest ) )
    {
        return false;
    }

    return writer.Write( digest );
}

bool HashGraphicsDesc( Sha256Writer& writer, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc )
{

    // Invariant: hash individual values rather than descriptor structs. Native
    // structs contain padding and pointers whose bytes are process-dependent.

    if ( !HashShaderBytecode( writer, desc.VS ) || !HashShaderBytecode( writer, desc.PS ) ||
         !HashShaderBytecode( writer, desc.DS ) || !HashShaderBytecode( writer, desc.HS ) ||
         !HashShaderBytecode( writer, desc.GS ) )
    {
        return false;
    }

    if ( !HashValue( writer, desc.StreamOutput.NumEntries ) )
    {
        return false;
    }

    for ( UINT index = 0; index < desc.StreamOutput.NumEntries; ++index )
    {
        const D3D12_SO_DECLARATION_ENTRY& entry = desc.StreamOutput.pSODeclaration[index];

        if ( !HashValue( writer, entry.Stream ) || !writer.Text( entry.SemanticName ) ||
             !HashValue( writer, entry.SemanticIndex ) || !HashValue( writer, entry.StartComponent ) ||
             !HashValue( writer, entry.ComponentCount ) || !HashValue( writer, entry.OutputSlot ) )
        {
            return false;
        }
    }

    if ( !HashValue( writer, desc.StreamOutput.NumStrides ) )
    {
        return false;
    }

    for ( UINT index = 0; index < desc.StreamOutput.NumStrides; ++index )
    {

        if ( !HashValue( writer, desc.StreamOutput.pBufferStrides[index] ) )
        {
            return false;
        }
    }

    if ( !HashValue( writer, desc.StreamOutput.RasterizedStream ) ||
         !HashValue( writer, desc.BlendState.AlphaToCoverageEnable ) ||
         !HashValue( writer, desc.BlendState.IndependentBlendEnable ) )
    {
        return false;
    }

    for ( const D3D12_RENDER_TARGET_BLEND_DESC& target : desc.BlendState.RenderTarget )
    {

        if ( !HashValue( writer, target.BlendEnable ) || !HashValue( writer, target.LogicOpEnable ) ||
             !HashValue( writer, target.SrcBlend ) || !HashValue( writer, target.DestBlend ) ||
             !HashValue( writer, target.BlendOp ) || !HashValue( writer, target.SrcBlendAlpha ) ||
             !HashValue( writer, target.DestBlendAlpha ) || !HashValue( writer, target.BlendOpAlpha ) ||
             !HashValue( writer, target.LogicOp ) || !HashValue( writer, target.RenderTargetWriteMask ) )
        {
            return false;
        }
    }

    const D3D12_RASTERIZER_DESC& raster = desc.RasterizerState;

    if ( !HashValue( writer, desc.SampleMask ) || !HashValue( writer, raster.FillMode ) ||
         !HashValue( writer, raster.CullMode ) || !HashValue( writer, raster.FrontCounterClockwise ) ||
         !HashValue( writer, raster.DepthBias ) || !HashValue( writer, raster.DepthBiasClamp ) ||
         !HashValue( writer, raster.SlopeScaledDepthBias ) || !HashValue( writer, raster.DepthClipEnable ) ||
         !HashValue( writer, raster.MultisampleEnable ) || !HashValue( writer, raster.AntialiasedLineEnable ) ||
         !HashValue( writer, raster.ForcedSampleCount ) || !HashValue( writer, raster.ConservativeRaster ) )
    {
        return false;
    }

    const D3D12_DEPTH_STENCIL_DESC& depth = desc.DepthStencilState;

    if ( !HashValue( writer, depth.DepthEnable ) || !HashValue( writer, depth.DepthWriteMask ) ||
         !HashValue( writer, depth.DepthFunc ) || !HashValue( writer, depth.StencilEnable ) ||
         !HashValue( writer, depth.StencilReadMask ) || !HashValue( writer, depth.StencilWriteMask ) ||
         !HashValue( writer, depth.FrontFace.StencilFailOp ) || !HashValue( writer, depth.FrontFace.StencilDepthFailOp ) ||
         !HashValue( writer, depth.FrontFace.StencilPassOp ) || !HashValue( writer, depth.FrontFace.StencilFunc ) ||
         !HashValue( writer, depth.BackFace.StencilFailOp ) || !HashValue( writer, depth.BackFace.StencilDepthFailOp ) ||
         !HashValue( writer, depth.BackFace.StencilPassOp ) || !HashValue( writer, depth.BackFace.StencilFunc ) ||
         !HashValue( writer, desc.InputLayout.NumElements ) )
    {
        return false;
    }

    for ( UINT index = 0; index < desc.InputLayout.NumElements; ++index )
    {
        const D3D12_INPUT_ELEMENT_DESC& element = desc.InputLayout.pInputElementDescs[index];

        if ( !writer.Text( element.SemanticName ) || !HashValue( writer, element.SemanticIndex ) ||
             !HashValue( writer, element.Format ) || !HashValue( writer, element.InputSlot ) ||
             !HashValue( writer, element.AlignedByteOffset ) || !HashValue( writer, element.InputSlotClass ) ||
             !HashValue( writer, element.InstanceDataStepRate ) )
        {
            return false;
        }
    }

    if ( !HashValue( writer, desc.IBStripCutValue ) || !HashValue( writer, desc.PrimitiveTopologyType ) ||
         !HashValue( writer, desc.NumRenderTargets ) )
    {
        return false;
    }

    for ( DXGI_FORMAT format : desc.RTVFormats )
    {

        if ( !HashValue( writer, format ) )
        {
            return false;
        }
    }

    // CachedPSO is an acceleration result for this recipe, not part of the
    // semantic recipe. Including it would make a warm row address itself with
    // a different key than the cold PSO that produced those bytes.
    return HashValue( writer, desc.DSVFormat ) && HashValue( writer, desc.SampleDesc.Count ) &&
           HashValue( writer, desc.SampleDesc.Quality ) && HashValue( writer, desc.NodeMask ) &&
           HashValue( writer, desc.Flags );
}

bool EnsureCacheDirectory( wchar_t ( &directory )[MAX_PATH] )
{
    const DWORD overrideLength = GetEnvironmentVariableW( L"SKULLBONEZ_PSO_CACHE_DIR", directory,
                                                          static_cast<DWORD>( MAX_PATH ) );

    if ( overrideLength > 0 && overrideLength < MAX_PATH )
    {
        return CreateDirectoryW( directory, nullptr ) || GetLastError() == ERROR_ALREADY_EXISTS;
    }

    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW( L"LOCALAPPDATA", localAppData, static_cast<DWORD>( MAX_PATH ) );

    if ( length == 0 || length >= MAX_PATH )
    {
        return false;
    }

    wchar_t product[MAX_PATH] = {};

    if ( swprintf_s( product, L"%s\\SkullbonezCore", localAppData ) < 0 ||
         ( !CreateDirectoryW( product, nullptr ) && GetLastError() != ERROR_ALREADY_EXISTS ) ||
         swprintf_s( directory, L"%s\\PipelineCache", product ) < 0 )
    {
        return false;
    }

    return CreateDirectoryW( directory, nullptr ) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool EntryNameDigest( const wchar_t* name, std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES>& digest )
{

    if ( !name || std::wcslen( name ) != Dx12CachedPsoStore::ENTRY_NAME_CHARS - 1 )
    {
        return false;
    }

    auto nibble = []( wchar_t value ) -> int
    {

        if ( value >= L'0' && value <= L'9' )
        {
            return value - L'0';
        }

        if ( value >= L'a' && value <= L'f' )
        {
            return value - L'a' + 10;
        }

        return -1;
    };

    for ( std::size_t index = 0; index < digest.size(); ++index )
    {
        const int high = nibble( name[4 + index * 2] );
        const int low = nibble( name[5 + index * 2] );

        if ( high < 0 || low < 0 )
        {
            return false;
        }

        digest[index] = static_cast<std::uint8_t>( ( high << 4 ) | low );
    }

    return true;
}

bool WriteAll( HANDLE file, SkullbonezCore::Core::ByteView bytes )
{

    if ( bytes.size() > MAXDWORD )
    {
        return false;
    }

    DWORD written = 0;
    return WriteFile( file, bytes.data(), static_cast<DWORD>( bytes.size() ), &written, nullptr ) && written == bytes.size();
}
} // namespace

bool Dx12CachedPsoStore::BuildEntryName( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                         const std::array<std::uint8_t, DIGEST_BYTES>& manifestDigest,
                                         const std::array<std::uint8_t, DIGEST_BYTES>& rootSignatureDigest,
                                         wchar_t ( &outName )[ENTRY_NAME_CHARS] )
{
    Sha256Writer writer;
    std::array<std::uint8_t, DIGEST_BYTES> digest = {};
    const char schema[] = "skullbonez-pso-entry-v1";

    if ( !writer.Open() ||
         !writer.Write( SkullbonezCore::Core::ObjectBytes( std::span<const char>( schema, sizeof( schema ) - 1 ) ) ) ||
         !writer.Write( manifestDigest ) || !writer.Write( rootSignatureDigest ) || !HashGraphicsDesc( writer, desc ) ||
         !writer.Finish( digest ) )
    {
        return false;
    }

    wchar_t hex[65] = {};

    DigestHex( digest, hex );
    return swprintf_s( outName, L"pso-%s", hex ) > 0;
}

bool Dx12CachedPsoStore::BuildPersistentEntryNameForTest( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                                          const std::array<std::uint8_t, DIGEST_BYTES>& manifestDigest,
                                                          const std::array<std::uint8_t, DIGEST_BYTES>& rootSignatureDigest,
                                                          wchar_t ( &outName )[ENTRY_NAME_CHARS] )
{
    return BuildEntryName( desc, manifestDigest, rootSignatureDigest, outName );
}

bool Dx12CachedPsoStore::Initialize( SkullbonezCore::Core::ByteView rootSignatureBytes )
{
    Shutdown();

    if ( !ReadManifestDigest( m_manifestDigest ) || !HashBytes( rootSignatureBytes, m_rootSignatureDigest ) )
    {
        SkullbonezCore::Core::Log().WriteEventf( "dx12_pso_disk_cache_cold_start owner=Dx12PipelineOwner reason=identity_unavailable" );

        ++m_failures;
        return false;
    }

    wchar_t directory[MAX_PATH] = {};
    wchar_t manifestHex[65] = {};

    wchar_t rootHex[65] = {};

    DigestHex( m_manifestDigest, manifestHex );
    DigestHex( m_rootSignatureDigest, rootHex );

    if ( !EnsureCacheDirectory( directory ) ||
         swprintf_s( m_cachePath, L"%s\\%s-%.16s-%.16s.bin", directory, CACHE_SCHEMA, manifestHex, rootHex ) < 0 )
    {
        SkullbonezCore::Core::Log().WriteEventf( "dx12_pso_disk_cache_cold_start owner=Dx12PipelineOwner reason=cache_directory_unwritable" );

        ++m_failures;
        return false;
    }

    m_file = CreateFileW( m_cachePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                          nullptr );

    if ( m_file == INVALID_HANDLE_VALUE )
    {
        SkullbonezCore::Core::Log()
            .WriteEventf( "dx12_pso_disk_cache_open owner=Dx12PipelineOwner mode=cold bytes=0 cap=%llu path=%ls",
                          static_cast<unsigned long long>( MAX_BLOB_BYTES ), m_cachePath );

        return true;
    }

    LARGE_INTEGER fileSize = {};

    if ( !GetFileSizeEx( m_file, &fileSize ) || fileSize.QuadPart < static_cast<LONGLONG>( sizeof( CacheFileHeader ) ) ||
         fileSize.QuadPart > static_cast<LONGLONG>( MAX_BLOB_BYTES ) )
    {
        ++m_failures;
        SkullbonezCore::Core::Log()
            .WriteEventf( "dx12_pso_disk_cache_cold_start owner=Dx12PipelineOwner reason=read_failed_or_oversize "
                          "cap=%llu error=%lu",
                          static_cast<unsigned long long>( MAX_BLOB_BYTES ), GetLastError() );

        CloseHandle( m_file );
        m_file = INVALID_HANDLE_VALUE;
        return true;
    }

    m_mappedSize = static_cast<std::size_t>( fileSize.QuadPart );
    m_mapping = CreateFileMappingW( m_file, nullptr, PAGE_READONLY, 0, 0, nullptr );

    // Why: MapViewOfFile is the Win32 ABI boundary. Persistent cache parsing
    // immediately treats the bounded read-only mapping as immutable bytes.
    m_mappedBytes = m_mapping ? static_cast<const std::uint8_t*>( MapViewOfFile( m_mapping, FILE_MAP_READ, 0, 0, 0 ) )
                              : nullptr;

    CacheFileHeader header = {};

    if ( m_mappedBytes )
    {
        std::memcpy( &header, m_mappedBytes, sizeof( header ) );
    }

    bool valid = m_mappedBytes && std::memcmp( header.magic, FILE_MAGIC, sizeof( FILE_MAGIC ) ) == 0 &&
                 header.version == 1 && header.entryCount <= m_mappedEntries.size() &&
                 std::memcmp( header.manifestDigest, m_manifestDigest.data(), m_manifestDigest.size() ) == 0 &&
                 std::memcmp( header.rootDigest, m_rootSignatureDigest.data(), m_rootSignatureDigest.size() ) == 0;

    const std::uint8_t* cursor = valid ? m_mappedBytes + sizeof( CacheFileHeader ) : nullptr;
    const std::uint8_t* end = valid ? m_mappedBytes + m_mappedSize : nullptr;

    for ( std::uint32_t index = 0; valid && index < header.entryCount; ++index )
    {
        valid = static_cast<std::size_t>( end - cursor ) >= sizeof( CacheEntryHeader );

        if ( !valid )
        {
            break;
        }

        CacheEntryHeader entry = {};

        std::memcpy( &entry, cursor, sizeof( entry ) );
        cursor += sizeof( CacheEntryHeader );
        valid = entry.blobBytes > 0 && static_cast<std::size_t>( end - cursor ) >= entry.blobBytes;

        if ( valid )
        {
            auto& target = m_mappedEntries[m_mappedEntryCount++];
            std::memcpy( target.digest.data(), entry.digest, target.digest.size() );
            target.bytes = cursor;
            target.size = entry.blobBytes;
            cursor += entry.blobBytes;
        }
    }

    valid = valid && cursor == end;

    if ( !valid )
    {
        ++m_failures;
        SkullbonezCore::Core::Log()
            .WriteEventf( "dx12_pso_disk_cache_cold_start owner=Dx12PipelineOwner reason=corrupt_format bytes=%llu",
                          static_cast<unsigned long long>( m_mappedSize ) );

        m_mappedEntryCount = 0;
    }

    m_loadedBytes = valid ? m_mappedSize : 0;
    SkullbonezCore::Core::Log()
        .WriteEventf( "dx12_pso_disk_cache_open owner=Dx12PipelineOwner mode=%s bytes=%llu cap=%llu path=%ls",
                      valid ? "warm" : "cold", static_cast<unsigned long long>( m_loadedBytes ),
                      static_cast<unsigned long long>( MAX_BLOB_BYTES ), m_cachePath );

    return true;
}

bool Dx12CachedPsoStore::Attach( D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc )
{
    wchar_t name[ENTRY_NAME_CHARS] = {};
    std::array<std::uint8_t, DIGEST_BYTES> digest = {};

    if ( !BuildEntryName( desc, m_manifestDigest, m_rootSignatureDigest, name ) || !EntryNameDigest( name, digest ) )
    {
        ++m_failures;
        return false;
    }

    for ( std::size_t index = 0; index < m_mappedEntryCount; ++index )
    {
        const MappedEntry& entry = m_mappedEntries[index];

        if ( !entry.rejected && entry.digest == digest )
        {
            desc.CachedPSO = { entry.bytes, entry.size };

            ++m_hits;
            return true;
        }
    }

    ++m_misses;
    return false;
}

void Dx12CachedPsoStore::RejectAttached( D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc )
{
    wchar_t name[ENTRY_NAME_CHARS] = {};
    std::array<std::uint8_t, DIGEST_BYTES> digest = {};

    const D3D12_CACHED_PIPELINE_STATE attached = desc.CachedPSO;
    desc.CachedPSO = {};

    if ( !attached.pCachedBlob || !BuildEntryName( desc, m_manifestDigest, m_rootSignatureDigest, name ) ||
         !EntryNameDigest( name, digest ) )
    {
        return;
    }

    for ( std::size_t index = 0; index < m_mappedEntryCount; ++index )
    {

        if ( m_mappedEntries[index].digest == digest )
        {
            m_mappedEntries[index].rejected = true;
            break;
        }
    }

    ++m_failures;
}

void Dx12CachedPsoStore::Store( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, ID3D12PipelineState* pipeline )
{

    if ( !pipeline )
    {
        return;
    }

    wchar_t name[ENTRY_NAME_CHARS] = {};

    if ( !BuildEntryName( desc, m_manifestDigest, m_rootSignatureDigest, name ) )
    {
        ++m_failures;
        return;
    }

    std::array<std::uint8_t, DIGEST_BYTES> digest = {};

    if ( !EntryNameDigest( name, digest ) )
    {
        ++m_failures;
        return;
    }

    for ( std::size_t index = 0; index < m_liveEntryCount; ++index )
    {

        if ( m_liveEntries[index].digest == digest )
        {
            return;
        }
    }

    if ( m_liveEntryCount >= m_liveEntries.size() )
    {
        ++m_failures;
        return;
    }

    m_liveEntries[m_liveEntryCount++] = { digest, pipeline };

    ++m_stores;
}

void Dx12CachedPsoStore::Persist()
{

    if ( !m_cachePath[0] || m_liveEntryCount == 0 )
    {
        return;
    }

    std::array<ComPtr<ID3DBlob>, 96> blobs;
    std::size_t totalBytes = sizeof( CacheFileHeader );
    std::uint32_t blobCount = 0;

    for ( std::size_t index = 0; index < m_liveEntryCount; ++index )
    {

        if ( m_liveEntries[index].pipeline &&
             SUCCEEDED( m_liveEntries[index].pipeline->GetCachedBlob( blobs[index].GetAddressOf() ) ) && blobs[index] )
        {
            totalBytes += sizeof( CacheEntryHeader ) + blobs[index]->GetBufferSize();
            ++blobCount;
        }
    }

    if ( blobCount == 0 || totalBytes > MAX_BLOB_BYTES )
    {
        ++m_failures;
        SkullbonezCore::Core::Log()
            .WriteEventf( "dx12_pso_disk_cache_write_skipped owner=Dx12PipelineOwner bytes=%llu cap=%llu",
                          static_cast<unsigned long long>( totalBytes ), static_cast<unsigned long long>( MAX_BLOB_BYTES ) );

        return;
    }

    wchar_t temporary[MAX_PATH] = {};

    if ( swprintf_s( temporary, L"%s.tmp", m_cachePath ) < 0 )
    {
        ++m_failures;
        return;
    }

    HANDLE file = CreateFileW( temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
    CacheFileHeader header = {};

    std::memcpy( header.magic, FILE_MAGIC, sizeof( FILE_MAGIC ) );
    header.version = 1;
    header.entryCount = blobCount;
    std::memcpy( header.manifestDigest, m_manifestDigest.data(), m_manifestDigest.size() );
    std::memcpy( header.rootDigest, m_rootSignatureDigest.data(), m_rootSignatureDigest.size() );
    bool wrote = file != INVALID_HANDLE_VALUE && WriteAll( file, SkullbonezCore::Core::ObjectBytes( header ) );

    for ( std::size_t index = 0; wrote && index < m_liveEntryCount; ++index )
    {

        if ( !blobs[index] )
        {
            continue;
        }

        CacheEntryHeader entry = {};

        std::memcpy( entry.digest, m_liveEntries[index].digest.data(), m_liveEntries[index].digest.size() );
        entry.blobBytes = static_cast<std::uint32_t>( blobs[index]->GetBufferSize() );

        // Why: ID3DBlob exposes cached driver bytes through its COM void-pointer
        // ABI. Narrow the synchronous file write here and retain no raw pointer.
        const SkullbonezCore::Core::ByteView blobBytes = { static_cast<const std::uint8_t*>( blobs[index]->GetBufferPointer() ),
                                                           blobs[index]->GetBufferSize() };

        wrote = WriteAll( file, SkullbonezCore::Core::ObjectBytes( entry ) ) && WriteAll( file, blobBytes );
    }

    wrote = wrote && FlushFileBuffers( file );

    if ( file != INVALID_HANDLE_VALUE )
    {
        CloseHandle( file );
    }

    if ( !wrote || !MoveFileExW( temporary, m_cachePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
    {
        DeleteFileW( temporary );
        ++m_failures;
        SkullbonezCore::Core::Log()
            .WriteEventf( "dx12_pso_disk_cache_write_failed owner=Dx12PipelineOwner bytes=%llu error=%lu",
                          static_cast<unsigned long long>( totalBytes ), GetLastError() );

        return;
    }

    SkullbonezCore::Core::Log()
        .WriteEventf( "dx12_pso_disk_cache_summary owner=Dx12PipelineOwner hits=%u misses=%u stores=%u failures=%u "
                      "loaded_bytes=%llu saved_bytes=%llu capacity=%u",
                      m_hits, m_misses, m_stores, m_failures, static_cast<unsigned long long>( m_loadedBytes ),
                      static_cast<unsigned long long>( totalBytes ), 96u );
}

void Dx12CachedPsoStore::Shutdown()
{

    // Lifetime: mapped warm bytes are needed only while creating PSOs. Close
    // the old file before atomic replacement so Windows cannot reject rename
    // with a sharing violation. Live PSOs remain valid for GetCachedBlob below.

    if ( m_mappedBytes )
    {
        UnmapViewOfFile( m_mappedBytes );
    }

    if ( m_mapping )
    {
        CloseHandle( m_mapping );
    }

    if ( m_file != INVALID_HANDLE_VALUE )
    {
        CloseHandle( m_file );
    }

    Persist();
    m_file = INVALID_HANDLE_VALUE;
    m_mapping = nullptr;
    m_mappedBytes = nullptr;
    m_mappedSize = 0;
    m_mappedEntries = {};
    m_mappedEntryCount = 0;
    m_liveEntries = {};
    m_liveEntryCount = 0;
    m_cachePath[0] = L'\0';
    m_manifestDigest = {};
    m_rootSignatureDigest = {};
    m_hits = 0;
    m_misses = 0;
    m_stores = 0;
    m_failures = 0;
    m_loadedBytes = 0;
}
} // namespace SkullbonezCore::Rendering
