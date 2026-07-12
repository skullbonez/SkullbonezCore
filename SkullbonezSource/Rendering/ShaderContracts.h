/*
File: SkullbonezSource/Rendering/ShaderContracts.h
Purpose:
  Lists engine-facing contracts for every shipping raster HLSL family.

Summary:
  Shader contracts name the uniforms, resources, texture slots, and vertex
  layout each pass expects. Startup verifies required ABI rows against generated
  DXIL reflection; per-draw setter diagnostics remain development-only.

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
  - Missing required startup ABI data is a recoverable shader-load failure in
    every build; missing per-draw setter calls remain development diagnostics.

Related:
  - SkullbonezSource/Rendering/DX12/ShaderDX12.cpp
  - SkullbonezSource/Rendering/ShaderReflectionContracts.h
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
    // Contract: raster resources use the named UnifiedRaster binding ABI.
    //
    // Slot N means SRV register tN, bound through BindTexture(handle, N). The
    // UnifiedRaster exposes t0..t5; semantic ownership lives in
    // RenderRasterBindingContract.h and reflection rejects any other slot.
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

struct ShaderVertexInputContract
{
    const char* baseName;
    const char* signature;
};

// CPU-owned input-layout vocabulary. Each token is semantic/index, component
// mask, and system value in the same order used to create the PSO layout.
inline const ShaderVertexInputContract* ShippingShaderVertexInputContracts()
{
    static constexpr ShaderVertexInputContract contracts[] = {
        { "collision_visualizer",
          "POSITION0:xyz:NONE,NORMAL0:xyz:NONE,TEXCOORD1:xyzw:NONE,TEXCOORD2:xyzw:NONE,TEXCOORD3:xyzw:NONE,TEXCOORD4:"
          "xyzw:NONE,TEXCOORD5:xyzw:NONE" },
        { "grid_line", "POSITION0:xyz:NONE,TEXCOORD0:xyz:NONE" },
        { "launcher_laser", "POSITION0:xyz:NONE,TEXCOORD0:xyzw:NONE" },
        { "lit_textured", "POSITION0:xyz:NONE,NORMAL0:xyz:NONE,TEXCOORD0:xy:NONE" },
        { "lit_textured_instanced",
          "POSITION0:xyz:NONE,NORMAL0:xyz:NONE,TEXCOORD0:xy:NONE,TEXCOORD1:xyzw:NONE,TEXCOORD2:xyzw:NONE,TEXCOORD3:"
          "xyzw:NONE,TEXCOORD4:xyzw:NONE,TEXCOORD5:xyzw:NONE,TEXCOORD6:xyzw:NONE,TEXCOORD7:xyzw:NONE,TEXCOORD8:xyzw:"
          "NONE" },
        { "post_tonemap", "POSITION0:xy:NONE,TEXCOORD0:xy:NONE" },
        { "post_volumetric_light", "POSITION0:xy:NONE,TEXCOORD0:xy:NONE" },
        { "shadow_depth", "POSITION0:xyz:NONE" },
        { "shadow_depth_instanced",
          "POSITION0:xyz:NONE,TEXCOORD1:xyzw:NONE,TEXCOORD2:xyzw:NONE,TEXCOORD3:xyzw:NONE,TEXCOORD4:xyzw:NONE" },
        { "sky_atmosphere", "POSITION0:xy:NONE,TEXCOORD0:xy:NONE" },
        { "soft_additive_ribbon", "POSITION0:xyz:NONE,TEXCOORD0:xyzw:NONE,TEXCOORD1:xyzw:NONE" },
        { "solid_color", "POSITION0:xy:NONE" },
        { "solid_color_batch", "POSITION0:xy:NONE,TEXCOORD0:xyzw:NONE" },
        { "text", "POSITION0:xy:NONE,TEXCOORD0:xy:NONE,TEXCOORD1:xyz:NONE" },
        { "tornado_fx", "POSITION0:xyz:NONE,TEXCOORD0:xyzw:NONE,TEXCOORD1:xyzw:NONE" },
        { "trajectory_ribbon",
          "POSITION0:xyz:NONE,TEXCOORD0:xyzw:NONE,TEXCOORD1:xyzw:NONE,TEXCOORD2:xy:NONE,SV_VertexID0:x:VERTID" },
        { "ui_render_target_preview", "POSITION0:xy:NONE,TEXCOORD0:xy:NONE" },
        { "UIBackdropBlur", "POSITION0:xy:NONE,TEXCOORD0:xy:NONE" },
        { "unlit_textured", "POSITION0:xyz:NONE,TEXCOORD0:xy:NONE" },
        { "water_calm", "POSITION0:xyz:NONE" },
        { "water_ocean", "POSITION0:xyz:NONE" },
    };
    return contracts;
}

inline constexpr size_t ShippingShaderVertexInputContractCount()
{
    return 21;
}

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

inline const ShaderResourceDecl* FindShaderResourceDecl( const ShaderProgramDesc& desc, const char* name )
{
    for ( size_t i = 0; name && i < desc.resourceCount; ++i )
    {
        if ( ShaderContractNameEquals( desc.resources[i].name, name ) )
        {
            return &desc.resources[i];
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

// Every shipping raster family has an independent CPU declaration. Generated
// DXIL reflection is evidence about the artifact, not the source of this table;
// keeping all 21 rows here catches semantic name/type/slot drift even when a
// moved resource would still fit the shared root signature.
inline const ShaderProgramDesc* ShippingRasterShaderContracts()
{
    static constexpr ShaderUniformDecl collisionVisualizerUniforms[] = {
        { "uView", ShaderValueType::Mat4, true },
        { "uProjection", ShaderValueType::Mat4, true },
        { "uClipPlane", ShaderValueType::Vec4, true },
        { "uLightPosition", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderUniformDecl viewProjectionUniforms[] = {
        { "uViewProj", ShaderValueType::Mat4, true },
    };
    static constexpr ShaderUniformDecl shadowDepthUniforms[] = {
        { "uModel", ShaderValueType::Mat4, true },
        { "uView", ShaderValueType::Mat4, true },
        { "uProjection", ShaderValueType::Mat4, true },
        { "uClipPlane", ShaderValueType::Vec4, true },
        { "uCinematicTerrain", ShaderValueType::Vec4, true },
        { "uCinematicBasin", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderUniformDecl shadowDepthInstancedUniforms[] = {
        { "uView", ShaderValueType::Mat4, true },
        { "uProjection", ShaderValueType::Mat4, true },
        { "uClipPlane", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderUniformDecl solidColorUniforms[] = {
        { "uProjection", ShaderValueType::Mat4, true },
        { "uColor", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderUniformDecl projectionUniforms[] = {
        { "uProjection", ShaderValueType::Mat4, true },
    };
    static constexpr ShaderUniformDecl trajectoryRibbonUniforms[] = {
        { "uViewProj", ShaderValueType::Mat4, true },
        { "uViewportPixels", ShaderValueType::Vec4, true },
        { "uRibbonStyle", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderUniformDecl previewUniforms[] = {
        { "uProjection", ShaderValueType::Mat4, true },
        { "uPreviewParams", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderUniformDecl backdropBlurUniforms[] = {
        { "uProjection", ShaderValueType::Mat4, true },
        { "uTexelSize", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderUniformDecl unlitTexturedUniforms[] = {
        { "uModel", ShaderValueType::Mat4, true },
        { "uView", ShaderValueType::Mat4, true },
        { "uProjection", ShaderValueType::Mat4, true },
        { "uColorTint", ShaderValueType::Vec4, true },
    };
    static constexpr ShaderResourceDecl textureAtT0[] = {
        { "uTexture", 0, ShaderResourceKind::Texture2D, true },
    };
    static constexpr ShaderResourceDecl fontAtT0[] = {
        { "uFontTexture", 0, ShaderResourceKind::Texture2D, true },
    };
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
        // Instanced materials are sourced from uMaterialTable; the legacy t0
        // declaration is optimized out of the shipping pixel stage.
        { "uTexture", 0, ShaderResourceKind::Texture2D, false },
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
        { "uDetailShadowViewProj", ShaderValueType::Mat4, false },
        { "uDetailShadowParams", ShaderValueType::Vec4, false },
        { "uDetailShadowFlags", ShaderValueType::Vec4, false },
    };
    static constexpr ShaderResourceDecl litTexturedResources[] = {
        { "uTexture", 0, ShaderResourceKind::Texture2D, true },
        { "uShadowMap", 3, ShaderResourceKind::Texture2D, false },
        { "uDetailShadowMap", 5, ShaderResourceKind::Texture2D, false },
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
        { "uBloomTexelSize", ShaderValueType::Vec4, true },
        { "uBloomParams", ShaderValueType::Vec4, true },
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
    };
    static constexpr ShaderResourceDecl volumetricResources[] = {
        { "uSceneTex", 0, ShaderResourceKind::Texture2D, true },
        { "uDepthTex", 1, ShaderResourceKind::Texture2D, true },
    };

    static constexpr ShaderProgramDesc contracts[] = {
        { "collision_visualizer",
          "debug",
          "P3_N3_I4x4_Color3",
          collisionVisualizerUniforms,
          sizeof( collisionVisualizerUniforms ) / sizeof( collisionVisualizerUniforms[0] ),
          nullptr,
          0 },
        { "grid_line",
          "debug",
          "P3_Color3",
          viewProjectionUniforms,
          sizeof( viewProjectionUniforms ) / sizeof( viewProjectionUniforms[0] ),
          nullptr,
          0 },
        { "launcher_laser",
          "effects",
          "P3_Color4",
          viewProjectionUniforms,
          sizeof( viewProjectionUniforms ) / sizeof( viewProjectionUniforms[0] ),
          nullptr,
          0 },
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
        { "shadow_depth",
          "shadow",
          "P3",
          shadowDepthUniforms,
          sizeof( shadowDepthUniforms ) / sizeof( shadowDepthUniforms[0] ),
          nullptr,
          0 },
        { "shadow_depth_instanced",
          "shadow",
          "P3_I4x4",
          shadowDepthInstancedUniforms,
          sizeof( shadowDepthInstancedUniforms ) / sizeof( shadowDepthInstancedUniforms[0] ),
          nullptr,
          0 },
        { "soft_additive_ribbon",
          "effects",
          "P3_Color4_UV4",
          viewProjectionUniforms,
          sizeof( viewProjectionUniforms ) / sizeof( viewProjectionUniforms[0] ),
          nullptr,
          0 },
        { "solid_color",
          "ui",
          "P2",
          solidColorUniforms,
          sizeof( solidColorUniforms ) / sizeof( solidColorUniforms[0] ),
          nullptr,
          0 },
        { "solid_color_batch",
          "ui",
          "P2_Color4",
          projectionUniforms,
          sizeof( projectionUniforms ) / sizeof( projectionUniforms[0] ),
          nullptr,
          0 },
        { "text",
          "ui",
          "P2_UV2_Color3",
          projectionUniforms,
          sizeof( projectionUniforms ) / sizeof( projectionUniforms[0] ),
          fontAtT0,
          sizeof( fontAtT0 ) / sizeof( fontAtT0[0] ) },
        { "tornado_fx",
          "effects",
          "P3_Color4_UV4",
          viewProjectionUniforms,
          sizeof( viewProjectionUniforms ) / sizeof( viewProjectionUniforms[0] ),
          nullptr,
          0 },
        { "trajectory_ribbon",
          "effects",
          "P3_Color4_UV4_Aux2",
          trajectoryRibbonUniforms,
          sizeof( trajectoryRibbonUniforms ) / sizeof( trajectoryRibbonUniforms[0] ),
          nullptr,
          0 },
        { "ui_render_target_preview",
          "ui",
          "FullscreenP2_UV2",
          previewUniforms,
          sizeof( previewUniforms ) / sizeof( previewUniforms[0] ),
          textureAtT0,
          sizeof( textureAtT0 ) / sizeof( textureAtT0[0] ) },
        { "UIBackdropBlur",
          "ui",
          "FullscreenP2_UV2",
          backdropBlurUniforms,
          sizeof( backdropBlurUniforms ) / sizeof( backdropBlurUniforms[0] ),
          textureAtT0,
          sizeof( textureAtT0 ) / sizeof( textureAtT0[0] ) },
        { "unlit_textured",
          "objects",
          "P3_UV2",
          unlitTexturedUniforms,
          sizeof( unlitTexturedUniforms ) / sizeof( unlitTexturedUniforms[0] ),
          textureAtT0,
          sizeof( textureAtT0 ) / sizeof( textureAtT0[0] ) },
    };
    return contracts;
}

inline constexpr size_t ShippingRasterShaderContractCount()
{
    return 21;
}

inline const ShaderProgramDesc* FindShaderProgramDesc( const char* pathOrBaseName )
{
    const ShaderProgramDesc* contracts = ShippingRasterShaderContracts();
    const size_t count = ShippingRasterShaderContractCount();
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
