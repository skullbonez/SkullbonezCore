/*
File: ShaderReflectionContracts.h
Purpose:
  Compares engine shader declarations with the fixed metadata baked from DXIL.

Summary:
  DXC reflection describes the binary ABI. Engine declarations are an
  independent CPU expectation; startup accepts a program only when required
  fields and resources agree.

Glossary:
  Reflection contract: Fixed description of the binary-visible fields,
    resources, and vertex inputs produced by the shader compiler.

Invariants:
  - Validation is allocation-free except for the caller-owned diagnostic text.
  - Generated metadata is immutable and covers every shipping stage.

Related:
  - SkullbonezData/generated/GeneratedShaderReflection.h
  - ShaderContracts.h
  - tools/bake_shaders.py
*/
#pragma once

#include "ShaderContracts.h"
#include "RenderRasterBindingContract.h"
#include "../../SkullbonezData/generated/GeneratedShaderReflection.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace SkullbonezCore::Rendering
{
inline const GeneratedShaderReflection::Stage* FindGeneratedShaderStage( const char* path, const char* stage )
{
    size_t baseLength = 0;
    const char* base = ShaderBaseNameFromPath( path, baseLength );

    for ( size_t i = 0; i < GeneratedShaderReflection::StageCount; ++i )
    {
        const auto& candidate = GeneratedShaderReflection::Stages[i];
        size_t candidateLength = 0;
        const char* candidateBase = ShaderBaseNameFromPath( candidate.source, candidateLength );

        if ( baseLength == candidateLength && std::strncmp( base, candidateBase, baseLength ) == 0 && stage &&
             std::strcmp( candidate.stage, stage ) == 0 )
        {
            return &candidate;
        }
    }

    return nullptr;
}

struct ShaderVertexInputLayoutElement
{
    const char* semantic = nullptr;
    std::uint32_t index = 0;
    size_t componentCount = 0;
};

// Concept: a vertex shader's reflected inputs are a required subset of the
// mesh layout. Extra mesh attributes are legal because depth-only shaders often
// consume POSITION from a richer POSITION/NORMAL/TEXCOORD vertex stream.
inline bool ValidateGeneratedShaderVertexInputLayout( const char* sourcePath, const ShaderVertexInputLayoutElement* elements,
                                                      size_t count, const char*& outError )
{
    const auto* reflected = FindGeneratedShaderStage( sourcePath, "vs" );

    if ( !reflected )
    {
        outError = "missing generated vertex-stage metadata";
        return false;
    }

    for ( std::uint32_t reflectedIndex = 0; reflectedIndex < reflected->inputCount; ++reflectedIndex )
    {
        const auto& expected = GeneratedShaderReflection::Inputs[reflected->inputStart + reflectedIndex];

        if ( std::strcmp( expected.systemValue, "NONE" ) != 0 )
        {
            continue;
        }

        const ShaderVertexInputLayoutElement* matchingElement = nullptr;

        for ( size_t cpuIndex = 0; cpuIndex < count; ++cpuIndex )
        {
            const auto& candidate = elements[cpuIndex];

            if ( candidate.semantic && std::strcmp( expected.semantic, candidate.semantic ) == 0 &&
                 expected.index == candidate.index )
            {
                matchingElement = &candidate;
                break;
            }
        }

        if ( !matchingElement || std::strlen( expected.mask ) != matchingElement->componentCount )
        {
            outError = "input layout semantic or format mismatch";
            return false;
        }
    }

    return true;
}

inline std::uint32_t ShaderValueByteSize( ShaderValueType type )
{
    switch ( type )
    {
    case ShaderValueType::Int:
    case ShaderValueType::Float:
        return 4;
    case ShaderValueType::Vec3:
        return 12;
    case ShaderValueType::Vec4:
        return 16;
    case ShaderValueType::Mat4:
        return 64;
    default:
        return 0;
    }
}

inline std::uint32_t GeneratedCbufferSize( const GeneratedShaderReflection::Stage& stage, const char* cbufferName )
{
    std::uint32_t size = 0;

    for ( std::uint32_t fieldIndex = 0; cbufferName && fieldIndex < stage.fieldCount; ++fieldIndex )
    {
        const auto& field = GeneratedShaderReflection::Fields[stage.fieldStart + fieldIndex];

        if ( std::strcmp( field.cbuffer, cbufferName ) == 0 )
        {
            size = (std::max)( size, field.offset + field.size );
        }
    }

    // Invariant: constant-buffer storage is rounded to 16-byte register rows,
    // independently for b0 draw data and b1 bindless indices.
    return ( size + 15u ) & ~15u;
}

inline bool ValidateGeneratedShaderProgramContract( const char* path, const ShaderProgramDesc& contract,
                                                    std::string& outError )
{
    const auto* vs = FindGeneratedShaderStage( path, "vs" );
    const auto* ps = FindGeneratedShaderStage( path, "ps" );

    if ( !vs || !ps )
    {
        outError = "missing generated raster-stage metadata";
        return false;
    }

    for ( size_t uniformIndex = 0; uniformIndex < contract.uniformCount; ++uniformIndex )
    {
        const ShaderUniformDecl& expected = contract.uniforms[uniformIndex];
        const GeneratedShaderReflection::Field* found = nullptr;
        const GeneratedShaderReflection::Stage* stages[] = { vs, ps };

        for ( const auto* reflectedStage : stages )
        {
            for ( std::uint32_t i = 0; i < reflectedStage->fieldCount; ++i )
            {
                const auto& field = GeneratedShaderReflection::Fields[reflectedStage->fieldStart + i];

                if ( std::strcmp( field.name, expected.name ) == 0 )
                {
                    found = &field;
                    break;
                }
            }
        }

        // Optional means the compiler may remove an unused declaration. When
        // the declaration survives, it still owns the same name and type.
        if ( ( expected.required && !found ) || ( found && found->size != ShaderValueByteSize( expected.type ) ) )
        {
            outError = std::string( "cbuffer field mismatch: " ) + expected.name;
            return false;
        }
    }

    for ( size_t resourceIndex = 0; resourceIndex < contract.resourceCount; ++resourceIndex )
    {
        const ShaderResourceDecl& expected = contract.resources[resourceIndex];
        const GeneratedShaderReflection::Resource* found = nullptr;
        const GeneratedShaderReflection::Stage* stages[] = { vs, ps };

        for ( const auto* reflectedStage : stages )
        {
            for ( std::uint32_t i = 0; i < reflectedStage->resourceCount; ++i )
            {
                const auto& resource = GeneratedShaderReflection::Resources[reflectedStage->resourceStart + i];

                if ( std::strcmp( resource.name, expected.name ) == 0 )
                {
                    found = &resource;
                    break;
                }
            }
        }

        const bool matches = found && found->registerClass == 't' &&
                             found->slot == static_cast<std::uint32_t>( expected.slot ) && found->space == 0 &&
                             std::strcmp( found->type, "texture" ) == 0 && std::strcmp( found->dimension, "2d" ) == 0;

        // Optional means absence is legal, not that a present declaration may
        // silently move to another UnifiedRaster slot.
        if ( ( expected.required && !found ) || ( found && !matches ) )
        {
            outError = std::string( "resource binding mismatch: " ) + expected.name;
            return false;
        }
    }

    // Invariant: the CPU table is exhaustive for authored ABI names. Generated
    // padding and non-texture root objects are implementation details; every
    // other reflected field/texture must have an independent CPU declaration.
    const GeneratedShaderReflection::Stage* stages[] = { vs, ps };

    for ( const auto* reflectedStage : stages )
    {
        for ( std::uint32_t i = 0; i < reflectedStage->fieldCount; ++i )
        {
            const auto& field = GeneratedShaderReflection::Fields[reflectedStage->fieldStart + i];

            if ( field.name[0] != '_' && !FindShaderUniformDecl( contract, field.name ) )
            {
                outError = std::string( "undeclared cbuffer field: " ) + field.name;
                return false;
            }
        }

        for ( std::uint32_t i = 0; i < reflectedStage->resourceCount; ++i )
        {
            const auto& resource = GeneratedShaderReflection::Resources[reflectedStage->resourceStart + i];

            if ( resource.registerClass == 't' && !FindShaderResourceDecl( contract, resource.name ) )
            {
                outError = std::string( "undeclared texture resource: " ) + resource.name;
                return false;
            }
        }
    }

    return true;
}

inline bool ValidateUnifiedRasterResource( const GeneratedShaderReflection::Stage& stage,
                                           const GeneratedShaderReflection::Resource& resource, std::string& outError )
{
    if ( resource.space != UnifiedRasterRootSignature::REGISTER_SPACE )
    {
        outError = std::string( stage.source ) + ":" + stage.stage + " uses non-zero register space for " + resource.name;
        return false;
    }

    if ( resource.registerClass == 'b' )
    {
        const bool drawConstants = resource.slot == UnifiedRasterRootSignature::SHADER_REGISTER_DRAW_CONSTANTS;
        const bool pixelTextureIndices = resource.slot == UnifiedRasterRootSignature::SHADER_REGISTER_TEXTURE_INDICES &&
                                         std::strcmp( stage.stage, "ps" ) == 0;

        if ( ( drawConstants || pixelTextureIndices ) && std::strcmp( resource.type, "cbuffer" ) == 0 )
        {
            return true;
        }
    }
    else if ( resource.registerClass == 's' )
    {
        if ( std::strcmp( stage.stage, "ps" ) == 0 && UnifiedRasterRootSignature::AcceptsSamplerRegister( resource.slot ) &&
             std::strcmp( resource.type, "sampler" ) == 0 )
        {
            return true;
        }
    }

    outError = std::string( stage.source ) + ":" + stage.stage + " binding " + resource.name + " (" +
               resource.registerClass + std::to_string( resource.slot ) +
               ") is outside the UnifiedRaster root-signature slot map";
    return false;
}

inline bool ValidateGeneratedUnifiedRasterRootSignature( std::string& outError )
{
    std::uint32_t rasterStageCount = 0;

    for ( size_t stageIndex = 0; stageIndex < GeneratedShaderReflection::StageCount; ++stageIndex )
    {
        const auto& stage = GeneratedShaderReflection::Stages[stageIndex];

        if ( std::strcmp( stage.stage, "vs" ) != 0 && std::strcmp( stage.stage, "ps" ) != 0 )
        {
            continue;
        }

        ++rasterStageCount;

        for ( std::uint32_t resourceIndex = 0; resourceIndex < stage.resourceCount; ++resourceIndex )
        {
            const auto& resource = GeneratedShaderReflection::Resources[stage.resourceStart + resourceIndex];

            if ( !ValidateUnifiedRasterResource( stage, resource, outError ) )
            {
                return false;
            }
        }
    }

    if ( rasterStageCount != 42 )
    {
        outError = "UnifiedRaster expected reflection for 42 raster stages, found " + std::to_string( rasterStageCount );
        return false;
    }

    return true;
}
} // namespace SkullbonezCore::Rendering
