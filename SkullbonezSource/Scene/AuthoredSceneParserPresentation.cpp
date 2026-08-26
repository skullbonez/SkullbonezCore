/*
File: AuthoredSceneParserPresentation.cpp
Purpose:
  Parses debug water, terrain, editor, UI, cinematic, and camera presentation fields.

Summary:
  This translation unit handles one schema domain while mutating the single
  AuthoredSceneParser result. Shared validation and failure policy live in
  AuthoredSceneParserSchema.h; top-level document order stays in AuthoredSceneParser.cpp.

Invariants:
  - Authored JSON field names remain command-line and scene-file compatibility.
  - Parser failure prevents caller-visible publication and is returned without
    an engine throw.
  - Stable scene identities and source ordering are preserved exactly.

Related:
  - AuthoredSceneParserSchema.h declares shared parser state and helpers.
  - Agentic/Reference/engine-glossary.md
*/
#include "AuthoredSceneParserSchema.h"

namespace SkullbonezCore
{
namespace Runtime
{
using AuthoredSceneParserDetail::CopyCheckedStringField;
using AuthoredSceneParserDetail::Fail;
using AuthoredSceneParserDetail::FindMember;
using AuthoredSceneParserDetail::Lowercase;
using AuthoredSceneParserDetail::ParsePhysicsDebugMode;
using AuthoredSceneParserDetail::ParserFailed;
using AuthoredSceneParserDetail::ParseUITab;
using AuthoredSceneParserDetail::ParseWaterReflectionMode;
using AuthoredSceneParserDetail::ReadBool;
using AuthoredSceneParserDetail::ReadFloat;
using AuthoredSceneParserDetail::ReadInt;
using AuthoredSceneParserDetail::ReadRequiredStringField;
using AuthoredSceneParserDetail::ReadString;
using AuthoredSceneParserDetail::ReadUInt;
using AuthoredSceneParserDetail::ReadVec3;
using AuthoredSceneParserDetail::RequireFixedArray;
using AuthoredSceneParserDetail::RequireMember;
using AuthoredSceneParserDetail::RequireObject;

void AuthoredSceneParser::ApplyPhysicsDebug( const Json& debug, const std::string& path )
{
    RequireObject( debug, path, "debug.physics" );

    if ( const Json* mode = FindMember( debug, "mode" ) )
    {
        m_scene.m_sceneOptions.physicsDebugFlags = ParsePhysicsDebugMode( *mode, path );
    }

    const auto applyFlag = [&]( const char* key, uint32_t flag )
    {
        if ( const Json* value = FindMember( debug, key ) )
        {
            if ( ReadBool( *value, path, key ) )
            {
                m_scene.m_sceneOptions.physicsDebugFlags |= flag;
            }
            else
            {
                m_scene.m_sceneOptions.physicsDebugFlags &= ~flag;
            }
        }
    };

    applyFlag( "axes", Physics::PHYSICS_DEBUG_AXES );
    applyFlag( "contacts", Physics::PHYSICS_DEBUG_CONTACTS );
    applyFlag( "sleep", Physics::PHYSICS_DEBUG_SLEEP );
    applyFlag( "pipeline", Physics::PHYSICS_DEBUG_PIPELINE );
    applyFlag( "terrainContact", Physics::PHYSICS_DEBUG_TERRAIN_CONTACT );

    if ( const Json* transparent = FindMember( debug, "transparent" ) )
    {
        m_scene.m_sceneOptions.physicsDebugTransparent = ReadBool( *transparent, path, "debug.physics.transparent" );
    }

    if ( const Json* alpha = FindMember( debug, "alpha" ) )
    {
        const float value = ReadFloat( *alpha, path, "debug.physics.alpha" );

        if ( value < 0.05f || value > 1.0f )
        {
            Fail( path, "debug.physics.alpha must be 0.05..1.0" );
        }

        m_scene.m_sceneOptions.physicsDebugAlpha = value;
    }

    if ( const Json* linger = FindMember( debug, "contactLinger" ) )
    {
        const float value = ReadFloat( *linger, path, "debug.physics.contactLinger" );

        if ( value < 0.0f || value > 5.0f )
        {
            Fail( path, "debug.physics.contactLinger must be 0.0..5.0" );
        }

        m_scene.m_sceneOptions.physicsDebugContactLinger = value;
    }
}

void AuthoredSceneParser::ApplyDebug( const Json& debug, const std::string& path )
{
    // Concept: water debug fields are explicit presentation overrides. They do
    // not mutate the authored water simulation or its physics parameters.
    RequireObject( debug, path, "debug" );

    if ( const Json* collisionVisualizer = FindMember( debug, "collisionVisualizer" ) )
    {
        m_scene.m_sceneOptions.collisionVisualizer = ReadBool( *collisionVisualizer, path, "debug.collisionVisualizer" );
    }

    if ( const Json* broadphaseOverlay = FindMember( debug, "broadphaseOverlay" ) )
    {
        m_scene.m_sceneOptions.broadphaseOverlay = ReadBool( *broadphaseOverlay, path, "debug.broadphaseOverlay" );
    }

    if ( const Json* waterFreeze = FindMember( debug, "waterFreeze" ) )
    {
        m_scene.m_sceneOptions.waterFreezeDebug = ReadBool( *waterFreeze, path, "debug.waterFreeze" );
    }

    if ( const Json* waterFlat = FindMember( debug, "waterFlat" ) )
    {
        m_scene.m_sceneOptions.waterFlatDebug = ReadBool( *waterFlat, path, "debug.waterFlat" );
    }

    if ( const Json* waterReflection = FindMember( debug, "waterReflection" ) )
    {
        m_scene.m_sceneOptions.waterReflectionMode = ParseWaterReflectionMode( *waterReflection, path );
    }

    if ( const Json* waterHidden = FindMember( debug, "waterHidden" ) )
    {
        m_scene.m_sceneOptions.waterHidden = ReadBool( *waterHidden, path, "debug.waterHidden" );
    }

    if ( const Json* terrainHidden = FindMember( debug, "terrainHidden" ) )
    {
        m_scene.m_sceneOptions.terrainHidden = ReadBool( *terrainHidden, path, "debug.terrainHidden" );
    }

    if ( const Json* physics = FindMember( debug, "physics" ) )
    {
        ApplyPhysicsDebug( *physics, path );
    }
}

void AuthoredSceneParser::ApplyTerrain( const Json& terrain, const std::string& path )
{
    RequireObject( terrain, path, "terrain" );

    if ( const Json* flatSlope = FindMember( terrain, "flatSlope" ) )
    {
        RequireObject( *flatSlope, path, "terrain.flatSlope" );
        m_scene.m_terrainOverride.hasFlatSlope = true;
        m_scene.m_terrainOverride.flatBaseY = ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "baseY" ),
                                                         path, "terrain.flatSlope.baseY" );

