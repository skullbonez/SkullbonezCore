/*
File: SkullbonezSource/ShaderContracts.h
Purpose:
  Lists engine-facing contracts for high-risk HLSL shader families.

Mental model:
  Shader contracts name the uniforms, resources, texture slots, and vertex
  layout each pass expects. They are diagnostics, not a new binding backend.

Glossary:
  Uniform: Named shader constant set by engine code before drawing.
  Resource: Shader-visible texture or buffer binding declared by a shader.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  ABI (Application Binary Interface): The compiled binding contract between
  C++ root parameters, shader registers, and draw-time texture slots.
  Vertex layout: Ordered per-vertex and per-instance data shape consumed by a
  shader program.

Invariants:
  - These contracts must not encode native D3D12 descriptors or root-signature
    implementation details.
  - Missing contract data should warn in development while release builds stay
    tolerant.

Related:
  - SkullbonezSource/ShaderDX12.cpp
  - Agentic/Reference/shader-inventory.md
*/
#pragma once

#include <cstddef>
#include <cstring>

namespace SkullbonezCore
{
namespace Rendering
{
enum class ShaderValueType
{
    Int,
    Float,
    Vec3,
    Vec4,
    Mat4
};

enum class ShaderResourceKind
{
    Texture2D
};

struct ShaderUniformDecl
{
    const char* name;
    ShaderValueType type;
    bool required;
};

struct ShaderResourceDecl
{
    const char* name;
    // Contract: ordinary raster resources use the current DX12 binding ABI.
    //
    // Slot N means SRV register tN, bound through BindTexture(handle, N). The
    // ABI currently exposes t0..t4; t4 is the object material table while the
    // per-instance stream still carries the draw-local material payload.
    int slot;
    ShaderResourceKind kind;
    bool required;
};

struct ShaderProgramDesc
{
    const char* baseName;
    const char* passCategory;
    const char* vertexLayout;
    const ShaderUniformDecl* uniforms;
    size_t uniformCount;
    const ShaderResourceDecl* resources;
    size_t resourceCount;
};

inline const char* ShaderValueTypeName( ShaderValueType type )
{
    switch ( type )
    {
    case ShaderValueType::Int:
        return "int";
    case ShaderValueType::Float:
        return "float";
    case ShaderValueType::Vec3:
        return "vec3";
    case ShaderValueType::Vec4:
        return "vec4";
    case ShaderValueType::Mat4:
        return "mat4";
    default:
        return "unknown";
    }
}

inline const char* ShaderResourceKindName( ShaderResourceKind kind )
{
    switch ( kind )
    {
    case ShaderResourceKind::Texture2D:
        return "Texture2D";
    default:
        return "unknown";
    }
}

inline bool ShaderContractNameEquals( const char* left, const char* right )
{
    return left && right && std::strcmp( left, right ) == 0;
}

// Looks up a uniform by authoring name. The optional index lets runtime
// diagnostics mark exactly which required uniforms were set during the current
// activation without duplicating the contract table.
inline const ShaderUniformDecl*
FindShaderUniformDecl( const ShaderProgramDesc& desc, const char* name, size_t* outIndex = nullptr )
{
    if ( outIndex )
    {
        *outIndex = static_cast<size_t>( -1 );
    }
    if ( !name )
    {
        return nullptr;
    }
    for ( size_t i = 0; i < desc.uniformCount; ++i )
    {
        if ( ShaderContractNameEquals( desc.uniforms[i].name, name ) )
        {
            if ( outIndex )
            {
                *outIndex = i;
            }
            return &desc.uniforms[i];
        }
    }
    return nullptr;
}

// Normalizes either "foo.hlsl" or ".../foo.hlsl" to the base shader family name.
// Contract tables use base names because asset manifests and debug output may
// pass either a path or a logical shader id.
inline const char* ShaderBaseNameFromPath( const char* path, size_t& outLength )
{
    const char* start = path ? path : "";
    for ( const char* cursor = start; *cursor != '\0'; ++cursor )
    {
        if ( *cursor == '/' || *cursor == '\\' )
        {
            start = cursor + 1;
        }
    }

    outLength = std::strlen( start );
    static constexpr const char* hlslExt = ".hlsl";
    static constexpr size_t hlslExtLen = 5;
    if ( outLength > hlslExtLen && std::strcmp( start + outLength - hlslExtLen, hlslExt ) == 0 )
    {
        outLength -= hlslExtLen;
    }
    return start;
}

inline bool ShaderContractMatchesBaseName( const char* baseName, const char* pathOrBaseName )
{
    size_t candidateLength = 0;
    const char* candidate = ShaderBaseNameFromPath( pathOrBaseName, candidateLength );
    return baseName && std::strlen( baseName ) == candidateLength &&
           std::strncmp( baseName, candidate, candidateLength ) == 0;
}

// High-risk shader families that get runtime contract diagnostics.
// This is intentionally a curated table, not a full reflection cache; the goal
// is to catch stale hand-written setters around passes that are expensive to
// debug visually.
inline const ShaderProgramDesc* HighRiskShaderContracts()
{
    static constexpr ShaderUniformDecl litTexturedInstancedUniforms[] = {
        { "uView", ShaderValueType::Mat4, true },
        { "uProjection", ShaderValueType::Mat4, true },
        { "uClipPlane", ShaderValueType::Vec4, true },
        { "uLightPosition", ShaderValueType::Vec4, true },
        { "uLightAmbient", ShaderValueType::Vec4, true },
        { "uLightDiffuse", ShaderValueType::Vec4, true },
        { "uMaterialAmbient", ShaderValueType::Vec4, false },
        { "uMaterialDiffuse", ShaderValueType::Vec4, false },
        { "uObjectStyle", ShaderValueType::Int, true },
        { "uPrimitiveShape", ShaderValueType::Int, true },
        { "uMaterialAlpha", ShaderValueType::Float, true },
        { "uShadowViewProj", ShaderValueType::Mat4, true },
        { "uShadowParams", ShaderValueType::Vec4, true },
        { "uShadowFlags", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderResourceDecl litTexturedInstancedResources[] = {
        { "uTexture", 0, ShaderResourceKind::Texture2D, true },
        { "uShadowMap", 3, ShaderResourceKind::Texture2D, false },
        { "uMaterialTable", 4, ShaderResourceKind::Texture2D, true },
    };

    static constexpr ShaderUniformDecl litTexturedUniforms[] = {
        { "uModel", ShaderValueType::Mat4, true },
        { "uView", ShaderValueType::Mat4, true },
        { "uProjection", ShaderValueType::Mat4, true },
        { "uClipPlane", ShaderValueType::Vec4, true },
        { "uLightPosition", ShaderValueType::Vec4, true },
        { "uLightAmbient", ShaderValueType::Vec4, true },
        { "uLightDiffuse", ShaderValueType::Vec4, true },
        { "uMaterialAmbient", ShaderValueType::Vec4, true },
        { "uMaterialDiffuse", ShaderValueType::Vec4, true },
        { "uCinematicTerrain", ShaderValueType::Vec4, true },
        { "uCinematicBasin", ShaderValueType::Vec4, true },
        { "uStyleModes", ShaderValueType::Vec4, true },
        { "uTerrainTint", ShaderValueType::Vec4, true },
        { "uTerrainAccent", ShaderValueType::Vec4, true },
        { "uTerrainGrid", ShaderValueType::Vec4, true },
        { "uShadowViewProj", ShaderValueType::Mat4, true },
        { "uShadowParams", ShaderValueType::Vec4, true },
        { "uShadowFlags", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderResourceDecl litTexturedResources[] = {
        { "uTexture", 0, ShaderResourceKind::Texture2D, true },
        { "uShadowMap", 3, ShaderResourceKind::Texture2D, false },
    };

    static constexpr ShaderUniformDecl waterCalmUniforms[] = {
        { "uModel", ShaderValueType::Mat4, true },
        { "uView", ShaderValueType::Mat4, true },
        { "uProjection", ShaderValueType::Mat4, true },
        { "uReflectVP", ShaderValueType::Mat4, true },
        { "uColorTint", ShaderValueType::Vec4, true },
        { "uReflectionStrength", ShaderValueType::Float, true },
        { "uWaterFresnelF0", ShaderValueType::Float, true },
        { "uCameraWorld", ShaderValueType::Vec3, true },
        { "uNoReflect", ShaderValueType::Int, true },
        { "uCinematicMode", ShaderValueType::Float, true },
        { "uWaterMode", ShaderValueType::Int, true },
        { "uSunGlintStrength", ShaderValueType::Float, true },
        { "uSunColor", ShaderValueType::Vec3, true },
        { "uBasinMask", ShaderValueType::Vec4, true },
        { "uBasinMaskFeather", ShaderValueType::Float, true },
    };
    static constexpr ShaderResourceDecl waterResources[] = {
        { "uReflectionTex", 1, ShaderResourceKind::Texture2D, true },
    };

    static constexpr ShaderUniformDecl waterOceanUniforms[] = {
        { "uModel", ShaderValueType::Mat4, true },
        { "uView", ShaderValueType::Mat4, true },
        { "uProjection", ShaderValueType::Mat4, true },
        { "uReflectVP", ShaderValueType::Mat4, true },
        { "uColorTint", ShaderValueType::Vec4, true },
        { "uTime", ShaderValueType::Float, true },
        { "uWaveHeight", ShaderValueType::Float, true },
        { "uReflectionStrength", ShaderValueType::Float, true },
        { "uWaterFresnelF0", ShaderValueType::Float, true },
        { "uCameraWorld", ShaderValueType::Vec3, true },
        { "uPerturbStrength", ShaderValueType::Float, true },
        { "uFlatWater", ShaderValueType::Int, true },
        { "uNoReflect", ShaderValueType::Int, true },
        { "uCinematicMode", ShaderValueType::Float, true },
        { "uSunGlintStrength", ShaderValueType::Float, true },
        { "uSunColor", ShaderValueType::Vec3, true },
    };

    static constexpr ShaderUniformDecl skyAtmosphereUniforms[] = {
        { "uSunParams", ShaderValueType::Vec4, true },
        { "uSunColor", ShaderValueType::Vec3, true },
        { "uHorizonColor", ShaderValueType::Vec3, true },
        { "uZenithColor", ShaderValueType::Vec3, true },
        { "uCloudParams", ShaderValueType::Vec4, true },
        { "uInvView", ShaderValueType::Mat4, true },
        { "uInvProjection", ShaderValueType::Mat4, true },
        { "uSkyMode", ShaderValueType::Int, true },
    };

    static constexpr ShaderUniformDecl tonemapUniforms[] = {
        { "uExposure", ShaderValueType::Float, true },
        { "uGamma", ShaderValueType::Float, true },
        { "uVolumetricCompositeStrength", ShaderValueType::Float, true },
        { "uDepthParams", ShaderValueType::Vec4, true },
        { "uFogParams", ShaderValueType::Vec4, true },
        { "uFogColor", ShaderValueType::Vec3, true },
        { "uSunShaftParams", ShaderValueType::Vec4, true },
        { "uSunColor", ShaderValueType::Vec3, true },
        { "uBloomParams", ShaderValueType::Vec4, true },
        { "uCloudParams", ShaderValueType::Vec4, true },
        { "uStyleGrade", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderResourceDecl tonemapResources[] = {
        { "uSceneTex", 0, ShaderResourceKind::Texture2D, true },
        { "uDepthTex", 1, ShaderResourceKind::Texture2D, true },
        { "uVolumetricTex", 2, ShaderResourceKind::Texture2D, true },
    };

    static constexpr ShaderUniformDecl volumetricUniforms[] = {
        { "uDepthParams", ShaderValueType::Vec4, true },
        { "uSunShaftParams", ShaderValueType::Vec4, true },
        { "uSunColor", ShaderValueType::Vec3, true },
        { "uVolumetricParams", ShaderValueType::Vec4, true },
        { "uCloudParams", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderResourceDecl volumetricResources[] = {
        { "uSceneTex", 0, ShaderResourceKind::Texture2D, true },
        { "uDepthTex", 1, ShaderResourceKind::Texture2D, true },
    };

    static constexpr ShaderProgramDesc contracts[] = {
        { "lit_textured_instanced",
          "objects",
          "P3_N3_UV2_I4x4_Material4x3",
          litTexturedInstancedUniforms,
          sizeof( litTexturedInstancedUniforms ) / sizeof( litTexturedInstancedUniforms[0] ),
          litTexturedInstancedResources,
          sizeof( litTexturedInstancedResources ) / sizeof( litTexturedInstancedResources[0] ) },
        { "lit_textured",
          "terrain",
          "P3_N3_UV2",
          litTexturedUniforms,
          sizeof( litTexturedUniforms ) / sizeof( litTexturedUniforms[0] ),
          litTexturedResources,
          sizeof( litTexturedResources ) / sizeof( litTexturedResources[0] ) },
        { "water_calm",
          "water",
          "P3",
          waterCalmUniforms,
          sizeof( waterCalmUniforms ) / sizeof( waterCalmUniforms[0] ),
          waterResources,
          sizeof( waterResources ) / sizeof( waterResources[0] ) },
        { "water_ocean",
          "water",
          "P3",
          waterOceanUniforms,
          sizeof( waterOceanUniforms ) / sizeof( waterOceanUniforms[0] ),
          waterResources,
          sizeof( waterResources ) / sizeof( waterResources[0] ) },
        { "sky_atmosphere",
          "sky",
          "FullscreenP2_UV2",
          skyAtmosphereUniforms,
          sizeof( skyAtmosphereUniforms ) / sizeof( skyAtmosphereUniforms[0] ),
          nullptr,
          0 },
        { "post_tonemap",
          "post",
          "FullscreenP2_UV2",
          tonemapUniforms,
          sizeof( tonemapUniforms ) / sizeof( tonemapUniforms[0] ),
          tonemapResources,
          sizeof( tonemapResources ) / sizeof( tonemapResources[0] ) },
        { "post_volumetric_light",
          "post",
          "FullscreenP2_UV2",
          volumetricUniforms,
          sizeof( volumetricUniforms ) / sizeof( volumetricUniforms[0] ),
          volumetricResources,
          sizeof( volumetricResources ) / sizeof( volumetricResources[0] ) },
    };
    return contracts;
}

inline size_t HighRiskShaderContractCount()
{
    return 7;
}

inline const ShaderProgramDesc* FindShaderProgramDesc( const char* pathOrBaseName )
{
    const ShaderProgramDesc* contracts = HighRiskShaderContracts();
    const size_t count = HighRiskShaderContractCount();
    for ( size_t i = 0; i < count; ++i )
    {
        if ( ShaderContractMatchesBaseName( contracts[i].baseName, pathOrBaseName ) )
        {
            return &contracts[i];
        }
    }
    return nullptr;
}
} // namespace Rendering
} // namespace SkullbonezCore
