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
  - The pinned reflection inventory contains 44 raster, compute, and library stages.
  - Compute reflection remains represented even though it has no raster PSO.

Related:
  - SkullbonezSource/Rendering/ShaderReflectionContracts.h
  - tools/bake_shaders.py
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Rendering/ShaderReflectionContracts.h"
#include "../SkullbonezSource/Runtime/App/ReplayPredictionRetainedGeometry.h"
#include "../SkullbonezData/shaders/shader_behavior.hlsli"

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
    REQUIRE( GeneratedShaderReflection::StageCount == 44u );
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

    const auto* raytracingLibrary = FindGeneratedShaderStage( "SkullbonezData/shaders/reflect.rt.hlsl", "lib" );
    REQUIRE( raytracingLibrary != nullptr );
    bool foundMaterialWrap = false;
    bool foundSkyClamp = false;
    for ( std::uint32_t i = 0; i < raytracingLibrary->resourceCount; ++i )
    {
        const auto& resource = GeneratedShaderReflection::Resources[raytracingLibrary->resourceStart + i];
        foundMaterialWrap = foundMaterialWrap ||
                            ( std::string( resource.name ) == "gSampler" && resource.registerClass == 's' &&
                              resource.slot == UnifiedRasterRootSignature::STATIC_SAMPLERS[0].shaderRegister );
        foundSkyClamp = foundSkyClamp ||
                        ( std::string( resource.name ) == "gSkySampler" && resource.registerClass == 's' &&
                          resource.slot == UnifiedRasterRootSignature::STATIC_SAMPLERS[1].shaderRegister );
    }
    CHECK( foundMaterialWrap );
    CHECK( foundSkyClamp );
}

TEST_CASE( "Shader behavior contracts: NPOT mip batches stop before odd shared reductions" )
{
    const GenerateMipsDispatchPlan first = PlanGenerateMipsDispatch( 10u, 10u, 3u );
    CHECK( first.mipCount == 1u );
    CHECK( first.finalWidth == 5u );
    CHECK( first.finalHeight == 5u );

    const GenerateMipsDispatchPlan second = PlanGenerateMipsDispatch( 5u, 5u, 2u );
    CHECK( second.mipCount == 2u );
    CHECK( second.finalWidth == 1u );
    CHECK( second.finalHeight == 1u );

    const GenerateMipsDispatchPlan rectangular = PlanGenerateMipsDispatch( 16u, 10u, 4u );
    CHECK( rectangular.mipCount == 1u );
    CHECK( rectangular.finalWidth == 8u );
    CHECK( rectangular.finalHeight == 5u );

    const GenerateMipsDispatchPlan powerOfTwo = PlanGenerateMipsDispatch( 16u, 8u, 4u );
    CHECK( powerOfTwo.mipCount == 4u );
    CHECK( powerOfTwo.finalWidth == 1u );
    CHECK( powerOfTwo.finalHeight == 1u );
}

TEST_CASE( "Shader behavior contracts: procedural longitude inputs meet at the wrap seam" )
{
    using namespace SkullbonezCore::Rendering::ShaderBehavior;

    CHECK( PeriodicLongitudeX( 0.0f ) == PeriodicLongitudeX( 1.0f ) );
    CHECK( PeriodicLongitudeY( 0.0f ) == PeriodicLongitudeY( 1.0f ) );
    CHECK( CloudLongitudeDomainX( 0.0f, 0.63f, 0.0f ) == CloudLongitudeDomainX( 1.0f, 0.63f, 0.0f ) );
    CHECK( CloudLongitudeDomainY( 0.0f, 0.63f ) == CloudLongitudeDomainY( 1.0f, 0.63f ) );
    CHECK( PeriodicTriangle( 0.0f, 5.0f, 0.36f ) == PeriodicTriangle( 1.0f, 5.0f, 0.36f ) );
    CHECK( PeriodicStreak( 0.0f, 0.63f ) == PeriodicStreak( 1.0f, 0.63f ) );

    const float beforeSeam = 1.0f - 1.0e-5f;
    CHECK( std::abs( PeriodicLongitudeX( beforeSeam ) - PeriodicLongitudeX( 0.0f ) ) < 1.0e-4f );
    CHECK( std::abs( PeriodicLongitudeY( beforeSeam ) - PeriodicLongitudeY( 0.0f ) ) < 1.0e-4f );
    CHECK( std::abs( PeriodicTriangle( beforeSeam, 6.0f, 0.68f ) - PeriodicTriangle( 0.0f, 6.0f, 0.68f ) ) <
           1.0e-3f );
    CHECK( std::abs( PeriodicStreak( beforeSeam, 0.63f ) - PeriodicStreak( 0.0f, 0.63f ) ) < 1.0e-3f );
}