        m_scene.m_terrainOverride.flatSlopeX = ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "slopeX" ),
                                                          path, "terrain.flatSlope.slopeX" );

        m_scene.m_terrainOverride.flatSlopeZ = ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "slopeZ" ),
                                                          path, "terrain.flatSlope.slopeZ" );
    }
}

void AuthoredSceneParser::ApplyEditor( const Json& editor, const std::string& path )
{
    RequireObject( editor, path, "editor" );

    if ( const Json* editable = FindMember( editor, "editableScene" ) )
    {
        m_scene.m_sceneOptions.editableScene = ReadBool( *editable, path, "editor.editableScene" );
    }
}

void AuthoredSceneParser::ApplyUI( const Json& ui, const std::string& path )
{
    RequireObject( ui, path, "ui" );
    SceneUIOptions& out = m_scene.m_UIOptions;
    out.hasSettings = true;

    if ( const Json* visible = FindMember( ui, "visible" ) )
    {
        out.hasVisible = true;
        out.isVisible = ReadBool( *visible, path, "ui.visible" );
    }

    if ( const Json* minimized = FindMember( ui, "minimized" ) )
    {
        out.hasMinimized = true;
        out.isMinimized = ReadBool( *minimized, path, "ui.minimized" );
    }

    if ( const Json* tab = FindMember( ui, "tab" ) )
    {
        out.hasActiveTab = true;
        out.activeTab = ParseUITab( *tab, path );
    }

    if ( const Json* rect = FindMember( ui, "rect" ) )
    {
        if ( !RequireFixedArray( *rect, path, "ui.rect", 4u, "integers" ) )
        {
            return;
        }

        const int windowX = ReadInt( ( *rect )[0], path, "ui.rect[0]" );
        const int windowY = ReadInt( ( *rect )[1], path, "ui.rect[1]" );
        const int windowW = ReadInt( ( *rect )[2], path, "ui.rect[2]" );
        const int windowH = ReadInt( ( *rect )[3], path, "ui.rect[3]" );

        if ( ParserFailed() )
        {
            return;
        }

        out.hasWindowRect = true;
        out.windowX = windowX;
        out.windowY = windowY;
        out.windowW = windowW;
        out.windowH = windowH;
    }

    if ( const Json* blur = FindMember( ui, "blur" ) )
    {
        out.hasBlur = true;
        out.blurEnabled = ReadBool( *blur, path, "ui.blur" );
    }

    if ( const Json* rendererCombo = FindMember( ui, "rendererCombo" ) )
    {
        out.hasRendererComboOpen = true;
        out.rendererComboOpen = ReadBool( *rendererCombo, path, "ui.rendererCombo" );
    }

    if ( const Json* waterCombo = FindMember( ui, "waterCombo" ) )
    {
        out.hasWaterComboOpen = true;
        out.waterComboOpen = ReadBool( *waterCombo, path, "ui.waterCombo" );
    }

    if ( const Json* sceneCombo = FindMember( ui, "sceneCombo" ) )
    {
        out.hasSceneComboOpen = true;
        out.sceneComboOpen = ReadBool( *sceneCombo, path, "ui.sceneCombo" );
    }

    if ( const Json* sceneFilter = FindMember( ui, "sceneFilter" ) )
    {
        const std::string filter = ReadString( *sceneFilter, path, "ui.sceneFilter" );

        if ( ParserFailed() || !CopyCheckedStringField( out.sceneFilter, filter, path, "ui.sceneFilter" ) )
        {
            return;
        }

        out.hasSceneFilter = true;
    }

    if ( const Json* profilerExpand = FindMember( ui, "profilerExpand" ) )
    {
        out.hasProfilerExpandAll = true;
        out.profilerExpandAll = ReadBool( *profilerExpand, path, "ui.profilerExpand" );
    }

    if ( const Json* timeline = FindMember( ui, "timeline" ) )
    {
        out.hasProfilerTimeline = true;
        out.profilerTimeline = ReadBool( *timeline, path, "ui.timeline" );
    }

    if ( const Json* histogram = FindMember( ui, "histogram" ) )
    {
        out.hasPerformanceHistogram = true;
        out.performanceHistogram = ReadBool( *histogram, path, "ui.histogram" );
    }

    if ( const Json* hitboxes = FindMember( ui, "hitboxes" ) )
    {
        out.hasHitboxOverlay = true;
        out.hitboxOverlay = ReadBool( *hitboxes, path, "ui.hitboxes" );
    }

    if ( const Json* scroll = FindMember( ui, "scroll" ) )
    {
        out.hasScrollY = true;

        if ( scroll->is_string() && Lowercase( scroll->get<std::string>() ) == "bottom" )
        {
            out.scrollY = 1000000.0f;
        }
        else
        {
            out.scrollY = ReadFloat( *scroll, path, "ui.scroll" );
        }
    }

    if ( const Json* mouse = FindMember( ui, "mouse" ) )
    {
        if ( !RequireFixedArray( *mouse, path, "ui.mouse", 2u, "integers" ) )
        {
            return;
        }

        const int mouseX = ReadInt( ( *mouse )[0], path, "ui.mouse[0]" );
        const int mouseY = ReadInt( ( *mouse )[1], path, "ui.mouse[1]" );

        if ( ParserFailed() )
        {
            return;
        }

        out.hasMouseOverride = true;
        out.mouseX = mouseX;
        out.mouseY = mouseY;
    }

    if ( const Json* stress = FindMember( ui, "stress" ) )
    {
        out.hasStress = true;
        out.stressEnabled = ReadBool( *stress, path, "ui.stress" );
    }

    if ( const Json* stressSeed = FindMember( ui, "stressSeed" ) )
    {
        out.hasStressSeed = true;
        out.stressSeed = ReadUInt( *stressSeed, path, "ui.stressSeed" );
    }

    if ( const Json* stressActions = FindMember( ui, "stressActions" ) )
    {
        const int actions = ReadInt( *stressActions, path, "ui.stressActions" );

        if ( actions < 0 )
        {
            Fail( path, "ui.stressActions must be >= 0" );
        }

        out.hasStressActions = true;
        out.stressActionsPerFrame = actions;
    }

    if ( const Json* testPattern = FindMember( ui, "testPattern" ) )
    {
        out.hasTestPattern = true;
        out.testPatternEnabled = ReadBool( *testPattern, path, "ui.testPattern" );
    }
}

