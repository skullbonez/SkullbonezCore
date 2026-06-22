/*
File: SkullbonezSource/RunInput.cpp
Purpose:
  Routes raw keyboard, mouse, and UI commands into runtime state changes.

Mental model:
  Input arbitration stays here.
  Editor, launcher, and replay behavior live in dedicated runtime files.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "EditorHullAssets.h"
#include "InputController.h"
#include "PhysicsMass.h"
#include "RuntimeFileWriter.h"
#include "WorkerPool.h"
#include "UI/UIInput.h"
#include "UI/UILayout.h"

#include <chrono>
#include <cfloat>
#include <cstddef>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;

namespace
{
RuntimeInputModeState BuildRuntimeInputModeState( const RunCameraState& camera, const RunEditorPlacementState& editor )
{
    RuntimeInputModeState state;
    state.flyCamera = camera.isFlyMode;
    state.launcher = camera.isLauncherMode;
    state.editor = editor.editorModeEnabled;
    state.editorPlacement = editor.placementModeEnabled;
    state.editorViewportLook = editor.viewportLookActive;
    state.editorPlacementScale = editor.placementScaleActive;
    state.editorGizmoDrag = editor.gizmoDragActive;
    state.editorGizmoRotation = editor.gizmoDragIsRotation;
    state.editorGizmoScale = editor.gizmoDragIsScale;
    return state;
}

struct RuntimeInputKeyBinding
{
    RuntimeInputAction action;
    int virtualKey;
};

void AdvanceTakeInputKeyboardActionMemories( RuntimeInputContext& input )
{
    static const RuntimeInputKeyBinding kBindings[] = { { RuntimeInputAction::ToggleFlyCamera, 'F' },
                                                        { RuntimeInputAction::ToggleLauncher, 'N' },
                                                        { RuntimeInputAction::ToggleEditor, VK_OEM_3 },
                                                        { RuntimeInputAction::ToggleEditorTool, VK_MENU },
                                                        { RuntimeInputAction::CycleEditorPlacementType, VK_TAB },
                                                        { RuntimeInputAction::CycleLauncherFireMode, 'M' },
                                                        { RuntimeInputAction::WriteLauncherReproSnapshot, VK_RETURN },
                                                        { RuntimeInputAction::ToggleWaterFreeze, '1' },
                                                        { RuntimeInputAction::CycleWaterReflection, '2' },
                                                        { RuntimeInputAction::ToggleWaterFlat, '3' },
                                                        { RuntimeInputAction::ToggleTerrainHidden, '4' },
                                                        { RuntimeInputAction::ToggleWaterHidden, '5' },
                                                        { RuntimeInputAction::ToggleCollisionVisualizer, 'V' },
                                                        { RuntimeInputAction::CyclePhysicsDebugOverlay, 'C' },
                                                        { RuntimeInputAction::ToggleTerrainContactProbe, 'O' },
                                                        { RuntimeInputAction::StepPhysicsPipelinePrevious, VK_F7 },
                                                        { RuntimeInputAction::StepPhysicsPipelineNext, VK_F8 },
                                                        { RuntimeInputAction::TogglePhysicsDebugTransparent, '6' },
                                                        { RuntimeInputAction::ReportRendererRuntimeRetired, 'Q' },
                                                        { RuntimeInputAction::ToggleBroadphaseOverlay, 'G' },
                                                        { RuntimeInputAction::ToggleUIVisibility, '0' },
                                                        { RuntimeInputAction::NavigateScenePrevious, VK_LEFT },
                                                        { RuntimeInputAction::NavigateSceneNext, VK_RIGHT },
                                                        { RuntimeInputAction::DismissOrExitUI, VK_ESCAPE },
                                                        { RuntimeInputAction::SaveSceneSnapshot, VK_F2 },
                                                        { RuntimeInputAction::SaveScreenshot, VK_F3 },
                                                        { RuntimeInputAction::ResetScene, 'R' },
                                                        { RuntimeInputAction::ResetSceneFromBackspace, VK_BACK } };

    for ( std::size_t i = 0; i < sizeof( kBindings ) / sizeof( kBindings[0] ); ++i )
    {
        input.SetActionDown( kBindings[i].action, Input::IsKeyDown( kBindings[i].virtualKey ) );
    }
}


constexpr float EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT = 16.0f;
constexpr float EDITOR_PLACEMENT_SCALE_WHEEL_UNIT = 1.0f;
constexpr float EDITOR_PLACEMENT_HULL_SCALE_PIXELS_PER_UNIT = 160.0f;
constexpr float EDITOR_PLACEMENT_HULL_SCALE_WHEEL_UNIT = 0.05f;

int EditorMouseWheelSteps( int wheelDelta )
{
    if ( wheelDelta == 0 )
    {
        return 0;
    }
    return wheelDelta / WHEEL_DELTA;
}

bool EditorPlacementUsesUniformScale( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    return type == SkullbonezCore::UI::EditorTab::OBJECT_BALL || type == SkullbonezCore::UI::EditorTab::OBJECT_SPHERE;
}

bool EditorPlacementUsesHullScaleFactors( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_WEDGE:
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_TRI_PRISM:
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_TAPERED_BLOCK:
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_PYRAMID:
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_HEX_PRISM:
    case SkullbonezCore::UI::EditorTab::OBJECT_HULL_DIAMOND:
    case SkullbonezCore::UI::EditorTab::OBJECT_ROCK_SLAB:
    case SkullbonezCore::UI::EditorTab::OBJECT_ROCK_LUMP:
    case SkullbonezCore::UI::EditorTab::OBJECT_ROCK_SHARD:
    case SkullbonezCore::UI::EditorTab::OBJECT_ROCK_CHIPPED:
    case SkullbonezCore::UI::EditorTab::OBJECT_ROOT_SMALL:
    case SkullbonezCore::UI::EditorTab::OBJECT_ROOT_LARGE:
        return true;
    default:
        return false;
    }
}

bool EditorPlacementUsesTreeScaleLock( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_SMALL:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_BIG:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_CEDAR:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_SMALL_SLOPE:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_BIG_SLOPE:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_CEDAR_SLOPE:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_SMALL_SLEEP:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_BIG_SLEEP:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_CEDAR_SLEEP:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_SMALL_ROOTED:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_BIG_ROOTED:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_CEDAR_ROOTED:
    case SkullbonezCore::UI::EditorTab::OBJECT_TREE_PINE_SHEDDING:
        return true;
    default:
        return false;
    }
}

Vector3 EditorDefaultPlacementScale( int objectType )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        return Vector3( 6.0f, 6.0f, 6.0f );
    case SkullbonezCore::UI::EditorTab::OBJECT_BALL:
        return Vector3( 4.0f, 4.0f, 4.0f );
    case SkullbonezCore::UI::EditorTab::OBJECT_SPHERE:
        return Vector3( 8.0f, 8.0f, 8.0f );
    default:
        return Vector3( 1.0f, 1.0f, 1.0f );
    }
}

Vector3 EditorClampPlacementScale( int objectType, const Vector3& scale )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    if ( EditorPlacementUsesTreeScaleLock( type ) )
    {
        return Vector3( 1.0f, 1.0f, 1.0f );
    }

    if ( EditorPlacementUsesUniformScale( type ) )
    {
        const float radius = std::clamp( scale.x, 0.25f, 200.0f );
        return Vector3( radius, radius, radius );
    }

    if ( EditorPlacementUsesHullScaleFactors( type ) )
    {
        return Vector3( std::clamp( scale.x, 0.05f, 20.0f ),
                        std::clamp( scale.y, 0.05f, 20.0f ),
                        std::clamp( scale.z, 0.05f, 20.0f ) );
    }

    return Vector3( std::clamp( scale.x, 0.25f, 200.0f ),
                    std::clamp( scale.y, 0.25f, 200.0f ),
                    std::clamp( scale.z, 0.25f, 200.0f ) );
}

Vector3 EditorPlacementScaleFromGesture( int objectType,
                                         const Vector3& startScale,
                                         float dragPixelsX,
                                         float dragPixelsY,
                                         int wheelSteps )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    if ( EditorPlacementUsesUniformScale( type ) )
    {
        const float dragUnits = ( dragPixelsX + dragPixelsY ) / ( EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT * 2.0f );
        const float radius =
            startScale.x + dragUnits + static_cast<float>( wheelSteps ) * EDITOR_PLACEMENT_SCALE_WHEEL_UNIT;
        return EditorClampPlacementScale( type, Vector3( radius, radius, radius ) );
    }

    if ( EditorPlacementUsesHullScaleFactors( type ) )
    {
        Vector3 scale = startScale;
        scale.x += dragPixelsX / EDITOR_PLACEMENT_HULL_SCALE_PIXELS_PER_UNIT;
        scale.z += dragPixelsY / EDITOR_PLACEMENT_HULL_SCALE_PIXELS_PER_UNIT;
        scale.y += static_cast<float>( wheelSteps ) * EDITOR_PLACEMENT_HULL_SCALE_WHEEL_UNIT;
        return EditorClampPlacementScale( type, scale );
    }

    Vector3 scale = startScale;
    scale.x += dragPixelsX / EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT;
    scale.z += dragPixelsY / EDITOR_PLACEMENT_SCALE_PIXELS_PER_UNIT;
    scale.y += static_cast<float>( wheelSteps ) * EDITOR_PLACEMENT_SCALE_WHEEL_UNIT;
    return EditorClampPlacementScale( type, scale );
}

uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param )
{
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return SCENE_CINE_EXPOSURE;
    case UICinematicParam::Gamma:
        return SCENE_CINE_GAMMA;
    case UICinematicParam::SkyMode:
    case UICinematicParam::TerrainMode:
    case UICinematicParam::ObjectStyle:
    case UICinematicParam::WaterMode:
        return SCENE_CINE_STYLE_MODES;
    case UICinematicParam::StyleSaturation:
    case UICinematicParam::StyleContrast:
    case UICinematicParam::StyleVignette:
        return SCENE_CINE_STYLE_GRADE;
    case UICinematicParam::SunX:
        return SCENE_CINE_SUN_SCREEN_X;
    case UICinematicParam::SunY:
        return SCENE_CINE_SUN_SCREEN_Y;
    case UICinematicParam::SunBrightness:
        return SCENE_CINE_SUN_INTENSITY;
    case UICinematicParam::SunRed:
        return SCENE_CINE_SUN_COLOR_R;
    case UICinematicParam::SunGreen:
        return SCENE_CINE_SUN_COLOR_G;
    case UICinematicParam::SunBlue:
        return SCENE_CINE_SUN_COLOR_B;
    case UICinematicParam::SkyGlow:
        return SCENE_CINE_SKY_GLOW_STRENGTH;
    case UICinematicParam::HorizonRed:
        return SCENE_CINE_SKY_HORIZON_R;
    case UICinematicParam::HorizonGreen:
        return SCENE_CINE_SKY_HORIZON_G;
    case UICinematicParam::HorizonBlue:
        return SCENE_CINE_SKY_HORIZON_B;
    case UICinematicParam::ZenithRed:
        return SCENE_CINE_SKY_ZENITH_R;
    case UICinematicParam::ZenithGreen:
        return SCENE_CINE_SKY_ZENITH_G;
    case UICinematicParam::ZenithBlue:
        return SCENE_CINE_SKY_ZENITH_B;
    case UICinematicParam::CloudCoverage:
        return SCENE_CINE_CLOUD_COVERAGE;
    case UICinematicParam::CloudSoftness:
        return SCENE_CINE_CLOUD_SOFTNESS;
    case UICinematicParam::CloudScale:
        return SCENE_CINE_CLOUD_SCALE;
    case UICinematicParam::CloudIntensity:
        return SCENE_CINE_CLOUD_INTENSITY;
    case UICinematicParam::ShaftStrength:
        return SCENE_CINE_SUN_SHAFT_STRENGTH;
    case UICinematicParam::ShaftFalloff:
        return SCENE_CINE_SUN_SHAFT_FALLOFF;
    case UICinematicParam::VolumetricStrength:
        return SCENE_CINE_VOLUMETRIC_STRENGTH;
    case UICinematicParam::VolumetricDensity:
        return SCENE_CINE_VOLUMETRIC_DENSITY;
    case UICinematicParam::VolumetricDecay:
        return SCENE_CINE_VOLUMETRIC_DECAY;
    case UICinematicParam::BloomThreshold:
        return SCENE_CINE_BLOOM_THRESHOLD;
    case UICinematicParam::BloomKnee:
        return SCENE_CINE_BLOOM_KNEE;
    case UICinematicParam::BloomStrength:
        return SCENE_CINE_BLOOM_STRENGTH;
    case UICinematicParam::BloomRadius:
        return SCENE_CINE_BLOOM_RADIUS;
    case UICinematicParam::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF;
    case UICinematicParam::TerrainTintRed:
    case UICinematicParam::TerrainTintGreen:
    case UICinematicParam::TerrainTintBlue:
        return SCENE_CINE_TERRAIN_TINT;
    case UICinematicParam::TerrainAccentRed:
    case UICinematicParam::TerrainAccentGreen:
    case UICinematicParam::TerrainAccentBlue:
        return SCENE_CINE_TERRAIN_ACCENT;
    case UICinematicParam::TerrainGridScale:
    case UICinematicParam::TerrainGridStrength:
        return SCENE_CINE_TERRAIN_GRID;
    case UICinematicParam::WaterTintRed:
    case UICinematicParam::WaterTintGreen:
    case UICinematicParam::WaterTintBlue:
        return SCENE_CINE_WATER_TINT;
    case UICinematicParam::WaterAlpha:
    case UICinematicParam::WaterReflection:
    case UICinematicParam::WaterGlint:
        return SCENE_CINE_WATER_PROFILE;
    case UICinematicParam::BasinCenterX:
    case UICinematicParam::BasinCenterZ:
    case UICinematicParam::BasinRadiusX:
    case UICinematicParam::BasinRadiusZ:
    case UICinematicParam::BasinFeather:
        return SCENE_CINE_BASIN_MASK;
    case UICinematicParam::BasinDepth:
        return SCENE_CINE_BASIN_DEPTH;
    case UICinematicParam::BasinRimLift:
        return SCENE_CINE_BASIN_RIM_LIFT;
    case UICinematicParam::FogDensity:
        return SCENE_CINE_FOG_DENSITY;
    case UICinematicParam::FogOpacity:
        return SCENE_CINE_FOG_MAX_OPACITY;
    case UICinematicParam::FogStart:
        return SCENE_CINE_FOG_START;
    case UICinematicParam::FogEnd:
        return SCENE_CINE_FOG_END;
    case UICinematicParam::FogRed:
        return SCENE_CINE_FOG_COLOR_R;
    case UICinematicParam::FogGreen:
        return SCENE_CINE_FOG_COLOR_G;
    case UICinematicParam::FogBlue:
        return SCENE_CINE_FOG_COLOR_B;
    default:
        return 0;
    }
}


uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature )
{
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        return SCENE_CINE_SKY_ATMOSPHERE;
    case UICinematicFeature::Clouds:
        return SCENE_CINE_CLOUDS;
    case UICinematicFeature::GodRays:
        return SCENE_CINE_GOD_RAYS;
    case UICinematicFeature::VolumetricLight:
        return SCENE_CINE_VOLUMETRIC_LIGHTING;
    case UICinematicFeature::Bloom:
        return SCENE_CINE_BLOOM;
    case UICinematicFeature::Fog:
        return SCENE_CINE_FOG;
    case UICinematicFeature::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF_ENABLED;
    case UICinematicFeature::Shadows:
        return SCENE_CINE_SHADOWS;
    default:
        return 0;
    }
}


void ApplyWorkerThreadCountOverride( int requestedWorkerThreads )
{
    const int clampedWorkerThreads =
        requestedWorkerThreads < 0
            ? -1
            : std::clamp( requestedWorkerThreads, 0, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
    SkullbonezCore::Threading::WorkerPool& workerPool = SkullbonezCore::Threading::WorkerPool::Instance();
    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    Cfg().workerThreads = clampedWorkerThreads;
    if ( workerPool.GetThreadCount() != resolvedWorkerThreads )
    {
        workerPool.Initialise( clampedWorkerThreads );
    }
}


void ApplyCinematicUIParam( CinematicRenderConfig& cinematic,
                            RunSceneState& scene,
                            UICinematicParam param,
                            float rawValue )
{
    // The UI sends "the user dragged this slider to this raw value." This helper
    // clamps the value into a safe range, writes it into the live cinematic
    // config, and marks the scene override bit so reloads keep the user's tweak.
    const auto clampValue = []( float value, float minValue, float maxValue ) -> float
    { return std::clamp( value, minValue, maxValue ); };
    const auto clampIntValue = []( float value, int minValue, int maxValue ) -> int
    { return std::clamp( static_cast<int>( std::round( value ) ), minValue, maxValue ); };

    switch ( param )
    {
    case UICinematicParam::Exposure:
        cinematic.exposure = clampValue( rawValue, 0.05f, 3.00f );
        scene.hasCinematicExposure = true;
        scene.cinematicExposure = cinematic.exposure;
        scene.cinematicOverrideMask |= SCENE_CINE_EXPOSURE;
        break;
    case UICinematicParam::Gamma:
        cinematic.gamma = clampValue( rawValue, 1.00f, 3.00f );
        scene.hasCinematicGamma = true;
        scene.cinematicGamma = cinematic.gamma;
        scene.cinematicOverrideMask |= SCENE_CINE_GAMMA;
        break;
    case UICinematicParam::SkyMode:
        cinematic.skyMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::TerrainMode:
        cinematic.terrainMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::ObjectStyle:
        cinematic.objectStyle = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::WaterMode:
        cinematic.waterMode = clampIntValue( rawValue, 0, 4 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::StyleSaturation:
        cinematic.styleSaturation = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleContrast:
        cinematic.styleContrast = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleVignette:
        cinematic.styleVignette = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::SunX:
        cinematic.sunScreenX = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_X;
        break;
    case UICinematicParam::SunY:
        cinematic.sunScreenY = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_Y;
        break;
    case UICinematicParam::SunBrightness:
        cinematic.sunIntensity = clampValue( rawValue, 0.00f, 40.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_INTENSITY;
        break;
    case UICinematicParam::SunRed:
        cinematic.sunColorR = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_R;
        break;
    case UICinematicParam::SunGreen:
        cinematic.sunColorG = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_G;
        break;
    case UICinematicParam::SunBlue:
        cinematic.sunColorB = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_B;
        break;
    case UICinematicParam::SkyGlow:
        cinematic.skyGlowStrength = clampValue( rawValue, 0.00f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_GLOW_STRENGTH;
        break;
    case UICinematicParam::HorizonRed:
        cinematic.skyHorizonR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_R;
        break;
    case UICinematicParam::HorizonGreen:
        cinematic.skyHorizonG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_G;
        break;
    case UICinematicParam::HorizonBlue:
        cinematic.skyHorizonB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_B;
        break;
    case UICinematicParam::ZenithRed:
        cinematic.skyZenithR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_R;
        break;
    case UICinematicParam::ZenithGreen:
        cinematic.skyZenithG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_G;
        break;
    case UICinematicParam::ZenithBlue:
        cinematic.skyZenithB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_B;
        break;
    case UICinematicParam::CloudCoverage:
        cinematic.cloudCoverage = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_COVERAGE;
        break;
    case UICinematicParam::CloudSoftness:
        cinematic.cloudSoftness = clampValue( rawValue, 0.01f, 0.65f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SOFTNESS;
        break;
    case UICinematicParam::CloudScale:
        cinematic.cloudScale = clampValue( rawValue, 0.50f, 12.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SCALE;
        break;
    case UICinematicParam::CloudIntensity:
        cinematic.cloudIntensity = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_INTENSITY;
        break;
    case UICinematicParam::ShaftStrength:
        cinematic.sunShaftStrength = clampValue( rawValue, 0.00f, 3.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_STRENGTH;
        break;
    case UICinematicParam::ShaftFalloff:
        cinematic.sunShaftFalloff = clampValue( rawValue, 0.25f, 5.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_FALLOFF;
        break;
    case UICinematicParam::VolumetricStrength:
        cinematic.volumetricStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_STRENGTH;
        break;
    case UICinematicParam::VolumetricDensity:
        cinematic.volumetricDensity = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DENSITY;
        break;
    case UICinematicParam::VolumetricDecay:
        cinematic.volumetricDecay = clampValue( rawValue, 0.800f, 0.995f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DECAY;
        break;
    case UICinematicParam::BloomThreshold:
        cinematic.bloomThreshold = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_THRESHOLD;
        break;
    case UICinematicParam::BloomKnee:
        cinematic.bloomKnee = clampValue( rawValue, 0.01f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_KNEE;
        break;
    case UICinematicParam::BloomStrength:
        cinematic.bloomStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_STRENGTH;
        break;
    case UICinematicParam::BloomRadius:
        cinematic.bloomRadius = clampValue( rawValue, 0.25f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_RADIUS;
        break;
    case UICinematicParam::TerrainRelief:
        cinematic.terrainRelief = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF;
        break;
    case UICinematicParam::TerrainTintRed:
        cinematic.terrainTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintGreen:
        cinematic.terrainTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintBlue:
        cinematic.terrainTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainAccentRed:
        cinematic.terrainAccentR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentGreen:
        cinematic.terrainAccentG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentBlue:
        cinematic.terrainAccentB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainGridScale:
        cinematic.terrainGridScale = clampValue( rawValue, 0.10f, 120.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::TerrainGridStrength:
        cinematic.terrainGridStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::WaterTintRed:
        cinematic.waterTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintGreen:
        cinematic.waterTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintBlue:
        cinematic.waterTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterAlpha:
        cinematic.waterAlpha = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterReflection:
        cinematic.waterReflectionStrength = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterGlint:
        cinematic.waterGlintStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::BasinCenterX:
        cinematic.basinCenterX = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinCenterZ:
        cinematic.basinCenterZ = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusX:
        cinematic.basinRadiusX = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusZ:
        cinematic.basinRadiusZ = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinFeather:
        cinematic.basinFeather = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinDepth:
        cinematic.basinDepth = clampValue( rawValue, 0.00f, 80.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_DEPTH;
        break;
    case UICinematicParam::BasinRimLift:
        cinematic.basinRimLift = clampValue( rawValue, 0.00f, 60.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_RIM_LIFT;
        break;
    case UICinematicParam::FogDensity:
        cinematic.fogDensity = clampValue( rawValue, 0.00000f, 0.00600f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_DENSITY;
        break;
    case UICinematicParam::FogOpacity:
        cinematic.fogMaxOpacity = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_MAX_OPACITY;
        break;
    case UICinematicParam::FogStart:
        cinematic.fogStart = clampValue( rawValue, 0.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_START;
        break;
    case UICinematicParam::FogEnd:
        cinematic.fogEnd = clampValue( rawValue, 100.00f, 4000.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_END;
        break;
    case UICinematicParam::FogRed:
        cinematic.fogColorR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_R;
        break;
    case UICinematicParam::FogGreen:
        cinematic.fogColorG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_G;
        break;
    case UICinematicParam::FogBlue:
        cinematic.fogColorB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_B;
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIParam( param );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}


void SetCinematicShadowsEnabledFromUI( CinematicRenderConfig& cinematic, RunSceneState& scene, bool enabled )
{
    // Shadow maps are configured next to the cinematic controls because the
    // original implementation grew from that renderer work, but the depth pass
    // now feeds normal rendering too. Toggling shadows from either the Options
    // tab or the Cine tab must therefore only touch the shadow flag and scene
    // override bits; it must not silently enable the HDR/post-processing stack.
    cinematic.shadowsEnabled = enabled;
    scene.cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    scene.uiCinematicOverrideMask |= SCENE_CINE_SHADOWS;
}

void ApplyOrdinaryRenderUIParam( OrdinaryRenderConfig& ordinary, UIRenderParam param, float rawValue )
{
    switch ( param )
    {
    case UIRenderParam::SunIntensity:
        ordinary.sunIntensity = std::clamp( rawValue, 0.0f, 4.0f );
        break;
    case UIRenderParam::SunRed:
        ordinary.sunColorR = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunGreen:
        ordinary.sunColorG = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunBlue:
        ordinary.sunColorB = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::AmbientStrength:
        ordinary.ambientStrength = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyRed:
        ordinary.skyAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyGreen:
        ordinary.skyAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyBlue:
        ordinary.skyAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundRed:
        ordinary.groundAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundGreen:
        ordinary.groundAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundBlue:
        ordinary.groundAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::ShadowStrength:
        ordinary.shadowStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::ShadowSoftness:
        ordinary.shadowSoftness = std::clamp( rawValue, 0.25f, 4.0f );
        break;
    case UIRenderParam::ShadowDepthBias:
        ordinary.shadowDepthBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::ShadowSlopeBias:
        ordinary.shadowSlopeBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::WaterRed:
        ordinary.waterTintR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterGreen:
        ordinary.waterTintG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterBlue:
        ordinary.waterTintB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterAlpha:
        ordinary.waterAlpha = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterReflection:
        ordinary.waterReflectionStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterFresnel:
        ordinary.waterFresnelF0 = std::clamp( rawValue, 0.0f, 0.12f );
        break;
    case UIRenderParam::BallRoughness:
        ordinary.ballRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BallSpecular:
        ordinary.ballSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::BoxRoughness:
        ordinary.boxRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BoxSpecular:
        ordinary.boxSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    default:
        break;
    }
}


void ToggleCinematicUIFeature( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicFeature feature )
{
    // Feature toggles are boolean pass switches: sky on/off, bloom on/off, etc.
    // Each toggle also marks the matching override bit for scene persistence.
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        cinematic.skyAtmosphereEnabled = !cinematic.skyAtmosphereEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ATMOSPHERE;
        break;
    case UICinematicFeature::Clouds:
        cinematic.cloudsEnabled = !cinematic.cloudsEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUDS;
        break;
    case UICinematicFeature::GodRays:
        cinematic.godRaysEnabled = !cinematic.godRaysEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_GOD_RAYS;
        break;
    case UICinematicFeature::VolumetricLight:
        cinematic.volumetricLightingEnabled = !cinematic.volumetricLightingEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_LIGHTING;
        break;
    case UICinematicFeature::Bloom:
        cinematic.bloomEnabled = !cinematic.bloomEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM;
        break;
    case UICinematicFeature::Fog:
        cinematic.fogEnabled = !cinematic.fogEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_FOG;
        break;
    case UICinematicFeature::TerrainRelief:
        cinematic.terrainReliefEnabled = !cinematic.terrainReliefEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF_ENABLED;
        break;
    case UICinematicFeature::Shadows:
        SetCinematicShadowsEnabledFromUI( cinematic, scene, !cinematic.shadowsEnabled );
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIFeature( feature );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}
} // namespace

void Run::StepPhysicsPipelineStage( int direction )
{
    const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    m_debug.physicsDebugFlags |= PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( m_debug.physicsDebugPipelineStageCursor + direction ) % stageCount;
    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }
    m_debug.physicsDebugPipelineStageCursor = nextStage;
}


void Run::TakeInput()
{
    if ( !Input::IsAppFocused() )
    {
        Input::SetSystemCursorVisible( true );
        if ( m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
        }
        ResetReplayScrubber();
        m_replayPrediction.checkboxHovered = false;
        m_replayPrediction.decreaseHovered = false;
        m_replayPrediction.increaseHovered = false;
        m_replayPrediction.horizonHovered = false;
        m_replayPrediction.horizonDragging = false;
        m_replayVelocityEdit.toggleHovered = false;
        m_replayVelocityEdit.keyboardAltWasDown = false;
        m_replayVelocityEdit.dragging = false;
        m_replayVelocityEdit.draggingAngular = false;
        m_replayVelocityEdit.activeAxis = -1;
        m_replayVelocityEdit.hotLinearAxis = -1;
        m_replayVelocityEdit.hotAngularAxis = -1;
        if ( m_replayVelocityEdit.mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
            m_replayVelocityEdit.mouseCaptured = false;
        }
        m_editor.viewportLookActive = false;
        m_editor.altShortcutWasDown = false;
        m_editor.tabShortcutWasDown = false;
        m_editor.tildeShortcutWasDown = false;
        m_editor.placementScaleActive = false;
        m_editor.placementScaleWheelSteps = 0;
        m_editor.gizmoDragActive = false;
        m_editor.gizmoDragIsRotation = false;
        m_editor.gizmoDragIsScale = false;
        m_editor.activeGizmoAxis = -1;
        m_editor.gizmoDragStartAxisT = 0.0f;
        m_editor.gizmoDragStartRotationAngle = 0.0f;
        m_editor.gizmoDragStartPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        m_editor.gizmoDragStartOrientation = IDENTITY_QUATERNION;
        InputController::ResetUnfocusedInput( m_camera, m_leftSceneCycleWasDown, m_rightSceneCycleWasDown );
        m_runtimeInput.ResetEdges();
        InputController::BeginFrame( m_runtimeInput,
                                     BuildRuntimeInputModeState( m_camera, m_editor ),
                                     false,
                                     true,
                                     true );
        m_UI.CancelInputCapture();
        RunUIStressActions();
        return;
    }

    const auto ReplayInspectionActive = [&]() -> bool
    { return m_replayScrubber.inspectionCameraActive || m_replayScrubber.paused || m_replayScrubber.simulationPaused; };
    const auto ReplayInspectionMouseLookActive = [&]() -> bool
    {
        return ReplayInspectionActive() && Input::IsRightMouseDown() && !m_UI.WantsNativeMouseCursor() &&
               !m_UI.BlocksCameraMouse();
    };
    const auto MouseLookOwnsCursor = [&]() -> bool
    {
        if ( m_UI.WantsNativeMouseCursor() || m_UI.BlocksCameraMouse() )
        {
            return false;
        }

        if ( m_editor.editorModeEnabled )
        {
            return m_editor.viewportLookActive;
        }

        if ( ReplayInspectionActive() )
        {
            return ReplayInspectionMouseLookActive();
        }

        return m_camera.isFlyMode;
    };
    const auto ShouldHideNativeCursor = [&]() -> bool
    {
        if ( MouseLookOwnsCursor() )
        {
            return true;
        }

        return m_editor.editorModeEnabled && m_editor.placementModeEnabled && m_editor.placementPreviewVisible &&
               !m_UI.WantsNativeMouseCursor() && !m_UI.BlocksCameraMouse();
    };
    const auto ApplyCursorOwnership = [&]() -> void { Input::SetSystemCursorVisible( !ShouldHideNativeCursor() ); };
    const auto ReleaseMouseToUI = [&]() -> void
    {
        if ( !MouseLookOwnsCursor() )
        {
            ReleaseCapture();
            InputController::ResetMouseLook( m_camera );
        }
    };
    const auto UpdateRuntimeInputModeAfterAction = [&]( RuntimeInputAction action,
                                                        RuntimeInputActionSource source ) -> void
    {
        InputController::ApplyModeAction(
            m_runtimeInput,
            InputController::ResolveMode( BuildRuntimeInputModeState( m_camera, m_editor ) ),
            action,
            source );
    };
    const auto ClearEditorManipulationState = [&]() -> void
    {
        m_editor.placementPreviewVisible = false;
        m_editor.placementScaleActive = false;
        m_editor.placementScaleWheelSteps = 0;
        m_editor.placementScale = EditorDefaultPlacementScale( m_editor.objectType );
        m_editor.placementScaleStart = m_editor.placementScale;
        m_editor.gizmoDragActive = false;
        m_editor.gizmoDragIsRotation = false;
        m_editor.gizmoDragIsScale = false;
        m_editor.activeGizmoAxis = -1;
        m_editor.placementAltitudeSteps = 0;
    };
    const auto ToggleEditorPlacementMode = [&]( RuntimeInputActionSource source ) -> void
    {
        EnterInteractiveSceneRun();
        m_editor.placementModeEnabled = m_editor.editorModeEnabled && !m_editor.placementModeEnabled;
        m_editor.viewportLookActive = false;
        ClearEditorManipulationState();
        ReleaseMouseToUI();
        ApplyCursorOwnership();
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, source );
    };
    const auto EnterFlyModeCamera = [&]() -> void
    {
        // Entering fly mode: generated demo mode snaps to free camera; scene mode stays
        // on the current camera so fly controls work without requiring CAMERA_FREE
        if ( !SceneState().isSceneMode )
        {
            m_systems.cameras->SelectCamera( CAMERA_FREE, false );
        }
        m_camera.cameraTime = 0.0f;
        XZBounds unbounded;
        unbounded.m_xMin = -99999.9f;
        unbounded.m_xMax = 99999.9f;
        unbounded.m_zMin = -99999.9f;
        unbounded.m_zMax = 99999.9f;
        uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
        m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
        if ( ShouldHideNativeCursor() )
        {
            Input::SetSystemCursorVisible( false );
        }
        else
        {
            ReleaseMouseToUI();
            Input::SetSystemCursorVisible( true );
        }
        InputController::ResetMouseLook( m_camera );
    };
    const auto ExitFlyModeCamera = [&]() -> void
    {
        // Exiting fly mode restores terrain bounds, the camera-cycle clock, and
        // the stock Windows cursor.
        uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
        m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
        Input::SetSystemCursorVisible( true );
        m_camera.cameraTime = 0.0f;
        // Exiting fly mode also exits launcher mode.
        m_camera.isLauncherMode = false;
        InputController::ResetMouseLook( m_camera );
    };

    ApplyCursorOwnership();

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera, m_editor ),
                                 true,
                                 UIBlocksKeyboardBeforeInput,
                                 m_UI.BlocksCameraMouse() );
    bool keyboardToggleEditorMode = false;
    if ( !UIBlocksKeyboardBeforeInput )
    {
        keyboardToggleEditorMode =
            InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleEditor, VK_OEM_3 );

        // Toggle fly mode with F (edge-detected so snapshot-loaded fly mode survives the next frame)
        bool prevFlyMode = m_camera.isFlyMode;
        bool keyboardModeAction = false;
        RuntimeInputAction keyboardModeActionName = RuntimeInputAction::None;
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleFlyCamera, 'F' ) )
        {
            m_camera.isFlyMode = !m_camera.isFlyMode;
            m_camera.isLauncherMode = false; // F-key fly never implies launcher mode.
            keyboardModeAction = true;
            keyboardModeActionName = RuntimeInputAction::ToggleFlyCamera;
        }

        // N key: toggle launcher mode with live simulation (edge-detected).
        // Entering also enters fly mode; exiting also exits fly mode.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleLauncher,
                                                              'N' ) )
            {
                m_camera.isLauncherMode = !m_camera.isLauncherMode;
                m_camera.isFlyMode = m_camera.isLauncherMode;
                keyboardModeAction = true;
                keyboardModeActionName = RuntimeInputAction::ToggleLauncher;
            }
        }

        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CycleLauncherFireMode,
                                                              'M' ) &&
                 m_camera.isLauncherMode )
            {
                m_rayCastTest.fireMode = m_rayCastTest.fireMode == RunLauncherFireMode::Laser
                                             ? RunLauncherFireMode::Projectile
                                             : RunLauncherFireMode::Laser;
            }
        }

#ifdef _DEBUG
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::WriteLauncherReproSnapshot,
                                                              VK_RETURN ) &&
                 m_camera.isLauncherMode && !m_replayScrubber.restoreConsumedThisFrame )
            {
                WriteLauncherReproSnapshot();
            }
        }
#endif

        if ( m_editor.editorModeEnabled )
        {
            m_camera.isFlyMode = true;
            m_camera.isLauncherMode = false;
            m_replayVelocityEdit.keyboardAltWasDown = Input::IsKeyDown( VK_MENU );
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleEditorTool,
                                                              VK_MENU ) )
            {
                ToggleEditorPlacementMode( RuntimeInputActionSource::Keyboard );
            }
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CycleEditorPlacementType,
                                                              VK_TAB ) )
            {
                if ( Input::IsKeyDown( VK_CONTROL ) )
                {
                    EnterInteractiveSceneRun();
                    m_editor.placeStaticObject = !m_editor.placeStaticObject;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorStaticPlacement,
                                                       RuntimeInputActionSource::Keyboard );
                }
                else
                {
                    EnterInteractiveSceneRun();
                    m_editor.objectType = ( m_editor.objectType + 1 ) % UI::EditorTab::OBJECT_TYPE_COUNT;
                    ClearEditorManipulationState();
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CycleEditorPlacementType,
                                                       RuntimeInputActionSource::Keyboard );
                }
            }
        }
        else
        {
            const bool altDown = Input::IsKeyDown( VK_MENU );
            if ( altDown && !m_replayVelocityEdit.keyboardAltWasDown )
            {
                SetReplayVelocityEditEnabled( !m_replayVelocityEdit.enabled );
            }
            m_replayVelocityEdit.keyboardAltWasDown = altDown;
            m_runtimeInput.SetActionDown( RuntimeInputAction::ToggleEditorTool, altDown );
            m_runtimeInput.SetActionDown( RuntimeInputAction::CycleEditorPlacementType, Input::IsKeyDown( VK_TAB ) );
            m_editor.altShortcutWasDown = altDown;
            m_editor.tabShortcutWasDown = Input::IsKeyDown( VK_TAB );
        }

        if ( m_camera.isFlyMode != prevFlyMode )
        {
            if ( m_camera.isFlyMode )
            {
                EnterFlyModeCamera();
            }
            else
            {
                ExitFlyModeCamera();
            }
        }
        if ( keyboardModeAction )
        {
            UpdateRuntimeInputModeAfterAction( keyboardModeActionName, RuntimeInputActionSource::Keyboard );
        }

        // Water m_shader debug toggles
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleWaterFreeze, '1' ) )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        // Key '2' cycles water reflection modes in a predictable loop:
        // FBO mirror rendering, then DXR raytraced reflection when supported,
        // then no reflection, then back to FBO. Machines without DXR skip the
        // unsupported mode instead of leaving the toggle in a dead state.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CycleWaterReflection,
                                                              '2' ) )
            {
                if ( !m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect )
                {
                    if ( Gfx().GetCapabilities().supportsDxrReflection )
                    {
                        m_debug.isWaterRTReflect = true;
                    }
                    else
                    {
                        m_debug.isWaterNoReflect = true;
                    }
                }
                else if ( m_debug.isWaterRTReflect )
                {
                    m_debug.isWaterRTReflect = false;
                    m_debug.isWaterNoReflect = true;
                }
                else
                {
                    m_debug.isWaterNoReflect = false;
                }
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleWaterFlat,
                                                              '3' ) )
            {
                m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleTerrainHidden,
                                                              '4' ) )
            {
                m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleWaterHidden,
                                                              '5' ) )
            {
                m_debug.isWaterHidden = !m_debug.isWaterHidden;
            }
        }
        // V key: collision visualizer for balls and boxes as solid debug colours.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleCollisionVisualizer,
                                                              'V' ) )
            {
                m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            }
        }

        // C key: cycle physics debug overlay - None -> Axes -> Contacts -> Sleep -> All -> None.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CyclePhysicsDebugOverlay,
                                                              'C' ) )
            {
                switch ( m_debug.physicsDebugFlags )
                {
                case PHYSICS_DEBUG_NONE:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_AXES;
                    break;
                case PHYSICS_DEBUG_AXES:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
                    break;
                case PHYSICS_DEBUG_CONTACTS:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_SLEEP;
                    break;
                case PHYSICS_DEBUG_SLEEP:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_ALL;
                    break;
                default:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
                    break;
                }
            }
        }

        // O key: toggle the terrain polygon/contact probe. It is independent of
        // the C-key debug cycle so it can be layered over any other physics view.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleTerrainContactProbe,
                                                              'O' ) )
            {
                m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            }
        }

        // F7/F8: step the physics pipeline visualizer through the bounded Catto
        // stage trace from the most recent physics tick. The simulation can be
        // paused with fly mode and advanced separately with Space.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::StepPhysicsPipelinePrevious,
                                                              VK_F7 ) )
            {
                StepPhysicsPipelineStage( -1 );
            }
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::StepPhysicsPipelineNext,
                                                              VK_F8 ) )
            {
                StepPhysicsPipelineStage( 1 );
            }
        }

        // 6 key: translucent debug collision volumes for inspecting axes/contact rows inside bodies.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::TogglePhysicsDebugTransparent,
                                                              '6' ) )
            {
                m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            }
        }

        // Q key used to cycle legacy renderers; it now reports that DX12 is the only runtime renderer.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ReportRendererRuntimeRetired,
                                                              'Q' ) )
            {
                fprintf( stderr, "Renderer switch ignored: DX12 is the only runtime renderer.\n" );
            }
        }

        // G key: toggle broadphase overlay, or cycle tracked ball if overlay is off.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::ToggleBroadphaseOverlay,
                                                          'G' ) )
        {
            if ( SceneState().isSceneMode && m_camera.trackBallIndex >= 0 && !m_debug.isBroadphaseOverlay )
            {
                int count = m_cGameModelCollection.GetModelCount();
                if ( count > 0 )
                {
                    m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
                }
            }
            else
            {
                m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            }
        }

        // 0 key: toggle the in-game diagnostics window. Tabs replace the old overlay cycle.
        // Edge-detected in both scene and generated demo modes; one toggle per keypress.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleUIVisibility,
                                                              '0' ) )
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( m_timers.simulationTimer.GetTotalTime() );
                m_debug.overlayMode = OverlayMode::None;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleUIVisibility,
                                                   RuntimeInputActionSource::Keyboard );
            }
        }

        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::NavigateScenePrevious,
                                                          VK_LEFT ) )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( -1 ) )
            {
                LoadAdjacentSceneFromBrowser( -1 );
            }
        }
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::NavigateSceneNext,
                                                          VK_RIGHT ) )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( 1 ) )
            {
                LoadAdjacentSceneFromBrowser( 1 );
            }
        }
    }
    else
    {
        AdvanceTakeInputKeyboardActionMemories( m_runtimeInput );
        m_leftSceneCycleWasDown = Input::IsKeyDown( VK_LEFT );
        m_rightSceneCycleWasDown = Input::IsKeyDown( VK_RIGHT );
        m_replayVelocityEdit.keyboardAltWasDown = Input::IsKeyDown( VK_MENU );
        m_editor.altShortcutWasDown = Input::IsKeyDown( VK_MENU );
        m_editor.tabShortcutWasDown = Input::IsKeyDown( VK_TAB );
        m_editor.tildeShortcutWasDown = Input::IsKeyDown( VK_OEM_3 );
    }

    bool suppressWorldActionThisFrame = UIBlocksKeyboardBeforeInput;
    int editorUnhandledWheelDelta = 0;
    if ( m_systems.window )
    {
        const int selectedSceneBrowserIndex = CurrentSceneBrowserIndex();
        InGameUIInputResult UIResult =
            m_UI.UpdateInput( m_systems.window->m_sWindow,
                              static_cast<int>( m_systems.window->m_sWindowDimensions.x ),
                              static_cast<int>( m_systems.window->m_sWindowDimensions.y ),
                              m_timers.simulationTimer.GetTotalTime(),
                              m_editor.editorModeEnabled,
                              m_editor.placementModeEnabled,
                              m_editor.placeStaticObject,
                              m_editor.autoTerrainAlign,
                              m_editor.objectType,
                              m_sceneBrowserNamePtrs.empty() ? nullptr : m_sceneBrowserNamePtrs.data(),
                              static_cast<int>( m_sceneBrowserNamePtrs.size() ),
                              selectedSceneBrowserIndex );
        editorUnhandledWheelDelta = UIResult.unhandledWheelDelta;
        const InGameUICommands& uiCommands = UIResult.commands;
        if ( uiCommands.ui.userInteracted )
        {
            EnterInteractiveSceneRun();
        }
        suppressWorldActionThisFrame =
            suppressWorldActionThisFrame || uiCommands.ui.userInteracted || m_UI.BlocksCameraMouse();
        const bool replayScrubberOwnsMouse =
            TickReplayScrubberInput( m_systems.window->m_sWindow, m_UI.BlocksCameraMouse() );
        const bool replayCauseTreeOwnsMouse =
            TickReplayCauseTreeInput( m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse );
        const bool replayVelocityEditOwnsMouse = TickReplayVelocityEditInput(
            m_systems.window->m_sWindow,
            m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse );
        suppressWorldActionThisFrame = suppressWorldActionThisFrame || replayScrubberOwnsMouse ||
                                       replayCauseTreeOwnsMouse || replayVelocityEditOwnsMouse;
        m_runtimeInput.BeginFrame( true,
                                   m_UI.BlocksKeyboard(),
                                   m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse ||
                                       replayVelocityEditOwnsMouse );

        // ESC flicks the diagnostics window between minimized and expanded, with
        // a very fast double-tap escape hatch for quitting interactive runs.
        // Run it after UI input processing so focused controls keep their local ESC
        // behavior first, such as closing the scene filter combo without also
        // hiding the whole diagnostics surface on the same frame.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::DismissOrExitUI,
                                                          VK_ESCAPE ) &&
             !uiCommands.ui.userInteracted )
        {
            constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
            const double UINow = m_timers.simulationTimer.GetTotalTime();
            if ( UINow - m_lastEscapeTapTime <= ESC_QUICK_EXIT_SECONDS )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( UINow );
                m_debug.overlayMode = OverlayMode::None;
                m_lastEscapeTapTime = UINow;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
        }

        if ( uiCommands.renderer.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleVsync, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.requestedObjectType >= 0 )
        {
            const int requestedObjectType =
                std::clamp( uiCommands.editor.requestedObjectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
            if ( requestedObjectType != m_editor.objectType )
            {
                m_editor.objectType = requestedObjectType;
                ClearEditorManipulationState();
            }
            else if ( uiCommands.editor.enterPlacementMode )
            {
                ClearEditorManipulationState();
            }
            if ( uiCommands.editor.enterPlacementMode && m_editor.editorModeEnabled )
            {
                EnterInteractiveSceneRun();
                m_editor.placementModeEnabled = true;
                m_editor.viewportLookActive = false;
                ReleaseMouseToUI();
                ApplyCursorOwnership();
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, RuntimeInputActionSource::UI );
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CycleEditorPlacementType,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.toggleEditorMode || keyboardToggleEditorMode )
        {
            EnterInteractiveSceneRun();
            const RuntimeInputActionSource toggleEditorSource =
                keyboardToggleEditorMode ? RuntimeInputActionSource::Keyboard : RuntimeInputActionSource::UI;
            m_editor.editorModeEnabled = !m_editor.editorModeEnabled;
            if ( m_editor.editorModeEnabled )
            {
                const bool wasFlyMode = m_camera.isFlyMode;
                m_editor.placementModeEnabled = true;
                m_editor.viewportLookActive = false;
                ClearEditorManipulationState();
                m_editor.restoreFlyModeAfterEditor = m_camera.isFlyMode;
                m_editor.restoreRayTestModeAfterEditor = m_camera.isLauncherMode;
                m_camera.isFlyMode = true;
                m_camera.isLauncherMode = false;
                if ( !wasFlyMode )
                {
                    EnterFlyModeCamera();
                }
                else
                {
                    InputController::ResetMouseLook( m_camera );
                }
                ApplyCursorOwnership();
            }
            else
            {
                const bool wasFlyMode = m_camera.isFlyMode;
                m_editor.viewportLookActive = false;
                m_editor.placementPreviewVisible = false;
                m_editor.placementModeEnabled = false;
                m_editor.gizmoDragActive = false;
                m_editor.gizmoDragIsRotation = false;
                m_editor.gizmoDragIsScale = false;
                m_editor.activeGizmoAxis = -1;
                m_editor.placementScaleActive = false;
                m_editor.placementScaleWheelSteps = 0;
                m_editor.placementScale = EditorDefaultPlacementScale( m_editor.objectType );
                m_editor.placementScaleStart = m_editor.placementScale;
                m_editor.placementAltitudeSteps = 0;
                m_camera.isFlyMode = m_editor.restoreFlyModeAfterEditor || m_editor.restoreRayTestModeAfterEditor;
                m_camera.isLauncherMode = m_editor.restoreRayTestModeAfterEditor;
                m_editor.restoreFlyModeAfterEditor = false;
                m_editor.restoreRayTestModeAfterEditor = false;
                if ( wasFlyMode && !m_camera.isFlyMode )
                {
                    ExitFlyModeCamera();
                }
                else
                {
                    InputController::ResetMouseLook( m_camera );
                }
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditor, toggleEditorSource );
        }
        if ( uiCommands.editor.togglePlacementMode )
        {
            ToggleEditorPlacementMode( RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.togglePlaceStatic )
        {
            EnterInteractiveSceneRun();
            m_editor.placeStaticObject = !m_editor.placeStaticObject;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorStaticPlacement,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.toggleTerrainAlign )
        {
            EnterInteractiveSceneRun();
            m_editor.autoTerrainAlign = !m_editor.autoTerrainAlign;
            m_editor.placementPreviewVisible = false;
            m_editor.placementScaleActive = false;
            m_editor.placementScaleWheelSteps = 0;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTerrainAlign,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleCollisionVisualizer )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCollisionVisualizer,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsSleepPolicy )
        {
            m_runtimeSettings.isPhysicsSleepEnabled = !m_runtimeSettings.isPhysicsSleepEnabled;
            m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsSleepPolicy,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsDebugFlags != 0 )
        {
            m_debug.physicsDebugFlags ^= ( uiCommands.physics.togglePhysicsDebugFlags & PHYSICS_DEBUG_ALL );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsDebugFlags,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelinePrevious )
        {
            StepPhysicsPipelineStage( -1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelinePrevious,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelineNext )
        {
            StepPhysicsPipelineStage( 1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelineNext,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsDebugTransparent )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsDebugTransparent,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleBroadphaseOverlay )
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleBroadphaseOverlay,
                                               RuntimeInputActionSource::UI );
        }
        bool tornadoFieldChanged = false;
        if ( uiCommands.physics.toggleTornado )
        {
            m_runtimeSettings.tornadoField.enabled = !m_runtimeSettings.tornadoField.enabled;
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornado, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleTornadoFieldVectors )
        {
            m_runtimeSettings.tornadoField.visualizeVelocityField =
                !m_runtimeSettings.tornadoField.visualizeVelocityField;
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornadoFieldVectors,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleRayCastVisualization )
        {
            m_rayCastTest.visualizeRays = !m_rayCastTest.visualizeRays;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRayCastVisualization,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoRadius )
        {
            m_runtimeSettings.tornadoField.radius =
                std::clamp( uiCommands.physics.requestedTornadoRadius, UI_TORNADO_RADIUS_MIN, UI_TORNADO_RADIUS_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoHeight )
        {
            m_runtimeSettings.tornadoField.height =
                std::clamp( uiCommands.physics.requestedTornadoHeight, UI_TORNADO_HEIGHT_MIN, UI_TORNADO_HEIGHT_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoInward )
        {
            m_runtimeSettings.tornadoField.inwardAcceleration =
                std::clamp( uiCommands.physics.requestedTornadoInward, UI_TORNADO_INWARD_MIN, UI_TORNADO_INWARD_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoSwirl )
        {
            m_runtimeSettings.tornadoField.swirlAcceleration =
                std::clamp( uiCommands.physics.requestedTornadoSwirl, UI_TORNADO_SWIRL_MIN, UI_TORNADO_SWIRL_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoLift )
        {
            m_runtimeSettings.tornadoField.liftAcceleration =
                std::clamp( uiCommands.physics.requestedTornadoLift, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( tornadoFieldChanged )
        {
            SyncTornadoFieldToPhysics();
        }
        if ( uiCommands.physics.toggleTerrainContactProbe )
        {
            m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTerrainContactProbe,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleTextOnly )
        {
            m_debug.isTextOnly = !m_debug.isTextOnly;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTextOnly, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleFixedStep )
        {
            SceneState().isFixedStep = !SceneState().isFixedStep;
            m_simulation.Reset();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleFixedStep, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleTerrainHidden )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTerrainHidden, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterHidden )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterHidden, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterFreeze )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterFreeze, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterFlat )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterFlat, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleShadows )
        {
            if ( IsCinematicRenderingEnabled() )
            {
                const bool shadowsActive = ActiveCinematicConfig().shadowsEnabled;
                m_cmdHasCinematicShadowsOverride = false;
                SetCinematicShadowsEnabledFromUI( ActiveCinematicConfig(), SceneState(), !shadowsActive );
            }
            else
            {
                Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.toggleShadows )
        {
            Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRenderShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.saveDefaults )
        {
            SaveRenderDefaults();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveRenderDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.requestedParam != UIRenderParam::None )
        {
            ApplyOrdinaryRenderUIParam( Cfg().ordinaryRender,
                                        uiCommands.renderTuning.requestedParam,
                                        uiCommands.renderTuning.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyRenderTuning, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.toggleWaterReflection )
        {
            if ( m_debug.isWaterNoReflect )
            {
                m_debug.isWaterNoReflect = false;
            }
            else
            {
                m_debug.isWaterNoReflect = true;
                m_debug.isWaterRTReflect = false;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterReflection,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.requestedWaterReflectionMode >= 0 )
        {
            const int mode = std::clamp( uiCommands.water.requestedWaterReflectionMode, 0, 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWaterReflectionMode,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.requestedTimeScale > 0.0f )
        {
            m_UITimeScaleOverride = std::clamp( uiCommands.sceneOptions.requestedTimeScale, 0.10f, 10.00f );
            SceneState().timeScale = m_UITimeScaleOverride;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetTimeScale, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSeed > 0 )
        {
            SceneState().rngSeed = static_cast<unsigned int>( std::clamp( uiCommands.run.requestedSeed, 1, 999999 ) );
            SceneState().rngState = SceneState().rngSeed;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetRunSeed, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestedPhysicsDebugAlpha >= 0.0f )
        {
            m_debug.physicsDebugAlpha = std::clamp( uiCommands.physics.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetPhysicsDebugAlpha, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestedPhysicsDebugContactLinger >= 0.0f )
        {
            m_debug.physicsDebugContactLinger =
                std::clamp( uiCommands.physics.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetPhysicsDebugContactLinger,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestRayCastImpulseStrength )
        {
            m_rayCastTest.impulseStrength = std::clamp( uiCommands.physics.requestedRayCastImpulseStrength,
                                                        UI_RAY_IMPULSE_MIN,
                                                        UI_RAY_IMPULSE_MAX );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetRayCastImpulseStrength,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestLauncherProjectileSpeed )
        {
            m_rayCastTest.projectileSpeed = std::clamp( uiCommands.physics.requestedLauncherProjectileSpeed,
                                                        UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                        UI_LAUNCHER_PROJECTILE_SPEED_MAX );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetLauncherProjectileSpeed,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.requestedModelCount >= 0 )
        {
            ApplyUIModelCountOverride( uiCommands.sceneOptions.requestedModelCount );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetModelCount, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.profiler.requestedWorkerThreads >= -1 )
        {
            ApplyWorkerThreadCountOverride( uiCommands.profiler.requestedWorkerThreads );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWorkerThreads, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBallCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int boxes =
                m_UISolverBoxCountOverride >= 0 ? m_UISolverBoxCountOverride : SceneState().solverBoxCount;
            ApplyUISolverObjectCounts(
                std::clamp( uiCommands.run.requestedSolverBallCount, 0, (std::max)( 0, modelCapacity - boxes ) ),
                boxes );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBoxCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int balls =
                m_UISolverBallCountOverride >= 0 ? m_UISolverBallCountOverride : SceneState().solverBallCount;
            ApplyUISolverObjectCounts(
                balls,
                std::clamp( uiCommands.run.requestedSolverBoxCount, 0, (std::max)( 0, modelCapacity - balls ) ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.requestWorldGravity || uiCommands.water.requestWorldFluidHeight ||
             uiCommands.water.requestWorldFluidDensity )
        {
            const float gravity = uiCommands.water.requestWorldGravity ? uiCommands.water.requestedWorldGravity
                                                                       : m_cWorldEnvironment.GetGravity();
            const float fluidHeight = uiCommands.water.requestWorldFluidHeight
                                          ? uiCommands.water.requestedWorldFluidHeight
                                          : m_cWorldEnvironment.GetFluidSurfaceHeight();
            const float fluidDensity = uiCommands.water.requestWorldFluidDensity
                                           ? uiCommands.water.requestedWorldFluidDensity
                                           : m_cWorldEnvironment.GetFluidDensity();
            ApplyUIWorldOverride( std::clamp( gravity, -100.0f, 0.0f ),
                                  std::clamp( fluidHeight, -100.0f, 200.0f ),
                                  std::clamp( fluidDensity, 0.0f, 5.0f ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyWorldWaterSettings,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.toggleRendering )
        {
            // Master Cine switch. Clearing m_cmdHasCinematicRenderingOverride lets
            // the runtime toggle become the new source of truth after launch.
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            const bool currentlyEnabled =
                m_cmdHasCinematicRenderingOverride ? m_cmdCinematicRendering : cinematic.enabled;
            cinematic.enabled = !currentlyEnabled;
            m_cmdHasCinematicRenderingOverride = false;
            if ( SceneState().isSceneMode )
            {
                SceneState().hasCinematicRenderingOverride = true;
                SceneState().isCinematicRenderingEnabled = cinematic.enabled;
                SceneState().cinematicOverrideMask |= SCENE_CINE_RENDERING;
                SceneState().uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCinematicRendering,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedModeSceneIndex >= -1 )
        {
            ApplyCinematicModeFromBrowserIndex( uiCommands.cinematic.requestedModeSceneIndex );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectCinematicScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedFeature != UICinematicFeature::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            if ( uiCommands.cinematic.requestedFeature == UICinematicFeature::Shadows )
            {
                m_cmdHasCinematicShadowsOverride = false;
            }
            ToggleCinematicUIFeature( cinematic, SceneState(), uiCommands.cinematic.requestedFeature );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCinematicFeature,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedParam != UICinematicParam::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            ApplyCinematicUIParam( cinematic,
                                   SceneState(),
                                   uiCommands.cinematic.requestedParam,
                                   uiCommands.cinematic.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyCinematicParam, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.resetScene )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ResetScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.resetSceneDefaults )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( false, true, false );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ResetSceneDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.requestDemoScene )
        {
            LoadDemoSceneFromUI();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::LoadDemoScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.saveSceneDefaults )
        {
            SaveCurrentSceneDefaults();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveSceneDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.createScene )
        {
            CreateSceneFromUI( uiCommands.scene.requestedSceneName );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CreateScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.requestedSceneIndex >= 0 )
        {
            LoadSceneFromBrowserIndex( uiCommands.scene.requestedSceneIndex );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectScene, RuntimeInputActionSource::UI );
        }

        RunUIStressActions();

        const bool editorViewportLookNow =
            m_editor.editorModeEnabled && Input::IsRightMouseDown() && !m_UI.BlocksCameraMouse();
        if ( editorViewportLookNow != m_editor.viewportLookActive )
        {
            InputController::ResetMouseLook( m_camera );
        }
        m_editor.viewportLookActive = editorViewportLookNow;
        if ( editorViewportLookNow != ( m_runtimeInput.CurrentMode() == RuntimeInputMode::EditorViewportLook ) )
        {
            UpdateRuntimeInputModeAfterAction( editorViewportLookNow ? RuntimeInputAction::BeginEditorViewportLook
                                                                     : RuntimeInputAction::EndEditorViewportLook,
                                               RuntimeInputActionSource::Mouse );
        }

        const int placementWheelSteps = EditorMouseWheelSteps( editorUnhandledWheelDelta );
        const bool placementLeftMouseNow = Input::IsLeftMouseDown();
        if ( m_editor.placementScaleActive && placementLeftMouseNow && !m_editor.viewportLookActive &&
             !m_UI.BlocksCameraMouse() )
        {
            if ( placementWheelSteps != 0 )
            {
                EnterInteractiveSceneRun();
                m_editor.placementScaleWheelSteps += placementWheelSteps;
            }

            const POINT currentClient = Input::GetClientMouseCoordinates();
            const float dragPixelsX = static_cast<float>( currentClient.x - m_editor.placementScaleStartClient.x );
            const float dragPixelsY = static_cast<float>( currentClient.y - m_editor.placementScaleStartClient.y );
            m_editor.placementScale = EditorPlacementScaleFromGesture( m_editor.objectType,
                                                                       m_editor.placementScaleStart,
                                                                       dragPixelsX,
                                                                       dragPixelsY,
                                                                       m_editor.placementScaleWheelSteps );
        }
        else if ( placementWheelSteps != 0 && m_editor.editorModeEnabled && m_editor.placementModeEnabled &&
                  !m_editor.viewportLookActive && !m_UI.BlocksCameraMouse() )
        {
            const int nextAltitudeSteps = (std::max)( 0, m_editor.placementAltitudeSteps + placementWheelSteps );
            if ( nextAltitudeSteps != m_editor.placementAltitudeSteps )
            {
                EnterInteractiveSceneRun();
                m_editor.placementAltitudeSteps = nextAltitudeSteps;
            }
        }
        ApplyCursorOwnership();
    }

    UpdateEditorInteractionPreview();

    // Editor and launcher actions share world clicks. UI hover/capture
    // suppresses both so panel interaction never mutates the scene.
    {
        const RuntimeMouseEdges mouseEdges =
            m_runtimeInput.CaptureMouseButtons( Input::IsLeftMouseDown(), Input::IsRightMouseDown() );
        const bool leftMouseNow = mouseEdges.leftDown;
        const bool leftPressed = mouseEdges.leftPressed;
        const bool leftReleased = mouseEdges.leftReleased;
        bool consumedWorldClick = false;

        if ( m_editor.placementScaleActive )
        {
            consumedWorldClick = true;
            if ( leftReleased || suppressWorldActionThisFrame )
            {
                if ( leftReleased && !suppressWorldActionThisFrame && m_editor.placementPreviewVisible )
                {
                    const int previousModelCount = m_cGameModelCollection.GetModelCount();
                    PlaceEditorObjectAtTerrainPoint( m_editor.objectType,
                                                     m_editor.placeStaticObject,
                                                     m_editor.placementTerrainPoint );
                    if ( m_cGameModelCollection.GetModelCount() > previousModelCount )
                    {
                        m_editor.selectedModelIndex = m_cGameModelCollection.GetModelCount() - 1;
                    }
                }
                m_editor.placementScaleActive = false;
                m_editor.placementScaleWheelSteps = 0;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::EndEditorPlacementScale,
                                                   RuntimeInputActionSource::Mouse );
            }
        }

        if ( !consumedWorldClick && m_editor.gizmoDragActive )
        {
            consumedWorldClick = true;
            if ( leftMouseNow && !suppressWorldActionThisFrame )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
                {
                    if ( m_editor.gizmoDragIsScale )
                    {
                        ScaleSelectedEditorObjectAlongAxis( rayOrigin, rayDirection );
                    }
                    else if ( m_editor.gizmoDragIsRotation )
                    {
                        RotateSelectedEditorObjectAroundAxis( rayOrigin, rayDirection );
                    }
                    else
                    {
                        MoveSelectedEditorObjectAlongAxis( rayOrigin, rayDirection );
                    }
                }
            }
            if ( leftReleased || suppressWorldActionThisFrame )
            {
                m_editor.gizmoDragActive = false;
                m_editor.gizmoDragIsRotation = false;
                m_editor.gizmoDragIsScale = false;
                m_editor.activeGizmoAxis = -1;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::EndEditorGizmoDrag,
                                                   RuntimeInputActionSource::Mouse );
            }
        }

        if ( !consumedWorldClick && leftPressed && !suppressWorldActionThisFrame )
        {
            const bool editorScaleMode =
                m_editor.editorModeEnabled && !m_editor.placementModeEnabled && Input::IsKeyDown( VK_CONTROL );
            if ( editorScaleMode && m_editor.selectedModelIndex >= 0 &&
                 m_editor.selectedModelIndex < m_cGameModelCollection.GetModelCount() && m_editor.hotGizmoAxis >= 0 )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                float axisT = 0.0f;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                     TryEditorAxisRayParameter( m_editor.hotGizmoAxis, rayOrigin, rayDirection, axisT ) )
                {
                    EnterInteractiveSceneRun();
                    GameModel& model = m_cGameModelCollection.GetModelAtIndex( m_editor.selectedModelIndex );
                    m_editor.gizmoDragActive = true;
                    m_editor.gizmoDragIsRotation = false;
                    m_editor.gizmoDragIsScale = true;
                    m_editor.activeGizmoAxis = m_editor.hotGizmoAxis;
                    m_editor.gizmoDragStartAxisT = axisT;
                    m_editor.gizmoDragStartShape = model.GetCollisionShape();
                    consumedWorldClick = true;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoScale,
                                                       RuntimeInputActionSource::Mouse );
                }
            }

            if ( m_editor.editorModeEnabled && !m_editor.placementModeEnabled && !editorScaleMode &&
                 m_editor.selectedModelIndex >= 0 && m_editor.hotRotationAxis >= 0 )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                float startAngle = 0.0f;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                     TryEditorRotationRayAngle( m_editor.hotRotationAxis, rayOrigin, rayDirection, startAngle ) )
                {
                    EnterInteractiveSceneRun();
                    m_editor.gizmoDragActive = true;
                    m_editor.gizmoDragIsRotation = true;
                    m_editor.gizmoDragIsScale = false;
                    m_editor.activeGizmoAxis = m_editor.hotRotationAxis;
                    m_editor.gizmoDragStartRotationAngle = startAngle;
                    m_editor.gizmoDragStartOrientation =
                        m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )]
                            .GetOrientation();
                    consumedWorldClick = true;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoRotate,
                                                       RuntimeInputActionSource::Mouse );
                }
            }

            if ( !consumedWorldClick && m_editor.editorModeEnabled && !m_editor.placementModeEnabled &&
                 !editorScaleMode && m_editor.selectedModelIndex >= 0 && m_editor.hotGizmoAxis >= 0 )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                float axisT = 0.0f;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                     TryEditorAxisRayParameter( m_editor.hotGizmoAxis, rayOrigin, rayDirection, axisT ) )
                {
                    EnterInteractiveSceneRun();
                    m_editor.gizmoDragActive = true;
                    m_editor.gizmoDragIsRotation = false;
                    m_editor.gizmoDragIsScale = false;
                    m_editor.activeGizmoAxis = m_editor.hotGizmoAxis;
                    m_editor.gizmoDragStartAxisT = axisT;
                    m_editor.gizmoDragStartPosition =
                        m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )]
                            .GetPosition();
                    consumedWorldClick = true;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoTranslate,
                                                       RuntimeInputActionSource::Mouse );
                }
            }

            if ( !consumedWorldClick && m_editor.editorModeEnabled )
            {
                if ( m_editor.placementModeEnabled )
                {
                    if ( m_editor.placementPreviewVisible )
                    {
                        m_editor.placementScaleActive = true;
                        m_editor.placementScaleWheelSteps = 0;
                        m_editor.placementScaleStart =
                            EditorClampPlacementScale( m_editor.objectType, m_editor.placementScale );
                        m_editor.placementScale = m_editor.placementScaleStart;
                        m_editor.placementScaleStartClient = Input::GetClientMouseCoordinates();
                        m_editor.placementScaleTerrainPoint = m_editor.placementTerrainPoint;
                        m_editor.placementScaleRayOrigin = m_editor.placementRayOrigin;
                        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorPlacementScale,
                                                           RuntimeInputActionSource::Mouse );
                    }
                }
                else
                {
                    Vector3 rayOrigin;
                    Vector3 rayDirection;
                    int pickedIndex = -1;
                    if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                         TryPickEditorModel( rayOrigin, rayDirection, pickedIndex ) )
                    {
                        m_editor.selectedModelIndex = pickedIndex;
                    }
                    else
                    {
                        m_editor.selectedModelIndex = -1;
                    }
                }
                consumedWorldClick = true;
            }
        }

        if ( !consumedWorldClick && leftPressed && !suppressWorldActionThisFrame && !m_editor.editorModeEnabled &&
             !m_UI.WantsNativeMouseCursor() && ( Input::IsKeyDown( VK_CONTROL ) || !m_camera.isLauncherMode ) )
        {
            const bool additiveReplayPick = Input::IsKeyDown( VK_SHIFT );
            TryPickReplayPathTargetFromMouse( additiveReplayPick, !additiveReplayPick );
            consumedWorldClick = true;
        }

        if ( !consumedWorldClick && m_camera.isLauncherMode && leftPressed && !suppressWorldActionThisFrame &&
             !m_UI.WantsNativeMouseCursor() )
        {
            EnterInteractiveSceneRun();
            FireRayCastTest();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::FireLauncher, RuntimeInputActionSource::Mouse );
        }
    }

    if ( m_UI.BlocksKeyboard() )
    {
        AdvanceTakeInputKeyboardActionMemories( m_runtimeInput );
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        ApplyCursorOwnership();
        return;
    }

    // F2: Save scene snapshot to Scenes/
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::SaveSceneSnapshot,
                                                          VK_F2 ) )
        {
            static int sSnapshotSeq = 0;
            char path[256] = {};
            if ( RuntimeFileWriter::NextNumberedPath( path,
                                                      sizeof( path ),
                                                      "Scenes",
                                                      "snapshot_",
                                                      ".scene.json",
                                                      sSnapshotSeq,
                                                      100 ) )
            {
                m_cGameModelCollection.SaveSceneSnapshot( path,
                                                          SceneState().isScenePhysics,
                                                          SceneState().isSceneText,
                                                          m_cWorldEnvironment,
                                                          m_systems.cameras->GetCameraTranslation(),
                                                          m_systems.cameras->GetCameraView(),
                                                          m_systems.cameras->GetCameraUp() );
            }
        }
    }

    // F3: Save screenshot to Screenshots/
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::SaveScreenshot, VK_F3 ) )
        {
            static int sScreenshotSeq = 0;
            char path[256] = {};
            if ( RuntimeFileWriter::NextNumberedPath( path,
                                                      sizeof( path ),
                                                      "Screenshots",
                                                      "screenshot_",
                                                      ".bmp",
                                                      sScreenshotSeq,
                                                      100 ) )
            {
                SaveScreenshot( path );
            }
        }
    }

    // R: reset/reload the current scene from scratch. Backspace remains as a scene-mode alias.
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ResetScene, 'R' ) )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
    }
    if ( SceneState().isSceneMode )
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::ResetSceneFromBackspace,
                                                          VK_BACK ) )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
    }

    const bool cameraMouseLookActive = ( !m_editor.editorModeEnabled && m_camera.isFlyMode &&
                                         ( !ReplayInspectionActive() || ReplayInspectionMouseLookActive() ) ) ||
                                       m_editor.viewportLookActive;
    const bool cameraKeyboardControlsActive = m_camera.isFlyMode || m_editor.viewportLookActive;
    if ( cameraMouseLookActive )
    {
        // Diagnostics UI owns the native cursor; mouse-look hides it while
        // consuming raw Win32 deltas, with cursor-position deltas as a
        // remote-desktop friendly fallback when raw input is unavailable.
        if ( !Input::IsAppFocused() )
        {
            InputController::ResetMouseLook( m_camera );
        }
        else if ( !MouseLookOwnsCursor() )
        {
            ApplyCursorOwnership();
            InputController::ResetMouseLook( m_camera );
        }
        else
        {
            Input::SetSystemCursorVisible( false );
            long rawX = 0;
            long rawY = 0;
            const bool hasRawDelta = Input::ConsumeRawMouseDelta( rawX, rawY );
            POINT currentClient = Input::GetClientMouseCoordinates();

            if ( m_camera.needsMouseLookReset )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
                m_camera.needsMouseLookReset = false;
            }
            else if ( hasRawDelta )
            {
                InputController::SetMouseLookDelta( m_camera, rawX, rawY );
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else if ( !m_camera.hasMouseLookLastClient )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else
            {
                InputController::SetMouseLookDelta( m_camera,
                                                    currentClient.x - m_camera.mouseLookLastClient.x,
                                                    currentClient.y - m_camera.mouseLookLastClient.y );
                m_camera.mouseLookLastClient = currentClient;
            }
        }
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        ApplyCursorOwnership();
    }

    if ( cameraKeyboardControlsActive )
    {
        // WASD movement
        m_camera.input.Set( InputState::Up, Input::IsKeyDown( 'W' ) );
        m_camera.input.Set( InputState::Left, Input::IsKeyDown( 'A' ) );
        m_camera.input.Set( InputState::Down, Input::IsKeyDown( 'S' ) );
        m_camera.input.Set( InputState::Right, Input::IsKeyDown( 'D' ) );
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
    }
}


void Run::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    if ( m_camera.isFlyMode || m_editor.viewportLookActive )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( ( !m_editor.editorModeEnabled || m_editor.viewportLookActive ) &&
             ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 ) )
        {
            m_systems.cameras->RotatePrimary( m_camera.input.xMove * mouseMovementQty,
                                              m_camera.input.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_camera.input.Get( InputState::Up ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Left ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Down ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Right ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        m_systems.cameras->ApplyPrimaryMovementBuffer();
    }

    // Clamp camera Y between m_terrain surface and Cfg().maxCameraHeight (not in fly mode, not in scene mode)
    if ( !m_camera.isFlyMode && !m_editor.viewportLookActive && !SceneState().isSceneMode )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation();
        float minY =
            m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) +
            Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}
