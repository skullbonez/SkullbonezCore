/*
File: ShaderBytecodeManifest.cpp
Purpose:
  Verifies baked shader source/bytecode hashes and opens DXIL reflection data.

Summary:
  The bake tool owns compilation. This file is only a verifier and loader: it
  hashes the authored source and baked bytes, finds their manifest row, and
  publishes a blob after every identity check succeeds. The backend cache keeps
  complete raster pairs so later lazy owners do not reparse the manifest.

Glossary:
  DXIL container reflection: Compiler metadata used to discover constant-buffer
    offsets without compiling source at startup.

Invariants:
  - Verification occurs during renderer startup or the explicit BackendInit-
    labelled developer reload transaction.
  - Hash comparison uses lowercase SHA-256 text emitted by the bake tool.
  - Developer hot reload is opt-in through one exact command-line token.
  - A cache row is visible only after both vertex and pixel stages verify.

Related:
  - tools/bake_shaders.py
  - ShaderDX12.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "ShaderBytecodeManifest.h"
#include "../ShaderReflectionContracts.h"
#include "../../Core/WindowConstants.h"

#include "../../../ThirdPtySource/nlohmann/json.hpp"

#include "../../Core/PlatformWin32.h"
#include <bcrypt.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <utility>

#pragma comment( lib, "bcrypt.lib" )

using Microsoft::WRL::ComPtr;
using nlohmann::json;

namespace SkullbonezCore::Rendering
{
namespace
{
// This projection follows the lazy shader owners reachable from the first
// gameplay frame. It is unconditional because scene/debug/cinematic policy can
// select any row after BackendInit without changing the backend contract.
constexpr std::array<const char*, 13> FIRST_GAMEPLAY_SHADER_BASE_NAMES = {
    "shaders/lit_textured",            // Terrain receiver.
    "shaders/shadow_depth",            // Terrain shadow caster.
    "shaders/unlit_textured",          // Authored skybox.
    "shaders/lit_textured_instanced",  // Primitive/object receiver.
    "shaders/shadow_depth_instanced",  // Primitive/object shadow caster.
    "shaders/water_calm",              // Calm WorldEnvironment water path.
    "shaders/water_ocean",             // Ocean WorldEnvironment water path.
    "shaders/collision_visualizer",    // CollisionVisualizer overlay.
    "shaders/sky_atmosphere",          // Cinematic SkyPass.
    "shaders/post_volumetric_light",   // VolumetricPass.
    "shaders/post_tonemap",            // TonemapPass.
    "shaders/launcher_laser",          // DebugOverlayPass launcher path.
    "shaders/ui_render_target_preview" // UiDrawSubmission preview path.
};

bool ReadBytes( const std::string& path, std::string& bytes )
{
    std::ifstream file( path, std::ios::binary );

    if ( !file.is_open() )
    {
        return false;
    }

    bytes = std::string( std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() );
    return file.good() || file.eof();
}

bool Sha256Hex( std::string& bytes, std::string& hex )
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    std::array<uint8_t, 1024> object = {};

    std::array<uint8_t, 32> digest = {};

    NTSTATUS status = BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 );

    if ( status >= 0 )
    {
        // Why: BCryptGetProperty exposes arbitrary property storage as
        // mutable bytes; the requested property is exactly one DWORD.
        status = BCryptGetProperty( algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>( &objectBytes ),
                                    sizeof( objectBytes ), &resultBytes, 0 );
    }

    if ( status >= 0 && objectBytes > object.size() )
    {
        BCryptCloseAlgorithmProvider( algorithm, 0 );
        return false;
    }

    if ( status >= 0 )
    {
        status = BCryptCreateHash( algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0 );
    }

    if ( status >= 0 && !bytes.empty() )
    {
        // Why: BCryptHashData's ABI accepts mutable bytes. ReadBytes owns this
        // writable string, and BCrypt borrows it synchronously without mutation.
        status = BCryptHashData( hash, reinterpret_cast<PUCHAR>( bytes.data() ), static_cast<ULONG>( bytes.size() ), 0 );
    }

    if ( status >= 0 )
    {
        status = BCryptFinishHash( hash, digest.data(), static_cast<ULONG>( digest.size() ), 0 );
    }

    if ( hash )
    {
        BCryptDestroyHash( hash );
    }

    if ( algorithm )
    {
        BCryptCloseAlgorithmProvider( algorithm, 0 );
    }

    if ( status < 0 )
    {
        return false;
    }

    static constexpr char digits[] = "0123456789abcdef";
    char hashText[65] = {};

    for ( size_t i = 0; i < digest.size(); ++i )
    {
        hashText[i * 2] = digits[digest[i] >> 4];
        hashText[i * 2 + 1] = digits[digest[i] & 0x0f];
    }

    hex = hashText;
    return true;
}

std::string NormalizePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    const size_t dataRoot = normalized.find( "SkullbonezData/" );
    return dataRoot == std::string::npos ? normalized : normalized.substr( dataRoot );
}

std::string ParentPath( const std::string& path )
{
    const size_t slash = path.find_last_of( "/\\" );
    return slash == std::string::npos ? std::string() : path.substr( 0, slash + 1 );
}

bool CommandLineHasExactToken( const char* expected )
{
    const char* cursor = GetCommandLineA();

    while ( cursor && *cursor )
    {
        while ( *cursor && std::isspace( static_cast<unsigned char>( *cursor ) ) )
        {
            ++cursor;
        }

        const bool quoted = *cursor == '"';

        if ( quoted )
        {
            ++cursor;
        }

        const char* begin = cursor;

        while ( *cursor && ( quoted ? *cursor != '"' : !std::isspace( static_cast<unsigned char>( *cursor ) ) ) )
        {
            ++cursor;
        }

        if ( static_cast<size_t>( cursor - begin ) == std::strlen( expected ) &&
             std::strncmp( begin, expected, std::strlen( expected ) ) == 0 )
        {
            return true;
        }

        if ( quoted && *cursor == '"' )
        {
            ++cursor;
        }
    }

    return false;
}

bool ResourceShapeMatches( const GeneratedShaderReflection::Resource& expected, const D3D12_SHADER_INPUT_BIND_DESC& actual )
{
    const bool typeMatches = ( std::strcmp( expected.type, "cbuffer" ) == 0 && actual.Type == D3D_SIT_CBUFFER ) ||
                             ( std::strcmp( expected.type, "sampler" ) == 0 && actual.Type == D3D_SIT_SAMPLER ) ||
                             ( std::strcmp( expected.type, "texture" ) == 0 && actual.Type == D3D_SIT_TEXTURE ) ||
                             ( std::strcmp( expected.type, "uav" ) == 0 && actual.Type == D3D_SIT_UAV_RWTYPED );

    const bool dimensionMatches = ( std::strcmp( expected.dimension, "na" ) == 0 &&
                                    actual.Dimension == D3D_SRV_DIMENSION_UNKNOWN ) ||
                                  ( std::strcmp( expected.dimension, "buffer" ) == 0 &&
                                    actual.Dimension == D3D_SRV_DIMENSION_BUFFER ) ||
                                  ( std::strcmp( expected.dimension, "2d" ) == 0 &&
                                    actual.Dimension == D3D_SRV_DIMENSION_TEXTURE2D ) ||
                                  ( std::strcmp( expected.dimension, "2darray" ) == 0 &&
                                    actual.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY ) ||
                                  ( std::strcmp( expected.dimension, "3d" ) == 0 &&
                                    actual.Dimension == D3D_SRV_DIMENSION_TEXTURE3D ) ||
                                  ( std::strcmp( expected.dimension, "cube" ) == 0 &&
                                    actual.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE );

    return typeMatches && dimensionMatches;
}

bool ValidateLoadedReflection( const char* hlslPath, const char* stage, ID3DBlob* blob, std::string& outError )
{
    const auto* expectedStage = FindGeneratedShaderStage( hlslPath, stage );

    if ( !expectedStage )
    {
        outError = "generated reflection has no matching stage";
        return false;
    }

    ComPtr<ID3D12ShaderReflection> reflection;
    HRESULT result = E_FAIL;

    if ( !ReflectShaderBytecode( blob, reflection, result ) )
    {
        outError = "cannot reflect loaded shader container";
        return false;
    }

    D3D12_SHADER_DESC shader = {};

    if ( FAILED( reflection->GetDesc( &shader ) ) )
    {
        outError = "cannot query loaded shader reflection";
        return false;
    }

    for ( std::uint32_t expectedIndex = 0; expectedIndex < expectedStage->fieldCount; ++expectedIndex )
    {
        const auto& expected = GeneratedShaderReflection::Fields[expectedStage->fieldStart + expectedIndex];
        const std::uint32_t expectedBufferSize = GeneratedCbufferSize( *expectedStage, expected.cbuffer );
        bool matched = false;

        for ( UINT cbIndex = 0; cbIndex < shader.ConstantBuffers && !matched; ++cbIndex )
        {
            ID3D12ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByIndex( cbIndex );
            D3D12_SHADER_BUFFER_DESC cbDesc = {};

            if ( !cb || FAILED( cb->GetDesc( &cbDesc ) ) || !cbDesc.Name ||
                 std::strcmp( cbDesc.Name, expected.cbuffer ) != 0 || cbDesc.Size != expectedBufferSize )
            {
                continue;
            }

            for ( UINT variableIndex = 0; variableIndex < cbDesc.Variables; ++variableIndex )
            {
                D3D12_SHADER_VARIABLE_DESC variable = {};

                ID3D12ShaderReflectionVariable* reflectedVariable = cb->GetVariableByIndex( variableIndex );
                matched = reflectedVariable && SUCCEEDED( reflectedVariable->GetDesc( &variable ) ) && variable.Name &&
                          std::strcmp( variable.Name, expected.name ) == 0 && variable.StartOffset == expected.offset &&
                          variable.Size == expected.size;

                if ( matched )
                {
                    break;
                }
            }
        }

        if ( !matched )
        {
            outError = std::string( "cbuffer field metadata mismatch: " ) + expected.name;
            return false;
        }
    }

    if ( shader.BoundResources != expectedStage->resourceCount )
    {
        outError = "bound-resource count metadata mismatch";
        return false;
    }

    for ( std::uint32_t expectedIndex = 0; expectedIndex < expectedStage->resourceCount; ++expectedIndex )
    {
        const auto& expected = GeneratedShaderReflection::Resources[expectedStage->resourceStart + expectedIndex];
        bool matched = false;

        for ( UINT resourceIndex = 0; resourceIndex < shader.BoundResources; ++resourceIndex )
        {
            D3D12_SHADER_INPUT_BIND_DESC resource = {};

            if ( FAILED( reflection->GetResourceBindingDesc( resourceIndex, &resource ) ) || !resource.Name )
            {
                continue;
            }

            const char registerClass = resource.Type == D3D_SIT_CBUFFER   ? 'b'
                                       : resource.Type == D3D_SIT_SAMPLER ? 's'
                                       : resource.Type == D3D_SIT_TEXTURE ? 't'
                                                                          : 'u';

            matched = std::strcmp( resource.Name, expected.name ) == 0 && registerClass == expected.registerClass &&
                      resource.BindPoint == expected.slot && resource.Space == expected.space &&
                      ResourceShapeMatches( expected, resource );

            if ( matched )
            {
                break;
            }
        }

        if ( !matched )
        {
            outError = std::string( "resource metadata mismatch: " ) + expected.name;
            return false;
        }
    }

    // Input signatures are compared against the concrete CPU layout when the
    // raster PSO is created. Compute stages intentionally have no CPU vertex
    // layout; their cbuffer and b/t/s/u metadata is still checked above.
    return true;
}
} // namespace

bool DevShaderHotReloadEnabled()
{
    // Invariant: this launch policy is immutable after process startup, so the
    // renderer can query it from the manual cold utility action.
    static const bool enabled = CommandLineHasExactToken( "--dev-shader-hot-reload" );
    return enabled;
}


ShaderBytecodeManifestCache::ProgramLoadSummary ShaderBytecodeManifestCache::LoadProgram( const char* hlslPath,
                                                                                          ComPtr<ID3DBlob>& outVertex,
                                                                                          ComPtr<ID3DBlob>& outPixel,
                                                                                          std::string& outError )
{
    ProgramLoadSummary summary;
    outVertex.Reset();
    outPixel.Reset();

    if ( !hlslPath || hlslPath[0] == '\0' )
    {
        outError = "missing shader path";
        return summary;
    }

    for ( std::size_t index = 0; index < m_programCount; ++index )
    {
        const Program& cached = m_programs[index];

        if ( cached.sourcePath == hlslPath )
        {
            outVertex = cached.vertexBytecode;
            outPixel = cached.pixelBytecode;
            summary.complete = true;
            summary.cacheHit = true;
            return summary;
        }
    }

    ComPtr<ID3DBlob> vertex;
    ComPtr<ID3DBlob> pixel;
    ++summary.stageLoads;

    if ( !LoadManifestCurrentShaderBytecode( hlslPath, "vs", vertex, outError ) )
    {
        return summary;
    }

    ++summary.stageLoads;

    if ( !LoadManifestCurrentShaderBytecode( hlslPath, "ps", pixel, outError ) )
    {
        return summary;
    }

    if ( m_programCount >= m_programs.size() )
    {
        outError = "shader bytecode cache capacity exhausted";
        return summary;
    }

    // Lifetime: cache rows own shared DXIL blobs for the backend epoch. Lazy
    // ShaderDX12 owners take ComPtr references without reopening the manifest.
    Program& destination = m_programs[m_programCount++];
    destination.sourcePath = hlslPath;
    destination.vertexBytecode = vertex;
    destination.pixelBytecode = pixel;
    outVertex = std::move( vertex );
    outPixel = std::move( pixel );
    summary.complete = true;
    summary.newlyPublished = true;
    return summary;
}


ShaderBytecodeManifestCache::PreparationSummary
ShaderBytecodeManifestCache::PrepareFirstGameplayPrograms( std::string& outError )
{
    PreparationSummary summary;
    std::string firstError;

    for ( const char* baseName : FIRST_GAMEPLAY_SHADER_BASE_NAMES )
    {
        ++summary.attempted;

        // Invariant: fixed path assembly stays on the stack, so a fully warm
        // invocation performs neither manifest IO nor path-string allocation.
        char sourcePath[MAX_PATH] = {};
        ComPtr<ID3DBlob> vertex;
        ComPtr<ID3DBlob> pixel;
        std::string programError;
        const int sourcePathLength = std::snprintf( sourcePath, sizeof( sourcePath ), "%s%s.hlsl", DATA_ROOT, baseName );
        const ProgramLoadSummary program = sourcePathLength > 0 &&
                                                   static_cast<std::size_t>( sourcePathLength ) < sizeof( sourcePath )
                                               ? LoadProgram( sourcePath, vertex, pixel, programError )
                                               : ProgramLoadSummary {};
        summary.stageLoads += program.stageLoads;
        summary.newlyPublished += program.newlyPublished ? 1u : 0u;
        summary.cacheHits += program.cacheHit ? 1u : 0u;

        if ( program.complete )
        {
            ++summary.complete;
        }
        else
        {
            if ( firstError.empty() )
            {
                firstError = std::string( sourcePath ) + ": " + programError;
            }
        }
    }

    outError = std::move( firstError );
    return summary;
}


void ShaderBytecodeManifestCache::Reset()
{
    m_programs = {};
    m_programCount = 0;
}

bool LoadManifestCurrentShaderBytecode( const char* hlslPath, const char* stage, ComPtr<ID3DBlob>& outBlob,
                                        std::string& outError )
{
    outBlob.Reset();

    if ( !hlslPath || !stage )
    {
        outError = "missing shader path or stage";
        return false;
    }

    const std::string sourcePath = hlslPath;
    const std::string manifestPath = ParentPath( sourcePath ) + "shader_manifest.json";
    std::ifstream manifestFile( manifestPath, std::ios::binary );

    if ( !manifestFile.is_open() )
    {
        outError = "cannot open freshness manifest " + manifestPath;
        return false;
    }

    const json manifest = json::parse( manifestFile, nullptr, false );

    if ( manifest.is_discarded() || !manifest.is_object() )
    {
        outError = "invalid freshness manifest " + manifestPath;
        return false;
    }

    const auto entriesIt = manifest.find( "entries" );

    if ( entriesIt == manifest.end() || !entriesIt->is_array() )
    {
        outError = "freshness manifest has no entries array";
        return false;
    }

    const std::string normalizedSource = NormalizePath( sourcePath );
    const json* matched = nullptr;

    for ( const json& entry : *entriesIt )
    {
        const auto sourceIt = entry.find( "source" );
        const auto stageIt = entry.find( "stage" );

        if ( sourceIt != entry.end() && sourceIt->is_string() && stageIt != entry.end() && stageIt->is_string() &&
             sourceIt->get_ref<const std::string&>() == normalizedSource && stageIt->get_ref<const std::string&>() == stage )
        {
            matched = &entry;
            break;
        }
    }

    if ( !matched )
    {
        outError = "freshness manifest has no row for " + normalizedSource + " stage=" + stage;
        return false;
    }

    const auto sourceHashIt = matched->find( "source_sha256" );
    const auto bytecodeHashIt = matched->find( "bytecode_sha256" );
    const auto bytecodePathIt = matched->find( "bytecode" );

    if ( sourceHashIt == matched->end() || !sourceHashIt->is_string() || bytecodeHashIt == matched->end() ||
         !bytecodeHashIt->is_string() || bytecodePathIt == matched->end() || !bytecodePathIt->is_string() )
    {
        outError = "freshness manifest row is incomplete for " + normalizedSource;
        return false;
    }

    std::string sourceBytes;
    std::string bytecodeBytes;
    std::string sourceHash;
    std::string bytecodeHash;
    const std::string bytecodePath = bytecodePathIt->get_ref<const std::string&>();

    if ( !ReadBytes( sourcePath, sourceBytes ) || !Sha256Hex( sourceBytes, sourceHash ) )
    {
        outError = "cannot hash shader source " + sourcePath;
        return false;
    }

    if ( sourceHash != sourceHashIt->get_ref<const std::string&>() )
    {
        outError = "shader source is newer than baked manifest: " + normalizedSource;
        return false;
    }

    if ( !ReadBytes( bytecodePath, bytecodeBytes ) || !Sha256Hex( bytecodeBytes, bytecodeHash ) )
    {
        outError = "cannot hash baked shader " + bytecodePath;
        return false;
    }

    if ( bytecodeHash != bytecodeHashIt->get_ref<const std::string&>() )
    {
        outError = "baked shader hash mismatch: " + bytecodePath;
        return false;
    }

    const HRESULT createResult = D3DCreateBlob( bytecodeBytes.size(), outBlob.ReleaseAndGetAddressOf() );

    if ( FAILED( createResult ) || !outBlob )
    {
        outError = "cannot allocate baked shader blob " + bytecodePath;
        return false;
    }

    std::memcpy( outBlob->GetBufferPointer(), bytecodeBytes.data(), bytecodeBytes.size() );

    if ( !ValidateLoadedReflection( hlslPath, stage, outBlob.Get(), outError ) )
    {
        outBlob.Reset();
        outError = "shader reflection metadata rejected owner=ShaderBytecodeManifest: " + outError;
        return false;
    }

    return true;
}

bool ReflectShaderBytecode( ID3DBlob* blob, ComPtr<ID3D12ShaderReflection>& outReflection, HRESULT& outResult )
{
    outReflection.Reset();

    if ( !blob )
    {
        outResult = E_POINTER;
        return false;
    }

    ComPtr<IDxcUtils> utils;
    outResult = DxcCreateInstance( CLSID_DxcUtils, IID_PPV_ARGS( &utils ) );

    if ( SUCCEEDED( outResult ) && utils )
    {
        DxcBuffer buffer = {};
        buffer.Ptr = blob->GetBufferPointer();
        buffer.Size = blob->GetBufferSize();
        buffer.Encoding = DXC_CP_ACP;
        outResult = utils->CreateReflection( &buffer, IID_PPV_ARGS( &outReflection ) );

        if ( SUCCEEDED( outResult ) && outReflection )
        {
            return true;
        }
    }

    // DXC container compatibility: D3DReflect is the final reflection route on
    // machines where the preferred container-reflection interface is absent.
    // Why: D3DReflect is a COM ABI that publishes an interface through an
    // untyped output pointer; ComPtr immediately owns the typed result.
    outResult = D3DReflect( blob->GetBufferPointer(), blob->GetBufferSize(), IID_ID3D12ShaderReflection,
                            reinterpret_cast<void**>( outReflection.ReleaseAndGetAddressOf() ) );

    return SUCCEEDED( outResult ) && outReflection;
}
} // namespace SkullbonezCore::Rendering