void AuthoredSceneParser::ApplyCinematicBool( const Json& cinematic, const std::string& path )
{
    struct BoolField
    {
        const char* key;
        bool SkullbonezCore::Core::CinematicRenderConfig::* field;
        uint64_t bit;
    };
    static constexpr BoolField kFields[] = {
        { "rendering", &SkullbonezCore::Core::CinematicRenderConfig::enabled, SCENE_CINE_RENDERING },
        { "skyAtmosphere", &SkullbonezCore::Core::CinematicRenderConfig::skyAtmosphereEnabled, SCENE_CINE_SKY_ATMOSPHERE },
        { "clouds", &SkullbonezCore::Core::CinematicRenderConfig::cloudsEnabled, SCENE_CINE_CLOUDS },
        { "godRays", &SkullbonezCore::Core::CinematicRenderConfig::godRaysEnabled, SCENE_CINE_GOD_RAYS },
        { "volumetricLighting", &SkullbonezCore::Core::CinematicRenderConfig::volumetricLightingEnabled,
          SCENE_CINE_VOLUMETRIC_LIGHTING },
        { "bloom", &SkullbonezCore::Core::CinematicRenderConfig::bloomEnabled, SCENE_CINE_BLOOM },
        { "fog", &SkullbonezCore::Core::CinematicRenderConfig::fogEnabled, SCENE_CINE_FOG },
        { "terrainReliefEnabled", &SkullbonezCore::Core::CinematicRenderConfig::terrainReliefEnabled,
          SCENE_CINE_TERRAIN_RELIEF_ENABLED },
    };

    for ( const BoolField& field : kFields )
    {
        if ( const Json* value = FindMember( cinematic, field.key ) )
        {
            const bool parsed = ReadBool( *value, path, field.key );
            m_scene.m_sceneOptions.cinematicRender.*( field.field ) = parsed;
            m_scene.m_sceneOptions.cinematicOverrideMask |= field.bit;

            if ( field.bit == SCENE_CINE_RENDERING )
            {
                m_scene.m_sceneOptions.hasCinematicRenderingOverride = true;
                m_scene.m_sceneOptions.cinematicRendering = parsed;
            }
        }
    }

    if ( const Json* value = FindMember( cinematic, "shadows" ) )
    {
        m_scene.m_sceneOptions.cinematicRender.shadow.enabled = ReadBool( *value, path, "shadows" );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    }
}

