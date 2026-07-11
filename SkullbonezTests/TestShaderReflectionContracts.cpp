/*
File: TestShaderReflectionContracts.cpp
Purpose:
  Proves every shipping stage has baked reflection and CPU declarations agree.

Mental model:
  The generated table is the DXIL-side ABI. These tests exercise the CPU-side
  matcher, including a deliberate bad-slot mutation that must be rejected.

Glossary:
  Mutation drill: A test-only change to a copied contract proving the checker
    rejects the same class of defect it is intended to prevent.

Invariants:
  - There are 43 raster/compute stages in the pinned P2 inventory.
  - Compute reflection remains represented even though it has no raster PSO.

Related:
  - SkullbonezSource/Rendering/ShaderReflectionContracts.h
  - tools/bake_shaders.py
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Rendering/ShaderReflectionContracts.h"

#include <string>

using namespace SkullbonezCore::Rendering;

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
    const ShaderProgramDesc* contracts = HighRiskShaderContracts();
    for ( size_t i = 0; i < HighRiskShaderContractCount(); ++i )
    {
        std::string error;
        CHECK_MESSAGE( ValidateGeneratedShaderProgramContract( contracts[i].baseName, contracts[i], error ),
                       std::string( contracts[i].baseName ),
                       ": ",
                       error );
    }
}

TEST_CASE( "Shader reflection contracts: every raster input signature matches the CPU table" )
{
    const ShaderVertexInputContract* contracts = ShippingShaderVertexInputContracts();
    REQUIRE( ShippingShaderVertexInputContractCount() == 21u );
    for ( size_t contractIndex = 0; contractIndex < ShippingShaderVertexInputContractCount(); ++contractIndex )
    {
        const auto* stage = FindGeneratedShaderStage( contracts[contractIndex].baseName, "vs" );
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

TEST_CASE( "Shader reflection contracts: deliberate resource-slot mismatch is rejected" )
{
    const ShaderProgramDesc& source = *FindShaderProgramDesc( "lit_textured.hlsl" );
    ShaderResourceDecl mutatedResources[2] = { source.resources[0], source.resources[1] };
    mutatedResources[0].slot = 2;
    ShaderProgramDesc mutated = source;
    mutated.resources = mutatedResources;

    std::string error;
    CHECK_FALSE( ValidateGeneratedShaderProgramContract( "lit_textured.hlsl", mutated, error ) );
    CHECK( error == "resource binding mismatch: uTexture" );
}

TEST_CASE( "Shader reflection contracts: deliberate cbuffer-size mismatch is rejected" )
{
    const ShaderProgramDesc& source = *FindShaderProgramDesc( "lit_textured.hlsl" );
    ShaderUniformDecl mutatedUniforms[18] = {};
    REQUIRE( source.uniformCount == 18u );
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
    CHECK( UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT == 6u );
    CHECK( UnifiedRasterRootSignature::TEXTURE_SLOTS[3].shaderRegister == 3u );
    CHECK( std::string( UnifiedRasterRootSignature::TEXTURE_SLOTS[3].name ) == "ShadowMap" );
    CHECK( UnifiedRasterRootSignature::TEXTURE_SLOTS[4].rootParameter == 5u );
    CHECK( std::string( UnifiedRasterRootSignature::TEXTURE_SLOTS[4].name ) == "MaterialTable" );
}

TEST_CASE( "Shader reflection contracts: UnifiedRaster rejects unowned slots" )
{
    const auto* pixelStage = FindGeneratedShaderStage( "lit_textured", "ps" );
    REQUIRE( pixelStage != nullptr );
    REQUIRE( pixelStage->resourceCount > 0u );

    GeneratedShaderReflection::Resource mutated = GeneratedShaderReflection::Resources[pixelStage->resourceStart];
    mutated.registerClass = 't';
    mutated.slot = 5;
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