TEST_CASE( "Shader behavior contracts: ribbon clipping rejects behind geometry and bounds straddles" )
{
    using namespace SkullbonezCore::Rendering::ShaderBehavior;

    Float4 behindStart = { -2.0f, 1.0f, -2.0f, -1.0f };
    Float4 behindEnd = { 2.0f, -1.0f, -1.0f, -0.5f };
    CHECK_FALSE( ClipSegmentToNearPlane( behindStart, behindEnd ) );

    Float4 eyeStart = { -2.0f, -4.0f, -1.0f, -1.0f };
    Float4 eyeEnd = { 2.0f, 4.0f, 1.0f, 1.0f };
    REQUIRE( ClipSegmentToNearPlane( eyeStart, eyeEnd ) );
    CHECK( std::isfinite( eyeStart.x ) );
    CHECK( std::isfinite( eyeStart.y ) );
    CHECK( eyeStart.w >= 0.00009f );
    CHECK( eyeStart.z >= 0.0f );
    CHECK( std::abs( eyeStart.x / eyeStart.w ) < 10.0f );
    CHECK( std::abs( eyeStart.y / eyeStart.w ) < 10.0f );

    Float4 nearStart = { -2.0f, 0.0f, -1.0f, 1.0f };
    Float4 nearEnd = { 2.0f, 0.0f, 1.0f, 1.0f };
    REQUIRE( ClipSegmentToNearPlane( nearStart, nearEnd ) );
    CHECK( nearStart.x == doctest::Approx( 0.0f ) );
    CHECK( nearStart.z == doctest::Approx( 0.0f ) );
    CHECK( nearStart.w == doctest::Approx( 1.0f ) );

    Float4 frontStart = { -0.25f, 0.5f, 0.25f, 1.0f };
    Float4 frontEnd = { 0.75f, -0.5f, 0.75f, 1.0f };
    const Float4 originalFrontStart = frontStart;
    const Float4 originalFrontEnd = frontEnd;
    REQUIRE( ClipSegmentToNearPlane( frontStart, frontEnd ) );
    CHECK( frontStart.x == originalFrontStart.x );
    CHECK( frontStart.y == originalFrontStart.y );
    CHECK( frontStart.z == originalFrontStart.z );
    CHECK( frontStart.w == originalFrontStart.w );
    CHECK( frontEnd.x == originalFrontEnd.x );
    CHECK( frontEnd.y == originalFrontEnd.y );
    CHECK( frontEnd.z == originalFrontEnd.z );
    CHECK( frontEnd.w == originalFrontEnd.w );
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

TEST_CASE( "Shader reflection contracts: separate skybox faces use the clamp sampler" )
{
    CHECK( UnifiedRasterRootSignature::STATIC_SAMPLERS[0].shaderRegister == 0u );
    CHECK( UnifiedRasterRootSignature::STATIC_SAMPLERS[0].addressMode ==
           UnifiedRasterRootSignature::StaticSampler::AddressMode::Wrap );
    CHECK( UnifiedRasterRootSignature::STATIC_SAMPLERS[1].shaderRegister == 1u );
    CHECK( UnifiedRasterRootSignature::STATIC_SAMPLERS[1].addressMode ==
           UnifiedRasterRootSignature::StaticSampler::AddressMode::Clamp );

    const auto* pixelStage = FindGeneratedShaderStage( "unlit_textured.hlsl", "ps" );
    REQUIRE( pixelStage != nullptr );

    bool foundClampSampler = false;
    bool foundWrapSampler = false;
    for ( std::uint32_t i = 0; i < pixelStage->resourceCount; ++i )
    {
        const auto& resource = GeneratedShaderReflection::Resources[pixelStage->resourceStart + i];
        if ( resource.registerClass == 's' )
        {
            foundClampSampler = foundClampSampler || resource.slot == 1u;
            foundWrapSampler = foundWrapSampler || resource.slot == 0u;
        }
    }

    CHECK( foundClampSampler );
    CHECK_FALSE( foundWrapSampler );
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