void AuthoredSceneParser::ApplyCinematicInt( const Json& cinematic, const std::string& path )
{
    struct IntField
    {
        const char* key;
        int SkullbonezCore::Core::ShadowQualityConfig::* field;
        uint64_t bit;
        int minValue;
        int maxValue;
    };
    static constexpr IntField kFields[] = {
        { "shadowMapSize", &SkullbonezCore::Core::ShadowQualityConfig::mapSize, SCENE_CINE_SHADOW_MAP_SIZE, 256, 8192 },
        { "shadowPcfRadius", &SkullbonezCore::Core::ShadowQualityConfig::pcfRadius, SCENE_CINE_SHADOW_PCF_RADIUS, 0, 3 },
    };

    for ( const IntField& field : kFields )
    {
        if ( const Json* value = FindMember( cinematic, field.key ) )
        {
            const int parsed = ReadInt( *value, path, field.key );

            if ( parsed < field.minValue || parsed > field.maxValue )
            {
                std::ostringstream message;
                message << "cinematic." << field.key << " must be " << field.minValue << ".." << field.maxValue;
                Fail( path, message.str() );
            }

            m_scene.m_sceneOptions.cinematicRender.shadow.*( field.field ) = parsed;
            m_scene.m_sceneOptions.cinematicOverrideMask |= field.bit;
        }
    }
}

