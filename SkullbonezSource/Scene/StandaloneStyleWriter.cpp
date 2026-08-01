/*
File: StandaloneStyleWriter.cpp
Purpose:
  Serializes complete detached presentation values as schema-v1 style JSON.

Summary:
  One ordered document builder writes all eighty cinematic atoms and every
  material field using the parser's established vocabulary. JSON output and
  the receipt listing are projections of that same document.

Glossary:
  Resolved field: Explicit value whose reload does not consult a later default
    or generator recipe.
  Canonical spelling: Existing style-parser key or material-kind text used by
    curated style files.

Invariants:
  - Root key, cinematic key, and object-material key order is stable.
  - nlohmann's deterministic dump owns shortest round-trippable float text.
  - Validation completes before any target path is opened or replaced.

Related:
  - SkullbonezSource/Scene/StandaloneStyleWriter.h
  - SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp
  - SkullbonezSource/Core/AtomicTextFileWriter.cpp
*/
#include "StandaloneStyleWriter.h"
#include "../Core/AtomicTextFileWriter.h"
#include "../Core/SbDiagnosticStore.h"

#include <climits>
#include <cmath>
#include <cstring>
#include <sstream>

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

namespace SkullbonezCore::Scene
{
namespace
{
using Json = nlohmann::ordered_json;
constexpr const char* OWNER = "Scene/StandaloneStyleWriter";

Json Vec2( float x, float y )
{
    return Json::array( { x, y } );
}

Json Vec3( float x, float y, float z )
{
    return Json::array( { x, y, z } );
}

bool TextTerminated( const char* text, size_t capacity )
{
    return text && std::memchr( text, '\0', capacity ) != nullptr;
}

Core::SbResult Validate( Core::SbDiagnosticStore& diagnostics, const StandaloneStyleSnapshot& snapshot )
{
    const Core::CinematicRenderConfig& c = snapshot.cinematic;
    const float values[] = {
        c.exposure,
        c.gamma,
        c.sunAzimuth,
        c.sunElevation,
        c.sunColorR,
        c.sunColorG,
        c.sunColorB,
        c.sunIntensity,
        c.skyHorizonR,
        c.skyHorizonG,
        c.skyHorizonB,
        c.skyZenithR,
        c.skyZenithG,
        c.skyZenithB,
        c.skyGlowStrength,
        c.cloudCoverage,
        c.cloudSoftness,
        c.cloudScale,
        c.cloudIntensity,
        c.sunShaftStrength,
        c.sunShaftFalloff,
        c.volumetricStrength,
        c.volumetricDensity,
        c.volumetricDecay,
        c.bloomThreshold,
        c.bloomKnee,
        c.bloomStrength,
        c.bloomRadius,
        c.terrainRelief,
        c.basinDepth,
        c.basinRimLift,
        c.fogColorR,
        c.fogColorG,
        c.fogColorB,
        c.fogStart,
        c.fogEnd,
        c.fogDensity,
        c.fogMaxOpacity,
        c.styleSaturation,
        c.styleContrast,
        c.styleVignette,
        c.terrainTintR,
        c.terrainTintG,
        c.terrainTintB,
        c.terrainAccentR,
        c.terrainAccentG,
        c.terrainAccentB,
        c.terrainGridScale,
        c.terrainGridStrength,
        c.waterTintR,
        c.waterTintG,
        c.waterTintB,
        c.waterAlpha,
        c.waterReflectionStrength,
        c.waterGlintStrength,
        c.basinCenterX,
        c.basinCenterZ,
        c.basinRadiusX,
        c.basinRadiusZ,
        c.basinFeather,
        c.shadow.strength,
        c.shadow.softness,
        c.shadow.depthBias,
        c.shadow.slopeBias,
        c.shadow.maxDistance,
    };

    for ( float value : values )
    {

        if ( !std::isfinite( value ) )
        {
            return diagnostics.Failure( OWNER, "Standalone style contains a non-finite cinematic value." );
        }
    }

    struct BoundedValue
    {
        float value;
        float minimum;
        float maximum;
    };

    // Invariant: this table mirrors the schema-v1 parser's scalar ranges. A
    // value rejected on fresh load must be rejected before file publication.
    const BoundedValue parserBounded[] = {
        { c.exposure, 0.0f, 16.0f },
        { c.gamma, 0.1f, 8.0f },
        { c.sunAzimuth, 0.0f, 1.0f },
        { c.sunElevation, 0.0f, 1.0f },
        { c.sunColorR, 0.0f, 4.0f },
        { c.sunColorG, 0.0f, 4.0f },
        { c.sunColorB, 0.0f, 4.0f },
        { c.sunIntensity, 0.0f, 80.0f },
        { c.skyHorizonR, 0.0f, 4.0f },
        { c.skyHorizonG, 0.0f, 4.0f },
        { c.skyHorizonB, 0.0f, 4.0f },
        { c.skyZenithR, 0.0f, 4.0f },
        { c.skyZenithG, 0.0f, 4.0f },
        { c.skyZenithB, 0.0f, 4.0f },
        { c.skyGlowStrength, 0.0f, 16.0f },
        { c.cloudCoverage, 0.0f, 1.0f },
        { c.cloudSoftness, 0.001f, 1.0f },
        { c.cloudScale, 0.1f, 64.0f },
        { c.cloudIntensity, 0.0f, 4.0f },
        { c.sunShaftStrength, 0.0f, 8.0f },
        { c.sunShaftFalloff, 0.1f, 10.0f },
        { c.volumetricStrength, 0.0f, 8.0f },
        { c.volumetricDensity, 0.0f, 8.0f },
        { c.volumetricDecay, 0.0f, 1.0f },
        { c.bloomThreshold, 0.0f, 16.0f },
        { c.bloomKnee, 0.001f, 8.0f },
        { c.bloomStrength, 0.0f, 8.0f },
        { c.bloomRadius, 0.1f, 32.0f },
        { c.terrainRelief, 0.0f, 4.0f },
        { c.basinDepth, 0.0f, 256.0f },
        { c.basinRimLift, 0.0f, 256.0f },
        { c.fogColorR, 0.0f, 4.0f },
        { c.fogColorG, 0.0f, 4.0f },
        { c.fogColorB, 0.0f, 4.0f },
        { c.fogStart, 0.0f, 10000.0f },
        { c.fogEnd, 0.0f, 20000.0f },
        { c.fogDensity, 0.0f, 0.1f },
        { c.fogMaxOpacity, 0.0f, 1.0f },
        { c.shadow.strength, 0.0f, 1.0f },
        { c.shadow.softness, 0.25f, 4.0f },
        { c.shadow.depthBias, 0.0f, 0.05f },
        { c.shadow.slopeBias, 0.0f, 0.05f },
        { c.shadow.maxDistance, 128.0f, 10000.0f },
    };

    for ( const BoundedValue& bounded : parserBounded )
    {

        if ( bounded.value < bounded.minimum || bounded.value > bounded.maximum )
        {
            return diagnostics.Failure( OWNER, "Standalone style contains a cinematic value outside parser bounds." );
        }
    }

    if ( c.shadow.mapSize < 256 || c.shadow.mapSize > 8192 || c.shadow.pcfRadius < 0 || c.shadow.pcfRadius > 3 )
    {
        return diagnostics.Failure( OWNER, "Standalone style contains a cinematic value outside parser bounds." );
    }

    for ( size_t index = 0; index < snapshot.materialRules.size(); ++index )
    {
        const StandaloneStyleMaterialRule& rule = snapshot.materialRules[index];
        const Rendering::RenderMaterial& material = rule.material;
        const int kind = static_cast<int>( material.kind );
        const float materialValues[] = {
            material.baseColor[0],     material.baseColor[1],     material.baseColor[2],     material.baseColor[3],
            material.emissiveColor[0], material.emissiveColor[1], material.emissiveColor[2], material.emissiveStrength,
            material.roughness,        material.metallic,         material.specular,         material.transmission,
            material.stylization,      material.textureMode,
        };

        if ( !TextTerminated( rule.target.data(), rule.target.size() ) || rule.target[0] == '\0' ||
             !TextTerminated( material.name, sizeof( material.name ) ) || kind < 0 || kind > 13 )
        {
            return diagnostics.Failure( OWNER, "Standalone material rule %zu has invalid identity text or kind.", index );
        }

        for ( float value : materialValues )
        {

            if ( !std::isfinite( value ) )
            {
                return diagnostics.Failure( OWNER, "Standalone material rule %zu contains a non-finite value.", index );
            }
        }

        if ( material.baseColor[3] < 0.0f || material.baseColor[3] > 1.0f || material.roughness < 0.0f ||
             material.roughness > 1.0f || material.metallic < 0.0f || material.metallic > 1.0f || material.specular < 0.0f ||
             material.specular > 1.0f || material.transmission < 0.0f || material.transmission > 1.0f ||
             material.stylization < 0.0f || material.stylization > 1.0f || material.emissiveStrength < 0.0f ||
             material.flags > static_cast<uint32_t>( INT_MAX ) || material.contactFlashAlpha != 0.0f ||
             material.textureMode != Rendering::RenderMaterialKindLegacyMode( material.kind ) )
        {
            return diagnostics.Failure( OWNER, "Standalone material rule %zu contains a value outside parser bounds.",
                                        index );
        }
    }

    return Core::SbResult::Success();
}

Json BuildDocument( const StandaloneStyleSnapshot& snapshot )
{
    const Core::CinematicRenderConfig& c = snapshot.cinematic;
    Json root;

    // Invariant: insertion order is serialized order for ordered_json. Keep
    // this root/cinematic/material sequence aligned with the LL0 contract and
    // the pinned byte fingerprint in TestLookLabSerialization.cpp.
    root["format"] = "skullbonez.style.json";
    root["version"] = 1;
    Json cinematic;
    cinematic["rendering"] = c.enabled;
    cinematic["skyAtmosphere"] = c.skyAtmosphereEnabled;
    cinematic["clouds"] = c.cloudsEnabled;
    cinematic["godRays"] = c.godRaysEnabled;
    cinematic["volumetricLighting"] = c.volumetricLightingEnabled;
    cinematic["bloom"] = c.bloomEnabled;
    cinematic["fog"] = c.fogEnabled;
    cinematic["terrainReliefEnabled"] = c.terrainReliefEnabled;
    cinematic["exposure"] = c.exposure;
    cinematic["gamma"] = c.gamma;
    cinematic["sunScreenX"] = c.sunAzimuth;
    cinematic["sunScreenY"] = c.sunElevation;
    cinematic["sunColorR"] = c.sunColorR;
    cinematic["sunColorG"] = c.sunColorG;
    cinematic["sunColorB"] = c.sunColorB;
    cinematic["sunIntensity"] = c.sunIntensity;
    cinematic["skyHorizonR"] = c.skyHorizonR;
    cinematic["skyHorizonG"] = c.skyHorizonG;
    cinematic["skyHorizonB"] = c.skyHorizonB;
    cinematic["skyZenithR"] = c.skyZenithR;
    cinematic["skyZenithG"] = c.skyZenithG;
    cinematic["skyZenithB"] = c.skyZenithB;
    cinematic["skyGlowStrength"] = c.skyGlowStrength;
    cinematic["cloudCoverage"] = c.cloudCoverage;
    cinematic["cloudSoftness"] = c.cloudSoftness;
    cinematic["cloudScale"] = c.cloudScale;
    cinematic["cloudIntensity"] = c.cloudIntensity;
    cinematic["sunShaftStrength"] = c.sunShaftStrength;
    cinematic["sunShaftFalloff"] = c.sunShaftFalloff;
    cinematic["volumetricStrength"] = c.volumetricStrength;
    cinematic["volumetricDensity"] = c.volumetricDensity;
    cinematic["volumetricDecay"] = c.volumetricDecay;
    cinematic["bloomThreshold"] = c.bloomThreshold;
    cinematic["bloomKnee"] = c.bloomKnee;
    cinematic["bloomStrength"] = c.bloomStrength;
    cinematic["bloomRadius"] = c.bloomRadius;
    cinematic["terrainRelief"] = c.terrainRelief;
    cinematic["basinDepth"] = c.basinDepth;
    cinematic["basinRimLift"] = c.basinRimLift;
    cinematic["fogColorR"] = c.fogColorR;
    cinematic["fogColorG"] = c.fogColorG;
    cinematic["fogColorB"] = c.fogColorB;
    cinematic["fogStart"] = c.fogStart;
    cinematic["fogEnd"] = c.fogEnd;
    cinematic["fogDensity"] = c.fogDensity;
    cinematic["fogMaxOpacity"] = c.fogMaxOpacity;
    cinematic["styleModes"] = Json::array( { c.skyMode, c.terrainMode, c.objectStyle, c.waterMode } );
    cinematic["styleGrade"] = Vec3( c.styleSaturation, c.styleContrast, c.styleVignette );
    cinematic["terrainTint"] = Vec3( c.terrainTintR, c.terrainTintG, c.terrainTintB );
    cinematic["terrainAccent"] = Vec3( c.terrainAccentR, c.terrainAccentG, c.terrainAccentB );
    cinematic["terrainGrid"] = Vec2( c.terrainGridScale, c.terrainGridStrength );
    cinematic["waterTint"] = Vec3( c.waterTintR, c.waterTintG, c.waterTintB );
    cinematic["waterProfile"] = Vec3( c.waterAlpha, c.waterReflectionStrength, c.waterGlintStrength );
    cinematic["basinMask"] = Json::array( { c.basinCenterX, c.basinCenterZ, c.basinRadiusX, c.basinRadiusZ, c.basinFeather } );
    cinematic["shadows"] = c.shadow.enabled;
    cinematic["shadowMapSize"] = c.shadow.mapSize;
    cinematic["shadowPcfRadius"] = c.shadow.pcfRadius;
    cinematic["shadowStrength"] = c.shadow.strength;
    cinematic["shadowSoftness"] = c.shadow.softness;
    cinematic["shadowDepthBias"] = c.shadow.depthBias;
    cinematic["shadowSlopeBias"] = c.shadow.slopeBias;
    cinematic["shadowMaxDistance"] = c.shadow.maxDistance;
    root["cinematic"] = std::move( cinematic );
    root["objectMaterials"] = Json::array();

    for ( const StandaloneStyleMaterialRule& rule : snapshot.materialRules )
    {
        const Rendering::RenderMaterial& material = rule.material;
        Json materialJson;
        materialJson["target"] = rule.target.data();
        materialJson["mode"] = Rendering::RenderMaterialKindName( material.kind );
        materialJson["name"] = material.name;
        materialJson["color"] = Vec3( material.baseColor[0], material.baseColor[1], material.baseColor[2] );
        materialJson["alpha"] = material.baseColor[3];
        materialJson["roughness"] = material.roughness;
        materialJson["metallic"] = material.metallic;
        materialJson["specular"] = material.specular;
        materialJson["transmission"] = material.transmission;
        materialJson["stylization"] = material.stylization;
        materialJson["emissive"] = Vec3( material.emissiveColor[0], material.emissiveColor[1], material.emissiveColor[2] );
        materialJson["strength"] = material.emissiveStrength;
        materialJson["flags"] = material.flags;
        root["objectMaterials"].push_back( std::move( materialJson ) );
    }

    return root;
}

void Flatten( const Json& value, const std::string& key, std::ostringstream& output )
{

    if ( value.is_object() )
    {

        for ( const auto& item : value.items() )
        {
            Flatten( item.value(), key.empty() ? item.key() : key + "." + item.key(), output );
        }
    }
    else if ( value.is_array() )
    {

        for ( size_t index = 0; index < value.size(); ++index )
        {
            Flatten( value[index], key + "[" + std::to_string( index ) + "]", output );
        }
    }
    else
    {
        output << key << '=' << value.dump() << '\n';
    }
}
} // namespace

Core::SbResult StandaloneStyleWriter::Serialize( Core::SbDiagnosticStore& diagnostics,
                                                 const StandaloneStyleSnapshot& snapshot, std::string& output )
{
    Core::SbResult validation = Validate( diagnostics, snapshot );

    if ( !validation.Ok() )
    {
        return validation;
    }

    output = BuildDocument( snapshot ).dump( 2 );
    output.push_back( '\n' );
    return Core::SbResult::Success();
}

Core::SbResult StandaloneStyleWriter::BuildFlattenedListing( Core::SbDiagnosticStore& diagnostics,
                                                             const StandaloneStyleSnapshot& snapshot, std::string& output )
{
    Core::SbResult validation = Validate( diagnostics, snapshot );

    if ( !validation.Ok() )
    {
        return validation;
    }

    std::ostringstream flattened;
    Flatten( BuildDocument( snapshot ), {}, flattened );
    output = flattened.str();
    return Core::SbResult::Success();
}

Core::SbResult StandaloneStyleWriter::SaveAtomic( Core::SbDiagnosticStore& diagnostics,
                                                  const StandaloneStyleSnapshot& snapshot, const char* path )
{
    std::string serialized;
    Core::SbResult result = Serialize( diagnostics, snapshot, serialized );

    if ( !result.Ok() )
    {
        return result;
    }

    return Core::WriteTextFileAtomic( diagnostics, OWNER, path, serialized );
}
} // namespace SkullbonezCore::Scene
