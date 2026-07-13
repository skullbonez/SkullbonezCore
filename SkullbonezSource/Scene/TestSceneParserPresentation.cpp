/*
File: TestSceneParserPresentation.cpp
Purpose:
  Parses debug water, terrain, editor, UI, cinematic, and camera presentation fields.

Summary:
  This translation unit handles one schema domain while mutating the single
  TestSceneParser result. Shared validation and failure policy live in
  TestSceneParserSchema.h; top-level document order stays in TestSceneParser.cpp.

Glossary:
  Schema domain: Cohesive authored section translated without creating another
    scene owner or intermediate model.
  Lane R: Recoverable invalid-input result accumulated by the active parser.

Invariants:
  - Authored JSON field names remain command-line and scene-file compatibility.
  - Parser failure stops further mutation and is returned without an engine throw.
  - Stable scene identities and source ordering are preserved exactly.

Related:
  - TestSceneParserSchema.h declares shared parser state and helpers.
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md owns this decomposition.
*/
#include "TestSceneParserSchema.h"

namespace SkullbonezCore
{
namespace Basics
{
using TestSceneParserDetail::CopyStringField;
using TestSceneParserDetail::Fail;
using TestSceneParserDetail::FindMember;
using TestSceneParserDetail::Lowercase;
using TestSceneParserDetail::ParsePhysicsDebugMode;
using TestSceneParserDetail::ParseUITab;
using TestSceneParserDetail::ParseWaterReflectionMode;
using TestSceneParserDetail::ReadBool;
using TestSceneParserDetail::ReadFloat;
using TestSceneParserDetail::ReadInt;
using TestSceneParserDetail::ReadRequiredStringField;
using TestSceneParserDetail::ReadString;
using TestSceneParserDetail::ReadUInt;
using TestSceneParserDetail::ReadVec3;
using TestSceneParserDetail::RequireArray;
using TestSceneParserDetail::RequireMember;
using TestSceneParserDetail::RequireObject;

void TestSceneParser::ApplyPhysicsDebug( const Json& debug, const std::string& path )
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

void TestSceneParser::ApplyDebug( const Json& debug, const std::string& path )
{
    // Concept: water debug fields are explicit presentation overrides. They do
    // not mutate the authored water simulation or its physics parameters.
    RequireObject( debug, path, "debug" );
    if ( const Json* collisionVisualizer = FindMember( debug, "collisionVisualizer" ) )
    {
        m_scene.m_sceneOptions.collisionVisualizer =
            ReadBool( *collisionVisualizer, path, "debug.collisionVisualizer" );
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

void TestSceneParser::ApplyTerrain( const Json& terrain, const std::string& path )
{
    RequireObject( terrain, path, "terrain" );
    if ( const Json* flatSlope = FindMember( terrain, "flatSlope" ) )
    {
        RequireObject( *flatSlope, path, "terrain.flatSlope" );
        m_scene.m_terrainOverride.hasFlatSlope = true;
        m_scene.m_terrainOverride.flatBaseY =
            ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "baseY" ),
                       path,
                       "terrain.flatSlope.baseY" );
        m_scene.m_terrainOverride.flatSlopeX =
            ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "slopeX" ),
                       path,
                       "terrain.flatSlope.slopeX" );
        m_scene.m_terrainOverride.flatSlopeZ =
            ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "slopeZ" ),
                       path,
                       "terrain.flatSlope.slopeZ" );
    }
}

void TestSceneParser::ApplyEditor( const Json& editor, const std::string& path )
{
    RequireObject( editor, path, "editor" );
    if ( const Json* editable = FindMember( editor, "editableScene" ) )
    {
        m_scene.m_sceneOptions.editableScene = ReadBool( *editable, path, "editor.editableScene" );
    }
}

void TestSceneParser::ApplyUI( const Json& ui, const std::string& path )
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
        RequireArray( *rect, path, "ui.rect" );
        if ( rect->size() != 4 )
        {
            Fail( path, "ui.rect must contain exactly 4 integers" );
        }
        out.hasWindowRect = true;
        out.windowX = ReadInt( ( *rect )[0], path, "ui.rect[0]" );
        out.windowY = ReadInt( ( *rect )[1], path, "ui.rect[1]" );
        out.windowW = ReadInt( ( *rect )[2], path, "ui.rect[2]" );
        out.windowH = ReadInt( ( *rect )[3], path, "ui.rect[3]" );
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
        out.hasSceneFilter = true;
        CopyStringField( out.sceneFilter, ReadString( *sceneFilter, path, "ui.sceneFilter" ) );
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
        RequireArray( *mouse, path, "ui.mouse" );
        if ( mouse->size() != 2 )
        {
            Fail( path, "ui.mouse must contain exactly 2 integers" );
        }
        out.hasMouseOverride = true;
        out.mouseX = ReadInt( ( *mouse )[0], path, "ui.mouse[0]" );
        out.mouseY = ReadInt( ( *mouse )[1], path, "ui.mouse[1]" );
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