void AuthoredSceneParser::ApplyCinematicFloat( const Json& cinematic, const std::string& path )
{
    // Invariant: each accepted scalar sets its matching override bit. Runtime
    // style merging relies on the value and mask changing atomically.
    struct FloatField
    {
        const char* key;
        float SkullbonezCore::Core::CinematicRenderConfig::* field;
        uint64_t bit;
        float minValue;
        float maxValue;
    };
    static constexpr FloatField kFields[] = {
        { "exposure", &SkullbonezCore::Core::CinematicRenderConfig::exposure, SCENE_CINE_EXPOSURE, 0.0f, 16.0f },
        { "gamma", &SkullbonezCore::Core::CinematicRenderConfig::gamma, SCENE_CINE_GAMMA, 0.1f, 8.0f },

        // Compatibility: scene/style JSON retains its original key spellings;

        // only the in-memory owner vocabulary changed.
        { "sunScreenX", &SkullbonezCore::Core::CinematicRenderConfig::sunAzimuth, SCENE_CINE_SUN_AZIMUTH, 0.0f, 1.0f },
        { "sunScreenY", &SkullbonezCore::Core::CinematicRenderConfig::sunElevation, SCENE_CINE_SUN_ELEVATION, 0.0f, 1.0f },
        { "sunColorR", &SkullbonezCore::Core::CinematicRenderConfig::sunColorR, SCENE_CINE_SUN_COLOR_R, 0.0f, 4.0f },
        { "sunColorG", &SkullbonezCore::Core::CinematicRenderConfig::sunColorG, SCENE_CINE_SUN_COLOR_G, 0.0f, 4.0f },
        { "sunColorB", &SkullbonezCore::Core::CinematicRenderConfig::sunColorB, SCENE_CINE_SUN_COLOR_B, 0.0f, 4.0f },
        { "sunIntensity", &SkullbonezCore::Core::CinematicRenderConfig::sunIntensity, SCENE_CINE_SUN_INTENSITY, 0.0f,
          80.0f },
        { "skyHorizonR", &SkullbonezCore::Core::CinematicRenderConfig::skyHorizonR, SCENE_CINE_SKY_HORIZON_R, 0.0f, 4.0f },
        { "skyHorizonG", &SkullbonezCore::Core::CinematicRenderConfig::skyHorizonG, SCENE_CINE_SKY_HORIZON_G, 0.0f, 4.0f },
        { "skyHorizonB", &SkullbonezCore::Core::CinematicRenderConfig::skyHorizonB, SCENE_CINE_SKY_HORIZON_B, 0.0f, 4.0f },
        { "skyZenithR", &SkullbonezCore::Core::CinematicRenderConfig::skyZenithR, SCENE_CINE_SKY_ZENITH_R, 0.0f, 4.0f },
        { "skyZenithG", &SkullbonezCore::Core::CinematicRenderConfig::skyZenithG, SCENE_CINE_SKY_ZENITH_G, 0.0f, 4.0f },
        { "skyZenithB", &SkullbonezCore::Core::CinematicRenderConfig::skyZenithB, SCENE_CINE_SKY_ZENITH_B, 0.0f, 4.0f },
        { "skyGlowStrength", &SkullbonezCore::Core::CinematicRenderConfig::skyGlowStrength, SCENE_CINE_SKY_GLOW_STRENGTH,
          0.0f, 16.0f },
        { "cloudCoverage", &SkullbonezCore::Core::CinematicRenderConfig::cloudCoverage, SCENE_CINE_CLOUD_COVERAGE, 0.0f,
          1.0f },
        { "cloudSoftness", &SkullbonezCore::Core::CinematicRenderConfig::cloudSoftness, SCENE_CINE_CLOUD_SOFTNESS, 0.001f,
          1.0f },
        { "cloudScale", &SkullbonezCore::Core::CinematicRenderConfig::cloudScale, SCENE_CINE_CLOUD_SCALE, 0.1f, 64.0f },
        { "cloudIntensity", &SkullbonezCore::Core::CinematicRenderConfig::cloudIntensity, SCENE_CINE_CLOUD_INTENSITY, 0.0f,
          4.0f },
        { "sunShaftStrength", &SkullbonezCore::Core::CinematicRenderConfig::sunShaftStrength, SCENE_CINE_SUN_SHAFT_STRENGTH,
          0.0f, 8.0f },
        { "sunShaftFalloff", &SkullbonezCore::Core::CinematicRenderConfig::sunShaftFalloff, SCENE_CINE_SUN_SHAFT_FALLOFF,
          0.1f, 10.0f },
        { "volumetricStrength", &SkullbonezCore::Core::CinematicRenderConfig::volumetricStrength,
          SCENE_CINE_VOLUMETRIC_STRENGTH, 0.0f, 8.0f },
        { "volumetricDensity", &SkullbonezCore::Core::CinematicRenderConfig::volumetricDensity,
          SCENE_CINE_VOLUMETRIC_DENSITY, 0.0f, 8.0f },
        { "volumetricDecay", &SkullbonezCore::Core::CinematicRenderConfig::volumetricDecay, SCENE_CINE_VOLUMETRIC_DECAY,
          0.0f, 1.0f },
        { "bloomThreshold", &SkullbonezCore::Core::CinematicRenderConfig::bloomThreshold, SCENE_CINE_BLOOM_THRESHOLD, 0.0f,
          16.0f },
        { "bloomKnee", &SkullbonezCore::Core::CinematicRenderConfig::bloomKnee, SCENE_CINE_BLOOM_KNEE, 0.001f, 8.0f },
        { "bloomStrength", &SkullbonezCore::Core::CinematicRenderConfig::bloomStrength, SCENE_CINE_BLOOM_STRENGTH, 0.0f,
          8.0f },
        { "bloomRadius", &SkullbonezCore::Core::CinematicRenderConfig::bloomRadius, SCENE_CINE_BLOOM_RADIUS, 0.1f, 32.0f },
        { "terrainRelief", &SkullbonezCore::Core::CinematicRenderConfig::terrainRelief, SCENE_CINE_TERRAIN_RELIEF, 0.0f,
          4.0f },
        { "basinDepth", &SkullbonezCore::Core::CinematicRenderConfig::basinDepth, SCENE_CINE_BASIN_DEPTH, 0.0f, 256.0f },
        { "basinRimLift", &SkullbonezCore::Core::CinematicRenderConfig::basinRimLift, SCENE_CINE_BASIN_RIM_LIFT, 0.0f,
          256.0f },
        { "fogColorR", &SkullbonezCore::Core::CinematicRenderConfig::fogColorR, SCENE_CINE_FOG_COLOR_R, 0.0f, 4.0f },
        { "fogColorG", &SkullbonezCore::Core::CinematicRenderConfig::fogColorG, SCENE_CINE_FOG_COLOR_G, 0.0f, 4.0f },
        { "fogColorB", &SkullbonezCore::Core::CinematicRenderConfig::fogColorB, SCENE_CINE_FOG_COLOR_B, 0.0f, 4.0f },
        { "fogStart", &SkullbonezCore::Core::CinematicRenderConfig::fogStart, SCENE_CINE_FOG_START, 0.0f, 10000.0f },
        { "fogEnd", &SkullbonezCore::Core::CinematicRenderConfig::fogEnd, SCENE_CINE_FOG_END, 0.0f, 20000.0f },
        { "fogDensity", &SkullbonezCore::Core::CinematicRenderConfig::fogDensity, SCENE_CINE_FOG_DENSITY, 0.0f, 0.1f },
        { "fogMaxOpacity", &SkullbonezCore::Core::CinematicRenderConfig::fogMaxOpacity, SCENE_CINE_FOG_MAX_OPACITY, 0.0f,
          1.0f },
    };

    for ( const FloatField& field : kFields )
    {
        if ( const Json* value = FindMember( cinematic, field.key ) )
        {
            const float parsed = ReadFloat( *value, path, field.key );

            if ( parsed < field.minValue || parsed > field.maxValue )
            {
                std::ostringstream message;
                message << "cinematic." << field.key << " must be " << field.minValue << ".." << field.maxValue;
                Fail( path, message.str() );
            }

            m_scene.m_sceneOptions.cinematicRender.*( field.field ) = parsed;
            m_scene.m_sceneOptions.cinematicOverrideMask |= field.bit;

            if ( field.bit == SCENE_CINE_EXPOSURE )
            {
                m_scene.m_sceneOptions.hasCinematicExposure = true;
                m_scene.m_sceneOptions.cinematicExposure = parsed;
            }
            else if ( field.bit == SCENE_CINE_GAMMA )
            {
                m_scene.m_sceneOptions.hasCinematicGamma = true;
                m_scene.m_sceneOptions.cinematicGamma = parsed;
            }
        }
    }

    // Ownership: shadow scalars target the nested shadow value owner while
    // keeping the same JSON keys, ranges, and override bits as before.
    struct ShadowFloatField
    {
        const char* key;
        float SkullbonezCore::Core::ShadowQualityConfig::* field;
        uint64_t bit;
        float minValue;
        float maxValue;
    };
    static constexpr ShadowFloatField kShadowFields[] = {
        { "shadowStrength", &SkullbonezCore::Core::ShadowQualityConfig::strength, SCENE_CINE_SHADOW_STRENGTH, 0.0f, 1.0f },
        { "shadowSoftness", &SkullbonezCore::Core::ShadowQualityConfig::softness, SCENE_CINE_SHADOW_SOFTNESS, 0.25f, 4.0f },
        { "shadowDepthBias", &SkullbonezCore::Core::ShadowQualityConfig::depthBias, SCENE_CINE_SHADOW_DEPTH_BIAS, 0.0f,
          0.05f },
        { "shadowSlopeBias", &SkullbonezCore::Core::ShadowQualityConfig::slopeBias, SCENE_CINE_SHADOW_SLOPE_BIAS, 0.0f,
          0.05f },
        { "shadowMaxDistance", &SkullbonezCore::Core::ShadowQualityConfig::maxDistance, SCENE_CINE_SHADOW_MAX_DISTANCE,
          128.0f, 10000.0f },
    };

    for ( const ShadowFloatField& field : kShadowFields )
    {
        if ( const Json* value = FindMember( cinematic, field.key ) )
        {
            const float parsed = ReadFloat( *value, path, field.key );

            if ( parsed < field.minValue || parsed > field.maxValue )
            {
                std::ostringstream message;
                message << "cinematic." << field.key << " must be " << field.minValue << ".." << field.maxValue;
                Fail( path, message.str() );
            }

            m_scene.m_sceneOptions.cinematicRender.shadow.*( field.field ) = parsed;
            m_scene.m_sceneOptions.cinematicOverrideMask |= field.bit;
        }
    }
}

