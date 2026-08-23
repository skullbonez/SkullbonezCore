/*
File: SkullbonezSource/UI/UIRenderAuthoringCatalog.h
Purpose:
  Defines the canonical metadata for ordinary and cinematic render controls.

Summary:
  GameUI and Dear ImGui operator surfaces share these enum-indexed labels,
  groups, ranges, steps, and formats. Runtime configuration values and owner
  commands remain outside this catalog.

Glossary:
  Section: Canonical right-rail grouping used to merge former Render, Sky, and
    Cine concepts without changing their established command enums.

Invariants:
  - Array order matches the corresponding command enum exactly.
  - Metadata has static storage and cannot allocate or retain runtime owners.

Related:
  - SkullbonezSource/UI/UICommands.h
  - SkullbonezSource/UI/UIFrameComposition.h
  - SkullbonezSource/UI/UITabCinematic.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Interaction/OperatorUiCommands.h"

namespace SkullbonezCore::UI
{
enum class UIRenderAuthoringSection
{
    Lighting,
    Environment,
    Shadows,
    Post,
    Water,
    TerrainMaterials,
    PredictionPaths
};

inline constexpr const char* UIRenderAuthoringSectionName( UIRenderAuthoringSection section )
{
    switch ( section )
    {
    case UIRenderAuthoringSection::Lighting:
        return "Lighting";
    case UIRenderAuthoringSection::Environment:
        return "Environment";
    case UIRenderAuthoringSection::Shadows:
        return "Shadows";
    case UIRenderAuthoringSection::Post:
        return "Post";
    case UIRenderAuthoringSection::Water:
        return "Water";
    case UIRenderAuthoringSection::TerrainMaterials:
        return "Terrain / Materials";
    case UIRenderAuthoringSection::PredictionPaths:
        return "Prediction paths";
    default:
        return "Rendering";
    }
}

struct RenderSliderSpec
{
    UIRenderAuthoringSection section;
    const char* label;
    UIRenderParam param;
    float minValue;
    float maxValue;
    float step;
    const char* valueFormat;
};

// Invariant: these tables are enum-indexed compatibility contracts shared by
// both operator surfaces; adding or reordering an enum requires updating the
// matching table and its compile-time count assertion in the same change.

inline constexpr RenderSliderSpec kRenderSliderSpecs[] = {
    { UIRenderAuthoringSection::Lighting, "Sun intensity", UIRenderParam::SunIntensity, 0.00f, 4.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Lighting, "Sun R", UIRenderParam::SunRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Lighting, "Sun G", UIRenderParam::SunGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Lighting, "Sun B", UIRenderParam::SunBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Lighting, "Ambient", UIRenderParam::AmbientStrength, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Sky R", UIRenderParam::SkyRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Sky G", UIRenderParam::SkyGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Sky B", UIRenderParam::SkyBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Ground R", UIRenderParam::GroundRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Ground G", UIRenderParam::GroundGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Ground B", UIRenderParam::GroundBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Shadows, "Strength", UIRenderParam::ShadowStrength, 0.00f, 1.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Shadows, "Softness", UIRenderParam::ShadowSoftness, 0.25f, 4.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Shadows, "Depth bias", UIRenderParam::ShadowDepthBias, 0.00000f, 0.00500f, 0.00001f,
      "%.5f" },
    { UIRenderAuthoringSection::Shadows, "Slope bias", UIRenderParam::ShadowSlopeBias, 0.00000f, 0.00500f, 0.00001f,
      "%.5f" },
    { UIRenderAuthoringSection::Water, "Water R", UIRenderParam::WaterRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Water G", UIRenderParam::WaterGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Water B", UIRenderParam::WaterBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Alpha", UIRenderParam::WaterAlpha, 0.00f, 1.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Reflection", UIRenderParam::WaterReflection, 0.00f, 1.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Fresnel F0", UIRenderParam::WaterFresnel, 0.000f, 0.120f, 0.001f, "%.3f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Ball roughness", UIRenderParam::BallRoughness, 0.25f, 2.00f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Ball specular", UIRenderParam::BallSpecular, 0.00f, 2.00f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Box roughness", UIRenderParam::BoxRoughness, 0.25f, 2.00f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Box specular", UIRenderParam::BoxSpecular, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::PredictionPaths, "Future width", UIRenderParam::TrajectoryFutureWidth, 1.00f, 6.00f, 0.05f,
      "%.2f px" },
    { UIRenderAuthoringSection::PredictionPaths, "Future opacity", UIRenderParam::TrajectoryFutureAlpha, 0.05f, 1.00f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::PredictionPaths, "Future edge feather", UIRenderParam::TrajectoryFutureEdgeFeather, 0.25f,
      1.25f, 0.05f, "%.2f px" },
    { UIRenderAuthoringSection::PredictionPaths, "Causal width", UIRenderParam::TrajectoryCausalWidth, 1.00f, 6.00f, 0.05f,
      "%.2f px" },
    { UIRenderAuthoringSection::PredictionPaths, "Causal opacity", UIRenderParam::TrajectoryCausalAlpha, 0.05f, 1.00f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::PredictionPaths, "Causal edge feather", UIRenderParam::TrajectoryCausalEdgeFeather, 0.25f,
      1.25f, 0.05f, "%.2f px" },
    { UIRenderAuthoringSection::PredictionPaths, "Baseline width", UIRenderParam::TrajectoryBaselineWidth, 1.00f, 6.00f,
      0.05f, "%.2f px" },
    { UIRenderAuthoringSection::PredictionPaths, "Baseline opacity", UIRenderParam::TrajectoryBaselineAlpha, 0.05f, 1.00f,
      0.01f, "%.2f" },
    { UIRenderAuthoringSection::PredictionPaths, "Baseline edge feather", UIRenderParam::TrajectoryBaselineEdgeFeather,
      0.25f, 1.25f, 0.05f, "%.2f px" },
    { UIRenderAuthoringSection::PredictionPaths, "Marker width", UIRenderParam::TrajectoryMarkerWidth, 1.00f, 6.00f, 0.05f,
      "%.2f px" },
    { UIRenderAuthoringSection::PredictionPaths, "Marker opacity", UIRenderParam::TrajectoryMarkerAlpha, 0.05f, 1.00f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::PredictionPaths, "Marker edge feather", UIRenderParam::TrajectoryMarkerEdgeFeather, 0.25f,
      1.25f, 0.05f, "%.2f px" },
    { UIRenderAuthoringSection::PredictionPaths, "Selected emphasis", UIRenderParam::TrajectorySelectedEmphasis, 0.00f,
      1.00f, 0.01f, "%.2f" },
};
static_assert( sizeof( kRenderSliderSpecs ) / sizeof( kRenderSliderSpecs[0] ) == static_cast<int>( UIRenderParam::Count ) );

inline constexpr bool RenderSliderStartsSection( int index )
{
    return index == 0 || kRenderSliderSpecs[index - 1].section != kRenderSliderSpecs[index].section;
}

struct CinematicSliderSpec
{
    UIRenderAuthoringSection section;
    const char* label;
    UICinematicParam param;
    float minValue;
    float maxValue;
    float step;
    const char* valueFormat;
};

inline constexpr CinematicSliderSpec kCinematicSliderSpecs[] = {
    { UIRenderAuthoringSection::Post, "Exposure", UICinematicParam::Exposure, 0.05f, 3.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Gamma", UICinematicParam::Gamma, 1.00f, 3.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Sky mode", UICinematicParam::SkyMode, 0.00f, 32.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Terrain mode", UICinematicParam::TerrainMode, 0.00f, 32.00f, 1.00f,
      "%.0f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Object style", UICinematicParam::ObjectStyle, 0.00f, 32.00f, 1.00f,
      "%.0f" },
    { UIRenderAuthoringSection::Water, "Water mode", UICinematicParam::WaterMode, 0.00f, 4.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::Post, "Saturation", UICinematicParam::StyleSaturation, 0.00f, 2.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Contrast", UICinematicParam::StyleContrast, 0.00f, 2.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Vignette", UICinematicParam::StyleVignette, 0.00f, 1.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Lighting, "Sun azimuth", UICinematicParam::SunAzimuth, 0.00f, 1.00f, 0.005f, "%.3f" },
    { UIRenderAuthoringSection::Lighting, "Sun elevation", UICinematicParam::SunElevation, 0.00f, 1.00f, 0.005f, "%.3f" },
    { UIRenderAuthoringSection::Lighting, "Sun brightness", UICinematicParam::SunBrightness, 0.00f, 40.00f, 0.10f, "%.1f" },
    { UIRenderAuthoringSection::Lighting, "Sun R", UICinematicParam::SunRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Lighting, "Sun G", UICinematicParam::SunGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Lighting, "Sun B", UICinematicParam::SunBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Sky glow", UICinematicParam::SkyGlow, 0.00f, 8.00f, 0.05f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Horizon R", UICinematicParam::HorizonRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Horizon G", UICinematicParam::HorizonGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Horizon B", UICinematicParam::HorizonBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Zenith R", UICinematicParam::ZenithRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Zenith G", UICinematicParam::ZenithGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Zenith B", UICinematicParam::ZenithBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Cloud coverage", UICinematicParam::CloudCoverage, 0.00f, 1.00f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::Environment, "Cloud softness", UICinematicParam::CloudSoftness, 0.01f, 0.65f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::Environment, "Cloud scale", UICinematicParam::CloudScale, 0.50f, 12.00f, 0.05f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Cloud intensity", UICinematicParam::CloudIntensity, 0.00f, 1.50f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::Post, "Shaft strength", UICinematicParam::ShaftStrength, 0.00f, 3.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Shaft falloff", UICinematicParam::ShaftFalloff, 0.25f, 5.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Volume strength", UICinematicParam::VolumetricStrength, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Volume density", UICinematicParam::VolumetricDensity, 0.00f, 2.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Volume decay", UICinematicParam::VolumetricDecay, 0.800f, 0.995f, 0.001f, "%.3f" },
    { UIRenderAuthoringSection::Post, "Bloom threshold", UICinematicParam::BloomThreshold, 0.00f, 4.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Bloom knee", UICinematicParam::BloomKnee, 0.01f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Bloom strength", UICinematicParam::BloomStrength, 0.00f, 2.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Post, "Bloom radius", UICinematicParam::BloomRadius, 0.25f, 8.00f, 0.05f, "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Terrain relief", UICinematicParam::TerrainRelief, 0.00f, 1.50f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Ground R", UICinematicParam::TerrainTintRed, 0.00f, 1.50f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Ground G", UICinematicParam::TerrainTintGreen, 0.00f, 1.50f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Ground B", UICinematicParam::TerrainTintBlue, 0.00f, 1.50f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Accent R", UICinematicParam::TerrainAccentRed, 0.00f, 1.50f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Accent G", UICinematicParam::TerrainAccentGreen, 0.00f, 1.50f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Accent B", UICinematicParam::TerrainAccentBlue, 0.00f, 1.50f, 0.01f,
      "%.2f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Grid scale", UICinematicParam::TerrainGridScale, 0.10f, 120.00f, 0.10f,
      "%.1f" },
    { UIRenderAuthoringSection::TerrainMaterials, "Grid strength", UICinematicParam::TerrainGridStrength, 0.00f, 4.00f,
      0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Water R", UICinematicParam::WaterTintRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Water G", UICinematicParam::WaterTintGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Water B", UICinematicParam::WaterTintBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Water alpha", UICinematicParam::WaterAlpha, 0.00f, 1.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Water reflection", UICinematicParam::WaterReflection, 0.00f, 1.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Water glint", UICinematicParam::WaterGlint, 0.00f, 4.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Basin center X", UICinematicParam::BasinCenterX, 0.00f, 1200.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::Water, "Basin center Z", UICinematicParam::BasinCenterZ, 0.00f, 1200.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::Water, "Basin radius X", UICinematicParam::BasinRadiusX, 1.00f, 500.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::Water, "Basin radius Z", UICinematicParam::BasinRadiusZ, 1.00f, 500.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::Water, "Basin feather", UICinematicParam::BasinFeather, 0.00f, 1.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Water, "Basin depth", UICinematicParam::BasinDepth, 0.00f, 80.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::Water, "Basin rim lift", UICinematicParam::BasinRimLift, 0.00f, 60.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::Environment, "Fog density", UICinematicParam::FogDensity, 0.00000f, 0.00600f, 0.00005f,
      "%.5f" },
    { UIRenderAuthoringSection::Environment, "Fog opacity", UICinematicParam::FogOpacity, 0.00f, 1.00f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Fog start", UICinematicParam::FogStart, 0.00f, 500.00f, 1.00f, "%.0f" },
    { UIRenderAuthoringSection::Environment, "Fog end", UICinematicParam::FogEnd, 100.00f, 4000.00f, 10.00f, "%.0f" },
    { UIRenderAuthoringSection::Environment, "Fog R", UICinematicParam::FogRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Fog G", UICinematicParam::FogGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { UIRenderAuthoringSection::Environment, "Fog B", UICinematicParam::FogBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
};
static_assert( sizeof( kCinematicSliderSpecs ) / sizeof( kCinematicSliderSpecs[0] ) ==
               static_cast<int>( UICinematicParam::Count ) );

inline constexpr bool CinematicSliderStartsSection( int index )
{
    return index == 0 || kCinematicSliderSpecs[index - 1].section != kCinematicSliderSpecs[index].section;
}

struct CinematicFeatureSpec
{
    UIRenderAuthoringSection section;
    const char* label;
    UICinematicFeature feature;
};

inline constexpr CinematicFeatureSpec kCinematicFeatureSpecs[] = {
    { UIRenderAuthoringSection::Environment, "Sky", UICinematicFeature::Sky },
    { UIRenderAuthoringSection::Environment, "Clouds", UICinematicFeature::Clouds },
    { UIRenderAuthoringSection::Post, "God rays", UICinematicFeature::GodRays },
    { UIRenderAuthoringSection::Post, "Volumetric light", UICinematicFeature::VolumetricLight },
    { UIRenderAuthoringSection::Post, "Bloom", UICinematicFeature::Bloom },
    { UIRenderAuthoringSection::Environment, "Fog", UICinematicFeature::Fog },
    { UIRenderAuthoringSection::TerrainMaterials, "Terrain relief", UICinematicFeature::TerrainRelief },
    { UIRenderAuthoringSection::Shadows, "Shadows", UICinematicFeature::Shadows },
};
static_assert( sizeof( kCinematicFeatureSpecs ) / sizeof( kCinematicFeatureSpecs[0] ) ==
               static_cast<int>( UICinematicFeature::Count ) );
} // namespace SkullbonezCore::UI
