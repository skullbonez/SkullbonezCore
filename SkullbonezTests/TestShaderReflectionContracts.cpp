/*
File: TestShaderReflectionContracts.cpp
Purpose:
  Proves every shipping stage has baked reflection and CPU declarations agree.

Summary:
  The generated table is the DXIL-side ABI. These tests exercise the CPU-side
  matcher, including a deliberate bad-slot mutation that must be rejected.

Glossary:
  Mutation drill: A test-only change to a copied contract proving the checker
    rejects the same class of defect it is intended to prevent.

Invariants:
  - The pinned reflection inventory contains 43 raster/compute stages.
  - Compute reflection remains represented even though it has no raster PSO.

Related:
  - SkullbonezSource/Rendering/ShaderReflectionContracts.h
  - tools/bake_shaders.py
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Rendering/ShaderReflectionContracts.h"
#include "../SkullbonezSource/Runtime/App/ReplayPredictionRetainedGeometry.h"

#include <string>

using namespace SkullbonezCore::Rendering;

namespace
{
const char* ReflectionSourceForContract( const char* contractBaseName )
{
    // Why: the Prediction owner preserves the approved physical asset identity,
    // while Rendering exposes only the feature-neutral retained-ribbon ABI.
    return ShaderContractMatchesBaseName( "retained_ribbon", contractBaseName )
               ? SkullbonezCore::Runtime::ReplayOverlay::PREDICTION_RETAINED_RIBBON_SHADER_BASE_NAME
               : contractBaseName;
}
} // namespace

TEST_CASE( "Shader reflection contracts: every shipping stage is represented" )
{
    REQUIRE( GeneratedShaderReflection::StageCount == 43u );
    for ( size_t i = 0; i < GeneratedShaderReflection::StageCount; ++i )
    {
        const auto& stage = GeneratedShaderReflection::Stages[i];
        CHECK( FindGeneratedShaderStage( stage.source, stage.stage ) == &stage );
    }

    const auto* compute = FindGeneratedShaderStage( "SkullbonezData/shaders/generate_mips.hlsl", "cs" );
    REQUIRE( compute != nullptr );
    CHECK( compute->cbufferSize == 16u );
    CHECK( compute->resourceCount == 7u );
    CHECK( compute->inputCount == 0u );
}

TEST_CASE( "Shader reflection contracts: CPU declarations match baked DXIL" )
{
    const ShaderProgramDesc* contracts = ShippingRasterShaderContracts();
    REQUIRE( ShippingRasterShaderContractCount() == ShippingShaderVertexInputContractCount() );
    for ( size_t i = 0; i < ShippingRasterShaderContractCount(); ++i )
    {
        std::string error;
        CHECK_MESSAGE( ValidateGeneratedShaderProgramContract( ReflectionSourceForContract( contracts[i].baseName ),
                                                               contracts[i],
                                                               error ),
                       std::string( contracts[i].baseName ),
                       ": ",
                       error );
    }
}

TEST_CASE( "Shader reflection contracts: bindless raster owns b1 and no t registers" )
{
    const auto* pixelStage = FindGeneratedShaderStage( "lit_textured.hlsl", "ps" );
    REQUIRE( pixelStage != nullptr );
    bool foundTextureIndices = false;
    for ( std::uint32_t i = 0; i < pixelStage->resourceCount; ++i )
    {
        const auto& resource = GeneratedShaderReflection::Resources[pixelStage->resourceStart + i];
        CHECK( resource.registerClass != 't' );
        if ( resource.registerClass == 'b' &&
             resource.slot == UnifiedRasterRootSignature::SHADER_REGISTER_TEXTURE_INDICES )
        {
            foundTextureIndices = true;
        }
    }
    CHECK( foundTextureIndices );
}

TEST_CASE( "Shader reflection contracts: each cbuffer owns an independent reflected size" )
{
    const auto* pixelStage = FindGeneratedShaderStage( "lit_textured.hlsl", "ps" );
    REQUIRE( pixelStage != nullptr );
    CHECK( GeneratedCbufferSize( *pixelStage, "Uniforms" ) == pixelStage->cbufferSize );
    CHECK( GeneratedCbufferSize( *pixelStage, "BindlessTextureIndices" ) == 32u );
    CHECK( GeneratedCbufferSize( *pixelStage, "BindlessTextureIndices" ) != pixelStage->cbufferSize );
}

TEST_CASE( "Shader reflection contracts: text bindless constants use API-visible cbuffer storage" )
{
    const auto* pixelStage = FindGeneratedShaderStage( "text.hlsl", "ps" );
    REQUIRE( pixelStage != nullptr );
    CHECK( pixelStage->cbufferSize == 32u );
    CHECK( GeneratedCbufferSize( *pixelStage, "BindlessTextureIndices" ) == 32u );
}

TEST_CASE( "Shader reflection contracts: bindless texture indices are pixel-stage only" )
{
    const auto* vertexStage = FindGeneratedShaderStage( "lit_textured.hlsl", "vs" );
    REQUIRE( vertexStage != nullptr );
    GeneratedShaderReflection::Resource textureIndices = {};
    textureIndices.name = "BindlessTextureIndices";
    textureIndices.registerClass = 'b';
    textureIndices.slot = UnifiedRasterRootSignature::SHADER_REGISTER_TEXTURE_INDICES;
    textureIndices.space = UnifiedRasterRootSignature::REGISTER_SPACE;
    textureIndices.type = "cbuffer";
    textureIndices.dimension = "na";

    std::string error;
    CHECK_FALSE( ValidateUnifiedRasterResource( *vertexStage, textureIndices, error ) );
    CHECK( error.find( "outside the UnifiedRaster root-signature slot map" ) != std::string::npos );
}

TEST_CASE( "Shader reflection contracts: every raster input signature matches the CPU table" )
{
    const ShaderVertexInputContract* contracts = ShippingShaderVertexInputContracts();
    REQUIRE( ShippingShaderVertexInputContractCount() == 21u );
    for ( size_t contractIndex = 0; contractIndex < ShippingShaderVertexInputContractCount(); ++contractIndex )
    {
        const auto* stage = FindGeneratedShaderStage( ReflectionSourceForContract( contracts[contractIndex].baseName ),
                                                      "vs" );
        REQUIRE( stage != nullptr );
        std::string signature;
        for ( std::uint32_t inputIndex = 0; inputIndex < stage->inputCount; ++inputIndex )
        {
            const auto& input = GeneratedShaderReflection::Inputs[stage->inputStart + inputIndex];
            if ( !signature.empty() )
            {
                signature += ',';
            }
            signature += input.semantic;
            signature += std::to_string( input.index );
            signature += ':';
            signature += input.mask;
            signature += ':';
            signature += input.systemValue;
        }
        CHECK( signature == contracts[contractIndex].signature );
    }
}

TEST_CASE( "Shader reflection contracts: shadow POSITION accepts a richer mesh layout" )
{
    ShaderVertexInputLayoutElement meshLayout[] = {
        { "POSITION", 0, 3 },
        { "NORMAL", 0, 3 },
        { "TEXCOORD", 0, 2 },
    };
    const char* error = nullptr;
    CHECK( ValidateGeneratedShaderVertexInputLayout( "shadow_depth.hlsl", meshLayout, 3, error ) );

    meshLayout[0].componentCount = 2;
    CHECK_FALSE( ValidateGeneratedShaderVertexInputLayout( "shadow_depth.hlsl", meshLayout, 3, error ) );
    CHECK( std::string( error ) == "input layout semantic or format mismatch" );
}

TEST_CASE( "Shader reflection contracts: deliberate bindless payload-slot mismatch is rejected" )
{
    const auto* pixelStage = FindGeneratedShaderStage( "lit_textured.hlsl", "ps" );
    REQUIRE( pixelStage != nullptr );
    GeneratedShaderReflection::Resource mutated = {};
    mutated.name = "BindlessTextureIndices";
    mutated.registerClass = 'b';
    mutated.slot = 2;
    mutated.space = 0;
    mutated.type = "cbuffer";
    mutated.dimension = "na";
    std::string error;
    CHECK_FALSE( ValidateUnifiedRasterResource( *pixelStage, mutated, error ) );
    CHECK( error.find( "outside the UnifiedRaster root-signature slot map" ) != std::string::npos );
}

TEST_CASE( "Shader reflection contracts: deliberate cbuffer-size mismatch is rejected" )
{
    const ShaderProgramDesc& source = *FindShaderProgramDesc( "lit_textured.hlsl" );
    ShaderUniformDecl mutatedUniforms[21] = {};
    REQUIRE( source.uniformCount == 21u );
    for ( size_t i = 0; i < source.uniformCount; ++i )
    {
        mutatedUniforms[i] = source.uniforms[i];
    }
    mutatedUniforms[0].type = ShaderValueType::Vec4;
    ShaderProgramDesc mutated = source;
    mutated.uniforms = mutatedUniforms;

    std::string error;
    CHECK_FALSE( ValidateGeneratedShaderProgramContract( "lit_textured.hlsl", mutated, error ) );
    CHECK( error == "cbuffer field mismatch: uModel" );
}

TEST_CASE( "Shader reflection contracts: every raster stage fits UnifiedRaster" )
{
    std::string error;
    CHECK_MESSAGE( ValidateGeneratedUnifiedRasterRootSignature( error ), error );

    CHECK( std::string( UnifiedRasterRootSignature::NAME ) == "UnifiedRaster" );
    CHECK( UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT == 2u );
    CHECK( UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES == 1u );
    CHECK( UnifiedRasterRootSignature::SHADER_REGISTER_TEXTURE_INDICES == 1u );
    CHECK( UnifiedRasterRootSignature::TEXTURE_SLOTS[3].payloadIndex == 3u );
    CHECK( std::string( UnifiedRasterRootSignature::TEXTURE_SLOTS[3].name ) == "ShadowMap" );
    CHECK( UnifiedRasterRootSignature::TEXTURE_SLOTS[4].payloadIndex == 4u );
    CHECK( std::string( UnifiedRasterRootSignature::TEXTURE_SLOTS[4].name ) == "MaterialTable" );
    CHECK( UnifiedRasterRootSignature::TEXTURE_SLOTS[5].payloadIndex == 5u );
    CHECK( std::string( UnifiedRasterRootSignature::TEXTURE_SLOTS[5].name ) == "DetailShadowMap" );
}

TEST_CASE( "Shader reflection contracts: UnifiedRaster rejects unowned slots" )
{
    const auto* pixelStage = FindGeneratedShaderStage( "lit_textured", "ps" );
    REQUIRE( pixelStage != nullptr );
    REQUIRE( pixelStage->resourceCount > 0u );

    GeneratedShaderReflection::Resource mutated = GeneratedShaderReflection::Resources[pixelStage->resourceStart];
    mutated.registerClass = 't';
    mutated.slot = 6;
    mutated.type = "texture";
    mutated.dimension = "2d";
    std::string error;
    CHECK_FALSE( ValidateUnifiedRasterResource( *pixelStage, mutated, error ) );
    CHECK( error.find( "outside the UnifiedRaster root-signature slot map" ) != std::string::npos );

    mutated.registerClass = 's';
    mutated.slot = 2;
    mutated.type = "sampler";
    mutated.dimension = "na";
    error.clear();
    CHECK_FALSE( ValidateUnifiedRasterResource( *pixelStage, mutated, error ) );
}