void AuthoredSceneParser::ApplyCinematicVector( const Json& cinematic, const std::string& path )
{
    SkullbonezCore::Core::CinematicRenderConfig& c = m_scene.m_sceneOptions.cinematicRender;

    if ( const Json* shadowParticipation = FindMember( cinematic, "shadowParticipation" ) )
    {
        if ( !RequireFixedArray( *shadowParticipation, path, "cinematic.shadowParticipation", 4u, "booleans" ) )
        {
            return;
        }

        // Invariant: shadow participation is one grouped override. Parsing all
        // four atoms before publishing its bit prevents omitted members from
        // silently borrowing process defaults during standalone-style reload.
        const bool terrainCasts = ReadBool( ( *shadowParticipation )[0], path, "cinematic.shadowParticipation[0]" );
        const bool objectsCast = ReadBool( ( *shadowParticipation )[1], path, "cinematic.shadowParticipation[1]" );
        const bool terrainReceives = ReadBool( ( *shadowParticipation )[2], path, "cinematic.shadowParticipation[2]" );
        const bool objectsReceive = ReadBool( ( *shadowParticipation )[3], path, "cinematic.shadowParticipation[3]" );

        if ( ParserFailed() )
        {
            return;
        }

        c.shadow.terrainCasts = terrainCasts;
        c.shadow.objectsCast = objectsCast;
        c.shadow.terrainReceives = terrainReceives;
        c.shadow.objectsReceive = objectsReceive;
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_SHADOW_PARTICIPATION;
    }

    if ( const Json* styleModes = FindMember( cinematic, "styleModes" ) )
    {
        if ( !RequireFixedArray( *styleModes, path, "cinematic.styleModes", 4u, "integers" ) )
        {
            return;
        }

        const int skyMode = ReadInt( ( *styleModes )[0], path, "cinematic.styleModes[0]" );
        const int terrainMode = ReadInt( ( *styleModes )[1], path, "cinematic.styleModes[1]" );
        const int objectStyle = ReadInt( ( *styleModes )[2], path, "cinematic.styleModes[2]" );
        const int waterMode = ReadInt( ( *styleModes )[3], path, "cinematic.styleModes[3]" );

        if ( ParserFailed() )
        {
            return;
        }

        c.skyMode = skyMode;
        c.terrainMode = terrainMode;
        c.objectStyle = objectStyle;
        c.waterMode = waterMode;
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
    }

    if ( const Json* styleGrade = FindMember( cinematic, "styleGrade" ) )
    {
        ReadVec3( *styleGrade, path, "cinematic.styleGrade", c.styleSaturation, c.styleContrast, c.styleVignette );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
    }

    if ( const Json* terrainTint = FindMember( cinematic, "terrainTint" ) )
    {
        ReadVec3( *terrainTint, path, "cinematic.terrainTint", c.terrainTintR, c.terrainTintG, c.terrainTintB );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
    }

    if ( const Json* terrainAccent = FindMember( cinematic, "terrainAccent" ) )
    {
        ReadVec3( *terrainAccent, path, "cinematic.terrainAccent", c.terrainAccentR, c.terrainAccentG, c.terrainAccentB );

        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
    }

    if ( const Json* terrainGrid = FindMember( cinematic, "terrainGrid" ) )
    {
        if ( !RequireFixedArray( *terrainGrid, path, "cinematic.terrainGrid", 2u, "numbers" ) )
        {
            return;
        }

        const float terrainGridScale = ReadFloat( ( *terrainGrid )[0], path, "cinematic.terrainGrid[0]" );
        const float terrainGridStrength = ReadFloat( ( *terrainGrid )[1], path, "cinematic.terrainGrid[1]" );

        if ( ParserFailed() )
        {
            return;
        }

        c.terrainGridScale = terrainGridScale;
        c.terrainGridStrength = terrainGridStrength;
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
    }

    if ( const Json* waterTint = FindMember( cinematic, "waterTint" ) )
    {
        ReadVec3( *waterTint, path, "cinematic.waterTint", c.waterTintR, c.waterTintG, c.waterTintB );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
    }

    if ( const Json* waterProfile = FindMember( cinematic, "waterProfile" ) )
    {
        ReadVec3( *waterProfile, path, "cinematic.waterProfile", c.waterAlpha, c.waterReflectionStrength,
                  c.waterGlintStrength );

        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
    }

    if ( const Json* basinMask = FindMember( cinematic, "basinMask" ) )
    {
        if ( !RequireFixedArray( *basinMask, path, "cinematic.basinMask", 5u, "numbers" ) )
        {
            return;
        }

        const float basinCenterX = ReadFloat( ( *basinMask )[0], path, "cinematic.basinMask[0]" );
        const float basinCenterZ = ReadFloat( ( *basinMask )[1], path, "cinematic.basinMask[1]" );
        const float basinRadiusX = ReadFloat( ( *basinMask )[2], path, "cinematic.basinMask[2]" );
        const float basinRadiusZ = ReadFloat( ( *basinMask )[3], path, "cinematic.basinMask[3]" );
        const float basinFeather = ReadFloat( ( *basinMask )[4], path, "cinematic.basinMask[4]" );

        if ( ParserFailed() )
        {
            return;
        }

        c.basinCenterX = basinCenterX;
        c.basinCenterZ = basinCenterZ;
        c.basinRadiusX = basinRadiusX;
        c.basinRadiusZ = basinRadiusZ;
        c.basinFeather = basinFeather;
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
    }
}

