/*
File: SkullbonezSource/UI/UI.cpp
Purpose:
  Implements SkullbonezUI widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UI.h"
#include "../Assets/AssetSystem.h"
#include "../Rendering/IRenderBackend.h"
#include "../Maths/Matrix4.h"
#include "../Physics/Debug/PhysicsDebugVisualizer.h"
#include "../Core/Profiler.h"
#include "../Rendering/Text.h"
#include "UIDraw.h"
#include "UIDrawList.h"
#include "UIDrawWidgets.h"
#include "UIInput.h"
#include "UILayout.h"
#include "UITabControls.h"
#include "UITabEditor.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include "UIStyle.h"
#include "UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::Layout;

namespace
{
uint32_t HashCombine( uint32_t seed, uint32_t value )
{
    seed ^= value;
    seed *= 16777619u;
    return seed;
}


uint32_t HashTextValue( uint32_t seed, const char* value )
{
    if ( !value )
    {
        return HashCombine( seed, 0u );
    }

    while ( *value != '\0' )
    {
        seed = HashCombine( seed, static_cast<uint8_t>( *value ) );
        ++value;
    }
    return HashCombine( seed, 0u );
}


uint32_t HashBool( uint32_t seed, bool value )
{
    return HashCombine( seed, value ? 1u : 0u );
}


uint32_t HashInt( uint32_t seed, int value )
{
    return HashCombine( seed, static_cast<uint32_t>( value ) );
}


uint32_t HashFloat( uint32_t seed, float value, float scale = 100.0f )
{
    return HashInt( seed, static_cast<int>( std::round( value * scale ) ) );
}

constexpr int CAMERA_MODE_OPTION_COUNT = 5;
const char* const kCameraModeOptions[CAMERA_MODE_OPTION_COUNT] = { "Demo", "Scene", "Free", "Launcher", "Manipulator" };
constexpr float MINIMIZED_CAMERA_MODE_COMBO_W = 92.0f;
constexpr float MINIMIZED_CAMERA_MODE_GAP = 8.0f;
constexpr float MINIMIZED_RESTORE_W = 42.0f;
constexpr float MINIMIZED_RUN_MAX_W = 330.0f;

UIRect MinimizedCameraModeComboBounds( const UIRect& minimized )
{
    return { minimized.x + minimized.w - MINIMIZED_RESTORE_W - MINIMIZED_CAMERA_MODE_COMBO_W,
             minimized.y + 6.0f,
             MINIMIZED_CAMERA_MODE_COMBO_W,
             24.0f };
}

float MinimizedWidthWithCameraModeCombo( const char* title, int screenW )
{
    constexpr float margin = 14.0f;
    constexpr float textSize = 12.5f;
    constexpr float titleLeft = 32.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const float titleW = Text2d::MeasureText( textSize, title ? title : "" );
    const float desiredW =
        titleLeft + titleW + MINIMIZED_CAMERA_MODE_GAP + MINIMIZED_CAMERA_MODE_COMBO_W + MINIMIZED_RESTORE_W;
    return std::clamp( desiredW, 154.0f, maxW );
}

void StripMinimizedRuntimeModeSuffix( const InGameUIFrameData& data, char* title, size_t titleSize )
{
    if ( !title || titleSize == 0 )
    {
        return;
    }

    const char* runtimeMode = data.runtimeInputModeLabel ? data.runtimeInputModeLabel : "";
    if ( runtimeMode[0] == '\0' || std::strcmp( runtimeMode, "Scene" ) == 0 )
    {
        return;
    }

    char suffix[80] = {};
    snprintf( suffix, sizeof( suffix ), "  [%s]", runtimeMode );
    const size_t titleLen = strlen( title );
    const size_t suffixLen = strlen( suffix );
    if ( titleLen >= suffixLen && std::strcmp( title + titleLen - suffixLen, suffix ) == 0 )
    {
        title[titleLen - suffixLen] = '\0';
    }
}

uint32_t HashRenderTargetPreviewCatalog( uint32_t hash, const InGameUIFrameData& data )
{
    const int count = std::clamp( data.renderTargetPreviewCount, 0, UI_RENDER_TARGET_PREVIEW_MAX );
    hash = HashInt( hash, count );
    for ( int i = 0; i < count; ++i )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
        hash = HashTextValue( hash, resource.label );
        hash = HashInt( hash, static_cast<int>( resource.textureHandle ) );
        hash = HashInt( hash, resource.width );
        hash = HashInt( hash, resource.height );
        hash = HashBool( hash, resource.available );
        hash = HashBool( hash, resource.depth );
        hash = HashBool( hash, resource.hdr );
    }
    return hash;
}


uint32_t BuildUIContentSignature( const InGameUIFrameData& data )
{
    uint32_t hash = 2166136261u;
    hash = HashTextValue( hash, data.rendererName );
    hash = HashTextValue( hash, data.sceneName );
    hash = HashInt( hash, data.sceneOptionCount );
    hash = HashInt( hash, data.selectedSceneOption );
    hash = HashInt( hash, data.selectedCineModeSceneOption );
    for ( int i = 0; i < data.sceneOptionCount && data.sceneOptions; ++i )
    {
        hash = HashTextValue( hash, data.sceneOptions[i] );
    }
    hash = HashInt( hash, data.drawCallsBeforeUI );
    hash = HashInt( hash, data.UIDrawCalls );
    hash = HashFloat( hash, data.fps );
    hash = HashFloat( hash, data.renderMs, 1000.0f );
    hash = HashFloat( hash, data.physicsMs, 1000.0f );
    hash = HashFloat( hash, data.cpuFrameMs, 1000.0f );
    hash = HashFloat( hash, data.gpuFrameMs, 1000.0f );
    hash = HashInt( hash, data.modelCount );
    hash = HashInt( hash, data.modelCapacity );
    hash = HashInt( hash, data.workerThreadCount );
    hash = HashInt( hash, data.maxWorkerThreadCount );
    hash = HashInt( hash, data.currentFrame );
    hash = HashInt( hash, data.targetFrameCount );
    hash = HashInt( hash, static_cast<int>( data.rngSeed ) );
    hash = HashInt( hash, data.solverBallCount );
    hash = HashInt( hash, data.solverBoxCount );
    hash = HashInt( hash, data.currentSceneIndex );
    hash = HashInt( hash, data.sceneCount );
    hash = HashInt( hash, static_cast<int>( std::round( data.now * 1000.0 ) ) );
    hash = HashBool( hash, data.sceneMode );
    hash = HashBool( hash, data.scenePhysicsEnabled );
    hash = HashBool( hash, data.sceneTextEnabled );
    hash = HashBool( hash, data.textOnly );
    hash = HashBool( hash, data.fixedStep );
    hash = HashBool( hash, data.exitOnComplete );
    hash = HashBool( hash, data.testComplete );
    hash = HashBool( hash, data.vsyncEnabled );
    hash = HashBool( hash, data.pipelineSyncEnabled );
    hash = HashFloat( hash, data.sceneEnergy, 1000.0f );
    hash = HashFloat( hash, data.timeScale, 1000.0f );
    hash = HashFloat( hash, data.trackHeight, 1000.0f );
    hash = HashFloat( hash, data.autoCycleInterval, 1000.0f );
    hash = HashFloat( hash, data.worldGravity, 1000.0f );
    hash = HashFloat( hash, data.worldFluidHeight, 1000.0f );
    hash = HashFloat( hash, data.worldFluidDensity, 1000.0f );
    hash = HashInt( hash, static_cast<int>( data.physicsDebugFlags ) );
    hash = HashTextValue( hash, data.physicsPipelineStageName );
    hash = HashInt( hash, data.physicsPipelineStageIndex );
    hash = HashInt( hash, data.physicsPipelineStageCount );
    hash = HashFloat( hash, data.physicsDebugAlpha, 1000.0f );
    hash = HashFloat( hash, data.physicsDebugContactLinger, 1000.0f );
    hash = HashBool( hash, data.physicsSleepEnabled );
    hash = HashBool( hash, data.collisionVisualizer );
    hash = HashBool( hash, data.physicsDebugTransparent );
    hash = HashBool( hash, data.broadphaseOverlay );
    hash = HashBool( hash, data.tornadoEnabled );
    hash = HashBool( hash, data.tornadoVisualShell );
    hash = HashBool( hash, data.tornadoFieldVectors );
    hash = HashBool( hash, data.rayCastVisualization );
    hash = HashFloat( hash, data.tornadoRadius, 100.0f );
    hash = HashFloat( hash, data.tornadoHeight, 100.0f );
    hash = HashFloat( hash, data.tornadoInwardAcceleration, 100.0f );
    hash = HashFloat( hash, data.tornadoSwirlAcceleration, 100.0f );
    hash = HashFloat( hash, data.tornadoLiftAcceleration, 100.0f );
    hash = HashFloat( hash, data.rayCastImpulseStrength, 100.0f );
    hash = HashFloat( hash, data.launcherProjectileSpeed, 100.0f );
    hash = HashBool( hash, data.waterFreezeDebug );
    hash = HashBool( hash, data.waterFlatDebug );
    hash = HashBool( hash, data.terrainHidden );
    hash = HashBool( hash, data.waterHidden );
    hash = HashBool( hash, data.waterNoReflect );
    hash = HashBool( hash, data.waterRTReflect );
    hash = HashBool( hash, data.cameraMouseActive );
    hash = HashBool( hash, data.nativeCursorVisible );
    hash = HashTextValue( hash, data.runtimeInputModeLabel );
    hash = HashInt( hash, data.cameraModeIndex );
    hash = HashInt( hash, static_cast<int>( data.cameraModeEnabledMask ) );
    hash = HashBool( hash, data.editorModeEnabled );
    hash = HashBool( hash, data.editorPlacementMode );
    hash = HashBool( hash, data.editorPlaceStatic );
    hash = HashBool( hash, data.editorTerrainAlign );
    hash = HashBool( hash, data.editorViewportLookActive );
    hash = HashInt( hash, data.editorObjectType );
    hash = HashBool( hash, data.canSaveSceneDefaults );
    hash = HashBool( hash, data.cinematicRendering );
    hash = HashBool( hash, data.ordinaryRender.shadowsEnabled );
    hash = HashFloat( hash, data.ordinaryRender.sunIntensity, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.sunColorR, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.sunColorG, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.sunColorB, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.ambientStrength, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.skyAmbientR, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.skyAmbientG, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.skyAmbientB, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.groundAmbientR, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.groundAmbientG, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.groundAmbientB, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.shadowStrength, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.shadowSoftness, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.shadowDepthBias, 100000.0f );
    hash = HashFloat( hash, data.ordinaryRender.shadowSlopeBias, 100000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterTintR, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterTintG, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterTintB, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterAlpha, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterReflectionStrength, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterFresnelF0, 10000.0f );
    hash = HashFloat( hash, data.ordinaryRender.ballRoughnessScale, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.ballSpecularScale, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.boxRoughnessScale, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.boxSpecularScale, 1000.0f );
    hash = HashBool( hash, data.cinematic.enabled );
    hash = HashBool( hash, data.cinematic.skyAtmosphereEnabled );
    hash = HashBool( hash, data.cinematic.cloudsEnabled );
    hash = HashBool( hash, data.cinematic.godRaysEnabled );
    hash = HashBool( hash, data.cinematic.volumetricLightingEnabled );
    hash = HashBool( hash, data.cinematic.bloomEnabled );
    hash = HashBool( hash, data.cinematic.fogEnabled );
    hash = HashBool( hash, data.cinematic.terrainReliefEnabled );
    hash = HashBool( hash, data.cinematic.shadowsEnabled );
    hash = HashFloat( hash, data.cinematic.exposure, 1000.0f );
    hash = HashFloat( hash, data.cinematic.gamma, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunScreenX, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunScreenY, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunIntensity, 100.0f );
    hash = HashFloat( hash, data.cinematic.skyGlowStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudCoverage, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudSoftness, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudScale, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudIntensity, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunShaftStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunShaftFalloff, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricDensity, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricDecay, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomThreshold, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomKnee, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomRadius, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainRelief, 1000.0f );
    hash = HashFloat( hash, data.cinematic.basinDepth, 100.0f );
    hash = HashFloat( hash, data.cinematic.basinRimLift, 100.0f );
    hash = HashFloat( hash, data.cinematic.fogColorR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogColorG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogColorB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogStart, 10.0f );
    hash = HashFloat( hash, data.cinematic.fogEnd, 10.0f );
    hash = HashFloat( hash, data.cinematic.fogDensity, 100000.0f );
    hash = HashFloat( hash, data.cinematic.fogMaxOpacity, 1000.0f );
    hash = HashInt( hash, data.cinematic.skyMode );
    hash = HashInt( hash, data.cinematic.terrainMode );
    hash = HashInt( hash, data.cinematic.objectStyle );
    hash = HashInt( hash, data.cinematic.waterMode );
    hash = HashFloat( hash, data.cinematic.styleSaturation, 1000.0f );
    hash = HashFloat( hash, data.cinematic.styleContrast, 1000.0f );
    hash = HashFloat( hash, data.cinematic.styleVignette, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainGridScale, 100.0f );
    hash = HashFloat( hash, data.cinematic.terrainGridStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterAlpha, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterReflectionStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterGlintStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.basinCenterX, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinCenterZ, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinRadiusX, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinRadiusZ, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinFeather, 1000.0f );
    hash = HashRenderTargetPreviewCatalog( hash, data );
    return hash;
}


uint32_t BuildUIInteractionSignature( int mouseX,
                                      int mouseY,
                                      bool rendererOpen,
                                      bool reflectionOpen,
                                      bool sceneOpen,
                                      bool cineSceneOpen,
                                      bool editorObjectOpen,
                                      bool renderTargetOpen,
                                      bool cameraModeOpen,
                                      int selectedRenderTarget,
                                      int activeSlider )
{
    uint32_t hash = 2166136261u;
    hash = HashInt( hash, mouseX );
    hash = HashInt( hash, mouseY );
    hash = HashBool( hash, rendererOpen );
    hash = HashBool( hash, reflectionOpen );
    hash = HashBool( hash, sceneOpen );
    hash = HashBool( hash, cineSceneOpen );
    hash = HashBool( hash, editorObjectOpen );
    hash = HashBool( hash, renderTargetOpen );
    hash = HashBool( hash, cameraModeOpen );
    hash = HashInt( hash, selectedRenderTarget );
    hash = HashInt( hash, activeSlider );
    return hash;
}


void FlushUIDrawList( const UIDrawList& drawList, int screenW, int screenH, float offsetX = 0.0f, float offsetY = 0.0f )
{
    PROFILE_GPU_BEGIN( "Frame/UI/Draw" );
    const UIDrawContext immediateDraw( screenW, screenH );
    drawList.Flush( immediateDraw, offsetX, offsetY );
    {
        DRAW_CALL_TRACE_SCOPE( "Widgets" );
        Text2d::FlushQuads();
    }
    {
        DRAW_CALL_TRACE_SCOPE( "Text" );
        Text2d::FlushText();
    }
    PROFILE_GPU_END( "Frame/UI/Draw" );
}

int RenderTargetPreviewCount( const InGameUIFrameData& data )
{
    return std::clamp( data.renderTargetPreviewCount, 0, UI_RENDER_TARGET_PREVIEW_MAX );
}

uint32_t RenderTargetPreviewDisabledMask( const InGameUIFrameData& data )
{
    uint32_t mask = 0;
    const int count = RenderTargetPreviewCount( data );
    for ( int i = 0; i < count; ++i )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
        if ( !resource.available || resource.textureHandle == 0 || resource.width <= 0 || resource.height <= 0 )
        {
            mask |= 1u << i;
        }
    }
    return mask;
}

int FirstAvailableRenderTargetPreview( const InGameUIFrameData& data )
{
    const int count = RenderTargetPreviewCount( data );
    for ( int i = 0; i < count; ++i )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
        if ( resource.available && resource.textureHandle != 0 && resource.width > 0 && resource.height > 0 )
        {
            return i;
        }
    }
    return count > 0 ? 0 : -1;
}

int ResolveRenderTargetPreviewSelection( const InGameUIFrameData& data, int selectedIndex )
{
    const int count = RenderTargetPreviewCount( data );
    if ( count <= 0 )
    {
        return -1;
    }

    if ( selectedIndex >= 0 && selectedIndex < count )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[selectedIndex];
        if ( resource.available && resource.textureHandle != 0 && resource.width > 0 && resource.height > 0 )
        {
            return selectedIndex;
        }
    }
    return FirstAvailableRenderTargetPreview( data );
}

const char* RenderTargetPreviewTypeText( const UIRenderTargetPreviewResource& resource )
{
    if ( resource.depth )
    {
        return "Depth SRV";
    }
    return resource.hdr ? "RGBA16F SRV" : "RGBA8 SRV";
}

UIRect IntersectRect( const UIRect& a, const UIRect& b )
{
    const float left = (std::max)( a.x, b.x );
    const float top = (std::max)( a.y, b.y );
    const float right = (std::min)( a.x + a.w, b.x + b.w );
    const float bottom = (std::min)( a.y + a.h, b.y + b.h );
    if ( right <= left || bottom <= top )
    {
        return {};
    }
    return { left, top, right - left, bottom - top };
}

UIRect FitRectToAspect( const UIRect& bounds, int width, int height )
{
    if ( bounds.w <= 1.0f || bounds.h <= 1.0f || width <= 0 || height <= 0 )
    {
        return bounds;
    }

    const float sourceAspect = static_cast<float>( width ) / static_cast<float>( height );
    float drawW = bounds.w;
    float drawH = bounds.w / sourceAspect;
    if ( drawH > bounds.h )
    {
        drawH = bounds.h;
        drawW = bounds.h * sourceAspect;
    }
    return { bounds.x + ( bounds.w - drawW ) * 0.5f, bounds.y + ( bounds.h - drawH ) * 0.5f, drawW, drawH };
}

void EnsureRenderTargetPreviewResources( std::unique_ptr<IShader>& shader, uint32_t& dynamicVB )
{
    if ( !IsGfxReady() )
    {
        return;
    }

    if ( !shader )
    {
        shader = SkullbonezCore::Assets::CreateShaderFromActiveAssets( "shader.ui_render_target_preview" );
        shader->Use();
        shader->SetInt( "uTexture", 0 );
    }

    if ( dynamicVB == 0 )
    {
        const int attribs[] = { 2, 2 };
        dynamicVB = Gfx().CreateDynamicVB( attribs, 2, 6 );
    }
}

void ResetRenderTargetPreviewResources( std::unique_ptr<IShader>& shader, uint32_t& dynamicVB )
{
    shader.reset();
    if ( dynamicVB != 0 )
    {
        if ( IsGfxReady() )
        {
            Gfx().DestroyDynamicVB( dynamicVB );
        }
        dynamicVB = 0;
    }
}

void DrawRenderTargetPreviewTexture( std::unique_ptr<IShader>& shader,
                                     uint32_t& dynamicVB,
                                     const UIDrawContext& draw,
                                     const UIRenderTargetPreviewResource& resource,
                                     const UIRect& bounds,
                                     const UIRect& clipBounds )
{
    if ( !resource.available || resource.textureHandle == 0 || bounds.w <= 1.0f || bounds.h <= 1.0f || !IsGfxReady() )
    {
        return;
    }

    EnsureRenderTargetPreviewResources( shader, dynamicVB );
    if ( !shader || dynamicVB == 0 )
    {
        return;
    }

    const UIRect visible = IntersectRect( bounds, clipBounds );
    if ( visible.w <= 1.0f || visible.h <= 1.0f )
    {
        return;
    }

    const float uvLeft = std::clamp( ( visible.x - bounds.x ) / bounds.w, 0.0f, 1.0f );
    const float uvRight = std::clamp( ( visible.x + visible.w - bounds.x ) / bounds.w, 0.0f, 1.0f );
    const float uvTop = std::clamp( ( visible.y - bounds.y ) / bounds.h, 0.0f, 1.0f );
    const float uvBottom = std::clamp( ( visible.y + visible.h - bounds.y ) / bounds.h, 0.0f, 1.0f );
    const float left = draw.TextX( visible.x );
    const float right = draw.TextX( visible.x + visible.w );
    const float top = draw.TextY( visible.y );
    const float bottom = draw.TextY( visible.y + visible.h );
    const float verts[] = {
        left, bottom, uvLeft, uvBottom, right, bottom, uvRight, uvBottom, right, top, uvRight, uvTop,
        left, bottom, uvLeft, uvBottom, right, top,    uvRight, uvTop,    left,  top, uvLeft,  uvTop,
    };

    const Matrix4 proj = Matrix4::Ortho( -draw.HalfW(), draw.HalfW(), -draw.HalfH(), draw.HalfH(), -1.0f, 1.0f );
    const bool depthTestWasEnabled = Gfx().IsDepthTestEnabled();
    const bool depthWriteWasEnabled = Gfx().IsDepthWriteEnabled();
    const bool blendWasEnabled = Gfx().IsBlendEnabled();
    BlendFactor blendSrc = BlendFactor::One;
    BlendFactor blendDst = BlendFactor::Zero;
    Gfx().GetBlendFunc( blendSrc, blendDst );

    const int mode = resource.depth ? 2 : ( resource.hdr ? 1 : 0 );
    Gfx().SetDepthTest( false );
    Gfx().SetDepthWrite( false );
    Gfx().SetBlend( false );
    shader->Use();
    shader->SetMat4( "uProjection", proj );
    shader->SetInt( "uTexture", 0 );
    shader->SetVec4( "uPreviewParams", static_cast<float>( mode ), 1.0f, 2.2f, 0.0f );
    Gfx().BindTexture( resource.textureHandle, 0 );
    {
        DRAW_CALL_TRACE_SCOPE( "RenderTargetPreview" );
        Gfx().UploadAndDrawDynamicVB( dynamicVB, verts, 6 );
    }
    Gfx().BindTexture( 0, 0 );
    Gfx().SetDepthWrite( depthWriteWasEnabled );
    Gfx().SetDepthTest( depthTestWasEnabled );
    Gfx().SetBlendFunc( blendSrc, blendDst );
    Gfx().SetBlend( blendWasEnabled );
}

int WaterReflectionModeFromData( const InGameUIFrameData& data )
{
    if ( data.waterNoReflect )
    {
        return 2;
    }
    return data.waterRTReflect ? 1 : 0;
}

constexpr int UI_RENDER_SLIDER_BASE = 6000;
constexpr float UI_RENDER_FEATURE_START_Y = 48.0f;
constexpr float UI_RENDER_START_Y = 118.0f;
constexpr float UI_RENDER_SECTION_H = 28.0f;
constexpr float UI_RENDER_ROW_H = 42.0f;
constexpr float UI_RENDER_SAVE_BUTTON_W = 126.0f;
constexpr float UI_TARGETS_COMBO_Y = 42.0f;
constexpr float UI_TARGETS_META_Y = 86.0f;
constexpr float UI_TARGETS_PREVIEW_Y = 132.0f;
constexpr float UI_TARGETS_PREVIEW_H = 260.0f;
constexpr float UI_TARGETS_CONTENT_H = 430.0f;

struct RenderSliderSpec
{
    const char* section;
    const char* label;
    UIRenderParam param;
    float minValue;
    float maxValue;
    float step;
    const char* valueFormat;
};

constexpr RenderSliderSpec kRenderSliderSpecs[] = {
    { "Light", "Sun intensity", UIRenderParam::SunIntensity, 0.00f, 4.00f, 0.01f, "%.2f" },
    { nullptr, "Sun R", UIRenderParam::SunRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun G", UIRenderParam::SunGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun B", UIRenderParam::SunBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Ambient", UIRenderParam::AmbientStrength, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Sky Ambient", "Sky R", UIRenderParam::SkyRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Sky G", UIRenderParam::SkyGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Sky B", UIRenderParam::SkyBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Ground Ambient", "Ground R", UIRenderParam::GroundRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground G", UIRenderParam::GroundGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground B", UIRenderParam::GroundBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Shadows", "Strength", UIRenderParam::ShadowStrength, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Softness", UIRenderParam::ShadowSoftness, 0.25f, 4.00f, 0.01f, "%.2f" },
    { nullptr, "Depth bias", UIRenderParam::ShadowDepthBias, 0.00000f, 0.00500f, 0.00001f, "%.5f" },
    { nullptr, "Slope bias", UIRenderParam::ShadowSlopeBias, 0.00000f, 0.00500f, 0.00001f, "%.5f" },
    { "Water", "Water R", UIRenderParam::WaterRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Water G", UIRenderParam::WaterGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Water B", UIRenderParam::WaterBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Alpha", UIRenderParam::WaterAlpha, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Reflection", UIRenderParam::WaterReflection, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Fresnel F0", UIRenderParam::WaterFresnel, 0.000f, 0.120f, 0.001f, "%.3f" },
    { "Materials", "Ball roughness", UIRenderParam::BallRoughness, 0.25f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Ball specular", UIRenderParam::BallSpecular, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Box roughness", UIRenderParam::BoxRoughness, 0.25f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Box specular", UIRenderParam::BoxSpecular, 0.00f, 2.00f, 0.01f, "%.2f" },
};
static_assert( sizeof( kRenderSliderSpecs ) / sizeof( kRenderSliderSpecs[0] ) ==
                   static_cast<int>( UIRenderParam::Count ),
               "Render slider specs must match UIRenderParam." );

constexpr int EDITOR_MINI_TREE_TYPE_NONE = -1;
constexpr int EDITOR_MINI_TREE_TYPE_SMALL = 0;
constexpr int EDITOR_MINI_TREE_TYPE_PINE = 1;
constexpr int EDITOR_MINI_TREE_TYPE_CEDAR = 2;
constexpr int EDITOR_MINI_TREE_TYPE_COUNT = 3;
constexpr int EDITOR_MINI_TREE_PLACEMENT_NONE = -1;
constexpr int EDITOR_MINI_TREE_PLACEMENT_FIXED = 0;
constexpr int EDITOR_MINI_TREE_PLACEMENT_SLEEPING = 1;
constexpr int EDITOR_MINI_TREE_PLACEMENT_ROOTED = 2;
constexpr int EDITOR_MINI_RAGDOLL_MODE_SLEEPING = 1;
constexpr int EDITOR_MINI_RAGDOLL_MODE_COUNT = 2;
constexpr int EDITOR_MINI_FLYOUT_OPTION_MAX = 3;
constexpr double EDITOR_MINI_HOLD_SECONDS = 0.32;
constexpr int EDITOR_MINI_HOLD_MODE_NONE = 0;
constexpr int EDITOR_MINI_HOLD_MODE_TREE_TYPES = 1;
constexpr int EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES = 2;

struct EditorMiniPaletteEntry
{
    int objectType;
    int treePlacement;
    int holdMode;
};

constexpr EditorMiniPaletteEntry kEditorMiniPaletteEntries[] = {
    { EditorTab::OBJECT_BOX, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_BALL, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_SPHERE, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_WEDGE, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_TRI_PRISM, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_TAPERED_BLOCK, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_PYRAMID, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_HEX_PRISM, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_HULL_DIAMOND, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_ROCK_SLAB, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_ROCK_LUMP, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_ROCK_SHARD, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_ROCK_CHIPPED, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_NONE },
    { EditorTab::OBJECT_TREE_BIG, EDITOR_MINI_TREE_PLACEMENT_FIXED, EDITOR_MINI_HOLD_MODE_TREE_TYPES },
    { EditorTab::OBJECT_TREE_BIG_SLEEP, EDITOR_MINI_TREE_PLACEMENT_SLEEPING, EDITOR_MINI_HOLD_MODE_TREE_TYPES },
    { EditorTab::OBJECT_TREE_BIG_ROOTED, EDITOR_MINI_TREE_PLACEMENT_ROOTED, EDITOR_MINI_HOLD_MODE_TREE_TYPES },
    { EditorTab::OBJECT_RAGDOLL, EDITOR_MINI_TREE_PLACEMENT_NONE, EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES },
};
constexpr int EDITOR_MINI_PALETTE_ENTRY_COUNT =
    static_cast<int>( sizeof( kEditorMiniPaletteEntries ) / sizeof( kEditorMiniPaletteEntries[0] ) );

struct EditorMiniPaletteLayout
{
    UIRect buttons[EDITOR_MINI_PALETTE_ENTRY_COUNT];
    UIRect flyoutOptions[EDITOR_MINI_FLYOUT_OPTION_MAX];
    UIRect bounds;
    UIRect flyoutBounds;
    float buttonSize = 0.0f;
    int buttonCount = 0;
    int flyoutOptionCount = 0;
    bool flyoutVisible = false;
};

bool IsEditorMiniTreePlacementValid( int placement )
{
    return placement >= EDITOR_MINI_TREE_PLACEMENT_FIXED && placement <= EDITOR_MINI_TREE_PLACEMENT_ROOTED;
}

int EditorMiniPaletteFlyoutOptionCount( int holdMode )
{
    if ( holdMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
    {
        return EDITOR_MINI_TREE_TYPE_COUNT;
    }
    if ( holdMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
    {
        return EDITOR_MINI_RAGDOLL_MODE_COUNT;
    }
    return 0;
}

bool EditorMiniTreeTypeForType( int objectType, int& outTreeType, int& outPlacement )
{
    switch ( objectType )
    {
    case EditorTab::OBJECT_TREE_SMALL:
        outTreeType = EDITOR_MINI_TREE_TYPE_SMALL;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_FIXED;
        return true;
    case EditorTab::OBJECT_TREE_SMALL_SLEEP:
        outTreeType = EDITOR_MINI_TREE_TYPE_SMALL;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_SLEEPING;
        return true;
    case EditorTab::OBJECT_TREE_SMALL_ROOTED:
        outTreeType = EDITOR_MINI_TREE_TYPE_SMALL;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_ROOTED;
        return true;
    case EditorTab::OBJECT_TREE_BIG:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_FIXED;
        return true;
    case EditorTab::OBJECT_TREE_BIG_SLEEP:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_SLEEPING;
        return true;
    case EditorTab::OBJECT_TREE_BIG_ROOTED:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_ROOTED;
        return true;
    case EditorTab::OBJECT_TREE_CEDAR:
        outTreeType = EDITOR_MINI_TREE_TYPE_CEDAR;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_FIXED;
        return true;
    case EditorTab::OBJECT_TREE_CEDAR_SLEEP:
        outTreeType = EDITOR_MINI_TREE_TYPE_CEDAR;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_SLEEPING;
        return true;
    case EditorTab::OBJECT_TREE_CEDAR_ROOTED:
        outTreeType = EDITOR_MINI_TREE_TYPE_CEDAR;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_ROOTED;
        return true;
    default:
        outTreeType = EDITOR_MINI_TREE_TYPE_NONE;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
        return false;
    }
}

bool EditorMiniPaletteTreeStateForType( int objectType, bool editorPlaceStatic, int& outPlacement, int& outTreeType )
{
    int treeType = EDITOR_MINI_TREE_TYPE_NONE;
    int placement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    if ( !EditorMiniTreeTypeForType( objectType, treeType, placement ) )
    {
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
        outTreeType = EDITOR_MINI_TREE_TYPE_NONE;
        return false;
    }
    if ( placement == EDITOR_MINI_TREE_PLACEMENT_FIXED && !editorPlaceStatic )
    {
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
        outTreeType = treeType;
        return false;
    }
    outPlacement = placement;
    outTreeType = treeType;
    return true;
}

int EditorMiniTreeObjectType( int treeType, int placement )
{
    if ( treeType == EDITOR_MINI_TREE_TYPE_SMALL )
    {
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_SLEEPING )
        {
            return EditorTab::OBJECT_TREE_SMALL_SLEEP;
        }
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_ROOTED )
        {
            return EditorTab::OBJECT_TREE_SMALL_ROOTED;
        }
        return EditorTab::OBJECT_TREE_SMALL;
    }
    if ( treeType == EDITOR_MINI_TREE_TYPE_PINE )
    {
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_SLEEPING )
        {
            return EditorTab::OBJECT_TREE_BIG_SLEEP;
        }
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_ROOTED )
        {
            return EditorTab::OBJECT_TREE_BIG_ROOTED;
        }
        return EditorTab::OBJECT_TREE_BIG;
    }
    if ( treeType == EDITOR_MINI_TREE_TYPE_CEDAR )
    {
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_SLEEPING )
        {
            return EditorTab::OBJECT_TREE_CEDAR_SLEEP;
        }
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_ROOTED )
        {
            return EditorTab::OBJECT_TREE_CEDAR_ROOTED;
        }
        return EditorTab::OBJECT_TREE_CEDAR;
    }
    return EditorTab::OBJECT_TREE_SMALL;
}

EditorMiniPaletteLayout BuildEditorMiniPaletteLayout( int screenW,
                                                      int screenH,
                                                      const UIRect& minimized,
                                                      int flyoutAnchorEntry,
                                                      bool flyoutOpen )
{
    EditorMiniPaletteLayout layout;
    layout.buttonCount = EDITOR_MINI_PALETTE_ENTRY_COUNT;

    constexpr float margin = 14.0f;
    const float topY = margin;
    const float bottomLimit = (std::max)( margin + 84.0f, minimized.y - 10.0f );
    const float availableH = (std::max)( 80.0f, bottomLimit - topY );
    float gap = 4.0f;
    float buttonSize = 32.0f;
    float requiredH =
        static_cast<float>( layout.buttonCount ) * buttonSize + static_cast<float>( layout.buttonCount - 1 ) * gap;
    if ( requiredH > availableH )
    {
        gap = 2.0f;
        buttonSize = std::floor( ( availableH - static_cast<float>( layout.buttonCount - 1 ) * gap ) /
                                 static_cast<float>( layout.buttonCount ) );
        buttonSize = std::clamp( buttonSize, 10.0f, 32.0f );
        requiredH =
            static_cast<float>( layout.buttonCount ) * buttonSize + static_cast<float>( layout.buttonCount - 1 ) * gap;
    }

    layout.buttonSize = buttonSize;
    const float x = margin;
    for ( int i = 0; i < layout.buttonCount; ++i )
    {
        layout.buttons[i] = { x, topY + static_cast<float>( i ) * ( buttonSize + gap ), buttonSize, buttonSize };
    }
    layout.bounds = { x, topY, buttonSize, requiredH };

    if ( flyoutOpen && flyoutAnchorEntry >= 0 && flyoutAnchorEntry < layout.buttonCount )
    {
        const int optionCount =
            EditorMiniPaletteFlyoutOptionCount( kEditorMiniPaletteEntries[flyoutAnchorEntry].holdMode );
        if ( optionCount <= 0 )
        {
            return layout;
        }
        const UIRect anchor = layout.buttons[flyoutAnchorEntry];
        const float optionSize = buttonSize;
        const float optionGap = (std::max)( 2.0f, std::floor( buttonSize * 0.12f ) );
        const float padding = 4.0f;
        const float flyoutW = padding * 2.0f + optionSize * static_cast<float>( optionCount ) +
                              optionGap * static_cast<float>( optionCount - 1 );
        const float flyoutH = padding * 2.0f + optionSize;
        float flyoutX = anchor.x + anchor.w + 8.0f;
        if ( flyoutX + flyoutW > static_cast<float>( screenW ) - margin )
        {
            flyoutX = anchor.x + anchor.w + 4.0f;
        }
        const float maxY = (std::max)( margin, static_cast<float>( screenH ) - margin - flyoutH );
        const float flyoutY = std::clamp( anchor.y + ( anchor.h - flyoutH ) * 0.5f, margin, maxY );
        layout.flyoutBounds = { flyoutX, flyoutY, flyoutW, flyoutH };
        layout.flyoutOptionCount = optionCount;
        for ( int i = 0; i < optionCount; ++i )
        {
            layout.flyoutOptions[i] = { flyoutX + padding + static_cast<float>( i ) * ( optionSize + optionGap ),
                                        flyoutY + padding,
                                        optionSize,
                                        optionSize };
        }
        layout.flyoutVisible = true;
    }

    return layout;
}

int HitEditorMiniPaletteButton( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY )
{
    for ( int i = 0; i < layout.buttonCount; ++i )
    {
        if ( layout.buttons[i].Contains( mouseX, mouseY ) )
        {
            return i;
        }
    }
    return -1;
}

int HitEditorMiniPaletteFlyoutOption( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY )
{
    if ( !layout.flyoutVisible )
    {
        return -1;
    }

    for ( int i = 0; i < layout.flyoutOptionCount; ++i )
    {
        if ( layout.flyoutOptions[i].Contains( mouseX, mouseY ) )
        {
            return i;
        }
    }
    return -1;
}

bool EditorMiniPaletteContains( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY )
{
    return HitEditorMiniPaletteButton( layout, mouseX, mouseY ) >= 0 ||
           HitEditorMiniPaletteFlyoutOption( layout, mouseX, mouseY ) >= 0 ||
           ( layout.flyoutVisible && layout.flyoutBounds.Contains( mouseX, mouseY ) );
}

bool IsBlockVisible( float contentY, float contentH, float blockY, float blockH )
{
    return blockY + blockH >= contentY && blockY <= contentY + contentH;
}

void DrawHitboxRect( const UIDrawContext& draw,
                     const UIRect& bounds,
                     float r,
                     float g,
                     float b,
                     float fillA = 0.060f,
                     float outlineA = 0.94f )
{
    if ( bounds.w <= 0.0f || bounds.h <= 0.0f )
    {
        return;
    }

    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, fillA );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, outlineA );
    if ( bounds.w > 4.0f && bounds.h > 4.0f )
    {
        draw.Outline( bounds.x + 1.0f, bounds.y + 1.0f, bounds.w - 2.0f, bounds.h - 2.0f, r, g, b, outlineA * 0.42f );
    }
}

void DrawComboHitboxes( const UIDrawContext& draw, const UIComboBox& combo, int optionCount, float r, float g, float b )
{
    DrawHitboxRect( draw, combo.Bounds(), r, g, b );
    if ( combo.IsOpen() )
    {
        DrawHitboxRect( draw, combo.DropdownBounds( optionCount ), 0.18f, 0.58f, 1.0f, 0.078f, 0.96f );
    }
}

void DrawTabHitboxes( const UIDrawContext& draw, const UITabBar& tabBar, int tabCount )
{
    const UIRect tabs = tabBar.Bounds();
    if ( tabCount <= 0 || tabs.w <= 0.0f || tabs.h <= 0.0f )
    {
        return;
    }

    const float tabW = tabs.w / static_cast<float>( tabCount );
    for ( int i = 0; i < tabCount; ++i )
    {
        DrawHitboxRect( draw,
                        { tabs.x + static_cast<float>( i ) * tabW, tabs.y, tabW, tabs.h },
                        1.0f,
                        0.80f,
                        0.18f,
                        0.052f,
                        0.84f );
    }
}

int SceneDropdownHitboxOptionCount( const SceneTab::UISceneTabState& state, const InGameUIFrameData& data )
{
    const int filteredSceneCount =
        SceneTab::CountFilteredOptions( data.sceneOptions, data.sceneOptionCount, state.filter );
    const int sceneVisibleCount = SceneComboVisibleCount( filteredSceneCount );
    return sceneVisibleCount == 0 && state.filter[0] != '\0' ? 1 : sceneVisibleCount;
}

void EllipsizeToWidth( char* text, size_t textSize, float pxSize, float maxWidth )
{
    if ( !text || textSize == 0 || Text2d::MeasureText( pxSize, text ) <= maxWidth )
    {
        return;
    }

    size_t len = strlen( text );
    while ( len > 3 && Text2d::MeasureText( pxSize, text ) > maxWidth )
    {
        text[len - 3] = '.';
        text[len - 2] = '.';
        text[len - 1] = '.';
        text[len] = '\0';
        --len;
    }
}

void DrawFittedText( const UIDrawContext& draw,
                     float x,
                     float y,
                     float pxSize,
                     const Style::UIColor& color,
                     const char* value,
                     float maxWidth )
{
    char text[192] = {};
    snprintf( text, sizeof( text ), "%s", value ? value : "" );
    EllipsizeToWidth( text, sizeof( text ), pxSize, maxWidth );
    draw.Text( x, y, pxSize, color.r, color.g, color.b, text );
}

int RenderSliderIndexFromActiveSlider( int activeSlider )
{
    const int index = activeSlider - UI_RENDER_SLIDER_BASE;
    return ( index >= 0 && index < static_cast<int>( UIRenderParam::Count ) ) ? index : -1;
}

float RenderSliderY( int index, float baseY )
{
    float y = baseY;
    for ( int i = 0; i <= index; ++i )
    {
        if ( kRenderSliderSpecs[i].section )
        {
            y += UI_RENDER_SECTION_H;
        }
        if ( i == index )
        {
            return y;
        }
        y += UI_RENDER_ROW_H;
    }
    return y;
}

int RenderContentHeight()
{
    float height = UI_RENDER_START_Y;
    for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
    {
        if ( kRenderSliderSpecs[i].section )
        {
            height += UI_RENDER_SECTION_H;
        }
        height += UI_RENDER_ROW_H;
    }
    return static_cast<int>( height + 18.0f );
}

int RenderTargetsContentHeight()
{
    return static_cast<int>( UI_TARGETS_CONTENT_H );
}

float RenderValueForParam( const OrdinaryRenderConfig& ordinary, UIRenderParam param )
{
    switch ( param )
    {
    case UIRenderParam::SunIntensity:
        return ordinary.sunIntensity;
    case UIRenderParam::SunRed:
        return ordinary.sunColorR;
    case UIRenderParam::SunGreen:
        return ordinary.sunColorG;
    case UIRenderParam::SunBlue:
        return ordinary.sunColorB;
    case UIRenderParam::AmbientStrength:
        return ordinary.ambientStrength;
    case UIRenderParam::SkyRed:
        return ordinary.skyAmbientR;
    case UIRenderParam::SkyGreen:
        return ordinary.skyAmbientG;
    case UIRenderParam::SkyBlue:
        return ordinary.skyAmbientB;
    case UIRenderParam::GroundRed:
        return ordinary.groundAmbientR;
    case UIRenderParam::GroundGreen:
        return ordinary.groundAmbientG;
    case UIRenderParam::GroundBlue:
        return ordinary.groundAmbientB;
    case UIRenderParam::ShadowStrength:
        return ordinary.shadowStrength;
    case UIRenderParam::ShadowSoftness:
        return ordinary.shadowSoftness;
    case UIRenderParam::ShadowDepthBias:
        return ordinary.shadowDepthBias;
    case UIRenderParam::ShadowSlopeBias:
        return ordinary.shadowSlopeBias;
    case UIRenderParam::WaterRed:
        return ordinary.waterTintR;
    case UIRenderParam::WaterGreen:
        return ordinary.waterTintG;
    case UIRenderParam::WaterBlue:
        return ordinary.waterTintB;
    case UIRenderParam::WaterAlpha:
        return ordinary.waterAlpha;
    case UIRenderParam::WaterReflection:
        return ordinary.waterReflectionStrength;
    case UIRenderParam::WaterFresnel:
        return ordinary.waterFresnelF0;
    case UIRenderParam::BallRoughness:
        return ordinary.ballRoughnessScale;
    case UIRenderParam::BallSpecular:
        return ordinary.ballSpecularScale;
    case UIRenderParam::BoxRoughness:
        return ordinary.boxRoughnessScale;
    case UIRenderParam::BoxSpecular:
        return ordinary.boxSpecularScale;
    default:
        return 0.0f;
    }
}

void SetRenderSliderResult( InGameUIInputResult& result,
                            const UISlider& slider,
                            int mouseX,
                            const RenderSliderSpec& spec )
{
    result.commands.renderTuning.requestedParam = spec.param;
    result.commands.renderTuning.requestedValue =
        slider.ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
}

float EditorMiniChipWidth( const char* label )
{
    return Text2d::MeasureText( 10.5f, label ? label : "" ) + 18.0f;
}


struct EditorMinimizedStatusLayout
{
    UIRect restoreButton;
    UIRect glyph;
    UIRect modeChip;
    UIRect bodyChip;
    UIRect alignChip;
    float labelX = 0.0f;
    float labelMaxW = 0.0f;
};


EditorMinimizedStatusLayout BuildEditorMinimizedStatusLayout( const UIRect& minimized,
                                                              bool editorPlacementMode,
                                                              bool editorPlaceStatic,
                                                              bool editorTerrainAlign )
{
    EditorMinimizedStatusLayout layout;
    layout.restoreButton = { minimized.x + minimized.w - 36.0f, minimized.y + 7.0f, 26.0f, 22.0f };
    layout.glyph = { minimized.x + 11.0f, minimized.y + 7.0f, 24.0f, 24.0f };

    const char* modeLabel = editorPlacementMode ? "Place" : "Gizmo";
    const char* bodyLabel = editorPlaceStatic ? "Static" : "Dynamic";
    const char* alignLabel = editorTerrainAlign ? "Align" : "Level";
    const float alignW = EditorMiniChipWidth( alignLabel );
    const float bodyW = EditorMiniChipWidth( bodyLabel );
    const float modeW = EditorMiniChipWidth( modeLabel );
    const float chipY = minimized.y + 9.0f;
    const float alignX = layout.restoreButton.x - 10.0f - alignW;
    const float bodyX = alignX - 8.0f - bodyW;
    const float modeX = bodyX - 8.0f - modeW;
    layout.modeChip = { modeX, chipY, modeW, 20.0f };
    layout.bodyChip = { bodyX, chipY, bodyW, 20.0f };
    layout.alignChip = { alignX, chipY, alignW, 20.0f };
    layout.labelX = layout.glyph.x + layout.glyph.w + 10.0f;
    layout.labelMaxW = (std::max)( 42.0f, modeX - layout.labelX - 10.0f );
    return layout;
}


EditorMinimizedStatusLayout BuildEditorMinimizedStatusLayout( const UIRect& minimized, const InGameUIFrameData& data )
{
    return BuildEditorMinimizedStatusLayout( minimized,
                                             data.editorPlacementMode,
                                             data.editorPlaceStatic,
                                             data.editorTerrainAlign );
}


float EditorMinimizedWidth( const InGameUIFrameData& data, int screenW )
{
    constexpr float margin = 14.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const char* shapeLabel = EditorTab::ObjectLabel( data.editorObjectType );
    const char* modeLabel = data.editorPlacementMode ? "Place" : "Gizmo";
    const char* bodyLabel = data.editorPlaceStatic ? "Static" : "Dynamic";
    const char* alignLabel = data.editorTerrainAlign ? "Align" : "Level";
    const float desiredW = 140.0f + Text2d::MeasureText( 12.0f, shapeLabel ) + EditorMiniChipWidth( modeLabel ) +
                           EditorMiniChipWidth( bodyLabel ) + EditorMiniChipWidth( alignLabel );
    return std::clamp( desiredW, 376.0f, maxW );
}


void DrawEditorMiniChip( const UIDrawContext& draw,
                         float x,
                         float y,
                         const char* label,
                         const Style::UIColor& fill,
                         const Style::UIColor& text,
                         bool hot )
{
    const Style::UIPalette& palette = Style::Palette();
    const float w = EditorMiniChipWidth( label );
    Style::UIColor chipFill = fill;
    chipFill.a = hot ? (std::min)( 1.0f, chipFill.a + 0.08f ) : chipFill.a;
    draw.RoundedRect( x, y, w, 20.0f, Style::Radii().smallButton, chipFill.r, chipFill.g, chipFill.b, chipFill.a );
    if ( hot )
    {
        draw.Outline( x, y, w, 20.0f, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b, 0.72f );
    }
    draw.Text( x + 9.0f, y + 5.0f, 10.5f, text.r, text.g, text.b, label );
}


bool IsEditorMiniRootType( int objectType )
{
    return objectType == EditorTab::OBJECT_ROOT_SMALL || objectType == EditorTab::OBJECT_ROOT_LARGE;
}


bool IsEditorMiniRockType( int objectType )
{
    return objectType >= EditorTab::OBJECT_ROCK_SLAB && objectType <= EditorTab::OBJECT_ROCK_CHIPPED;
}


bool IsEditorMiniHullType( int objectType )
{
    return objectType >= EditorTab::OBJECT_HULL_WEDGE && objectType <= EditorTab::OBJECT_HULL_DIAMOND;
}


bool EditorMiniTreeVisualForType( int objectType,
                                  int& outTreeType,
                                  int& outPlacement,
                                  bool& outSlope,
                                  bool& outShedding )
{
    outSlope = false;
    outShedding = false;
    if ( EditorMiniTreeTypeForType( objectType, outTreeType, outPlacement ) )
    {
        return true;
    }

    outPlacement = EDITOR_MINI_TREE_PLACEMENT_FIXED;
    switch ( objectType )
    {
    case EditorTab::OBJECT_TREE_SMALL_SLOPE:
        outTreeType = EDITOR_MINI_TREE_TYPE_SMALL;
        outSlope = true;
        return true;
    case EditorTab::OBJECT_TREE_BIG_SLOPE:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outSlope = true;
        return true;
    case EditorTab::OBJECT_TREE_CEDAR_SLOPE:
        outTreeType = EDITOR_MINI_TREE_TYPE_CEDAR;
        outSlope = true;
        return true;
    case EditorTab::OBJECT_TREE_PINE_SHEDDING:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outShedding = true;
        return true;
    default:
        outTreeType = EDITOR_MINI_TREE_TYPE_NONE;
        return false;
    }
}


void DrawEditorMiniRootSilhouette( const UIDrawContext& draw,
                                   const UIRect& bounds,
                                   bool largeRoot,
                                   const Style::UIColor& color,
                                   float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.52f;
    const float r = (std::min)( bounds.w, bounds.h ) * ( largeRoot ? 0.36f : 0.31f );
    const float stemW = (std::max)( 2.0f, r * 0.24f );

    draw.Rect( cx - stemW * 0.5f, cy - r * 1.05f, stemW, r * 1.18f, color.r, color.g, color.b, alpha );
    draw.Triangle( cx - stemW * 0.2f,
                   cy - r * 0.10f,
                   cx - r * 1.02f,
                   cy + r * 0.68f,
                   cx - stemW * 1.15f,
                   cy + r * 0.17f,
                   color.r,
                   color.g,
                   color.b,
                   alpha * 0.94f );
    draw.Triangle( cx + stemW * 0.2f,
                   cy - r * 0.08f,
                   cx + r * 1.02f,
                   cy + r * 0.68f,
                   cx + stemW * 1.15f,
                   cy + r * 0.18f,
                   color.r,
                   color.g,
                   color.b,
                   alpha * 0.94f );
    draw.Triangle( cx,
                   cy + r * 0.04f,
                   cx - r * 0.22f,
                   cy + r * 1.02f,
                   cx + r * 0.16f,
                   cy + r * 0.35f,
                   color.r,
                   color.g,
                   color.b,
                   alpha * 0.84f );
    if ( largeRoot )
    {
        draw.Triangle( cx - r * 0.18f,
                       cy + r * 0.18f,
                       cx - r * 0.72f,
                       cy + r * 1.00f,
                       cx - r * 0.38f,
                       cy + r * 0.38f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.78f );
        draw.Triangle( cx + r * 0.14f,
                       cy + r * 0.18f,
                       cx + r * 0.74f,
                       cy + r * 1.00f,
                       cx + r * 0.36f,
                       cy + r * 0.38f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.78f );
    }
}


void DrawEditorMiniTreeSilhouette( const UIDrawContext& draw,
                                   const UIRect& bounds,
                                   int family,
                                   bool rooted,
                                   bool slope,
                                   bool shedding,
                                   const Style::UIColor& color,
                                   float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.51f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.32f;
    const float trunkW = (std::max)( 2.0f, r * 0.24f );

    if ( slope )
    {
        draw.Triangle( cx - r * 1.22f,
                       cy + r * 1.10f,
                       cx + r * 1.20f,
                       cy + r * 0.66f,
                       cx + r * 1.20f,
                       cy + r * 1.10f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.42f );
    }

    draw.Rect( cx - trunkW * 0.5f, cy + r * 0.05f, trunkW, r * 0.94f, color.r, color.g, color.b, alpha * 0.74f );

    if ( family == EDITOR_MINI_TREE_TYPE_SMALL )
    {
        draw.RoundedRect( cx - r * 0.72f,
                          cy - r * 0.72f,
                          r * 1.44f,
                          r * 0.92f,
                          999.0f,
                          color.r,
                          color.g,
                          color.b,
                          alpha );
        draw.RoundedRect( cx - r * 0.54f,
                          cy - r * 1.12f,
                          r * 1.08f,
                          r * 0.82f,
                          999.0f,
                          color.r,
                          color.g,
                          color.b,
                          alpha * 0.94f );
    }
    else if ( family == EDITOR_MINI_TREE_TYPE_CEDAR )
    {
        draw.Triangle( cx,
                       cy - r * 1.25f,
                       cx - r * 0.48f,
                       cy - r * 0.30f,
                       cx + r * 0.48f,
                       cy - r * 0.30f,
                       color.r,
                       color.g,
                       color.b,
                       alpha );
        draw.Triangle( cx,
                       cy - r * 0.82f,
                       cx - r * 0.70f,
                       cy + r * 0.28f,
                       cx + r * 0.70f,
                       cy + r * 0.28f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.92f );
        draw.Triangle( cx,
                       cy - r * 0.24f,
                       cx - r * 0.82f,
                       cy + r * 0.88f,
                       cx + r * 0.82f,
                       cy + r * 0.88f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.84f );
    }
    else
    {
        draw.Triangle( cx,
                       cy - r * 1.18f,
                       cx - r * 0.70f,
                       cy - r * 0.06f,
                       cx + r * 0.70f,
                       cy - r * 0.06f,
                       color.r,
                       color.g,
                       color.b,
                       alpha );
        draw.Triangle( cx,
                       cy - r * 0.58f,
                       cx - r * 0.96f,
                       cy + r * 0.56f,
                       cx + r * 0.96f,
                       cy + r * 0.56f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.90f );
        draw.Triangle( cx,
                       cy + r * 0.00f,
                       cx - r * 1.12f,
                       cy + r * 0.98f,
                       cx + r * 1.12f,
                       cy + r * 0.98f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.80f );
    }

    if ( rooted )
    {
        draw.Triangle( cx,
                       cy + r * 0.76f,
                       cx - r * 0.62f,
                       cy + r * 1.18f,
                       cx - trunkW * 0.5f,
                       cy + r * 0.88f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.84f );
        draw.Triangle( cx,
                       cy + r * 0.76f,
                       cx + r * 0.62f,
                       cy + r * 1.18f,
                       cx + trunkW * 0.5f,
                       cy + r * 0.88f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.84f );
    }

    if ( shedding )
    {
        draw.RoundedRect( cx - r * 1.12f,
                          cy + r * 1.03f,
                          3.0f,
                          3.0f,
                          999.0f,
                          color.r,
                          color.g,
                          color.b,
                          alpha * 0.80f );
        draw.RoundedRect( cx - r * 0.18f,
                          cy + r * 1.15f,
                          3.0f,
                          3.0f,
                          999.0f,
                          color.r,
                          color.g,
                          color.b,
                          alpha * 0.78f );
        draw.RoundedRect( cx + r * 0.92f,
                          cy + r * 0.94f,
                          3.0f,
                          3.0f,
                          999.0f,
                          color.r,
                          color.g,
                          color.b,
                          alpha * 0.78f );
    }
}


void DrawEditorMiniHullSilhouette( const UIDrawContext& draw,
                                   const UIRect& bounds,
                                   int objectType,
                                   const Style::UIColor& color,
                                   float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.34f;

    switch ( objectType )
    {
    case EditorTab::OBJECT_HULL_WEDGE:
        draw.Triangle( cx - r * 1.05f,
                       cy + r * 0.82f,
                       cx + r * 1.05f,
                       cy + r * 0.82f,
                       cx + r * 0.36f,
                       cy - r * 0.86f,
                       color.r,
                       color.g,
                       color.b,
                       alpha );
        draw.Rect( cx - r * 1.05f, cy + r * 0.62f, r * 2.10f, r * 0.24f, color.r, color.g, color.b, alpha * 0.70f );
        return;
    case EditorTab::OBJECT_HULL_TRI_PRISM:
        draw.Triangle( cx - r * 0.98f,
                       cy + r * 0.78f,
                       cx - r * 0.18f,
                       cy - r * 0.78f,
                       cx + r * 0.58f,
                       cy + r * 0.78f,
                       color.r,
                       color.g,
                       color.b,
                       alpha );
        draw.Triangle( cx - r * 0.36f,
                       cy + r * 0.42f,
                       cx + r * 0.42f,
                       cy - r * 0.96f,
                       cx + r * 1.04f,
                       cy + r * 0.42f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.70f );
        draw.Rect( cx - r * 0.96f, cy + r * 0.64f, r * 1.54f, 2.0f, color.r, color.g, color.b, alpha * 0.82f );
        draw.Rect( cx - r * 0.36f, cy + r * 0.32f, r * 1.38f, 2.0f, color.r, color.g, color.b, alpha * 0.62f );
        return;
    case EditorTab::OBJECT_HULL_TAPERED_BLOCK:
        draw.Rect( cx - r * 0.54f, cy - r * 0.70f, r * 1.08f, r * 1.40f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx - r * 0.54f,
                       cy - r * 0.70f,
                       cx - r * 1.02f,
                       cy + r * 0.72f,
                       cx - r * 0.54f,
                       cy + r * 0.72f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.86f );
        draw.Triangle( cx + r * 0.54f,
                       cy - r * 0.70f,
                       cx + r * 1.02f,
                       cy + r * 0.72f,
                       cx + r * 0.54f,
                       cy + r * 0.72f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.86f );
        return;
    case EditorTab::OBJECT_HULL_PYRAMID:
        draw.Triangle( cx,
                       cy - r * 0.98f,
                       cx - r * 1.04f,
                       cy + r * 0.82f,
                       cx + r * 1.04f,
                       cy + r * 0.82f,
                       color.r,
                       color.g,
                       color.b,
                       alpha );
        draw.Rect( cx - 1.0f, cy - r * 0.40f, 2.0f, r * 1.10f, color.r, color.g, color.b, alpha * 0.54f );
        return;
    case EditorTab::OBJECT_HULL_HEX_PRISM:
        draw.Rect( cx - r * 0.66f, cy - r * 0.86f, r * 1.32f, r * 1.72f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx - r * 0.66f,
                       cy - r * 0.86f,
                       cx - r * 1.12f,
                       cy,
                       cx - r * 0.66f,
                       cy + r * 0.86f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.82f );
        draw.Triangle( cx + r * 0.66f,
                       cy - r * 0.86f,
                       cx + r * 1.12f,
                       cy,
                       cx + r * 0.66f,
                       cy + r * 0.86f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.82f );
        return;
    case EditorTab::OBJECT_HULL_DIAMOND:
        draw.Triangle( cx, cy - r * 1.08f, cx + r * 0.96f, cy, cx, cy + r * 1.08f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx,
                       cy - r * 1.08f,
                       cx - r * 0.96f,
                       cy,
                       cx,
                       cy + r * 1.08f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.88f );
        return;
    default:
        draw.Triangle( cx - r, cy + r, cx + r, cy + r, cx, cy - r, color.r, color.g, color.b, alpha );
        return;
    }
}


void DrawEditorMiniRockSilhouette( const UIDrawContext& draw,
                                   const UIRect& bounds,
                                   int objectType,
                                   const Style::UIColor& color,
                                   float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.52f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.34f;

    switch ( objectType )
    {
    case EditorTab::OBJECT_ROCK_SLAB:
        draw.Rect( cx - r * 1.12f, cy + r * 0.10f, r * 2.24f, r * 0.56f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx - r * 1.12f,
                       cy + r * 0.10f,
                       cx - r * 0.66f,
                       cy - r * 0.44f,
                       cx - r * 0.10f,
                       cy + r * 0.10f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.78f );
        draw.Triangle( cx + r * 1.12f,
                       cy + r * 0.10f,
                       cx + r * 0.50f,
                       cy - r * 0.52f,
                       cx + r * 0.08f,
                       cy + r * 0.10f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.74f );
        return;
    case EditorTab::OBJECT_ROCK_LUMP:
        draw.RoundedRect( cx - r * 1.02f,
                          cy - r * 0.30f,
                          r * 2.04f,
                          r * 1.02f,
                          r * 0.40f,
                          color.r,
                          color.g,
                          color.b,
                          alpha );
        draw.Triangle( cx - r * 0.94f,
                       cy + r * 0.12f,
                       cx - r * 0.34f,
                       cy - r * 0.86f,
                       cx + r * 0.12f,
                       cy + r * 0.12f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.82f );
        return;
    case EditorTab::OBJECT_ROCK_SHARD:
        draw.Triangle( cx,
                       cy - r * 1.16f,
                       cx - r * 0.58f,
                       cy + r * 0.90f,
                       cx + r * 0.42f,
                       cy + r * 0.90f,
                       color.r,
                       color.g,
                       color.b,
                       alpha );
        draw.Triangle( cx + r * 0.14f,
                       cy - r * 0.58f,
                       cx + r * 0.96f,
                       cy + r * 0.72f,
                       cx + r * 0.40f,
                       cy + r * 0.90f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.72f );
        return;
    case EditorTab::OBJECT_ROCK_CHIPPED:
        draw.Rect( cx - r * 0.86f, cy - r * 0.62f, r * 1.44f, r * 1.36f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx + r * 0.24f,
                       cy - r * 0.62f,
                       cx + r * 0.86f,
                       cy - r * 0.04f,
                       cx + r * 0.58f,
                       cy - r * 0.62f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.58f );
        draw.Triangle( cx - r * 0.86f,
                       cy + r * 0.74f,
                       cx - r * 0.38f,
                       cy + r * 0.28f,
                       cx + r * 0.58f,
                       cy + r * 0.74f,
                       color.r,
                       color.g,
                       color.b,
                       alpha * 0.76f );
        return;
    default:
        draw.Triangle( cx - r, cy + r, cx + r, cy + r, cx, cy - r, color.r, color.g, color.b, alpha );
        return;
    }
}


void DrawEditorMiniIcon( const UIDrawContext& draw,
                         const UIRect& bounds,
                         int objectType,
                         const Style::UIColor& color,
                         float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.31f;
    const int type = std::clamp( objectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );

    if ( type == EditorTab::OBJECT_BALL )
    {
        draw.RoundedRect( cx - r, cy - r, r * 2.0f, r * 2.0f, 999.0f, color.r, color.g, color.b, alpha );
        draw.RoundedRect( cx - r * 0.42f,
                          cy - r * 0.48f,
                          r * 0.38f,
                          r * 0.30f,
                          999.0f,
                          color.r,
                          color.g,
                          color.b,
                          alpha * 0.46f );
        return;
    }
    if ( type == EditorTab::OBJECT_SPHERE )
    {
        draw.RoundedRect( cx - r, cy - r, r * 2.0f, r * 2.0f, 999.0f, color.r, color.g, color.b, alpha * 0.74f );
        draw.Rect( cx - r * 0.72f, cy - 1.0f, r * 1.44f, 2.0f, color.r, color.g, color.b, alpha * 0.90f );
        draw.Rect( cx - 1.0f, cy - r * 0.72f, 2.0f, r * 1.44f, color.r, color.g, color.b, alpha * 0.58f );
        return;
    }
    if ( type == EditorTab::OBJECT_BOX )
    {
        draw.Rect( cx - r * 0.66f, cy - r * 0.86f, r * 1.42f, r * 1.42f, color.r, color.g, color.b, alpha * 0.48f );
        draw.Rect( cx - r * 0.92f, cy - r * 0.58f, r * 1.48f, r * 1.48f, color.r, color.g, color.b, alpha );
        draw.Rect( cx + r * 0.56f, cy - r * 0.38f, r * 0.22f, r * 1.26f, color.r, color.g, color.b, alpha * 0.56f );
        return;
    }
    if ( type == EditorTab::OBJECT_RAGDOLL || type == EditorTab::OBJECT_RAGDOLL_SLEEP )
    {
        draw.RoundedRect( cx - r * 0.34f,
                          cy - r * 1.05f,
                          r * 0.68f,
                          r * 0.68f,
                          999.0f,
                          color.r,
                          color.g,
                          color.b,
                          alpha );
        draw.Rect( cx - r * 0.42f, cy - r * 0.34f, r * 0.84f, r * 0.92f, color.r, color.g, color.b, alpha );
        draw.Rect( cx - r * 1.05f, cy - r * 0.18f, r * 0.52f, r * 0.32f, color.r, color.g, color.b, alpha * 0.82f );
        draw.Rect( cx + r * 0.53f, cy - r * 0.18f, r * 0.52f, r * 0.32f, color.r, color.g, color.b, alpha * 0.82f );
        draw.Rect( cx - r * 0.50f, cy + r * 0.65f, r * 0.34f, r * 0.68f, color.r, color.g, color.b, alpha * 0.82f );
        draw.Rect( cx + r * 0.16f, cy + r * 0.65f, r * 0.34f, r * 0.68f, color.r, color.g, color.b, alpha * 0.82f );
        if ( type == EditorTab::OBJECT_RAGDOLL_SLEEP )
        {
            draw.Rect( cx + r * 0.56f, cy - r * 1.04f, r * 0.48f, 2.0f, color.r, color.g, color.b, alpha );
            draw.Rect( cx + r * 0.70f, cy - r * 0.82f, r * 0.40f, 2.0f, color.r, color.g, color.b, alpha * 0.78f );
        }
        return;
    }
    if ( IsEditorMiniRootType( type ) )
    {
        DrawEditorMiniRootSilhouette( draw, bounds, type == EditorTab::OBJECT_ROOT_LARGE, color, alpha );
        return;
    }

    int treeType = EDITOR_MINI_TREE_TYPE_NONE;
    int treePlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    bool treeSlope = false;
    bool treeShedding = false;
    if ( EditorMiniTreeVisualForType( type, treeType, treePlacement, treeSlope, treeShedding ) )
    {
        DrawEditorMiniTreeSilhouette( draw,
                                      bounds,
                                      treeType,
                                      treePlacement == EDITOR_MINI_TREE_PLACEMENT_ROOTED,
                                      treeSlope,
                                      treeShedding,
                                      color,
                                      alpha );
        return;
    }
    if ( IsEditorMiniHullType( type ) )
    {
        DrawEditorMiniHullSilhouette( draw, bounds, type, color, alpha );
        return;
    }
    if ( IsEditorMiniRockType( type ) )
    {
        DrawEditorMiniRockSilhouette( draw, bounds, type, color, alpha );
        return;
    }

    draw.Triangle( cx - r, cy + r, cx + r, cy + r, cx, cy - r, color.r, color.g, color.b, alpha );
}


void DrawEditorMiniGlyph( const UIDrawContext& draw, const UIRect& bounds, int objectType )
{
    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedRect( bounds.x,
                      bounds.y,
                      bounds.w,
                      bounds.h,
                      6.0f,
                      palette.control.r,
                      palette.control.g,
                      palette.control.b,
                      0.92f );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, palette.border.r, palette.border.g, palette.border.b, 0.75f );
    DrawEditorMiniIcon( draw, bounds, objectType, palette.accentStrong, 0.92f );
}


void DrawEditorMiniVariantMarker( const UIDrawContext& draw,
                                  const UIRect& bounds,
                                  int variant,
                                  const Style::UIColor& color )
{
    const float x = bounds.x + bounds.w - 8.0f;
    const float y = bounds.y + bounds.h - 8.0f;
    if ( variant == EDITOR_MINI_TREE_PLACEMENT_SLEEPING )
    {
        draw.Rect( x - 3.0f, y - 3.0f, 2.0f, 6.0f, color.r, color.g, color.b, 0.92f );
        draw.Rect( x + 1.0f, y - 3.0f, 2.0f, 6.0f, color.r, color.g, color.b, 0.92f );
        return;
    }
    if ( variant == EDITOR_MINI_TREE_PLACEMENT_ROOTED )
    {
        draw.Rect( x - 1.0f, y - 5.0f, 2.0f, 7.0f, color.r, color.g, color.b, 0.92f );
        draw.Triangle( x, y, x - 5.0f, y + 4.0f, x - 1.0f, y + 1.0f, color.r, color.g, color.b, 0.92f );
        draw.Triangle( x, y, x + 5.0f, y + 4.0f, x + 1.0f, y + 1.0f, color.r, color.g, color.b, 0.92f );
        return;
    }
    draw.RoundedRect( x - 3.0f, y - 3.0f, 6.0f, 6.0f, 999.0f, color.r, color.g, color.b, 0.88f );
}


void DrawEditorMiniHoldMarker( const UIDrawContext& draw,
                               const UIRect& bounds,
                               const Style::UIColor& color,
                               bool active )
{
    const float dot = (std::max)( 2.0f, std::floor( bounds.w * 0.075f ) );
    const float x = bounds.x + bounds.w - dot - 5.0f;
    const float y = bounds.y + 5.0f;
    const float gap = (std::max)( 1.0f, dot * 0.75f );
    const float alpha = active ? 0.95f : 0.52f;
    draw.RoundedRect( x, y, dot, dot, 999.0f, color.r, color.g, color.b, alpha );
    draw.RoundedRect( x, y + dot + gap, dot, dot, 999.0f, color.r, color.g, color.b, alpha );
    draw.RoundedRect( x, y + ( dot + gap ) * 2.0f, dot, dot, 999.0f, color.r, color.g, color.b, alpha );
}


void DrawEditorMiniPaletteButton( const UIDrawContext& draw,
                                  const UIRect& bounds,
                                  int objectType,
                                  bool selected,
                                  bool hot,
                                  int variantMarker,
                                  bool holdCapable,
                                  bool holdActive )
{
    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor fill = hot ? palette.controlHover : palette.control;
    if ( selected )
    {
        fill = palette.windowRaised;
    }
    fill.a = selected ? 0.96f : 0.88f;

    draw.RoundedRect( bounds.x + 2.0f,
                      bounds.y + 2.0f,
                      bounds.w,
                      bounds.h,
                      Style::Radii().smallButton,
                      0.0f,
                      0.0f,
                      0.0f,
                      0.20f );
    draw.RoundedPanel( bounds, Style::Radii().smallButton, fill, selected ? palette.accentStrong : palette.border );
    const Style::UIColor icon = selected ? palette.accentStrong : palette.textSecondary;
    const UIRect iconBounds = { bounds.x + 3.0f,
                                bounds.y + 3.0f,
                                (std::max)( 4.0f, bounds.w - 6.0f ),
                                (std::max)( 4.0f, bounds.h - 6.0f ) };
    DrawEditorMiniIcon( draw, iconBounds, objectType, icon, selected || hot ? 0.98f : 0.86f );
    if ( variantMarker >= 0 )
    {
        DrawEditorMiniVariantMarker( draw, bounds, variantMarker, selected ? palette.accentStrong : palette.textMuted );
    }
    if ( holdCapable )
    {
        DrawEditorMiniHoldMarker( draw, bounds, holdActive ? palette.accentStrong : palette.textMuted, holdActive );
    }
}


void DrawEditorMiniTooltip( const UIDrawContext& draw,
                            const UIRect& anchor,
                            const char* label,
                            int screenW,
                            int screenH )
{
    if ( !label || label[0] == '\0' || screenW <= 0 || screenH <= 0 )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const float textSize = 10.5f;
    const float padX = 8.0f;
    const float padY = 5.0f;
    const float margin = 6.0f;
    const float maxTextW =
        (std::max)( 32.0f, (std::min)( 220.0f, static_cast<float>( screenW ) - margin * 2.0f - padX * 2.0f ) );

    char tooltip[80] = {};
    snprintf( tooltip, sizeof( tooltip ), "%s", label );
    Chrome::FitTitleText( tooltip, sizeof( tooltip ), textSize, maxTextW );

    const float textW = Text2d::MeasureText( textSize, tooltip );
    const float w = std::ceil( textW + padX * 2.0f );
    const float h = std::ceil( textSize + padY * 2.0f + 2.0f );

    float x = anchor.x + anchor.w + 10.0f;
    if ( x + w > static_cast<float>( screenW ) - margin )
    {
        x = anchor.x - w - 10.0f;
    }
    const float maxX = (std::max)( margin, static_cast<float>( screenW ) - w - margin );
    x = std::clamp( x, margin, maxX );

    float y = anchor.y + anchor.h * 0.5f - h * 0.5f;
    const float maxY = (std::max)( margin, static_cast<float>( screenH ) - h - margin );
    y = std::clamp( y, margin, maxY );

    draw.RoundedRect( x + 2.0f, y + 2.0f, w, h, Style::Radii().smallButton, 0.0f, 0.0f, 0.0f, 0.28f );
    draw.RoundedPanel( { x, y, w, h }, Style::Radii().smallButton, palette.windowRaised, palette.border );
    draw.Text( x + padX,
               y + padY + 1.0f,
               textSize,
               palette.textPrimary.r,
               palette.textPrimary.g,
               palette.textPrimary.b,
               tooltip );
}


int EditorMiniRagdollObjectType( int mode )
{
    return mode == EDITOR_MINI_RAGDOLL_MODE_SLEEPING ? EditorTab::OBJECT_RAGDOLL_SLEEP : EditorTab::OBJECT_RAGDOLL;
}


bool EditorMiniSelectionRequestsStatic( int holdMode, int treePlacement, bool& outPlaceStatic )
{
    if ( holdMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
    {
        outPlaceStatic = treePlacement != EDITOR_MINI_TREE_PLACEMENT_SLEEPING;
        return true;
    }
    if ( holdMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
    {
        outPlaceStatic = false;
        return true;
    }
    outPlaceStatic = false;
    return false;
}


const char* EditorMiniPaletteEntryLabel( const EditorMiniPaletteEntry& entry )
{
    if ( entry.holdMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
    {
        switch ( entry.treePlacement )
        {
        case EDITOR_MINI_TREE_PLACEMENT_FIXED:
            return "Fixed tree";
        case EDITOR_MINI_TREE_PLACEMENT_SLEEPING:
            return "Sleeping tree";
        case EDITOR_MINI_TREE_PLACEMENT_ROOTED:
            return "Rooted tree";
        default:
            break;
        }
    }
    if ( entry.holdMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
    {
        return "Ragdoll";
    }
    return EditorTab::ObjectLabel( entry.objectType );
}


void DrawEditorMiniPalette( const UIDrawContext& draw,
                            const EditorMiniPaletteLayout& layout,
                            int editorObjectType,
                            bool editorPlaceStatic,
                            int mouseX,
                            int mouseY,
                            int flyoutTreePlacement,
                            int flyoutHoldMode,
                            int pressedEntry,
                            int screenW,
                            int screenH )
{
    const Style::UIPalette& palette = Style::Palette();
    int currentTreePlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    int currentTreeType = EDITOR_MINI_TREE_TYPE_NONE;
    const bool currentTreeState =
        EditorMiniPaletteTreeStateForType( editorObjectType, editorPlaceStatic, currentTreePlacement, currentTreeType );

    const char* tooltipLabel = nullptr;
    UIRect tooltipAnchor = {};

    for ( int i = 0; i < layout.buttonCount; ++i )
    {
        const EditorMiniPaletteEntry& entry = kEditorMiniPaletteEntries[i];
        const bool treeEntry = IsEditorMiniTreePlacementValid( entry.treePlacement );
        const bool ragdollEntry = entry.holdMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES;
        const bool selected = treeEntry ? ( currentTreeState && currentTreePlacement == entry.treePlacement )
                                        : ( ragdollEntry ? ( editorObjectType == EditorTab::OBJECT_RAGDOLL ||
                                                             editorObjectType == EditorTab::OBJECT_RAGDOLL_SLEEP )
                                                         : entry.objectType == editorObjectType );
        const bool hot = layout.buttons[i].Contains( mouseX, mouseY );
        const int marker = treeEntry ? entry.treePlacement
                                     : ( ragdollEntry && editorObjectType == EditorTab::OBJECT_RAGDOLL_SLEEP
                                             ? EDITOR_MINI_TREE_PLACEMENT_SLEEPING
                                             : -1 );
        const bool holdCapable = entry.holdMode != EDITOR_MINI_HOLD_MODE_NONE;
        const bool holdActive = holdCapable && i == pressedEntry && flyoutHoldMode != EDITOR_MINI_HOLD_MODE_NONE;
        DrawEditorMiniPaletteButton( draw,
                                     layout.buttons[i],
                                     entry.objectType,
                                     selected,
                                     hot,
                                     marker,
                                     holdCapable,
                                     holdActive );
        if ( hot )
        {
            tooltipLabel = EditorMiniPaletteEntryLabel( entry );
            tooltipAnchor = layout.buttons[i];
        }
    }

    if ( layout.flyoutVisible )
    {
        draw.RoundedRect( layout.flyoutBounds.x + 2.0f,
                          layout.flyoutBounds.y + 2.0f,
                          layout.flyoutBounds.w,
                          layout.flyoutBounds.h,
                          Style::Radii().control,
                          0.0f,
                          0.0f,
                          0.0f,
                          0.24f );
        draw.RoundedPanel( layout.flyoutBounds, Style::Radii().control, palette.window, palette.border );
        for ( int option = 0; option < layout.flyoutOptionCount; ++option )
        {
            int marker = -1;
            int type = EditorTab::OBJECT_BOX;
            bool selected = false;
            if ( flyoutHoldMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
            {
                marker = flyoutTreePlacement;
                type = EditorMiniTreeObjectType( option, flyoutTreePlacement );
                selected = currentTreeState && currentTreePlacement == flyoutTreePlacement && currentTreeType == option;
            }
            else if ( flyoutHoldMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
            {
                type = EditorMiniRagdollObjectType( option );
                marker = option == EDITOR_MINI_RAGDOLL_MODE_SLEEPING ? EDITOR_MINI_TREE_PLACEMENT_SLEEPING : -1;
                selected = type == editorObjectType;
            }
            const bool hot = layout.flyoutOptions[option].Contains( mouseX, mouseY );
            DrawEditorMiniPaletteButton( draw,
                                         layout.flyoutOptions[option],
                                         type,
                                         selected,
                                         hot,
                                         marker,
                                         false,
                                         false );
            if ( hot )
            {
                tooltipLabel = EditorTab::ObjectLabel( type );
                tooltipAnchor = layout.flyoutOptions[option];
            }
        }
    }

    if ( tooltipLabel )
    {
        DrawEditorMiniTooltip( draw, tooltipAnchor, tooltipLabel, screenW, screenH );
    }
}


void DrawEditorMinimizedWindow( const UIDrawContext& draw,
                                const UIRect& minimized,
                                const InGameUIFrameData& data,
                                int mouseX,
                                int mouseY )
{
    const Style::UIPalette& palette = Style::Palette();
    const EditorMinimizedStatusLayout layout = BuildEditorMinimizedStatusLayout( minimized, data );
    draw.RoundedRect( minimized.x + 4.0f,
                      minimized.y + 5.0f,
                      minimized.w,
                      minimized.h,
                      Style::Radii().window,
                      0.0f,
                      0.0f,
                      0.0f,
                      0.26f );
    draw.RoundedPanel( minimized, Style::Radii().window, palette.window, palette.border );

    DrawEditorMiniGlyph( draw, layout.glyph, data.editorObjectType );

    const char* modeLabel = data.editorPlacementMode ? "Place" : "Gizmo";
    const char* bodyLabel = data.editorPlaceStatic ? "Static" : "Dynamic";
    const char* alignLabel = data.editorTerrainAlign ? "Align" : "Level";

    char shapeLabel[64] = {};
    snprintf( shapeLabel, sizeof( shapeLabel ), "%s", EditorTab::ObjectLabel( data.editorObjectType ) );
    Chrome::FitTitleText( shapeLabel, sizeof( shapeLabel ), 12.0f, layout.labelMaxW );
    draw.Text( layout.labelX,
               minimized.y + 13.0f,
               12.0f,
               palette.textPrimary.r,
               palette.textPrimary.g,
               palette.textPrimary.b,
               shapeLabel );

    Style::UIColor modeFill = palette.accent;
    modeFill.a = 0.92f;
    Style::UIColor bodyFill = data.editorPlaceStatic ? palette.control : palette.warningAccent;
    bodyFill.a = 0.92f;
    Style::UIColor alignFill = data.editorTerrainAlign ? palette.accentStrong : palette.control;
    alignFill.a = 0.92f;
    DrawEditorMiniChip( draw,
                        layout.modeChip.x,
                        layout.modeChip.y,
                        modeLabel,
                        modeFill,
                        palette.textPrimary,
                        layout.modeChip.Contains( mouseX, mouseY ) );
    DrawEditorMiniChip( draw,
                        layout.bodyChip.x,
                        layout.bodyChip.y,
                        bodyLabel,
                        bodyFill,
                        palette.textPrimary,
                        layout.bodyChip.Contains( mouseX, mouseY ) );
    DrawEditorMiniChip( draw,
                        layout.alignChip.x,
                        layout.alignChip.y,
                        alignLabel,
                        alignFill,
                        palette.textPrimary,
                        layout.alignChip.Contains( mouseX, mouseY ) );

    draw.RoundedPanel( layout.restoreButton, Style::Radii().smallButton, palette.control, palette.border );
    const float plusX = layout.restoreButton.x + layout.restoreButton.w * 0.5f;
    const float plusY = layout.restoreButton.y + layout.restoreButton.h * 0.5f;
    draw.Rect( plusX - 5.0f,
               plusY - 1.0f,
               10.0f,
               2.0f,
               palette.textSecondary.r,
               palette.textSecondary.g,
               palette.textSecondary.b,
               0.96f );
    draw.Rect( plusX - 1.0f,
               plusY - 5.0f,
               2.0f,
               10.0f,
               palette.textSecondary.r,
               palette.textSecondary.g,
               palette.textSecondary.b,
               0.96f );
}


} // namespace

bool InGameUI::IsVisible() const
{
    return m_window.isVisible;
}


bool InGameUI::IsMinimized() const
{
    return m_window.isMinimized;
}


void InGameUI::SetVisible( bool visible, double now )
{
    m_window.isVisible = visible;
    m_cache.Reset();
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Visibility );
    if ( visible )
    {
        m_window.isMinimized = false;
        m_scrollbarVisibleUntil = now + 1.2;
        CancelEditorMiniPaletteInteraction();
    }
    else
    {
        m_window.isMinimized = true;
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        m_interaction.blocksCameraMouse = false;
        CancelEditorMiniPaletteInteraction();
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
    }
}


void InGameUI::ToggleVisible( double now )
{
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }
    SetMinimized( !m_window.isMinimized, now );
}


void InGameUI::CancelEditorMiniPaletteInteraction()
{
    m_editorMiniPalettePressActive = false;
    m_editorMiniPaletteFlyoutOpen = false;
    m_editorMiniPalettePressedEntry = -1;
    m_editorMiniPalettePressedObjectType = -1;
    m_editorMiniPalettePressedTreePlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    m_editorMiniPalettePressedHoldMode = EDITOR_MINI_HOLD_MODE_NONE;
    m_editorMiniPalettePressStart = 0.0;
}


void InGameUI::SetMinimized( bool minimized, double now )
{
    if ( m_window.isMinimized == minimized )
    {
        return;
    }

    const UIRect currentBounds = Chrome::WindowRect( m_window );
    const UIRect minimizedBounds = MinimizedRect( m_lastScreenW, m_lastScreenH, m_window.minimizedWidth );
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    CancelEditorMiniPaletteInteraction();
    if ( minimized )
    {
        m_window.isMinimized = true;
        Chrome::BeginWindowAnimation( m_window, currentBounds, minimizedBounds, now, true );
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
        m_activeSlider = 0;
    }
    else
    {
        m_window.isMinimized = false;
        m_cameraModeCombo.Close();
        Chrome::BeginWindowAnimation( m_window, minimizedBounds, Chrome::WindowRect( m_window ), now, false );
        m_scrollbarVisibleUntil = now + 1.2;
    }
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::WindowState );
    m_cache.Reset();
}


void InGameUI::ToggleMaximizeMinimize( int screenW, int screenH, double now )
{
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }

    if ( m_window.isMinimized )
    {
        SetMinimized( false, now );
        return;
    }

    SetMaximized( !m_window.isMaximized, screenW, screenH, now );
}


void InGameUI::SetActiveTab( InGameUITab tab )
{
    const int tabIndex = static_cast<int>( tab );
    if ( tabIndex < 0 || tabIndex >= static_cast<int>( InGameUITab::Count ) )
    {
        tab = InGameUITab::Scene;
    }
    m_activeTab = tab;
    m_scrollY = 0.0f;
    m_rendererCombo.Close();
    m_reflectionCombo.Close();
    CloseSceneCombo();
    m_editorTab.objectCombo.Close();
    CinematicTab::CloseCombo( m_cinematicTab );
    m_renderTargetCombo.Close();
    m_cameraModeCombo.Close();
    m_activeSlider = 0;
    SceneTab::ResetPreviewState( m_sceneTab );
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Content );
    m_cache.Reset();
}


InGameUITab InGameUI::GetActiveTab() const
{
    return m_activeTab;
}


void InGameUI::CancelInputCapture()
{
    m_interaction.leftWasDown = false;
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    m_activeSlider = 0;
    SceneTab::ResetPreviewState( m_sceneTab );
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
    m_editorTab.objectCombo.Close();
    m_renderTargetCombo.Close();
    m_cameraModeCombo.Close();
    CancelEditorMiniPaletteInteraction();
}


bool InGameUI::BlocksCameraMouse() const
{
    return m_interaction.blocksCameraMouse;
}


bool InGameUI::BlocksKeyboard() const
{
    return m_window.isVisible && !m_window.isMinimized &&
           ( m_sceneCombo.IsOpen() || CinematicTab::IsComboOpen( m_cinematicTab ) || m_editorTab.objectCombo.IsOpen() ||
             m_renderTargetCombo.IsOpen() );
}


bool InGameUI::WantsNativeMouseCursor() const
{
    return m_window.isVisible && !m_window.isMinimized;
}


void InGameUI::SetWindowBounds( int x, int y, int width, int height )
{
    m_window.x = x;
    m_window.y = y;
    m_window.width = width;
    m_window.height = height;
    m_window.restoreX = x;
    m_window.restoreY = y;
    m_window.restoreW = width;
    m_window.restoreH = height;
    m_window.hasAppliedDefaultPlacement = true;
    m_window.isMaximized = false;
    m_window.animationActive = false;
    m_scrollY = 0.0f;
    m_scrollbarVisibleUntil = 0.0;
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
    m_cache.Reset();
}


void InGameUI::SetBlurEnabled( bool enabled )
{
    if ( m_blurPreviewEnabled != enabled )
    {
        m_blurPreviewEnabled = enabled;
        m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Toggle );
        m_cache.Reset();
    }
}


void InGameUI::SetRendererComboOpen( bool open )
{
    m_rendererCombo.SetOpen( open );
    if ( open )
    {
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
    }
}


void InGameUI::SetWaterComboOpen( bool open )
{
    m_reflectionCombo.SetOpen( open );
    if ( open )
    {
        m_rendererCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
    }
}


void InGameUI::SetSceneComboOpen( bool open )
{
    m_sceneCombo.SetOpen( open );
    if ( open )
    {
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
        SceneTab::CaptureFilterKeyState( m_sceneTab );
    }
    else
    {
        SceneTab::ClearFilter( m_sceneTab );
    }
}


void InGameUI::SetSceneFilter( const char* filter )
{
    SceneTab::SetFilter( m_sceneTab, filter );
}


void InGameUI::SetProfilerExpandAll( bool expandAll )
{
    ProfilerTab::SetExpandAll( m_profilerTab, expandAll );
    m_cache.Reset();
}


void InGameUI::SetProfilerTimelineEnabled( bool enabled )
{
    ProfilerTab::SetTimelineEnabled( m_profilerTab, enabled );
    m_cache.Reset();
}


void InGameUI::SetPerformanceHistogramEnabled( bool enabled )
{
    ProfilerTab::SetPerformanceHistogramEnabled( m_profilerTab, enabled );
    m_cache.Reset();
}


void InGameUI::SetHitboxOverlayEnabled( bool enabled )
{
    if ( m_hitboxOverlayEnabled != enabled )
    {
        m_hitboxOverlayEnabled = enabled;
        m_cache.Reset();
    }
}


void InGameUI::SetScrollY( float scrollY )
{
    m_scrollY = (std::max)( 0.0f, scrollY );
    m_scrollbarVisibleUntil = 1.2;
    m_cache.Reset();
}


void InGameUI::SetMouseOverride( bool enabled, int x, int y )
{
    m_hasMouseOverride = enabled;
    m_mouseOverrideX = x;
    m_mouseOverrideY = y;
    if ( enabled )
    {
        m_mouseX = x;
        m_mouseY = y;
    }
}


void InGameUI::SetMaximized( bool maximized, int screenW, int screenH, double now )
{
    if ( Chrome::SetMaximized( m_window, maximized, screenW, screenH, now ) )
    {
        m_scrollbarVisibleUntil = 0.0;
        m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
    }
}


void InGameUI::ResetResources()
{
    m_backdropBlur.ResetResources();
    ResetRenderTargetPreviewResources( m_renderTargetPreviewShader, m_renderTargetPreviewVB );
    m_cache.Reset();
}


void InGameUI::DrawHitboxOverlay( const UIDrawContext& draw,
                                  const InGameUIFrameData& data,
                                  const UIRect& windowBounds,
                                  const UIRect& contentBounds,
                                  const UIRect& footerBounds ) const
{
    if ( !m_hitboxOverlayEnabled )
    {
        return;
    }

    constexpr float chromeR = 0.16f;
    constexpr float chromeG = 0.86f;
    constexpr float chromeB = 1.00f;
    constexpr float contentR = 0.30f;
    constexpr float contentG = 1.00f;
    constexpr float contentB = 0.42f;
    constexpr float footerR = 1.00f;
    constexpr float footerG = 0.22f;
    constexpr float footerB = 0.82f;
    constexpr float buttonR = 1.00f;
    constexpr float buttonG = 0.62f;
    constexpr float buttonB = 0.18f;

    DrawHitboxRect( draw, windowBounds, chromeR, chromeG, chromeB, 0.018f, 0.44f );

    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    DrawHitboxRect( draw, titleButtons.minimize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.maximize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.close, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    if ( !m_window.isMaximized )
    {
        DrawHitboxRect(
            draw,
            { windowBounds.x + windowBounds.w - 26.0f, windowBounds.y + windowBounds.h - 26.0f, 26.0f, 26.0f },
            chromeR,
            chromeG,
            chromeB,
            0.050f,
            0.86f );
    }

    DrawTabHitboxes( draw, m_tabBar, static_cast<int>( InGameUITab::Count ) );
    DrawHitboxRect( draw, contentBounds, contentR, contentG, contentB, 0.018f, 0.48f );

    switch ( m_activeTab )
    {
    case InGameUITab::Scene:
        DrawComboHitboxes( draw,
                           m_sceneCombo,
                           SceneDropdownHitboxOptionCount( m_sceneTab, data ),
                           contentR,
                           contentG,
                           contentB );
        DrawHitboxRect( draw, m_resetSceneButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_resetDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_saveDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_sceneTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Editor:
        DrawHitboxRect( draw, m_editorTab.editorModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_editorTab.placementModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_editorTab.staticObjectToggle.Bounds(), contentR, contentG, contentB );
        DrawComboHitboxes( draw, m_editorTab.objectCombo, EditorTab::OBJECT_TYPE_COUNT, contentR, contentG, contentB );
        break;
    case InGameUITab::Physics:
        for ( int i = 0; i < 12; ++i )
        {
            DrawHitboxRect( draw, m_physicsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }
        DrawHitboxRect( draw, m_physicsTab.pipelinePrevButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_physicsTab.pipelineNextButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_physicsTab.alphaSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.contactLingerSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.rayImpulseSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.launcherProjectileSpeedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.worldGravitySlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoRadiusSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoInwardSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoSwirlSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoLiftSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Options:
        for ( int i = 0; i < 6; ++i )
        {
            DrawHitboxRect( draw, m_optionsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }
        DrawHitboxRect( draw, m_optionsTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_optionsTab.modelCountSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Render:
        DrawHitboxRect( draw, m_renderShadowToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_saveRenderDefaultsButton.Bounds(), contentR, contentG, contentB );
        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            DrawHitboxRect( draw, m_renderSliders[i].Bounds(), contentR, contentG, contentB );
        }
        break;
    case InGameUITab::Targets:
        DrawComboHitboxes( draw, m_renderTargetCombo, m_lastRenderTargetPreviewCount, contentR, contentG, contentB );
        break;
    case InGameUITab::Keys:
        DrawHitboxRect( draw, m_controlsTab.seedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.solverBallSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.solverBoxSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.worldFluidHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.worldFluidDensitySlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Cinematic:
        CinematicTab::DrawHitboxes( m_cinematicTab, draw, data, contentR, contentG, contentB );
        break;
    case InGameUITab::Profiler:
        DrawHitboxRect( draw, m_profilerTab.workerToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_profilerTab.workerThreadSlider.Bounds(), contentR, contentG, contentB );
        break;
    default:
        break;
    }

    if ( ContentHeight() > static_cast<int>( contentBounds.h ) )
    {
        DrawHitboxRect( draw, m_scrollBar.Bounds(), 0.18f, 0.82f, 0.95f, 0.060f, 0.86f );
    }

    DrawHitboxRect( draw, footerBounds, footerR, footerG, footerB, 0.020f, 0.54f );
    DrawComboHitboxes( draw, m_rendererCombo, 1, footerR, footerG, footerB );
    DrawComboHitboxes( draw, m_reflectionCombo, 3, footerR, footerG, footerB );
    DrawHitboxRect( draw, m_blurToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_vsyncToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_histogramToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_timelineToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_hitboxToggle.Bounds(), footerR, footerG, footerB );
}


int InGameUI::ContentHeight() const
{
    switch ( m_activeTab )
    {
    case InGameUITab::Scene:
        return SceneTab::ContentHeight();
    case InGameUITab::Keys:
        return ControlsTab::ContentHeight();
    case InGameUITab::Profiler:
        return ProfilerTab::ContentHeight( m_profilerTab );
    case InGameUITab::Editor:
        return EditorTab::ContentHeight();
    case InGameUITab::Physics:
        return PhysicsTab::ContentHeight();
    case InGameUITab::Options:
        return OptionsTab::ContentHeight();
    case InGameUITab::Render:
        return RenderContentHeight();
    case InGameUITab::Targets:
        return RenderTargetsContentHeight();
    case InGameUITab::Cinematic:
        return CinematicTab::ContentHeight();
    default:
        return ControlsTab::ContentHeight();
    }
}


void InGameUI::CloseSceneCombo()
{
    SceneTab::CloseCombo( m_sceneTab, m_sceneCombo );
}


InGameUIInputResult InGameUI::UpdateInput( HWND hwnd,
                                           int screenW,
                                           int screenH,
                                           double now,
                                           bool editorModeEnabled,
                                           bool editorPlacementMode,
                                           bool editorPlaceStatic,
                                           bool editorTerrainAlign,
                                           int editorObjectType,
                                           int cameraModeIndex,
                                           uint32_t cameraModeEnabledMask,
                                           const char* const* sceneOptions,
                                           int sceneOptionCount,
                                           int selectedSceneOption )
{
    PROFILE_SCOPED( "Frame/UI/Input" );
    InGameUIInputResult result;
    editorObjectType = std::clamp( editorObjectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );
    (void)editorObjectType;
    cameraModeIndex = std::clamp( cameraModeIndex, 0, CAMERA_MODE_OPTION_COUNT - 1 );
    cameraModeEnabledMask &= ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u;
    m_interaction.blocksCameraMouse = false;
    const InputControl::UIInputSnapshot input = InputControl::CaptureSnapshot( m_interaction.leftWasDown,
                                                                               m_hasMouseOverride,
                                                                               m_mouseOverrideX,
                                                                               m_mouseOverrideY );
    const int wheelDelta = input.wheelDelta;
    result.unhandledWheelDelta = wheelDelta;
    if ( !m_window.isVisible )
    {
        return result;
    }
    ProfilerTab::ApplyDefaultExpansion( m_profilerTab );

    m_mouseX = input.mouseX;
    m_mouseY = input.mouseY;

    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    const int minW = 520;
    const int minH = 250;
    const int margin = 10;
    const int titleH = 44;
    const int tabH = 44;
    const int bottomH = 78;
    const int contentPad = 18;
    const int maxW = (std::max)( minW, screenW - margin * 2 );
    const int maxH = (std::max)( minH, screenH - margin * 2 );

    if ( !m_window.hasAppliedDefaultPlacement )
    {
        Chrome::ApplyDefaultWindowPlacement( m_window, screenW, screenH );
    }
    Chrome::ClampWindowToScreen( m_window, screenW, screenH, minW, minH, margin );

    const bool leftNow = input.leftDown;
    if ( m_window.isMinimized )
    {
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        const bool insideMinimized = minimized.Contains( m_mouseX, m_mouseY );
        const bool showEditorMiniPalette = editorModeEnabled;
        const UIRect cameraModeComboBounds = MinimizedCameraModeComboBounds( minimized );
        m_cameraModeCombo.SetLabelVisible( false );
        m_cameraModeCombo.SetBounds( cameraModeComboBounds.x,
                                     cameraModeComboBounds.y,
                                     cameraModeComboBounds.w,
                                     cameraModeComboBounds.h );
        m_cameraModeCombo.SetDropUp( true );
        bool cameraModeComboHandled = false;
        bool insideCameraModeCombo = false;
        const uint32_t cameraModeDisabledMask = ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) & ~cameraModeEnabledMask;
        if ( !showEditorMiniPalette )
        {
            if ( m_editorMiniPalettePressActive )
            {
                InputControl::EndMouseCapture();
            }
            CancelEditorMiniPaletteInteraction();

            const bool comboOptionHit =
                m_cameraModeCombo.IsOpen() &&
                m_cameraModeCombo.HitOption( m_mouseX, m_mouseY, CAMERA_MODE_OPTION_COUNT ) >= 0;
            const bool comboDropdownHit =
                m_cameraModeCombo.IsOpen() &&
                m_cameraModeCombo.DropdownBounds( CAMERA_MODE_OPTION_COUNT ).Contains( m_mouseX, m_mouseY );
            insideCameraModeCombo =
                m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) || comboOptionHit || comboDropdownHit;
            if ( input.leftPressed && m_cameraModeCombo.IsOpen() )
            {
                const int option = m_cameraModeCombo.HitOption( m_mouseX, m_mouseY, CAMERA_MODE_OPTION_COUNT );
                const bool optionDisabled =
                    option >= 0 && option < 32 && ( cameraModeDisabledMask & ( 1u << option ) ) != 0;
                if ( option >= 0 && option < CAMERA_MODE_OPTION_COUNT && !optionDisabled )
                {
                    result.commands.run.requestedCameraMode = option;
                    result.commands.ui.userInteracted = true;
                    m_cameraModeCombo.Close();
                    m_cache.Reset();
                    cameraModeComboHandled = true;
                }
                else if ( m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) )
                {
                    m_cameraModeCombo.ToggleOpen();
                    result.commands.ui.userInteracted = true;
                    m_cache.Reset();
                    cameraModeComboHandled = true;
                }
                else if ( option < 0 )
                {
                    m_cameraModeCombo.Close();
                    result.commands.ui.userInteracted = true;
                    m_cache.Reset();
                    cameraModeComboHandled = true;
                }
            }
            else if ( input.leftPressed && m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_cameraModeCombo.ToggleOpen();
                result.commands.ui.userInteracted = true;
                m_cache.Reset();
                cameraModeComboHandled = true;
            }
        }
        else
        {
            m_cameraModeCombo.Close();
        }

        EditorMiniPaletteLayout editorMiniPalette;
        bool editorMiniPaletteHandled = false;
        bool insideEditorMiniPalette = false;
        bool editorMinimizedStatusHandled = false;
        bool insideEditorMinimizedStatusControl = false;
        if ( showEditorMiniPalette )
        {
            const EditorMinimizedStatusLayout statusLayout = BuildEditorMinimizedStatusLayout( minimized,
                                                                                               editorPlacementMode,
                                                                                               editorPlaceStatic,
                                                                                               editorTerrainAlign );
            const bool insideModeChip = statusLayout.modeChip.Contains( m_mouseX, m_mouseY );
            const bool insideBodyChip = statusLayout.bodyChip.Contains( m_mouseX, m_mouseY );
            const bool insideAlignChip = statusLayout.alignChip.Contains( m_mouseX, m_mouseY );
            insideEditorMinimizedStatusControl = insideModeChip || insideBodyChip || insideAlignChip;
            if ( input.leftPressed && insideModeChip )
            {
                result.commands.editor.togglePlacementMode = true;
                result.commands.ui.userInteracted = true;
                editorMinimizedStatusHandled = true;
            }
            else if ( input.leftPressed && insideBodyChip )
            {
                result.commands.editor.togglePlaceStatic = true;
                result.commands.ui.userInteracted = true;
                editorMinimizedStatusHandled = true;
            }
            else if ( input.leftPressed && insideAlignChip )
            {
                result.commands.editor.toggleTerrainAlign = true;
                result.commands.ui.userInteracted = true;
                editorMinimizedStatusHandled = true;
            }

            if ( m_editorMiniPalettePressActive && !leftNow && !input.leftReleased )
            {
                CancelEditorMiniPaletteInteraction();
                InputControl::EndMouseCapture();
            }

            if ( m_editorMiniPalettePressActive && !m_editorMiniPaletteFlyoutOpen &&
                 m_editorMiniPalettePressedHoldMode != EDITOR_MINI_HOLD_MODE_NONE &&
                 now - m_editorMiniPalettePressStart >= EDITOR_MINI_HOLD_SECONDS )
            {
                m_editorMiniPaletteFlyoutOpen = true;
            }

            editorMiniPalette = BuildEditorMiniPaletteLayout( screenW,
                                                              screenH,
                                                              minimized,
                                                              m_editorMiniPalettePressedEntry,
                                                              m_editorMiniPaletteFlyoutOpen );
            insideEditorMiniPalette = EditorMiniPaletteContains( editorMiniPalette, m_mouseX, m_mouseY );

            const auto SelectEditorMiniPaletteObject =
                [&]( int objectType, bool requestPlaceStatic = false, bool placeStatic = false ) -> void
            {
                result.commands.editor.requestedObjectType =
                    std::clamp( objectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );
                if ( requestPlaceStatic )
                {
                    result.commands.editor.requestPlaceStatic = true;
                    result.commands.editor.requestedPlaceStatic = placeStatic;
                }
                result.commands.editor.enterPlacementMode = true;
                result.commands.ui.userInteracted = true;
                editorMiniPaletteHandled = true;
            };

            if ( input.leftPressed && insideEditorMiniPalette )
            {
                const int pressedButton = HitEditorMiniPaletteButton( editorMiniPalette, m_mouseX, m_mouseY );
                if ( pressedButton >= 0 )
                {
                    const EditorMiniPaletteEntry& entry = kEditorMiniPaletteEntries[pressedButton];
                    if ( entry.holdMode != EDITOR_MINI_HOLD_MODE_NONE )
                    {
                        m_editorMiniPalettePressActive = true;
                        m_editorMiniPaletteFlyoutOpen = false;
                        m_editorMiniPalettePressedEntry = pressedButton;
                        m_editorMiniPalettePressedObjectType = entry.objectType;
                        m_editorMiniPalettePressedTreePlacement = entry.treePlacement;
                        m_editorMiniPalettePressedHoldMode = entry.holdMode;
                        m_editorMiniPalettePressStart = now;
                        result.commands.ui.userInteracted = true;
                        editorMiniPaletteHandled = true;
                        InputControl::BeginMouseCapture( hwnd );
                    }
                    else
                    {
                        SelectEditorMiniPaletteObject( entry.objectType );
                    }
                }
            }

            if ( m_editorMiniPalettePressActive )
            {
                result.commands.ui.userInteracted = true;
                editorMiniPaletteHandled = true;
                if ( input.leftReleased )
                {
                    int selectedObjectType = -1;
                    bool requestPlaceStatic = false;
                    bool requestedPlaceStatic = false;
                    if ( m_editorMiniPaletteFlyoutOpen )
                    {
                        const int flyoutOption =
                            HitEditorMiniPaletteFlyoutOption( editorMiniPalette, m_mouseX, m_mouseY );
                        if ( flyoutOption >= 0 )
                        {
                            if ( m_editorMiniPalettePressedHoldMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
                            {
                                selectedObjectType =
                                    EditorMiniTreeObjectType( flyoutOption, m_editorMiniPalettePressedTreePlacement );
                            }
                            else if ( m_editorMiniPalettePressedHoldMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
                            {
                                selectedObjectType = EditorMiniRagdollObjectType( flyoutOption );
                            }
                        }
                    }
                    else if ( m_editorMiniPalettePressedEntry >= 0 &&
                              m_editorMiniPalettePressedEntry < editorMiniPalette.buttonCount &&
                              editorMiniPalette.buttons[m_editorMiniPalettePressedEntry].Contains( m_mouseX,
                                                                                                   m_mouseY ) )
                    {
                        selectedObjectType = m_editorMiniPalettePressedObjectType;
                    }

                    if ( selectedObjectType >= 0 )
                    {
                        requestPlaceStatic = EditorMiniSelectionRequestsStatic( m_editorMiniPalettePressedHoldMode,
                                                                                m_editorMiniPalettePressedTreePlacement,
                                                                                requestedPlaceStatic );
                        SelectEditorMiniPaletteObject( selectedObjectType, requestPlaceStatic, requestedPlaceStatic );
                    }
                    CancelEditorMiniPaletteInteraction();
                    InputControl::EndMouseCapture();
                }
            }
        }

        if ( insideMinimized || insideCameraModeCombo || cameraModeComboHandled || insideEditorMiniPalette ||
             insideEditorMinimizedStatusControl || m_editorMiniPalettePressActive )
        {
            result.unhandledWheelDelta = 0;
        }
        if ( input.leftPressed && insideMinimized && !cameraModeComboHandled && !editorMiniPaletteHandled &&
             !editorMinimizedStatusHandled )
        {
            SetMinimized( false, now );
            result.commands.ui.userInteracted = true;
        }
        m_interaction.leftWasDown = leftNow;
        m_interaction.blocksCameraMouse = insideMinimized || insideCameraModeCombo || cameraModeComboHandled ||
                                          insideEditorMiniPalette || insideEditorMinimizedStatusControl ||
                                          m_editorMiniPalettePressActive;
        return result;
    }

    const UIRect inputBounds = Chrome::CurrentWindowRect( m_window, now );
    const int inputX = static_cast<int>( std::round( inputBounds.x ) );
    const int inputY = static_cast<int>( std::round( inputBounds.y ) );
    const int inputW = static_cast<int>( std::round( inputBounds.w ) );
    const int inputH = static_cast<int>( std::round( inputBounds.h ) );
    const UIRect inputHitBounds = { static_cast<float>( inputX ),
                                    static_cast<float>( inputY ),
                                    static_cast<float>( inputW ),
                                    static_cast<float>( inputH ) };
    const bool inside =
        m_mouseX >= inputX && m_mouseX <= inputX + inputW && m_mouseY >= inputY && m_mouseY <= inputY + inputH;
    const bool inTitle = inside && m_mouseY < inputY + titleH;
    const bool inTabs = inside && m_mouseY >= inputY + titleH && m_mouseY < inputY + titleH + tabH;
    const bool inResize =
        !m_window.isMaximized && inside && Chrome::IsResizeHotspot( inputHitBounds, m_mouseX, m_mouseY );
    const int contentY = inputY + titleH + tabH + 12;
    const int contentH = (std::max)( 24, inputH - titleH - tabH - bottomH - contentPad );
    const int bottomY = inputY + inputH - bottomH;
    const bool inContent = inside && m_mouseY >= contentY && m_mouseY <= contentY + contentH;
    const float maxScroll = static_cast<float>( (std::max)( 0, ContentHeight() - contentH ) );
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( inputHitBounds );

    if ( inside )
    {
        result.unhandledWheelDelta = 0;
    }

    m_tabBar.SetBounds( static_cast<float>( inputX + 14 ),
                        static_cast<float>( inputY + titleH ),
                        static_cast<float>( inputW - 28 ),
                        static_cast<float>( tabH ) );
    const float footerX = static_cast<float>( inputX );
    const float footerY = static_cast<float>( bottomY );
    const UIRect rendererComboBounds = FooterRendererComboBounds( footerX, footerY );
    const UIRect waterComboBounds = FooterWaterComboBounds( footerX, footerY );
    const UIRect blurBounds = FooterBlurBounds( footerX, footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( footerX, footerY );
    const UIRect hitboxBounds = FooterHitboxBounds( footerX, footerY );
    const UIRect timelineBounds = FooterTimelineBounds( footerX, footerY );
    const UIRect perfBounds = FooterPerfBounds( footerX, footerY );
    m_rendererCombo.SetBounds( rendererComboBounds.x,
                               rendererComboBounds.y,
                               rendererComboBounds.w,
                               rendererComboBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurBounds.x, blurBounds.y, blurBounds.w, blurBounds.h );
    m_vsyncToggle.SetBounds( vsyncBounds.x, vsyncBounds.y, vsyncBounds.w, vsyncBounds.h );
    m_hitboxToggle.SetBounds( hitboxBounds.x, hitboxBounds.y, hitboxBounds.w, hitboxBounds.h );
    m_histogramToggle.SetBounds( perfBounds.x, perfBounds.y, perfBounds.w, perfBounds.h );
    m_timelineToggle.SetBounds( timelineBounds.x, timelineBounds.y, timelineBounds.w, timelineBounds.h );
    {
        const float contentX = static_cast<float>( inputX + contentPad );
        const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
        const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
        m_renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );
        m_renderTargetCombo.SetDropUp( false );
    }

    if ( ( leftNow && ( inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0 ) ) ||
         ( wheelDelta != 0 && inside ) )
    {
        result.commands.ui.userInteracted = true;
    }

    if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::UpdateFilterTyping( m_sceneTab, m_sceneCombo, result, sceneOptions, sceneOptionCount );
    }

    bool wheelHandled = false;
    if ( wheelDelta != 0 && m_sceneCombo.IsOpen() && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = static_cast<float>( inputX + contentPad );
        const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
        const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
        wheelHandled = SceneTab::HandleComboWheel( m_sceneTab,
                                                   m_sceneCombo,
                                                   sceneOptions,
                                                   sceneOptionCount,
                                                   m_mouseX,
                                                   m_mouseY,
                                                   wheelDelta,
                                                   contentX,
                                                   rowBase,
                                                   contentW );
    }

    if ( wheelDelta != 0 && inContent && !wheelHandled )
    {
        m_scrollY -= static_cast<float>( wheelDelta ) / static_cast<float>( WHEEL_DELTA ) * 42.0f;
        m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
        m_scrollbarVisibleUntil = now + 1.4;
    }

    if ( input.leftPressed )
    {
        if ( titleButtons.minimize.Contains( m_mouseX, m_mouseY ) || titleButtons.close.Contains( m_mouseX, m_mouseY ) )
        {
            SetMinimized( true, now );
        }
        else if ( titleButtons.maximize.Contains( m_mouseX, m_mouseY ) )
        {
            SetMaximized( !m_window.isMaximized, screenW, screenH, now );
        }
        else if ( inResize )
        {
            m_interaction.isResizing = true;
            m_interaction.resizeStartMouseX = m_mouseX;
            m_interaction.resizeStartMouseY = m_mouseY;
            m_interaction.resizeStartW = inputW;
            m_interaction.resizeStartH = inputH;
            InputControl::BeginMouseCapture( hwnd );
        }
        else if ( inTitle )
        {
            m_interaction.isDragging = true;
            m_interaction.dragOffsetX = m_mouseX - inputX;
            m_interaction.dragOffsetY = m_mouseY - inputY;
            InputControl::BeginMouseCapture( hwnd );
        }
        else if ( inTabs )
        {
            static const int kTabCount = static_cast<int>( InGameUITab::Count );
            const int index = m_tabBar.HitTest( m_mouseX, m_mouseY, kTabCount );
            if ( index >= 0 && index < kTabCount )
            {
                SetActiveTab( static_cast<InGameUITab>( index ) );
                m_scrollbarVisibleUntil = now + 1.0;
            }
        }
        else if ( m_sceneCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Scene )
            {
                const float contentX = static_cast<float>( inputX + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
                SceneTab::HandleOpenComboClick( m_sceneTab,
                                                m_sceneCombo,
                                                m_resetSceneButton,
                                                m_resetDefaultsButton,
                                                m_saveDefaultsButton,
                                                result,
                                                sceneOptions,
                                                sceneOptionCount,
                                                m_mouseX,
                                                m_mouseY,
                                                contentX,
                                                rowBase,
                                                contentW );
            }
            else
            {
                CloseSceneCombo();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
            m_renderTargetCombo.Close();
        }
        else if ( CinematicTab::IsComboOpen( m_cinematicTab ) )
        {
            CinematicTab::HandleOpenComboClick( m_cinematicTab,
                                                result,
                                                sceneOptions,
                                                sceneOptionCount,
                                                m_mouseX,
                                                m_mouseY );
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            m_editorTab.objectCombo.Close();
            m_renderTargetCombo.Close();
        }
        else if ( m_renderTargetCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Targets )
            {
                const int option = m_renderTargetCombo.HitOption( m_mouseX, m_mouseY, m_lastRenderTargetPreviewCount );
                const bool optionDisabled =
                    option >= 0 && option < 32 && ( m_lastRenderTargetDisabledMask & ( 1u << option ) ) != 0;
                if ( option >= 0 && option < m_lastRenderTargetPreviewCount && !optionDisabled )
                {
                    m_selectedRenderTargetPreview = option;
                    m_renderTargetCombo.Close();
                    m_cache.Reset();
                }
                else if ( m_renderTargetCombo.HitBox( m_mouseX, m_mouseY ) )
                {
                    m_renderTargetCombo.ToggleOpen();
                }
                else if ( option < 0 )
                {
                    m_renderTargetCombo.Close();
                }
            }
            else
            {
                m_renderTargetCombo.Close();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
        else if ( m_editorTab.objectCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Editor )
            {
                const float contentX = static_cast<float>( inputX + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
                EditorTab::HandleContentClick( m_editorTab, result, m_mouseX, m_mouseY, contentX, rowBase, contentW );
            }
            else
            {
                m_editorTab.objectCombo.Close();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_renderTargetCombo.Close();
        }
        else if ( m_reflectionCombo.IsOpen() )
        {
            const int option = m_reflectionCombo.HitOption( m_mouseX, m_mouseY, 3 );
            if ( option >= 0 && option < 3 )
            {
                result.commands.water.requestedWaterReflectionMode = option;
                m_reflectionCombo.Close();
            }
            else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
            }
            else
            {
                m_reflectionCombo.Close();
            }
            m_rendererCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
            m_renderTargetCombo.Close();
        }
        else if ( m_rendererCombo.IsOpen() )
        {
            const int option = m_rendererCombo.HitOption( m_mouseX, m_mouseY, 1 );
            if ( option == 0 )
            {
                m_rendererCombo.Close();
            }
            else if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                CinematicTab::CloseCombo( m_cinematicTab );
                m_editorTab.objectCombo.Close();
                m_renderTargetCombo.Close();
            }
            else if ( !m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Profiler )
        {
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            if ( ProfilerTab::HandleContentClick( m_profilerTab,
                                                  result,
                                                  m_activeSlider,
                                                  inputX + contentPad,
                                                  contentY,
                                                  contentW,
                                                  m_scrollY,
                                                  m_mouseX,
                                                  m_mouseY,
                                                  m_lastWorkerThreadCount,
                                                  m_lastMaxWorkerThreadCount ) )
            {
                InputControl::BeginMouseCapture( hwnd );
                m_scrollbarVisibleUntil = now + 1.2;
            }
            m_rendererCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Scene )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const bool sceneClickHandled = SceneTab::HandleContentClick( m_sceneTab,
                                                                         m_sceneCombo,
                                                                         m_resetSceneButton,
                                                                         m_resetDefaultsButton,
                                                                         m_saveDefaultsButton,
                                                                         result,
                                                                         m_activeSlider,
                                                                         sceneOptions,
                                                                         sceneOptionCount,
                                                                         selectedSceneOption,
                                                                         m_mouseX,
                                                                         m_mouseY,
                                                                         contentX,
                                                                         rowBase,
                                                                         contentW );
            m_rendererCombo.Close();
            if ( sceneClickHandled )
            {
                m_reflectionCombo.Close();
                CinematicTab::CloseCombo( m_cinematicTab );
                m_editorTab.objectCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Editor )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            EditorTab::HandleContentClick( m_editorTab, result, m_mouseX, m_mouseY, contentX, rowBase, contentW );
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
        }
        else if ( inContent && m_activeTab == InGameUITab::Physics )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const int previousActiveSlider = m_activeSlider;
            if ( PhysicsTab::HandleContentClick( m_physicsTab,
                                                 result,
                                                 m_activeSlider,
                                                 m_mouseX,
                                                 m_mouseY,
                                                 contentX,
                                                 rowBase,
                                                 contentW ) &&
                 m_activeSlider != 0 && m_activeSlider != previousActiveSlider )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Options )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            if ( OptionsTab::HandleContentClick( m_optionsTab,
                                                 result,
                                                 m_activeSlider,
                                                 m_mouseX,
                                                 m_mouseY,
                                                 contentX,
                                                 rowBase,
                                                 contentW,
                                                 m_lastModelCapacity ) )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Render )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
            bool capturedSlider = false;

            const float colW = (std::max)( 148.0f, contentW * 0.46f );
            m_renderShadowToggle.SetBounds( contentX, scrolledY + UI_RENDER_FEATURE_START_Y, colW, 24.0f );
            m_saveRenderDefaultsButton.SetBounds( contentX + contentW - UI_RENDER_SAVE_BUTTON_W,
                                                  scrolledY + UI_RENDER_FEATURE_START_Y,
                                                  UI_RENDER_SAVE_BUTTON_W,
                                                  24.0f );
            if ( m_renderShadowToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.renderTuning.toggleShadows = true;
            }
            else if ( m_saveRenderDefaultsButton.HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.renderTuning.saveDefaults = true;
            }
            else
            {
                const float rowBase = scrolledY + UI_RENDER_START_Y;
                for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
                {
                    m_renderSliders[i].SetBounds( contentX, RenderSliderY( i, rowBase ), contentW, 34.0f );
                    if ( m_renderSliders[i].HitTest( m_mouseX, m_mouseY ) )
                    {
                        m_activeSlider = UI_RENDER_SLIDER_BASE + i;
                        SetRenderSliderResult( result, m_renderSliders[i], m_mouseX, kRenderSliderSpecs[i] );
                        capturedSlider = true;
                        break;
                    }
                }
            }

            if ( capturedSlider )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_renderTargetCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Targets )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
            m_renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );
            if ( m_renderTargetCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_renderTargetCombo.ToggleOpen();
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                CinematicTab::CloseCombo( m_cinematicTab );
                m_editorTab.objectCombo.Close();
            }
            else
            {
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                CinematicTab::CloseCombo( m_cinematicTab );
                m_editorTab.objectCombo.Close();
                m_renderTargetCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Cinematic )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
            const bool capturedSlider = CinematicTab::HandleContentClick( m_cinematicTab,
                                                                          result,
                                                                          m_activeSlider,
                                                                          m_mouseX,
                                                                          m_mouseY,
                                                                          contentX,
                                                                          scrolledY,
                                                                          contentW );

            if ( capturedSlider )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Keys )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            if ( ControlsTab::HandleContentClick( m_controlsTab,
                                                  result,
                                                  m_activeSlider,
                                                  m_mouseX,
                                                  m_mouseY,
                                                  contentX,
                                                  rowBase,
                                                  contentW,
                                                  m_lastModelCapacity,
                                                  m_lastSolverBallCount,
                                                  m_lastSolverBoxCount ) )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
        }
        else if ( inside && m_mouseY >= inputY + inputH - bottomH )
        {
            if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                CinematicTab::CloseCombo( m_cinematicTab );
                m_editorTab.objectCombo.Close();
                m_renderTargetCombo.Close();
            }
            else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
                m_rendererCombo.Close();
                CloseSceneCombo();
                CinematicTab::CloseCombo( m_cinematicTab );
                m_editorTab.objectCombo.Close();
                m_renderTargetCombo.Close();
            }
            else if ( m_blurToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                m_blurPreviewEnabled = !m_blurPreviewEnabled;
                m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Toggle );
            }
            else if ( m_vsyncToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.renderer.toggleVsync = true;
            }
            else if ( m_hitboxToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetHitboxOverlayEnabled( !m_hitboxOverlayEnabled );
            }
            else if ( m_histogramToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetPerformanceHistogramEnabled( !ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) );
            }
            else if ( m_timelineToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetProfilerTimelineEnabled( !ProfilerTab::TimelineEnabled( m_profilerTab ) );
            }
        }
        else
        {
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
            m_renderTargetCombo.Close();
        }
    }

    if ( leftNow && m_activeSlider != 0 )
    {
        // Sliders update previews continuously while dragged.  Heavy operations
        // such as rebuilding generated bodies are delayed until mouse release,
        // but cheap scalar controls are emitted every frame for immediate feedback.
        if ( !SceneTab::UpdateActiveSlider( m_sceneTab, m_activeSlider, m_mouseX, result ) &&
             !ProfilerTab::UpdateActiveSlider( m_profilerTab,
                                               m_activeSlider,
                                               m_mouseX,
                                               m_lastMaxWorkerThreadCount,
                                               result ) &&
             !OptionsTab::UpdateActiveSlider( m_optionsTab, m_activeSlider, m_mouseX, m_lastModelCapacity, result ) &&
             !PhysicsTab::UpdateActiveSlider( m_physicsTab, m_activeSlider, m_mouseX, result ) )
        {
            const int renderSlider = RenderSliderIndexFromActiveSlider( m_activeSlider );
            if ( renderSlider >= 0 )
            {
                SetRenderSliderResult( result,
                                       m_renderSliders[renderSlider],
                                       m_mouseX,
                                       kRenderSliderSpecs[renderSlider] );
            }
            else
            {
                if ( !CinematicTab::UpdateActiveSlider( m_cinematicTab, m_activeSlider, m_mouseX, result ) )
                {
                    ControlsTab::UpdateActiveSlider( m_controlsTab,
                                                     m_activeSlider,
                                                     m_mouseX,
                                                     m_lastModelCapacity,
                                                     m_lastSolverBallCount,
                                                     m_lastSolverBoxCount,
                                                     result );
                }
            }
        }
    }

    if ( leftNow && m_interaction.isDragging )
    {
        const int oldX = m_window.x;
        const int oldY = m_window.y;
        m_window.x = std::clamp( m_mouseX - m_interaction.dragOffsetX,
                                 margin,
                                 (std::max)( margin, screenW - m_window.width - margin ) );
        m_window.y = std::clamp( m_mouseY - m_interaction.dragOffsetY,
                                 margin,
                                 (std::max)( margin, screenH - m_window.height - margin ) );
        if ( oldX != m_window.x || oldY != m_window.y )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }
    if ( leftNow && m_interaction.isResizing )
    {
        const int oldW = m_window.width;
        const int oldH = m_window.height;
        m_window.width =
            std::clamp( m_interaction.resizeStartW + m_mouseX - m_interaction.resizeStartMouseX, minW, maxW );
        m_window.height =
            std::clamp( m_interaction.resizeStartH + m_mouseY - m_interaction.resizeStartMouseY, minH, maxH );
        m_scrollbarVisibleUntil = now + 1.4;
        if ( oldW != m_window.width || oldH != m_window.height )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }

    if ( input.leftReleased )
    {
        // Commit deferred slider previews exactly once on release.  This avoids
        // rebuilding solver objects or generated model pools every mouse-move
        // while still letting the drawn slider thumb track the user's drag.
        if ( !SceneTab::CommitActiveSlider( m_sceneTab, m_activeSlider, result ) &&
             !ProfilerTab::CommitActiveSlider( m_profilerTab, m_activeSlider, result ) &&
             !OptionsTab::CommitActiveSlider( m_optionsTab, m_activeSlider, result ) &&
             !PhysicsTab::CommitActiveSlider( m_physicsTab, m_activeSlider, result ) )
        {
            const int renderSlider = RenderSliderIndexFromActiveSlider( m_activeSlider );
            if ( renderSlider >= 0 )
            {
                SetRenderSliderResult( result,
                                       m_renderSliders[renderSlider],
                                       m_mouseX,
                                       kRenderSliderSpecs[renderSlider] );
            }
            else
            {
                if ( !CinematicTab::CommitActiveSlider( m_cinematicTab, m_activeSlider, m_mouseX, result ) )
                {
                    ControlsTab::CommitActiveSlider( m_controlsTab, m_activeSlider, result );
                }
            }
        }
        m_activeSlider = 0;
        SceneTab::ResetPreviewState( m_sceneTab );
        ProfilerTab::ResetPreviewState( m_profilerTab );
        OptionsTab::ResetPreviewState( m_optionsTab );
        PhysicsTab::ResetPreviewState( m_physicsTab );
        ControlsTab::ResetPreviewState( m_controlsTab );
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        InputControl::EndMouseCapture();
    }

    m_interaction.leftWasDown = leftNow;
    m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
    m_interaction.blocksCameraMouse =
        inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0;
    return result;
}


void InGameUI::Draw( const InGameUIFrameData& data )
{
    if ( !m_window.isVisible )
    {
        return;
    }
    DRAW_CALL_TRACE_SCOPE( "Frame/UI/Draw" );

    const int screenW = (std::max)( 1, data.screenW );
    const int screenH = (std::max)( 1, data.screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    m_lastModelCapacity = std::clamp( data.modelCapacity, 1, MAX_GAME_MODELS );
    m_lastSolverBallCount = std::clamp( data.solverBallCount, UI_SOLVER_COUNT_MIN, m_lastModelCapacity );
    m_lastSolverBoxCount = std::clamp( data.solverBoxCount, UI_SOLVER_COUNT_MIN, m_lastModelCapacity );
    m_lastMaxWorkerThreadCount = (std::max)( 1, data.maxWorkerThreadCount );
    m_lastWorkerThreadCount = std::clamp( data.workerThreadCount, 0, m_lastMaxWorkerThreadCount );
    m_lastRenderTargetPreviewCount = RenderTargetPreviewCount( data );
    m_lastRenderTargetDisabledMask = RenderTargetPreviewDisabledMask( data );
    m_selectedRenderTargetPreview = ResolveRenderTargetPreviewSelection( data, m_selectedRenderTargetPreview );
    if ( ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) )
    {
        ProfilerTab::PushPerformanceHistogramSample( m_profilerTab, data.cpuFrameMs, data.gpuFrameMs );
    }

    if ( m_window.isMinimized )
    {
        m_cache.Reset();
        UIDrawList& drawList = m_cache.MutableDrawList();
        drawList.Clear();
        const UIDrawContext draw( screenW, screenH, &drawList );
        if ( m_window.animationActive && m_window.animationToMinimized )
        {
            const UIRect animBounds = Chrome::CurrentWindowRect( m_window, data.now );
            if ( m_window.animationActive )
            {
                Chrome::DrawWindowAnimationShell( draw, animBounds );
                if ( ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) )
                {
                    ProfilerTab::DrawPerformanceHistogram( m_profilerTab, draw, data );
                }
                FlushUIDrawList( drawList, screenW, screenH );
                return;
            }
        }

        char titleText[192] = {};
        Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
        if ( !data.editorModeEnabled )
        {
            StripMinimizedRuntimeModeSuffix( data, titleText, sizeof( titleText ) );
        }
        m_window.minimizedWidth =
            data.editorModeEnabled
                ? EditorMinimizedWidth( data, screenW )
                : (std::min)( MinimizedWidthWithCameraModeCombo( titleText, screenW ), MINIMIZED_RUN_MAX_W );
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        if ( data.editorModeEnabled )
        {
            const EditorMiniPaletteLayout editorMiniPalette =
                BuildEditorMiniPaletteLayout( screenW,
                                              screenH,
                                              minimized,
                                              m_editorMiniPalettePressedEntry,
                                              m_editorMiniPaletteFlyoutOpen );
            DrawEditorMiniPalette( draw,
                                   editorMiniPalette,
                                   data.editorObjectType,
                                   data.editorPlaceStatic,
                                   m_mouseX,
                                   m_mouseY,
                                   m_editorMiniPalettePressedTreePlacement,
                                   m_editorMiniPalettePressedHoldMode,
                                   m_editorMiniPalettePressedEntry,
                                   screenW,
                                   screenH );
            DrawEditorMinimizedWindow( draw, minimized, data, m_mouseX, m_mouseY );
        }
        else
        {
            const UIRect cameraModeComboBounds = MinimizedCameraModeComboBounds( minimized );
            m_cameraModeCombo.SetLabelVisible( false );
            m_cameraModeCombo.SetBounds( cameraModeComboBounds.x,
                                         cameraModeComboBounds.y,
                                         cameraModeComboBounds.w,
                                         cameraModeComboBounds.h );
            m_cameraModeCombo.SetDropUp( true );
            const float titleMaxW =
                (std::max)( 40.0f, cameraModeComboBounds.x - ( minimized.x + 32.0f ) - MINIMIZED_CAMERA_MODE_GAP );
            Chrome::FitTitleText( titleText, sizeof( titleText ), 12.5f, titleMaxW );
            Chrome::DrawMinimizedWindow( draw, minimized, titleText );
            const int cameraModeIndex = std::clamp( data.cameraModeIndex, 0, CAMERA_MODE_OPTION_COUNT - 1 );
            const uint32_t cameraModeDisabledMask =
                ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) &
                ~( data.cameraModeEnabledMask & ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) );
            m_cameraModeCombo.Draw( draw,
                                    "",
                                    kCameraModeOptions,
                                    CAMERA_MODE_OPTION_COUNT,
                                    cameraModeIndex,
                                    m_mouseX,
                                    m_mouseY,
                                    cameraModeDisabledMask );
        }
        if ( ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) )
        {
            ProfilerTab::DrawPerformanceHistogram( m_profilerTab, draw, data );
        }
        FlushUIDrawList( drawList, screenW, screenH );
        return;
    }

    PROFILE_BEGIN( "Frame/UI/Layout" );
    const UIRect windowBounds = Chrome::CurrentWindowRect( m_window, data.now );
    const float x = windowBounds.x;
    const float y = windowBounds.y;
    const float w = windowBounds.w;
    const float h = windowBounds.h;
    const float titleH = 44.0f;
    const float tabH = 44.0f;
    const float bottomH = 78.0f;
    const float pad = 18.0f;
    const float contentX = x + pad;
    const float contentY = y + titleH + tabH + 12.0f;
    const float contentW = w - pad * 2.0f - 8.0f;
    const float contentH = (std::max)( 30.0f, h - titleH - tabH - bottomH - pad );
    const float scrolledY = contentY - m_scrollY;
    char titleText[192] = {};
    Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
    const bool useTitleStats = w - 36.0f < 560.0f;
    char titleStat[32] = {};
    float titleStatW = 0.0f;
    float titleStatX = 0.0f;
    float titleMaxW = w - 150.0f;
    if ( useTitleStats )
    {
        snprintf( titleStat, sizeof( titleStat ), "%.0f FPS", data.fps );
        titleStatW = Text2d::MeasureText( 10.5f, titleStat );
        titleStatX = (std::max)( x + 148.0f, x + w - 128.0f - titleStatW );
        titleMaxW = titleStatX - ( x + 20.0f ) - 10.0f;
    }
    Chrome::FitTitleText( titleText, sizeof( titleText ), 15.5f, (std::max)( 40.0f, titleMaxW ) );
    ProfilerTab::ApplyDefaultExpansion( m_profilerTab );
    ProfilerTab::ApplyExpandAll( m_profilerTab );

    UICacheFrameKey cacheKey;
    cacheKey.screenW = screenW;
    cacheKey.screenH = screenH;
    cacheKey.windowBounds = windowBounds;
    cacheKey.activeTab = static_cast<int>( m_activeTab );
    cacheKey.scrollY = m_scrollY;
    cacheKey.blurEnabled = m_blurPreviewEnabled;
    cacheKey.contentSignature = BuildUIContentSignature( data );
    cacheKey.styleSignature = HashBool( HashBool( 2166136261u, m_blurPreviewEnabled ), m_hitboxOverlayEnabled );
    cacheKey.interactionSignature = BuildUIInteractionSignature( m_mouseX,
                                                                 m_mouseY,
                                                                 m_rendererCombo.IsOpen(),
                                                                 m_reflectionCombo.IsOpen(),
                                                                 m_sceneCombo.IsOpen(),
                                                                 CinematicTab::IsComboOpen( m_cinematicTab ),
                                                                 m_editorTab.objectCombo.IsOpen(),
                                                                 m_renderTargetCombo.IsOpen(),
                                                                 m_cameraModeCombo.IsOpen(),
                                                                 m_selectedRenderTargetPreview,
                                                                 m_activeSlider );
    m_cache.BeginFrame( cacheKey );
    PROFILE_END( "Frame/UI/Layout" );

    const bool drawsLiveRenderTargetPreview = m_activeTab == InGameUITab::Targets;
    if ( !drawsLiveRenderTargetPreview && m_cache.CanReplayPositionOnly( cacheKey ) )
    {
        const float replayOffsetX = m_cache.ReplayOffsetX( cacheKey );
        const float replayOffsetY = m_cache.ReplayOffsetY( cacheKey );
        FlushUIDrawList( m_cache.DrawList(), screenW, screenH, replayOffsetX, replayOffsetY );
        m_cache.StoreFrame( cacheKey );
        return;
    }

    UIDrawList& drawList = m_cache.MutableDrawList();
    drawList.Clear();
    const UIDrawContext draw( screenW, screenH, &drawList );
    PROFILE_BEGIN( "Frame/UI/DrawBuild" );

    const UIRect blurBounds = { x, y, w, h };
    Text2d::FlushQuads();
    PROFILE_BEGIN( "Frame/UI/Blur" );
    m_backdropBlur.Draw( draw, blurBounds, screenW, screenH, data.currentFrame, data.now, m_blurPreviewEnabled );
    PROFILE_END( "Frame/UI/Blur" );

    Chrome::DrawWindowFrame( draw, windowBounds, titleH, tabH, m_blurPreviewEnabled, titleText );
    Chrome::DrawTitleButtons( draw,
                              Chrome::GetTitleButtonRects( windowBounds ),
                              m_window.isMaximized,
                              m_mouseX,
                              m_mouseY );

    static const char* kTabs[] =
        { "Profile", "Scene", "Editor", "Physics", "Options", "Render", "Targets", "Controls", "Cine" };
    const int tabCount = static_cast<int>( InGameUITab::Count );
    const float tabPad = 14.0f;
    m_tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    m_tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( m_activeTab ) );

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedPanel( { contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f },
                       Style::Radii().window,
                       palette.windowSubtle,
                       palette.innerBorder );

    if ( m_activeTab == InGameUITab::Profiler )
    {
        ProfilerTab::Draw( m_profilerTab,
                           draw,
                           data,
                           contentX,
                           contentY,
                           contentW,
                           contentH,
                           m_scrollY,
                           m_activeSlider );
    }
    else if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::Draw( m_sceneTab,
                        m_sceneCombo,
                        m_resetSceneButton,
                        m_resetDefaultsButton,
                        m_saveDefaultsButton,
                        draw,
                        data,
                        contentX,
                        contentY,
                        contentW,
                        contentH,
                        scrolledY,
                        m_mouseX,
                        m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Physics )
    {
        PhysicsTab::Draw( m_physicsTab,
                          draw,
                          data,
                          contentX,
                          contentY,
                          contentW,
                          contentH,
                          scrolledY,
                          m_activeSlider,
                          m_mouseX,
                          m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Editor )
    {
        EditorTab::Draw( m_editorTab,
                         draw,
                         data,
                         contentX,
                         contentY,
                         contentW,
                         contentH,
                         scrolledY,
                         m_mouseX,
                         m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Options )
    {
        OptionsTab::Draw( m_optionsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY, m_activeSlider );
    }
    else if ( m_activeTab == InGameUITab::Render )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Render" );
        DrawContentToggle( draw,
                           contentY,
                           contentH,
                           m_renderShadowToggle,
                           contentX,
                           scrolledY + UI_RENDER_FEATURE_START_Y,
                           colW,
                           "Shadows",
                           data.ordinaryRender.shadowsEnabled );
        m_saveRenderDefaultsButton.SetBounds( contentX + contentW - UI_RENDER_SAVE_BUTTON_W,
                                              scrolledY + UI_RENDER_FEATURE_START_Y,
                                              UI_RENDER_SAVE_BUTTON_W,
                                              24.0f );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_RENDER_FEATURE_START_Y, 24.0f ) )
        {
            m_saveRenderDefaultsButton.Draw( draw, "Save CFG", m_mouseX, m_mouseY );
        }

        const float baseY = scrolledY + UI_RENDER_START_Y;
        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            const RenderSliderSpec& spec = kRenderSliderSpecs[i];
            const float sliderY = RenderSliderY( i, baseY );
            if ( spec.section && IsRowVisible( contentY, contentH, sliderY - UI_RENDER_SECTION_H + 4.0f, 18.0f ) )
            {
                DrawSectionTitle( draw,
                                  contentX,
                                  contentY,
                                  contentH,
                                  sliderY - UI_RENDER_SECTION_H + 4.0f,
                                  12.0f,
                                  spec.section );
            }
            const float value =
                std::clamp( RenderValueForParam( data.ordinaryRender, spec.param ), spec.minValue, spec.maxValue );
            snprintf( buf, sizeof( buf ), spec.valueFormat, value );
            m_renderSliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );
            if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
            {
                m_renderSliders[i].Draw( draw, spec.label, buf, value, spec.minValue, spec.maxValue );
            }
        }
    }
    else if ( m_activeTab == InGameUITab::Targets )
    {
        const int targetCount = RenderTargetPreviewCount( data );
        const int selectedIndex = m_selectedRenderTargetPreview;
        const bool hasSelection = selectedIndex >= 0 && selectedIndex < targetCount;
        const UIRenderTargetPreviewResource* selected =
            hasSelection ? &data.renderTargetPreviews[selectedIndex] : nullptr;
        const bool selectedAvailable = selected && selected->available && selected->textureHandle != 0 &&
                                       selected->width > 0 && selected->height > 0;
        const Style::UIPalette& targetPalette = Style::Palette();
        const char* options[UI_RENDER_TARGET_PREVIEW_MAX] = {};
        int liveCount = 0;
        for ( int i = 0; i < targetCount; ++i )
        {
            const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
            options[i] = resource.label;
            if ( resource.available && resource.textureHandle != 0 && resource.width > 0 && resource.height > 0 )
            {
                ++liveCount;
            }
        }

        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Targets" );

        char countText[64];
        snprintf( countText, sizeof( countText ), "%d / %d live", liveCount, targetCount );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_META_Y - 24.0f, 18.0f ) )
        {
            DrawLabelValueAt( draw,
                              contentY,
                              contentH,
                              contentX,
                              scrolledY + UI_TARGETS_META_Y - 24.0f,
                              "Resources",
                              countText,
                              targetPalette.accent.r,
                              targetPalette.accent.g,
                              targetPalette.accent.b );
        }

        if ( selected )
        {
            char detailText[160];
            if ( selectedAvailable )
            {
                snprintf( detailText,
                          sizeof( detailText ),
                          "%s, %d x %d, #%u",
                          RenderTargetPreviewTypeText( *selected ),
                          selected->width,
                          selected->height,
                          selected->textureHandle );
            }
            else
            {
                snprintf( detailText, sizeof( detailText ), "%s, n/a", RenderTargetPreviewTypeText( *selected ) );
            }
            DrawLabelValueAt( draw,
                              contentY,
                              contentH,
                              contentX,
                              scrolledY + UI_TARGETS_META_Y,
                              "Selected",
                              detailText,
                              selectedAvailable ? targetPalette.textPrimary.r : targetPalette.textMuted.r,
                              selectedAvailable ? targetPalette.textPrimary.g : targetPalette.textMuted.g,
                              selectedAvailable ? targetPalette.textPrimary.b : targetPalette.textMuted.b );
        }

        const UIRect previewPanel = { contentX, scrolledY + UI_TARGETS_PREVIEW_Y, contentW, UI_TARGETS_PREVIEW_H };
        const UIRect previewClip = { contentX, contentY, contentW, contentH };
        UIRect previewImage = previewPanel;
        if ( IsBlockVisible( contentY, contentH, previewPanel.y, previewPanel.h ) )
        {
            draw.RoundedPanel( previewPanel,
                               Style::Radii().control,
                               targetPalette.windowSubtle,
                               targetPalette.innerBorder );
            const UIRect previewInset = { previewPanel.x + 10.0f,
                                          previewPanel.y + 10.0f,
                                          (std::max)( 1.0f, previewPanel.w - 20.0f ),
                                          (std::max)( 1.0f, previewPanel.h - 20.0f ) };
            previewImage = selected ? FitRectToAspect( previewInset, selected->width, selected->height ) : previewInset;
            draw.RoundedRect( previewImage.x - 1.0f,
                              previewImage.y - 1.0f,
                              previewImage.w + 2.0f,
                              previewImage.h + 2.0f,
                              Style::Radii().control,
                              0.01f,
                              0.015f,
                              0.018f,
                              0.92f );
        }

        if ( selectedAvailable && IsBlockVisible( contentY, contentH, previewImage.y, previewImage.h ) )
        {
            FlushUIDrawList( drawList, screenW, screenH );
            drawList.Clear();
            DrawRenderTargetPreviewTexture( m_renderTargetPreviewShader,
                                            m_renderTargetPreviewVB,
                                            draw,
                                            *selected,
                                            previewImage,
                                            previewClip );
        }
        else if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_PREVIEW_Y + 116.0f, 18.0f ) )
        {
            draw.Text( previewPanel.x + 18.0f,
                       previewPanel.y + 116.0f,
                       12.0f,
                       targetPalette.textMuted.r,
                       targetPalette.textMuted.g,
                       targetPalette.textMuted.b,
                       "Not available this frame" );
        }

        if ( IsBlockVisible( contentY, contentH, previewPanel.y, previewPanel.h ) )
        {
            draw.Outline( previewImage.x,
                          previewImage.y,
                          previewImage.w,
                          previewImage.h,
                          targetPalette.border.r,
                          targetPalette.border.g,
                          targetPalette.border.b,
                          0.72f );
        }

        const char* selectedText = selected ? selected->label : "No targets";
        m_renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_COMBO_Y, 24.0f ) )
        {
            m_renderTargetCombo.Draw( draw,
                                      "View",
                                      selectedText,
                                      options,
                                      targetCount,
                                      selectedIndex,
                                      m_mouseX,
                                      m_mouseY,
                                      m_lastRenderTargetDisabledMask );
        }
    }
    else if ( m_activeTab == InGameUITab::Cinematic )
    {
        CinematicTab::Draw( m_cinematicTab,
                            draw,
                            data,
                            contentX,
                            contentY,
                            contentW,
                            contentH,
                            scrolledY,
                            m_mouseX,
                            m_mouseY );
    }
    else
    {
        ControlsTab::Draw( m_controlsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY );
    }

    m_scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    m_scrollBar
        .Draw( draw, static_cast<float>( ContentHeight() ), contentH, m_scrollY, m_scrollbarVisibleUntil, data.now );

    const float by = y + h - bottomH;
    draw.Rect( x + 16.0f, by, w - 32.0f, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.14f );
    const float footerPad = 18.0f;
    const float footerGap = 16.0f;
    const float footerX = x + footerPad;
    const float footerW = (std::max)( 120.0f, w - footerPad * 2.0f );
    const bool hasSeparateStats = footerW >= 560.0f;
    const float controlsW = hasSeparateStats ? 462.0f : footerW;
    draw.RoundedPanel( { footerX, by + 16.0f, controlsW, 56.0f },
                       Style::Radii().control,
                       palette.windowSubtle,
                       palette.innerBorder );

    const UIRect rendererComboBounds = FooterRendererComboBounds( x, by );
    const UIRect waterComboBounds = FooterWaterComboBounds( x, by );
    const UIRect blurFooterBounds = FooterBlurBounds( x, by );
    const UIRect vsyncFooterBounds = FooterVsyncBounds( x, by );
    const UIRect hitboxFooterBounds = FooterHitboxBounds( x, by );
    const UIRect timelineFooterBounds = FooterTimelineBounds( x, by );
    const UIRect perfFooterBounds = FooterPerfBounds( x, by );
    m_rendererCombo.SetBounds( rendererComboBounds.x,
                               rendererComboBounds.y,
                               rendererComboBounds.w,
                               rendererComboBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurFooterBounds.x, blurFooterBounds.y, blurFooterBounds.w, blurFooterBounds.h );
    m_vsyncToggle.SetBounds( vsyncFooterBounds.x, vsyncFooterBounds.y, vsyncFooterBounds.w, vsyncFooterBounds.h );
    m_hitboxToggle.SetBounds( hitboxFooterBounds.x, hitboxFooterBounds.y, hitboxFooterBounds.w, hitboxFooterBounds.h );
    m_histogramToggle.SetBounds( perfFooterBounds.x, perfFooterBounds.y, perfFooterBounds.w, perfFooterBounds.h );
    m_timelineToggle.SetBounds( timelineFooterBounds.x,
                                timelineFooterBounds.y,
                                timelineFooterBounds.w,
                                timelineFooterBounds.h );
    static const char* kRendererOptions[] = { "DX12" };
    static const char* kReflectionOptions[] = { "FBO", "DXR", "None" };
    m_rendererCombo.Draw( draw, "Renderer", kRendererOptions, 1, 0, m_mouseX, m_mouseY );
    DrawFooterToggle( draw, blurFooterBounds, "Blur", m_blurPreviewEnabled );
    DrawFooterToggle( draw, vsyncFooterBounds, "VSync", data.vsyncEnabled );
    DrawFooterToggle( draw, hitboxFooterBounds, "Hitboxes", m_hitboxOverlayEnabled );
    DrawFooterToggle( draw, perfFooterBounds, "Perf", ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) );
    DrawFooterToggle( draw, timelineFooterBounds, "Timeline", ProfilerTab::TimelineEnabled( m_profilerTab ) );
    m_reflectionCombo.Draw( draw,
                            "Water",
                            kReflectionOptions,
                            3,
                            WaterReflectionModeFromData( data ),
                            m_mouseX,
                            m_mouseY,
                            ReflectionDisabledMask() );

    char status[128];
    const float frameDisplayMs = data.fps > 0.0f ? 1000.0f / data.fps : 0.0f;
    const int cpuPercent =
        static_cast<int>( std::clamp( ( data.renderMs + data.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int gpuPercent = static_cast<int>( std::clamp( data.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.drawCallsBeforeUI + data.UIDrawCalls;
    snprintf( status, sizeof( status ), "%.0f", data.fps );
    if ( hasSeparateStats )
    {
        const float statsX = footerX + controlsW + footerGap;
        const float statsW = (std::max)( 120.0f, x + w - footerPad - statsX );
        draw.RoundedPanel( { statsX, by + 16.0f, statsW, 56.0f },
                           Style::Radii().control,
                           palette.windowSubtle,
                           palette.innerBorder );

        if ( statsW < 350.0f )
        {
            char fpsText[32];
            char frameText[32];
            char drawText[32];
            snprintf( fpsText, sizeof( fpsText ), "%.0f", data.fps );
            snprintf( frameText, sizeof( frameText ), "%.2f ms", frameDisplayMs );
            snprintf( drawText, sizeof( drawText ), "%d/%d", drawCalls, data.UIDrawCalls );
            DrawCompactFooterStat( draw,
                                   statsX,
                                   by + 23.0f,
                                   "FPS",
                                   fpsText,
                                   palette.accent.r,
                                   palette.accent.g,
                                   palette.accent.b );
            DrawCompactFooterStat( draw,
                                   statsX,
                                   by + 41.0f,
                                   "Frame",
                                   frameText,
                                   palette.textPrimary.r,
                                   palette.textPrimary.g,
                                   palette.textPrimary.b );
            DrawCompactFooterStat( draw,
                                   statsX,
                                   by + 59.0f,
                                   "Draw/UI",
                                   drawText,
                                   palette.textPrimary.r,
                                   palette.textPrimary.g,
                                   palette.textPrimary.b );
        }
        else
        {
            DrawFooterStatCell( draw,
                                statsX + 18.0f,
                                by,
                                "FPS",
                                status,
                                palette.accent.r,
                                palette.accent.g,
                                palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 78.0f, by );
            snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
            DrawFooterStatCell( draw,
                                statsX + 100.0f,
                                by,
                                "Frame Time",
                                status,
                                palette.textPrimary.r,
                                palette.textPrimary.g,
                                palette.textPrimary.b );
            DrawFooterStatDivider( draw, statsX + 190.0f, by );
            snprintf( status, sizeof( status ), "%d%%", cpuPercent );
            DrawFooterStatCell( draw,
                                statsX + 212.0f,
                                by,
                                "CPU",
                                status,
                                palette.accent.r,
                                palette.accent.g,
                                palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 266.0f, by );
            snprintf( status, sizeof( status ), "%d%%", gpuPercent );
            DrawFooterStatCell( draw,
                                statsX + 288.0f,
                                by,
                                "GPU",
                                status,
                                palette.accent.r,
                                palette.accent.g,
                                palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 342.0f, by );
            snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.UIDrawCalls );
            DrawFooterStatCell( draw,
                                statsX + statsW - 112.0f,
                                by,
                                "Draws / UI",
                                status,
                                palette.textPrimary.r,
                                palette.textPrimary.g,
                                palette.textPrimary.b );
        }
    }
    else
    {
        if ( titleStatW > 0.0f && titleStatX + titleStatW < x + w - 116.0f )
        {
            draw.Text( titleStatX, y + 17.0f, 10.5f, palette.accent.r, palette.accent.g, palette.accent.b, titleStat );
        }
    }

    if ( ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) )
    {
        ProfilerTab::DrawPerformanceHistogram( m_profilerTab, draw, data );
    }

    draw.Rect( x + w - 24.0f,
               y + h - 9.0f,
               14.0f,
               2.0f,
               palette.textMuted.r,
               palette.textMuted.g,
               palette.textMuted.b,
               0.58f );
    draw.Rect( x + w - 18.0f,
               y + h - 15.0f,
               8.0f,
               2.0f,
               palette.textMuted.r,
               palette.textMuted.g,
               palette.textMuted.b,
               0.46f );
    draw.Rect( x + w - 12.0f,
               y + h - 21.0f,
               2.0f,
               2.0f,
               palette.textMuted.r,
               palette.textMuted.g,
               palette.textMuted.b,
               0.38f );

    DrawHitboxOverlay( draw,
                       data,
                       windowBounds,
                       { contentX, contentY, contentW, contentH },
                       { footerX, by + 16.0f, controlsW, 56.0f } );

    PROFILE_END( "Frame/UI/DrawBuild" );
    FlushUIDrawList( drawList, screenW, screenH );
    if ( drawsLiveRenderTargetPreview )
    {
        m_cache.Reset();
    }
    else
    {
        m_cache.StoreFrame( cacheKey );
    }
}