void TestSceneParser::ApplyCinematicBool( const Json& cinematic, const std::string& path )
{
    struct BoolField
    {
        const char* key;
        bool CinematicRenderConfig::* field;
        uint64_t bit;
    };
    static constexpr BoolField kFields[] = {
        { "rendering", &CinematicRenderConfig::enabled, SCENE_CINE_RENDERING },
        { "skyAtmosphere", &CinematicRenderConfig::skyAtmosphereEnabled, SCENE_CINE_SKY_ATMOSPHERE },
        { "clouds", &CinematicRenderConfig::cloudsEnabled, SCENE_CINE_CLOUDS },
        { "godRays", &CinematicRenderConfig::godRaysEnabled, SCENE_CINE_GOD_RAYS },
        { "volumetricLighting", &CinematicRenderConfig::volumetricLightingEnabled, SCENE_CINE_VOLUMETRIC_LIGHTING },
        { "bloom", &CinematicRenderConfig::bloomEnabled, SCENE_CINE_BLOOM },
        { "fog", &CinematicRenderConfig::fogEnabled, SCENE_CINE_FOG },
        { "terrainReliefEnabled", &CinematicRenderConfig::terrainReliefEnabled, SCENE_CINE_TERRAIN_RELIEF_ENABLED },
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

void TestSceneParser::ApplyCinematicInt( const Json& cinematic, const std::string& path )
{
    struct IntField
    {
        const char* key;
        int ShadowQualityConfig::* field;
        uint64_t bit;
        int minValue;
        int maxValue;
    };
    static constexpr IntField kFields[] = {
        { "shadowMapSize", &ShadowQualityConfig::mapSize, SCENE_CINE_SHADOW_MAP_SIZE, 256, 8192 },
        { "shadowPcfRadius", &ShadowQualityConfig::pcfRadius, SCENE_CINE_SHADOW_PCF_RADIUS, 0, 3 },
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

void TestSceneParser::ApplyCinematicFloat( const Json& cinematic, const std::string& path )
{
    // Invariant: each accepted scalar sets its matching override bit. Runtime
    // style merging relies on the value and mask changing atomically.
    struct FloatField
    {
        const char* key;
        float CinematicRenderConfig::* field;
        uint64_t bit;
        float minValue;
        float maxValue;
    };
    static constexpr FloatField kFields[] = {
        { "exposure", &CinematicRenderConfig::exposure, SCENE_CINE_EXPOSURE, 0.0f, 16.0f },
        { "gamma", &CinematicRenderConfig::gamma, SCENE_CINE_GAMMA, 0.1f, 8.0f },
        // Compatibility: scene/style JSON retains its original key spellings;
        // only the in-memory owner vocabulary changed.
        { "sunScreenX", &CinematicRenderConfig::sunAzimuth, SCENE_CINE_SUN_AZIMUTH, 0.0f, 1.0f },
        { "sunScreenY", &CinematicRenderConfig::sunElevation, SCENE_CINE_SUN_ELEVATION, 0.0f, 1.0f },
        { "sunColorR", &CinematicRenderConfig::sunColorR, SCENE_CINE_SUN_COLOR_R, 0.0f, 4.0f },
        { "sunColorG", &CinematicRenderConfig::sunColorG, SCENE_CINE_SUN_COLOR_G, 0.0f, 4.0f },
        { "sunColorB", &CinematicRenderConfig::sunColorB, SCENE_CINE_SUN_COLOR_B, 0.0f, 4.0f },
        { "sunIntensity", &CinematicRenderConfig::sunIntensity, SCENE_CINE_SUN_INTENSITY, 0.0f, 80.0f },
        { "skyHorizonR", &CinematicRenderConfig::skyHorizonR, SCENE_CINE_SKY_HORIZON_R, 0.0f, 4.0f },
        { "skyHorizonG", &CinematicRenderConfig::skyHorizonG, SCENE_CINE_SKY_HORIZON_G, 0.0f, 4.0f },
        { "skyHorizonB", &CinematicRenderConfig::skyHorizonB, SCENE_CINE_SKY_HORIZON_B, 0.0f, 4.0f },
        { "skyZenithR", &CinematicRenderConfig::skyZenithR, SCENE_CINE_SKY_ZENITH_R, 0.0f, 4.0f },
        { "skyZenithG", &CinematicRenderConfig::skyZenithG, SCENE_CINE_SKY_ZENITH_G, 0.0f, 4.0f },
        { "skyZenithB", &CinematicRenderConfig::skyZenithB, SCENE_CINE_SKY_ZENITH_B, 0.0f, 4.0f },
        { "skyGlowStrength", &CinematicRenderConfig::skyGlowStrength, SCENE_CINE_SKY_GLOW_STRENGTH, 0.0f, 16.0f },
        { "cloudCoverage", &CinematicRenderConfig::cloudCoverage, SCENE_CINE_CLOUD_COVERAGE, 0.0f, 1.0f },
        { "cloudSoftness", &CinematicRenderConfig::cloudSoftness, SCENE_CINE_CLOUD_SOFTNESS, 0.001f, 1.0f },
        { "cloudScale", &CinematicRenderConfig::cloudScale, SCENE_CINE_CLOUD_SCALE, 0.1f, 64.0f },
        { "cloudIntensity", &CinematicRenderConfig::cloudIntensity, SCENE_CINE_CLOUD_INTENSITY, 0.0f, 4.0f },
        { "sunShaftStrength", &CinematicRenderConfig::sunShaftStrength, SCENE_CINE_SUN_SHAFT_STRENGTH, 0.0f, 8.0f },
        { "sunShaftFalloff", &CinematicRenderConfig::sunShaftFalloff, SCENE_CINE_SUN_SHAFT_FALLOFF, 0.1f, 10.0f },
        { "volumetricStrength",
          &CinematicRenderConfig::volumetricStrength,
          SCENE_CINE_VOLUMETRIC_STRENGTH,
          0.0f,
          8.0f },
        { "volumetricDensity", &CinematicRenderConfig::volumetricDensity, SCENE_CINE_VOLUMETRIC_DENSITY, 0.0f, 8.0f },
        { "volumetricDecay", &CinematicRenderConfig::volumetricDecay, SCENE_CINE_VOLUMETRIC_DECAY, 0.0f, 1.0f },
        { "bloomThreshold", &CinematicRenderConfig::bloomThreshold, SCENE_CINE_BLOOM_THRESHOLD, 0.0f, 16.0f },
        { "bloomKnee", &CinematicRenderConfig::bloomKnee, SCENE_CINE_BLOOM_KNEE, 0.001f, 8.0f },
        { "bloomStrength", &CinematicRenderConfig::bloomStrength, SCENE_CINE_BLOOM_STRENGTH, 0.0f, 8.0f },
        { "bloomRadius", &CinematicRenderConfig::bloomRadius, SCENE_CINE_BLOOM_RADIUS, 0.1f, 32.0f },
        { "terrainRelief", &CinematicRenderConfig::terrainRelief, SCENE_CINE_TERRAIN_RELIEF, 0.0f, 4.0f },
        { "basinDepth", &CinematicRenderConfig::basinDepth, SCENE_CINE_BASIN_DEPTH, 0.0f, 256.0f },
        { "basinRimLift", &CinematicRenderConfig::basinRimLift, SCENE_CINE_BASIN_RIM_LIFT, 0.0f, 256.0f },
        { "fogColorR", &CinematicRenderConfig::fogColorR, SCENE_CINE_FOG_COLOR_R, 0.0f, 4.0f },
        { "fogColorG", &CinematicRenderConfig::fogColorG, SCENE_CINE_FOG_COLOR_G, 0.0f, 4.0f },
        { "fogColorB", &CinematicRenderConfig::fogColorB, SCENE_CINE_FOG_COLOR_B, 0.0f, 4.0f },
        { "fogStart", &CinematicRenderConfig::fogStart, SCENE_CINE_FOG_START, 0.0f, 10000.0f },
        { "fogEnd", &CinematicRenderConfig::fogEnd, SCENE_CINE_FOG_END, 0.0f, 20000.0f },
        { "fogDensity", &CinematicRenderConfig::fogDensity, SCENE_CINE_FOG_DENSITY, 0.0f, 0.1f },
        { "fogMaxOpacity", &CinematicRenderConfig::fogMaxOpacity, SCENE_CINE_FOG_MAX_OPACITY, 0.0f, 1.0f },
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
        float ShadowQualityConfig::* field;
        uint64_t bit;
        float minValue;
        float maxValue;
    };
    static constexpr ShadowFloatField kShadowFields[] = {
        { "shadowStrength", &ShadowQualityConfig::strength, SCENE_CINE_SHADOW_STRENGTH, 0.0f, 1.0f },
        { "shadowSoftness", &ShadowQualityConfig::softness, SCENE_CINE_SHADOW_SOFTNESS, 0.25f, 4.0f },
        { "shadowDepthBias", &ShadowQualityConfig::depthBias, SCENE_CINE_SHADOW_DEPTH_BIAS, 0.0f, 0.05f },
        { "shadowSlopeBias", &ShadowQualityConfig::slopeBias, SCENE_CINE_SHADOW_SLOPE_BIAS, 0.0f, 0.05f },
        { "shadowMaxDistance", &ShadowQualityConfig::maxDistance, SCENE_CINE_SHADOW_MAX_DISTANCE, 128.0f, 10000.0f },
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

void TestSceneParser::ApplyCinematicVector( const Json& cinematic, const std::string& path )
{
    CinematicRenderConfig& c = m_scene.m_sceneOptions.cinematicRender;

    if ( const Json* styleModes = FindMember( cinematic, "styleModes" ) )
    {
        RequireArray( *styleModes, path, "cinematic.styleModes" );
        if ( styleModes->size() != 4 )
        {
            Fail( path, "cinematic.styleModes must contain exactly 4 integers" );
        }
        c.skyMode = ReadInt( ( *styleModes )[0], path, "cinematic.styleModes[0]" );
        c.terrainMode = ReadInt( ( *styleModes )[1], path, "cinematic.styleModes[1]" );
        c.objectStyle = ReadInt( ( *styleModes )[2], path, "cinematic.styleModes[2]" );
        c.waterMode = ReadInt( ( *styleModes )[3], path, "cinematic.styleModes[3]" );
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
        ReadVec3( *terrainAccent,
                  path,
                  "cinematic.terrainAccent",
                  c.terrainAccentR,
                  c.terrainAccentG,
                  c.terrainAccentB );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
    }
    if ( const Json* terrainGrid = FindMember( cinematic, "terrainGrid" ) )
    {
        RequireArray( *terrainGrid, path, "cinematic.terrainGrid" );
        if ( terrainGrid->size() != 2 )
        {
            Fail( path, "cinematic.terrainGrid must contain exactly 2 numbers" );
        }
        c.terrainGridScale = ReadFloat( ( *terrainGrid )[0], path, "cinematic.terrainGrid[0]" );
        c.terrainGridStrength = ReadFloat( ( *terrainGrid )[1], path, "cinematic.terrainGrid[1]" );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
    }
    if ( const Json* waterTint = FindMember( cinematic, "waterTint" ) )
    {
        ReadVec3( *waterTint, path, "cinematic.waterTint", c.waterTintR, c.waterTintG, c.waterTintB );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
    }
    if ( const Json* waterProfile = FindMember( cinematic, "waterProfile" ) )
    {
        ReadVec3( *waterProfile,
                  path,
                  "cinematic.waterProfile",
                  c.waterAlpha,
                  c.waterReflectionStrength,
                  c.waterGlintStrength );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
    }
    if ( const Json* basinMask = FindMember( cinematic, "basinMask" ) )
    {
        RequireArray( *basinMask, path, "cinematic.basinMask" );
        if ( basinMask->size() != 5 )
        {
            Fail( path, "cinematic.basinMask must contain exactly 5 numbers" );
        }
        c.basinCenterX = ReadFloat( ( *basinMask )[0], path, "cinematic.basinMask[0]" );
        c.basinCenterZ = ReadFloat( ( *basinMask )[1], path, "cinematic.basinMask[1]" );
        c.basinRadiusX = ReadFloat( ( *basinMask )[2], path, "cinematic.basinMask[2]" );
        c.basinRadiusZ = ReadFloat( ( *basinMask )[3], path, "cinematic.basinMask[3]" );
        c.basinFeather = ReadFloat( ( *basinMask )[4], path, "cinematic.basinMask[4]" );
        m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
    }
}

void TestSceneParser::ApplyCinematic( const Json& cinematic, const std::string& path )
{
    RequireObject( cinematic, path, "cinematic" );
    ApplyCinematicBool( cinematic, path );
    ApplyCinematicInt( cinematic, path );
    ApplyCinematicFloat( cinematic, path );
    ApplyCinematicVector( cinematic, path );
}

void TestSceneParser::ApplyCamera( const Json& camera, const std::string& path )
{
    // Invariant: scene files expose one camera record with normalized direction
    // vectors; invalid cardinality or degenerate vectors fail the whole parse.
    RequireObject( camera, path, "camera" );
    if ( static_cast<int>( m_scene.m_cameras.size() ) >= TOTAL_CAMERA_COUNT )
    {
        Fail( path, "Too many cameras in scene" );
    }

    SceneCamera out = {};
    ReadRequiredStringField( out.name, camera, path, "camera", "name" );
    ReadVec3( RequireMember( camera, path, "camera", "position" ),
              path,
              "camera.position",
              out.m_position.x,
              out.m_position.y,
              out.m_position.z );
    ReadVec3( RequireMember( camera, path, "camera", "view" ),
              path,
              "camera.view",
              out.view.x,
              out.view.y,
              out.view.z );
    ReadVec3( RequireMember( camera, path, "camera", "up" ), path, "camera.up", out.up.x, out.up.y, out.up.z );
    m_scene.m_cameras.push_back( out );
}


} // namespace Basics
} // namespace SkullbonezCore