void AuthoredSceneParser::ApplyCinematic( const Json& cinematic, const std::string& path )
{
    RequireObject( cinematic, path, "cinematic" );
    ApplyCinematicBool( cinematic, path );
    ApplyCinematicInt( cinematic, path );
    ApplyCinematicFloat( cinematic, path );
    ApplyCinematicVector( cinematic, path );
}

void AuthoredSceneParser::ApplyCamera( const Json& camera, const std::string& path )
{
    // Invariant: scene files expose one camera record with normalized direction
    // vectors; invalid cardinality or degenerate vectors fail the whole parse.
    RequireObject( camera, path, "camera" );

    if ( static_cast<int>( m_scene.m_cameras.size() ) >= SkullbonezCore::Scene::Capacity::AUTHORED_CAMERA_COUNT )
    {
        Fail( path, "Too many cameras in scene" );
    }

    SceneCamera out = {};
    ReadRequiredStringField( out.name, camera, path, "camera", "name" );
    ReadVec3( RequireMember( camera, path, "camera", "position" ), path, "camera.position", out.m_position.x,
              out.m_position.y, out.m_position.z );

    ReadVec3( RequireMember( camera, path, "camera", "view" ), path, "camera.view", out.view.x, out.view.y, out.view.z );

    ReadVec3( RequireMember( camera, path, "camera", "up" ), path, "camera.up", out.up.x, out.up.y, out.up.z );
    m_scene.m_cameras.push_back( out );
}


} // namespace Runtime
} // namespace SkullbonezCore
