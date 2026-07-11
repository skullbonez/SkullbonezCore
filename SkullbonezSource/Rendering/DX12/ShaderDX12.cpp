/*
File: SkullbonezSource/Rendering/DX12/ShaderDX12.cpp
Purpose:
  Loads, reflects, and binds baked shaders for the DX12 renderer.

Mental model:
  The offline bake owns compilation. This wrapper accepts manifest-current
  DXIL, reflects its constant layout during cold startup, and publishes the
  bytecode and uniform uploads needed by pipeline draws.

Glossary:
  Upload arena: Frame-scoped CPU-visible staging memory used for packed shader
  constants before a draw binds their GPU address.
  CBV (Constant Buffer View): Descriptor or root binding that lets shaders read
  a packed block of constants.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Address zero means constant upload failed; FlushCB must not dereference it
    or clear the dirty bit.
  - Runtime source compilation requires the explicit developer launch option.

Related:
  - SkullbonezSource/Rendering/DX12/ShaderDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "ShaderDX12.h"
#include "ShaderBytecodeManifest.h"
#include "RenderBackendDX12.h"
#include "../ShaderContracts.h"
#include "../ShaderReflectionContracts.h"
#include "../../Core/Log.h"
#include <d3d11shader.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>


using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using Microsoft::WRL::ComPtr;


namespace
{
size_t HashShaderBytecode( ID3DBlob* blob )
{
    // Why: PSO cache keys must survive scene reloads. Shader blobs can be
    // reallocated at new addresses even when their compiled bytecode is
    // identical, so pointer identity is too volatile for long stress runs.
    if ( !blob )
    {
        return 0;
    }

    const auto* bytes = static_cast<const uint8_t*>( blob->GetBufferPointer() );
    const SIZE_T size = blob->GetBufferSize();
    size_t hash = sizeof( size_t ) >= 8 ? static_cast<size_t>( 1469598103934665603ull ) : 2166136261u;
    const size_t prime = sizeof( size_t ) >= 8 ? static_cast<size_t>( 1099511628211ull ) : 16777619u;
    for ( SIZE_T i = 0; i < size; ++i )
    {
        hash ^= static_cast<size_t>( bytes[i] );
        hash *= prime;
    }
    hash ^= static_cast<size_t>( size );
    hash *= prime;
    return hash;
}
} // namespace


ShaderDX12::ShaderDX12( Dx12RenderDevice& device,
                        Dx12PipelineOwner& pipeline,
                        Dx12UploadReservations& uploadReservations )
    : m_device( device ), m_pipeline( pipeline ), m_uploadReservations( uploadReservations ), m_cbReflectedSize( 0 ),
      m_cbSize( 0 ), m_cbDirty( false ), m_vsBytecodeHash( 0 ), m_psBytecodeHash( 0 ), m_contract( nullptr )
{
}


ShaderDX12::~ShaderDX12() = default;


bool ShaderDX12::Compile( const char* hlslPath )
{
    m_sourcePath = hlslPath ? hlslPath : "";
    m_contract = FindShaderProgramDesc( hlslPath );
    m_uniformMap.clear();
    m_cbReflectedSize = 0;
    m_cbSize = 0;
    m_cbData.clear();
    m_vsBytecodeHash = 0;
    m_psBytecodeHash = 0;
#ifdef _DEBUG
    m_resourceMap.clear();
#endif

    std::string loadError;
    bool loadedBaked = LoadManifestCurrentShaderBytecode( hlslPath, "vs", m_vsBlob, loadError ) &&
                       LoadManifestCurrentShaderBytecode( hlslPath, "ps", m_psBlob, loadError );
    if ( !loadedBaked )
    {
        // Lane R: stale/missing authored assets fail startup. Source compilation
        // is retained only as an explicit developer escape hatch until hot reload
        // owns the same cold path.
        if ( !DevShaderSourceCompileEnabled() )
        {
            Log().WriteEventf( "dx12_shader_bytecode_rejected path=%s reason=%s",
                               hlslPath ? hlslPath : "<null>",
                               loadError.c_str() );
            Log().FlushAll();
            return false;
        }

        std::ifstream file( hlslPath, std::ios::binary );
        if ( !file.is_open() )
        {
            Log().WriteEventf( "dx12_shader_file_open_failed path=%s", hlslPath ? hlslPath : "<null>" );
            Log().FlushAll();
            return false;
        }
        const std::string source( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        Log().WriteEventf( "dx12_shader_dev_source_fallback path=%s reason=%s",
                           hlslPath ? hlslPath : "<null>",
                           loadError.c_str() );
        fprintf( stdout,
                 "[shader] DEV source fallback path=%s reason=%s\n",
                 hlslPath ? hlslPath : "<null>",
                 loadError.c_str() );
        fflush( stdout );

        auto compileFallback = [&]( const char* entry, const char* target, const char* stage, ComPtr<ID3DBlob>& blob )
        {
            ComPtr<ID3DBlob> errors;
            const HRESULT result = D3DCompile( source.c_str(),
                                               source.size(),
                                               hlslPath,
                                               nullptr,
                                               D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                               entry,
                                               target,
                                               flags,
                                               0,
                                               blob.ReleaseAndGetAddressOf(),
                                               errors.GetAddressOf() );
            if ( FAILED( result ) )
            {
                const char* message = errors ? static_cast<const char*>( errors->GetBufferPointer() ) : "<none>";
                Log().WriteEventf( "dx12_shader_compile_failed stage=%s hresult=0x%08X path=%s message=%s",
                                   stage,
                                   static_cast<unsigned int>( result ),
                                   hlslPath ? hlslPath : "<null>",
                                   message );
                Log().FlushAll();
                return false;
            }
            return true;
        };
        if ( !compileFallback( "main_vs", "vs_5_0", "vs", m_vsBlob ) ||
             !compileFallback( "main_ps", "ps_5_0", "ps", m_psBlob ) )
        {
            return false;
        }
    }

    m_vsBytecodeHash = HashShaderBytecode( m_vsBlob.Get() );
    m_psBytecodeHash = HashShaderBytecode( m_psBlob.Get() );

    if ( m_contract )
    {
        std::string contractError;
        if ( !ValidateGeneratedShaderProgramContract( hlslPath, *m_contract, contractError ) )
        {
            // Lane R: authored shader assets are external startup inputs. Reject
            // a stale CPU/DXIL ABI with the owning shader and exact mismatch.
            Log().WriteEventf( "dx12_shader_reflection_contract_rejected owner=ShaderDX12 path=%s reason=%s",
                               hlslPath ? hlslPath : "<null>",
                               contractError.c_str() );
            Log().FlushAll();
            return false;
        }
    }

    // Reflect both stages so PS-only post/sky uniforms are visible to SetFloat/SetVec*.
    if ( !ReflectCB( m_vsBlob.Get(), hlslPath, "vs" ) || !ReflectCB( m_psBlob.Get(), hlslPath, "ps" ) )
    {
        return false;
    }
#ifdef _DEBUG
    if ( m_contract )
    {
        m_contractUniformsSet.assign( m_contract->uniformCount, static_cast<uint8_t>( 0 ) );
        m_contractMissingRequiredLogged.assign( m_contract->uniformCount, static_cast<uint8_t>( 0 ) );
        ReportContractReflectionMismatch();
    }
#endif

    return true;
}


bool ShaderDX12::ReflectCB( ID3DBlob* blob, const char* hlslPath, const char* stageName )
{
    // Use D3DReflect to inspect the compiled shader bytecode and discover constant buffer layouts.
    // Reflection tells us the name, offset, and size of each variable in the shader's cbuffer,
    // so we can write data at the correct byte offsets when setting uniforms from C++.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dreflect
    ComPtr<ID3D12ShaderReflection> reflect;
    if ( !blob )
    {
        Log().WriteEventf( "dx12_shader_reflect_failed stage=%s hresult=0x%08X path=%s reason=missing_bytecode",
                           stageName ? stageName : "unknown",
                           static_cast<unsigned int>( E_POINTER ),
                           hlslPath ? hlslPath : "<null>" );
        Log().FlushAll();
        return false;
    }
    HRESULT hr = E_FAIL;
    if ( !ReflectShaderBytecode( blob, reflect, hr ) )
    {
        // Lane R: reflection depends on compiler output and device tooling. A
        // failed reflection pass means this shader cannot expose a safe uniform
        // contract, so report failure to Compile() instead of throwing.
        Log().WriteEventf( "dx12_shader_reflect_failed stage=%s hresult=0x%08X path=%s",
                           stageName ? stageName : "unknown",
                           static_cast<unsigned int>( FAILED( hr ) ? hr : E_FAIL ),
                           hlslPath ? hlslPath : "<null>" );
        Log().FlushAll();
        return false;
    }

    // Hazard: reflection descriptor structs are undefined when their query
    // fails. Stop at the first bad query rather than publishing offsets, sizes,
    // or resource slots from zero-initialized placeholder data.
    auto reflectionFailure = [&]( const char* operation, HRESULT result )
    {
        Log().WriteEventf( "dx12_shader_reflect_failed stage=%s operation=%s hresult=0x%08X path=%s",
                           stageName ? stageName : "unknown",
                           operation ? operation : "unknown",
                           static_cast<unsigned int>( result ),
                           hlslPath ? hlslPath : "<null>" );
        Log().FlushAll();
        return false;
    };

    D3D12_SHADER_DESC shaderDesc = {};
    hr = reflect->GetDesc( &shaderDesc );
    if ( FAILED( hr ) )
    {
        return reflectionFailure( "shader GetDesc", hr );
    }

    const GeneratedShaderReflection::Stage* bakedStage = FindGeneratedShaderStage( hlslPath, stageName );
    if ( !bakedStage )
    {
        return reflectionFailure( "generated metadata lookup", E_INVALIDARG );
    }

#ifdef _DEBUG
    for ( UINT i = 0; i < shaderDesc.BoundResources; ++i )
    {
        D3D12_SHADER_INPUT_BIND_DESC bindDesc = {};
        hr = reflect->GetResourceBindingDesc( i, &bindDesc );
        if ( FAILED( hr ) )
        {
            return reflectionFailure( "GetResourceBindingDesc", hr );
        }
        if ( bindDesc.Name && bindDesc.Name[0] != '\0' )
        {
            m_resourceMap[bindDesc.Name] = { bindDesc.BindPoint, bindDesc.Type, bindDesc.Dimension };
        }
    }
#endif

    for ( UINT i = 0; i < shaderDesc.ConstantBuffers; ++i )
    {
        ID3D12ShaderReflectionConstantBuffer* cb = reflect->GetConstantBufferByIndex( i );
        if ( !cb )
        {
            return reflectionFailure( "GetConstantBufferByIndex", E_POINTER );
        }
        D3D12_SHADER_BUFFER_DESC bufDesc = {};
        hr = cb->GetDesc( &bufDesc );
        if ( FAILED( hr ) )
        {
            return reflectionFailure( "constant buffer GetDesc", hr );
        }

        if ( bufDesc.Size > m_cbReflectedSize )
        {
            m_cbReflectedSize = bufDesc.Size;
        }

        for ( UINT v = 0; v < bufDesc.Variables; ++v )
        {
            ID3D12ShaderReflectionVariable* var = cb->GetVariableByIndex( v );
            if ( !var )
            {
                return reflectionFailure( "GetVariableByIndex", E_POINTER );
            }
            D3D12_SHADER_VARIABLE_DESC varDesc = {};
            hr = var->GetDesc( &varDesc );
            if ( FAILED( hr ) )
            {
                return reflectionFailure( "variable GetDesc", hr );
            }
            m_uniformMap[varDesc.Name] = { varDesc.StartOffset, varDesc.Size };
        }
    }

    // Invariant: startup reflects the loaded bytes again and compares every
    // field against the checked-in POD table. This catches stale generated
    // metadata, offsets, or cbuffer sizes even if an asset was copied by hand.
    if ( bakedStage->cbufferSize != 0 )
    {
        bool sizeMatched = false;
        for ( UINT i = 0; i < shaderDesc.ConstantBuffers; ++i )
        {
            ID3D12ShaderReflectionConstantBuffer* cb = reflect->GetConstantBufferByIndex( i );
            D3D12_SHADER_BUFFER_DESC desc = {};
            if ( cb && SUCCEEDED( cb->GetDesc( &desc ) ) && desc.Name &&
                 std::strcmp( desc.Name, bakedStage->cbufferName ) == 0 && desc.Size == bakedStage->cbufferSize )
            {
                sizeMatched = true;
            }
        }
        if ( !sizeMatched )
        {
            return reflectionFailure( "generated cbuffer size mismatch", E_INVALIDARG );
        }
    }
    for ( std::uint32_t expectedIndex = 0; expectedIndex < bakedStage->fieldCount; ++expectedIndex )
    {
        const auto& expected = GeneratedShaderReflection::Fields[bakedStage->fieldStart + expectedIndex];
        bool matched = false;
        for ( UINT i = 0; i < shaderDesc.ConstantBuffers && !matched; ++i )
        {
            ID3D12ShaderReflectionConstantBuffer* cb = reflect->GetConstantBufferByIndex( i );
            D3D12_SHADER_BUFFER_DESC cbDesc = {};
            if ( !cb || FAILED( cb->GetDesc( &cbDesc ) ) || !cbDesc.Name ||
                 std::strcmp( cbDesc.Name, expected.cbuffer ) != 0 )
            {
                continue;
            }
            for ( UINT variableIndex = 0; variableIndex < cbDesc.Variables; ++variableIndex )
            {
                D3D12_SHADER_VARIABLE_DESC variable = {};
                ID3D12ShaderReflectionVariable* reflectedVariable = cb->GetVariableByIndex( variableIndex );
                if ( reflectedVariable && SUCCEEDED( reflectedVariable->GetDesc( &variable ) ) && variable.Name &&
                     std::strcmp( variable.Name, expected.name ) == 0 && variable.StartOffset == expected.offset &&
                     variable.Size == expected.size )
                {
                    matched = true;
                    break;
                }
            }
        }
        if ( !matched )
        {
            return reflectionFailure( "generated cbuffer field mismatch", E_INVALIDARG );
        }
    }
    // Align CB size to 256 bytes (DX12 requirement)
    m_cbSize = ( m_cbReflectedSize + 255 ) & ~255u;
    m_cbData.resize( m_cbSize, 0 );
    return true;
}


void ShaderDX12::Use() const
{
    if ( m_device.Device() )
    {
        m_pipeline.SetActiveShader( const_cast<ShaderDX12*>( this ) );
    }
#ifdef _DEBUG
    ResetContractActivation();
#endif
}


#ifdef _DEBUG
namespace
{
bool ContainsWarningKey( const std::vector<std::string>& warnings, const std::string& key )
{
    for ( const std::string& warning : warnings )
    {
        if ( warning == key )
        {
            return true;
        }
    }
    return false;
}

ShaderValueType ShaderValueTypeForSetter( const char* setterName )
{
    if ( std::strcmp( setterName, "SetInt" ) == 0 )
    {
        return ShaderValueType::Int;
    }
    if ( std::strcmp( setterName, "SetFloat" ) == 0 )
    {
        return ShaderValueType::Float;
    }
    if ( std::strcmp( setterName, "SetVec3" ) == 0 )
    {
        return ShaderValueType::Vec3;
    }
    if ( std::strcmp( setterName, "SetVec4" ) == 0 )
    {
        return ShaderValueType::Vec4;
    }
    return ShaderValueType::Mat4;
}

const char* ShaderInputTypeName( D3D_SHADER_INPUT_TYPE type )
{
    switch ( type )
    {
    case D3D_SIT_CBUFFER:
        return "CBUFFER";
    case D3D_SIT_TBUFFER:
        return "TBUFFER";
    case D3D_SIT_TEXTURE:
        return "TEXTURE";
    case D3D_SIT_SAMPLER:
        return "SAMPLER";
    case D3D_SIT_UAV_RWTYPED:
        return "UAV_RWTYPED";
    case D3D_SIT_STRUCTURED:
        return "STRUCTURED";
    case D3D_SIT_UAV_RWSTRUCTURED:
        return "UAV_RWSTRUCTURED";
    default:
        return "UNKNOWN";
    }
}

const char* ShaderInputDimensionName( D3D_SRV_DIMENSION dimension )
{
    switch ( dimension )
    {
    case D3D_SRV_DIMENSION_UNKNOWN:
        return "UNKNOWN";
    case D3D_SRV_DIMENSION_BUFFER:
        return "BUFFER";
    case D3D_SRV_DIMENSION_TEXTURE1D:
        return "TEXTURE1D";
    case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
        return "TEXTURE1DARRAY";
    case D3D_SRV_DIMENSION_TEXTURE2D:
        return "TEXTURE2D";
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
        return "TEXTURE2DARRAY";
    case D3D_SRV_DIMENSION_TEXTURE2DMS:
        return "TEXTURE2DMS";
    case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
        return "TEXTURE2DMSARRAY";
    case D3D_SRV_DIMENSION_TEXTURE3D:
        return "TEXTURE3D";
    case D3D_SRV_DIMENSION_TEXTURECUBE:
        return "TEXTURECUBE";
    case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        return "TEXTURECUBEARRAY";
    default:
        return "UNKNOWN";
    }
}

bool ShaderResourceKindMatches( ShaderResourceKind kind,
                                D3D_SHADER_INPUT_TYPE reflectedType,
                                D3D_SRV_DIMENSION reflectedDimension )
{
    switch ( kind )
    {
    case ShaderResourceKind::Texture2D:
        return reflectedType == D3D_SIT_TEXTURE && reflectedDimension == D3D_SRV_DIMENSION_TEXTURE2D;
    default:
        return false;
    }
}
} // namespace


void ShaderDX12::ResetContractActivation() const
{
    if ( !m_contract )
    {
        return;
    }
    if ( m_contractUniformsSet.size() != m_contract->uniformCount )
    {
        m_contractUniformsSet.assign( m_contract->uniformCount, static_cast<uint8_t>( 0 ) );
    }
    else
    {
        std::fill( m_contractUniformsSet.begin(), m_contractUniformsSet.end(), static_cast<uint8_t>( 0 ) );
    }
}


void ShaderDX12::MarkContractUniformSet( const char* name, const char* setterName ) const
{
    if ( !m_contract || !name )
    {
        return;
    }

    size_t uniformIndex = static_cast<size_t>( -1 );
    const ShaderUniformDecl* uniform = FindShaderUniformDecl( *m_contract, name, &uniformIndex );
    if ( !uniform )
    {
        for ( size_t i = 0; i < m_contract->resourceCount; ++i )
        {
            const ShaderResourceDecl& resource = m_contract->resources[i];
            if ( ShaderContractNameEquals( resource.name, name ) )
            {
                const std::string key = std::string( name ) + ":" + setterName + ":resource_via_uniform_api";
                if ( !ContainsWarningKey( m_missingUniformWarnings, key ) )
                {
                    m_missingUniformWarnings.push_back( key );
                    Log().WriteEventf(
                        "shader_contract_resource_set_with_uniform_api shader=%s resource=%s slot=%d setter=%s",
                        m_contract->baseName,
                        name,
                        resource.slot,
                        setterName );
                }
                return;
            }
        }

        const std::string key = std::string( name ) + ":" + setterName + ":not_in_contract";
        if ( !ContainsWarningKey( m_missingUniformWarnings, key ) )
        {
            m_missingUniformWarnings.push_back( key );
            Log().WriteEventf( "shader_contract_stale_uniform shader=%s uniform=%s setter=%s reason=not_in_contract",
                               m_contract->baseName,
                               name,
                               setterName );
        }
        return;
    }

    const ShaderValueType setterType = ShaderValueTypeForSetter( setterName );
    if ( uniform->type != setterType )
    {
        const std::string key = std::string( name ) + ":" + setterName + ":type_mismatch";
        if ( !ContainsWarningKey( m_typeMismatchWarnings, key ) )
        {
            m_typeMismatchWarnings.push_back( key );
            Log().WriteEventf( "shader_contract_uniform_type_mismatch shader=%s uniform=%s setter=%s expected=%s",
                               m_contract->baseName,
                               name,
                               setterName,
                               ShaderValueTypeName( uniform->type ) );
        }
    }

    if ( uniformIndex < m_contractUniformsSet.size() )
    {
        m_contractUniformsSet[uniformIndex] = static_cast<uint8_t>( 1 );
    }
}


void ShaderDX12::ReportMissingRequiredContractUniforms() const
{
    if ( !m_contract || m_contractUniformsSet.size() != m_contract->uniformCount )
    {
        return;
    }

    if ( m_contractMissingRequiredLogged.size() != m_contract->uniformCount )
    {
        m_contractMissingRequiredLogged.assign( m_contract->uniformCount, static_cast<uint8_t>( 0 ) );
    }

    for ( size_t i = 0; i < m_contract->uniformCount; ++i )
    {
        const ShaderUniformDecl& uniform = m_contract->uniforms[i];
        if ( uniform.required && m_contractUniformsSet[i] == static_cast<uint8_t>( 0 ) &&
             m_contractMissingRequiredLogged[i] == static_cast<uint8_t>( 0 ) )
        {
            m_contractMissingRequiredLogged[i] = static_cast<uint8_t>( 1 );
            Log().WriteEventf( "shader_contract_required_uniform_not_set shader=%s uniform=%s pass=%s",
                               m_contract->baseName,
                               uniform.name,
                               m_contract->passCategory ? m_contract->passCategory : "unknown" );
        }
    }
}


void ShaderDX12::ReportContractReflectionMismatch() const
{
    if ( !m_contract )
    {
        return;
    }

    for ( size_t i = 0; i < m_contract->uniformCount; ++i )
    {
        const ShaderUniformDecl& uniform = m_contract->uniforms[i];
        if ( uniform.required && m_uniformMap.find( uniform.name ) == m_uniformMap.end() )
        {
            Log().WriteEventf( "shader_contract_required_uniform_not_reflected shader=%s uniform=%s expected=%s",
                               m_contract->baseName,
                               uniform.name,
                               ShaderValueTypeName( uniform.type ) );
        }
    }

    for ( size_t i = 0; i < m_contract->resourceCount; ++i )
    {
        const ShaderResourceDecl& resource = m_contract->resources[i];
        const auto reflected = m_resourceMap.find( resource.name );
        if ( reflected == m_resourceMap.end() )
        {
            if ( resource.required )
            {
                Log().WriteEventf(
                    "shader_contract_required_resource_not_reflected shader=%s resource=%s slot=t%d expected_kind=%s",
                    m_contract->baseName,
                    resource.name,
                    resource.slot,
                    ShaderResourceKindName( resource.kind ) );
            }
            continue;
        }

        if ( !ShaderResourceKindMatches( resource.kind, reflected->second.type, reflected->second.dimension ) )
        {
            Log().WriteEventf( "shader_contract_resource_kind_mismatch shader=%s resource=%s expected_kind=%s "
                               "reflected_type=%s reflected_dimension=%s",
                               m_contract->baseName,
                               resource.name,
                               ShaderResourceKindName( resource.kind ),
                               ShaderInputTypeName( reflected->second.type ),
                               ShaderInputDimensionName( reflected->second.dimension ) );
        }

        if ( reflected->second.bindPoint != static_cast<UINT>( resource.slot ) )
        {
            Log().WriteEventf(
                "shader_contract_resource_slot_mismatch shader=%s resource=%s expected=t%d reflected=t%u",
                m_contract->baseName,
                resource.name,
                resource.slot,
                reflected->second.bindPoint );
        }
    }
}


void ShaderDX12::ReportUniformNotReflected( const char* name, const char* setterName ) const
{
    if ( !m_contract )
    {
        return;
    }
    if ( !FindShaderUniformDecl( *m_contract, name ) )
    {
        return;
    }
    const char* shaderName = m_contract ? m_contract->baseName : "<unmanifested>";
    const std::string key =
        std::string( shaderName ) + ":" + ( name ? name : "" ) + ":" + setterName + ":not_reflected";
    if ( ContainsWarningKey( m_missingUniformWarnings, key ) )
    {
        return;
    }

    m_missingUniformWarnings.push_back( key );
    Log().WriteEventf( "shader_uniform_not_reflected shader=%s uniform=%s setter=%s",
                       shaderName,
                       name ? name : "<null>",
                       setterName );
}
#endif

const ShaderDX12::UniformInfo* ShaderDX12::FindUniformInfo( const char* name ) const
{
    if ( !name )
    {
        return nullptr;
    }

    // Why: std::unordered_map<std::string, ...>::find(const char*) constructs a
    // temporary std::string in C++17. Uniform tables are small and populated at
    // shader compile time, so a direct compare keeps per-draw setters allocation
    // free while preserving the reflected-name contract.
    for ( const auto& entry : m_uniformMap )
    {
        if ( std::strcmp( entry.first.c_str(), name ) == 0 )
        {
            return &entry.second;
        }
    }
    return nullptr;
}


void ShaderDX12::SetInt( const char* name, int value ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetInt" );
#endif
    const UniformInfo* uniform = FindUniformInfo( name );
    if ( !uniform )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetInt" );
#endif
        return;
    }
    memcpy( m_cbData.data() + uniform->offset, &value, sizeof( int ) );
    m_cbDirty = true;
}


void ShaderDX12::SetFloat( const char* name, float value ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetFloat" );
#endif
    const UniformInfo* uniform = FindUniformInfo( name );
    if ( !uniform )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetFloat" );
#endif
        return;
    }
    memcpy( m_cbData.data() + uniform->offset, &value, sizeof( float ) );
    m_cbDirty = true;
}


void ShaderDX12::SetVec3( const char* name, float x, float y, float z ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetVec3" );
#endif
    const UniformInfo* uniform = FindUniformInfo( name );
    if ( !uniform )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetVec3" );
#endif
        return;
    }
    float v[3] = { x, y, z };
    memcpy( m_cbData.data() + uniform->offset, v, sizeof( v ) );
    m_cbDirty = true;
}


void ShaderDX12::SetVec3( const char* name, const Vector3& v ) const
{
    SetVec3( name, v.x, v.y, v.z );
}


void ShaderDX12::SetVec4( const char* name, float x, float y, float z, float w ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetVec4" );
#endif
    const UniformInfo* uniform = FindUniformInfo( name );
    if ( !uniform )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetVec4" );
#endif
        return;
    }
    float v[4] = { x, y, z, w };
    memcpy( m_cbData.data() + uniform->offset, v, sizeof( v ) );
    m_cbDirty = true;
}


void ShaderDX12::SetMat4( const char* name, const Matrix4& m ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetMat4" );
#endif
    const UniformInfo* uniform = FindUniformInfo( name );
    if ( !uniform )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetMat4" );
#endif
        return;
    }
    // HLSL uses #pragma pack_matrix(column_major) — send data as-is
    memcpy( m_cbData.data() + uniform->offset, m.Data(), 64 );
    m_cbDirty = true;
}


bool ShaderDX12::SetConstantBufferBytes( const void* data, size_t size, const char* debugName ) const
{
    (void)debugName;
    if ( !data || size == 0 || m_cbSize == 0 )
    {
        return false;
    }

    // Invariant: typed upload structs are a binary mirror of the HLSL cbuffer,
    // not a partial update API. Require the reflected byte size exactly so field
    // order, padding, and matrix packing cannot silently drift between C++ and
    // shader code.
    if ( size != m_cbReflectedSize )
    {
#ifdef _DEBUG
        Log().WriteEventf(
            "shader_typed_cbuffer_size_mismatch shader=%s block=%s bytes=%llu reflected_bytes=%u aligned_bytes=%u",
            m_contract ? m_contract->baseName : "<unmanifested>",
            debugName ? debugName : "<unnamed>",
            static_cast<unsigned long long>( size ),
            m_cbReflectedSize,
            m_cbSize );
#endif
        return false;
    }

    std::fill( m_cbData.begin(), m_cbData.end(), static_cast<uint8_t>( 0 ) );
    memcpy( m_cbData.data(), data, size );
    m_cbDirty = true;

#ifdef _DEBUG
    if ( m_contract )
    {
        // Typed blocks replace per-uniform setters for this activation. Mark
        // all contract uniforms as set so Debug diagnostics still catch stale
        // reflected layouts without falsely reporting every field in the block.
        if ( m_contractUniformsSet.size() != m_contract->uniformCount )
        {
            m_contractUniformsSet.assign( m_contract->uniformCount, static_cast<uint8_t>( 1 ) );
        }
        else
        {
            std::fill( m_contractUniformsSet.begin(), m_contractUniformsSet.end(), static_cast<uint8_t>( 1 ) );
        }
    }
#endif

    return true;
}


D3D12_GPU_VIRTUAL_ADDRESS ShaderDX12::FlushCB() const
{
    if ( m_cbSize == 0 )
    {
        return 0;
    }

#ifdef _DEBUG
    ReportMissingRequiredContractUniforms();
#endif

    if ( !m_device.Device() )
    {
        return 0;
    }

    // Constant buffers are steady-frame data and use the device owner's current
    // frame arena directly. Capacity exhaustion is a fatal renderer policy
    // violation; no wrapper may reach through a broad backend host to flush it.
    D3D12_GPU_VIRTUAL_ADDRESS addr = m_uploadReservations.ReserveConstantUpload( m_cbSize );
    if ( addr == 0 )
    {
        return 0;
    }
    memcpy( m_uploadReservations.UploadPointer( addr ), m_cbData.data(), m_cbSize );
    m_cbDirty = false;
    return addr;
}


const void* ShaderDX12::GetVSBytecode() const
{
    return m_vsBlob ? m_vsBlob->GetBufferPointer() : nullptr;
}


SIZE_T ShaderDX12::GetVSBytecodeSize() const
{
    return m_vsBlob ? m_vsBlob->GetBufferSize() : 0;
}


size_t ShaderDX12::GetVSBytecodeHash() const
{
    return m_vsBytecodeHash;
}


const void* ShaderDX12::GetPSBytecode() const
{
    return m_psBlob ? m_psBlob->GetBufferPointer() : nullptr;
}


SIZE_T ShaderDX12::GetPSBytecodeSize() const
{
    return m_psBlob ? m_psBlob->GetBufferSize() : 0;
}


size_t ShaderDX12::GetPSBytecodeHash() const
{
    return m_psBytecodeHash;
}


bool ShaderDX12::ValidateInputLayout( const D3D12_INPUT_ELEMENT_DESC* elements,
                                      UINT count,
                                      const char*& outError ) const
{
    constexpr UINT MAX_INPUT_ELEMENTS = 16;
    if ( count > MAX_INPUT_ELEMENTS )
    {
        outError = "input layout exceeds fixed validation capacity";
        return false;
    }
    auto componentCount = []( DXGI_FORMAT format ) -> size_t
    {
        switch ( format )
        {
        case DXGI_FORMAT_R32_FLOAT:
            return 1;
        case DXGI_FORMAT_R32G32_FLOAT:
            return 2;
        case DXGI_FORMAT_R32G32B32_FLOAT:
            return 3;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return 4;
        default:
            return 0;
        }
    };
    ShaderVertexInputLayoutElement contractElements[MAX_INPUT_ELEMENTS] = {};
    for ( UINT index = 0; index < count; ++index )
    {
        contractElements[index] = { elements[index].SemanticName,
                                    elements[index].SemanticIndex,
                                    componentCount( elements[index].Format ) };
    }
    // DX12 permits a mesh to expose attributes unused by a particular shader;
    // shadow depth, for example, reads POSITION from a richer mesh layout.
    return ValidateGeneratedShaderVertexInputLayout( m_sourcePath.c_str(), contractElements, count, outError );
}
