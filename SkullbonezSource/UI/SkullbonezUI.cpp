/*
File: SkullbonezSource/UI/SkullbonezUI.cpp
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
  - SkullbonezSource/UI/SkullbonezUI.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezUI.h"
#include "../SkullbonezAssetSystem.h"
#include "../SkullbonezIRenderBackend.h"
#include "../SkullbonezMatrix4.h"
#include "../SkullbonezPhysicsDebugVisualizer.h"
#include "../SkullbonezProfiler.h"
#include "../SkullbonezText.h"
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
    hash = HashBool( hash, data.tornadoFieldVectors );
    hash = HashBool( hash, data.rayCastVisualization );
    hash = HashFloat( hash, data.tornadoRadius, 100.0f );
    hash = HashFloat( hash, data.tornadoHeight, 100.0f );
    hash = HashFloat( hash, data.tornadoInwardAcceleration, 100.0f );
    hash = HashFloat( hash, data.tornadoSwirlAcceleration, 100.0f );
    hash = HashFloat( hash, data.tornadoLiftAcceleration, 100.0f );
    hash = HashFloat( hash, data.rayCastImpulseStrength, 100.0f );
    hash = HashBool( hash, data.waterFreezeDebug );
    hash = HashBool( hash, data.waterFlatDebug );
    hash = HashBool( hash, data.terrainHidden );
    hash = HashBool( hash, data.waterHidden );
    hash = HashBool( hash, data.waterNoReflect );
    hash = HashBool( hash, data.waterRTReflect );
    hash = HashBool( hash, data.cameraMouseActive );
    hash = HashBool( hash, data.nativeCursorVisible );
    hash = HashBool( hash, data.editorModeEnabled );
    hash = HashBool( hash, data.editorPlacementMode );
    hash = HashBool( hash, data.editorPlaceStatic );
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


uint32_t BuildUIInteractionSignature( int mouseX, int mouseY, bool rendererOpen, bool reflectionOpen, bool sceneOpen, bool cineSceneOpen, bool editorObjectOpen, bool renderTargetOpen, int selectedRenderTarget, int activeSlider )
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
        left,
        bottom,
        uvLeft,
        uvBottom,
        right,
        bottom,
        uvRight,
        uvBottom,
        right,
        top,
        uvRight,
        uvTop,
        left,
        bottom,
        uvLeft,
        uvBottom,
        right,
        top,
        uvRight,
        uvTop,
        left,
        top,
        uvLeft,
        uvTop,
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
static_assert( sizeof( kRenderSliderSpecs ) / sizeof( kRenderSliderSpecs[0] ) == static_cast<int>( UIRenderParam::Count ),
               "Render slider specs must match UIRenderParam." );

constexpr int UI_CINEMATIC_SLIDER_BASE = 5000;
constexpr int UI_CINE_SCENE_MAX_OPTIONS = 32;
constexpr float UI_CINEMATIC_SCENE_Y = 42.0f;
constexpr float UI_CINEMATIC_FEATURE_START_Y = 96.0f;
constexpr float UI_CINEMATIC_START_Y = 266.0f;
constexpr float UI_CINEMATIC_SECTION_H = 28.0f;
constexpr float UI_CINEMATIC_ROW_H = 42.0f;

struct CinematicSliderSpec
{
    // One row in the Cine tab. Keeping label/range/step together makes it clear
    // which UI slider controls which render setting.
    const char* section;
    const char* label;
    UICinematicParam param;
    float minValue;
    float maxValue;
    float step;
    const char* valueFormat;
};

struct CinematicFeatureSpec
{
    // One toggle in the Cine tab, such as Bloom or Fog.
    const char* label;
    UICinematicFeature feature;
};

constexpr CinematicSliderSpec kCinematicSliderSpecs[] = {
    { "Tonemap", "Exposure", UICinematicParam::Exposure, 0.05f, 3.00f, 0.01f, "%.2f" },
    { nullptr, "Gamma", UICinematicParam::Gamma, 1.00f, 3.00f, 0.01f, "%.2f" },
    { "Style", "Sky mode", UICinematicParam::SkyMode, 0.00f, 32.00f, 1.00f, "%.0f" },
    { nullptr, "Terrain mode", UICinematicParam::TerrainMode, 0.00f, 32.00f, 1.00f, "%.0f" },
    { nullptr, "Object style", UICinematicParam::ObjectStyle, 0.00f, 32.00f, 1.00f, "%.0f" },
    { nullptr, "Water mode", UICinematicParam::WaterMode, 0.00f, 4.00f, 1.00f, "%.0f" },
    { nullptr, "Saturation", UICinematicParam::StyleSaturation, 0.00f, 2.50f, 0.01f, "%.2f" },
    { nullptr, "Contrast", UICinematicParam::StyleContrast, 0.00f, 2.50f, 0.01f, "%.2f" },
    { nullptr, "Vignette", UICinematicParam::StyleVignette, 0.00f, 1.00f, 0.01f, "%.2f" },
    { "Sun", "Sun X", UICinematicParam::SunX, 0.00f, 1.00f, 0.005f, "%.3f" },
    { nullptr, "Sun Y", UICinematicParam::SunY, 0.00f, 1.00f, 0.005f, "%.3f" },
    { nullptr, "Brightness", UICinematicParam::SunBrightness, 0.00f, 40.00f, 0.10f, "%.1f" },
    { nullptr, "Sun R", UICinematicParam::SunRed, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun G", UICinematicParam::SunGreen, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Sun B", UICinematicParam::SunBlue, 0.00f, 2.00f, 0.01f, "%.2f" },
    { "Sky", "Glow", UICinematicParam::SkyGlow, 0.00f, 8.00f, 0.05f, "%.2f" },
    { nullptr, "Horizon R", UICinematicParam::HorizonRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Horizon G", UICinematicParam::HorizonGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Horizon B", UICinematicParam::HorizonBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Zenith R", UICinematicParam::ZenithRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Zenith G", UICinematicParam::ZenithGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Zenith B", UICinematicParam::ZenithBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Clouds", "Coverage", UICinematicParam::CloudCoverage, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Softness", UICinematicParam::CloudSoftness, 0.01f, 0.65f, 0.01f, "%.2f" },
    { nullptr, "Scale", UICinematicParam::CloudScale, 0.50f, 12.00f, 0.05f, "%.2f" },
    { nullptr, "Intensity", UICinematicParam::CloudIntensity, 0.00f, 1.50f, 0.01f, "%.2f" },
    { "Shafts", "Strength", UICinematicParam::ShaftStrength, 0.00f, 3.00f, 0.01f, "%.2f" },
    { nullptr, "Falloff", UICinematicParam::ShaftFalloff, 0.25f, 5.00f, 0.01f, "%.2f" },
    { "Volume", "Strength", UICinematicParam::VolumetricStrength, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Density", UICinematicParam::VolumetricDensity, 0.00f, 2.50f, 0.01f, "%.2f" },
    { nullptr, "Decay", UICinematicParam::VolumetricDecay, 0.800f, 0.995f, 0.001f, "%.3f" },
    { "Bloom", "Threshold", UICinematicParam::BloomThreshold, 0.00f, 4.00f, 0.01f, "%.2f" },
    { nullptr, "Knee", UICinematicParam::BloomKnee, 0.01f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Strength", UICinematicParam::BloomStrength, 0.00f, 2.00f, 0.01f, "%.2f" },
    { nullptr, "Radius", UICinematicParam::BloomRadius, 0.25f, 8.00f, 0.05f, "%.2f" },
    { "Terrain", "Relief", UICinematicParam::TerrainRelief, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground R", UICinematicParam::TerrainTintRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground G", UICinematicParam::TerrainTintGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Ground B", UICinematicParam::TerrainTintBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Accent R", UICinematicParam::TerrainAccentRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Accent G", UICinematicParam::TerrainAccentGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Accent B", UICinematicParam::TerrainAccentBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Grid scale", UICinematicParam::TerrainGridScale, 0.10f, 120.00f, 0.10f, "%.1f" },
    { nullptr, "Grid strength", UICinematicParam::TerrainGridStrength, 0.00f, 4.00f, 0.01f, "%.2f" },
    { "Water", "Water R", UICinematicParam::WaterTintRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Water G", UICinematicParam::WaterTintGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Water B", UICinematicParam::WaterTintBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Alpha", UICinematicParam::WaterAlpha, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Reflection", UICinematicParam::WaterReflection, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Glint", UICinematicParam::WaterGlint, 0.00f, 4.00f, 0.01f, "%.2f" },
    { "Basin", "Center X", UICinematicParam::BasinCenterX, 0.00f, 1200.00f, 1.00f, "%.0f" },
    { nullptr, "Center Z", UICinematicParam::BasinCenterZ, 0.00f, 1200.00f, 1.00f, "%.0f" },
    { nullptr, "Radius X", UICinematicParam::BasinRadiusX, 1.00f, 500.00f, 1.00f, "%.0f" },
    { nullptr, "Radius Z", UICinematicParam::BasinRadiusZ, 1.00f, 500.00f, 1.00f, "%.0f" },
    { nullptr, "Feather", UICinematicParam::BasinFeather, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Basin Depth", UICinematicParam::BasinDepth, 0.00f, 80.00f, 1.00f, "%.0f" },
    { nullptr, "Rim Lift", UICinematicParam::BasinRimLift, 0.00f, 60.00f, 1.00f, "%.0f" },
    { "Fog", "Density", UICinematicParam::FogDensity, 0.00000f, 0.00600f, 0.00005f, "%.5f" },
    { nullptr, "Opacity", UICinematicParam::FogOpacity, 0.00f, 1.00f, 0.01f, "%.2f" },
    { nullptr, "Start", UICinematicParam::FogStart, 0.00f, 500.00f, 1.00f, "%.0f" },
    { nullptr, "End", UICinematicParam::FogEnd, 100.00f, 4000.00f, 10.00f, "%.0f" },
    { nullptr, "Fog R", UICinematicParam::FogRed, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Fog G", UICinematicParam::FogGreen, 0.00f, 1.50f, 0.01f, "%.2f" },
    { nullptr, "Fog B", UICinematicParam::FogBlue, 0.00f, 1.50f, 0.01f, "%.2f" },
};
static_assert( sizeof( kCinematicSliderSpecs ) / sizeof( kCinematicSliderSpecs[0] ) == static_cast<int>( UICinematicParam::Count ),
               "Cinematic slider specs must match UICinematicParam." );

constexpr CinematicFeatureSpec kCinematicFeatureSpecs[] = {
    { "Sky", UICinematicFeature::Sky },
    { "Clouds", UICinematicFeature::Clouds },
    { "God rays", UICinematicFeature::GodRays },
    { "Volume", UICinematicFeature::VolumetricLight },
    { "Bloom", UICinematicFeature::Bloom },
    { "Fog", UICinematicFeature::Fog },
    { "Relief", UICinematicFeature::TerrainRelief },
    { "Shadows", UICinematicFeature::Shadows },
};
static_assert( sizeof( kCinematicFeatureSpecs ) / sizeof( kCinematicFeatureSpecs[0] ) == static_cast<int>( UICinematicFeature::Count ),
               "Cinematic feature specs must match UICinematicFeature." );

bool IsBlockVisible( float contentY, float contentH, float blockY, float blockH )
{
    return blockY + blockH >= contentY && blockY <= contentY + contentH;
}

void DrawHitboxRect( const UIDrawContext& draw, const UIRect& bounds, float r, float g, float b, float fillA = 0.060f, float outlineA = 0.94f )
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
        DrawHitboxRect( draw, { tabs.x + static_cast<float>( i ) * tabW, tabs.y, tabW, tabs.h }, 1.0f, 0.80f, 0.18f, 0.052f, 0.84f );
    }
}

int SceneDropdownHitboxOptionCount( const SceneTab::UISceneTabState& state, const InGameUIFrameData& data )
{
    const int filteredSceneCount = SceneTab::CountFilteredOptions( data.sceneOptions, data.sceneOptionCount, state.filter );
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

void DrawFittedText( const UIDrawContext& draw, float x, float y, float pxSize, const Style::UIColor& color, const char* value, float maxWidth )
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

void SetRenderSliderResult( InGameUIInputResult& result, const UISlider& slider, int mouseX, const RenderSliderSpec& spec )
{
    result.commands.renderTuning.requestedParam = spec.param;
    result.commands.renderTuning.requestedValue = slider.ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
}

bool IsCineSceneOptionName( const char* name )
{
    if ( !name )
    {
        return false;
    }
    return strncmp( name, "concept_", 8 ) == 0 ||
           strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr ||
           strstr( name, "cine_" ) == name;
}

int BuildCineSceneOptions( const char* const* sceneOptions,
                           int sceneOptionCount,
                           const char* labels[UI_CINE_SCENE_MAX_OPTIONS],
                           int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS] )
{
    int count = 0;
    labels[count] = SceneTab::DEMO_SCENE_OPTION;
    sceneIndices[count] = -1;
    ++count;

    for ( int i = 0; i < sceneOptionCount && sceneOptions && count < UI_CINE_SCENE_MAX_OPTIONS; ++i )
    {
        if ( IsCineSceneOptionName( sceneOptions[i] ) )
        {
            labels[count] = sceneOptions[i];
            sceneIndices[count] = i;
            ++count;
        }
    }
    return count;
}

int SelectedCineSceneOption( const int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS], int cineOptionCount, int selectedSceneOption )
{
    for ( int i = 0; i < cineOptionCount; ++i )
    {
        if ( sceneIndices[i] == selectedSceneOption )
        {
            return i;
        }
    }
    return 0;
}

int CinematicSliderIndexFromActiveSlider( int activeSlider )
{
    // Other UI tabs already use m_activeSlider. Give Cine sliders their own id
    // range so dragging can continue even if the mouse leaves the slider bounds.
    const int index = activeSlider - UI_CINEMATIC_SLIDER_BASE;
    return ( index >= 0 && index < static_cast<int>( UICinematicParam::Count ) ) ? index : -1;
}

float CinematicSliderY( int index, float baseY )
{
    // Sections add extra vertical space. Calculating this from the spec array
    // keeps hit testing and drawing in lockstep.
    float y = baseY;
    for ( int i = 0; i <= index; ++i )
    {
        if ( kCinematicSliderSpecs[i].section )
        {
            y += UI_CINEMATIC_SECTION_H;
        }
        if ( i == index )
        {
            return y;
        }
        y += UI_CINEMATIC_ROW_H;
    }
    return y;
}

int CinematicContentHeight()
{
    float height = UI_CINEMATIC_START_Y;
    for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
    {
        if ( kCinematicSliderSpecs[i].section )
        {
            height += UI_CINEMATIC_SECTION_H;
        }
        height += UI_CINEMATIC_ROW_H;
    }
    return static_cast<int>( height + 18.0f );
}

float CinematicValueForParam( const CinematicRenderConfig& cinematic, UICinematicParam param )
{
    // Read the live value for a Cine slider. This is the inverse of the command
    // application in SkullbonezRunInput.cpp.
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return cinematic.exposure;
    case UICinematicParam::Gamma:
        return cinematic.gamma;
    case UICinematicParam::SkyMode:
        return static_cast<float>( cinematic.skyMode );
    case UICinematicParam::TerrainMode:
        return static_cast<float>( cinematic.terrainMode );
    case UICinematicParam::ObjectStyle:
        return static_cast<float>( cinematic.objectStyle );
    case UICinematicParam::WaterMode:
        return static_cast<float>( cinematic.waterMode );
    case UICinematicParam::StyleSaturation:
        return cinematic.styleSaturation;
    case UICinematicParam::StyleContrast:
        return cinematic.styleContrast;
    case UICinematicParam::StyleVignette:
        return cinematic.styleVignette;
    case UICinematicParam::SunX:
        return cinematic.sunScreenX;
    case UICinematicParam::SunY:
        return cinematic.sunScreenY;
    case UICinematicParam::SunBrightness:
        return cinematic.sunIntensity;
    case UICinematicParam::SunRed:
        return cinematic.sunColorR;
    case UICinematicParam::SunGreen:
        return cinematic.sunColorG;
    case UICinematicParam::SunBlue:
        return cinematic.sunColorB;
    case UICinematicParam::SkyGlow:
        return cinematic.skyGlowStrength;
    case UICinematicParam::HorizonRed:
        return cinematic.skyHorizonR;
    case UICinematicParam::HorizonGreen:
        return cinematic.skyHorizonG;
    case UICinematicParam::HorizonBlue:
        return cinematic.skyHorizonB;
    case UICinematicParam::ZenithRed:
        return cinematic.skyZenithR;
    case UICinematicParam::ZenithGreen:
        return cinematic.skyZenithG;
    case UICinematicParam::ZenithBlue:
        return cinematic.skyZenithB;
    case UICinematicParam::CloudCoverage:
        return cinematic.cloudCoverage;
    case UICinematicParam::CloudSoftness:
        return cinematic.cloudSoftness;
    case UICinematicParam::CloudScale:
        return cinematic.cloudScale;
    case UICinematicParam::CloudIntensity:
        return cinematic.cloudIntensity;
    case UICinematicParam::ShaftStrength:
        return cinematic.sunShaftStrength;
    case UICinematicParam::ShaftFalloff:
        return cinematic.sunShaftFalloff;
    case UICinematicParam::VolumetricStrength:
        return cinematic.volumetricStrength;
    case UICinematicParam::VolumetricDensity:
        return cinematic.volumetricDensity;
    case UICinematicParam::VolumetricDecay:
        return cinematic.volumetricDecay;
    case UICinematicParam::BloomThreshold:
        return cinematic.bloomThreshold;
    case UICinematicParam::BloomKnee:
        return cinematic.bloomKnee;
    case UICinematicParam::BloomStrength:
        return cinematic.bloomStrength;
    case UICinematicParam::BloomRadius:
        return cinematic.bloomRadius;
    case UICinematicParam::TerrainRelief:
        return cinematic.terrainRelief;
    case UICinematicParam::TerrainTintRed:
        return cinematic.terrainTintR;
    case UICinematicParam::TerrainTintGreen:
        return cinematic.terrainTintG;
    case UICinematicParam::TerrainTintBlue:
        return cinematic.terrainTintB;
    case UICinematicParam::TerrainAccentRed:
        return cinematic.terrainAccentR;
    case UICinematicParam::TerrainAccentGreen:
        return cinematic.terrainAccentG;
    case UICinematicParam::TerrainAccentBlue:
        return cinematic.terrainAccentB;
    case UICinematicParam::TerrainGridScale:
        return cinematic.terrainGridScale;
    case UICinematicParam::TerrainGridStrength:
        return cinematic.terrainGridStrength;
    case UICinematicParam::WaterTintRed:
        return cinematic.waterTintR;
    case UICinematicParam::WaterTintGreen:
        return cinematic.waterTintG;
    case UICinematicParam::WaterTintBlue:
        return cinematic.waterTintB;
    case UICinematicParam::WaterAlpha:
        return cinematic.waterAlpha;
    case UICinematicParam::WaterReflection:
        return cinematic.waterReflectionStrength;
    case UICinematicParam::WaterGlint:
        return cinematic.waterGlintStrength;
    case UICinematicParam::BasinCenterX:
        return cinematic.basinCenterX;
    case UICinematicParam::BasinCenterZ:
        return cinematic.basinCenterZ;
    case UICinematicParam::BasinRadiusX:
        return cinematic.basinRadiusX;
    case UICinematicParam::BasinRadiusZ:
        return cinematic.basinRadiusZ;
    case UICinematicParam::BasinFeather:
        return cinematic.basinFeather;
    case UICinematicParam::BasinDepth:
        return cinematic.basinDepth;
    case UICinematicParam::BasinRimLift:
        return cinematic.basinRimLift;
    case UICinematicParam::FogDensity:
        return cinematic.fogDensity;
    case UICinematicParam::FogOpacity:
        return cinematic.fogMaxOpacity;
    case UICinematicParam::FogStart:
        return cinematic.fogStart;
    case UICinematicParam::FogEnd:
        return cinematic.fogEnd;
    case UICinematicParam::FogRed:
        return cinematic.fogColorR;
    case UICinematicParam::FogGreen:
        return cinematic.fogColorG;
    case UICinematicParam::FogBlue:
        return cinematic.fogColorB;
    default:
        return 0.0f;
    }
}

void SetCinematicSliderResult( InGameUIInputResult& result, const UISlider& slider, int mouseX, const CinematicSliderSpec& spec )
{
    result.commands.cinematic.requestedParam = spec.param;
    result.commands.cinematic.requestedValue = slider.ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
}

float CinematicFeatureY( int index, float baseY )
{
    return baseY + static_cast<float>( index / 2 ) * CONTENT_TOGGLE_ROW_H;
}

float CinematicFeatureX( int index, float contentX, float colW )
{
    return ( index % 2 == 0 ) ? contentX : contentX + colW + 18.0f;
}

bool CinematicFeatureEnabled( const CinematicRenderConfig& cinematic, UICinematicFeature feature )
{
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        return cinematic.skyAtmosphereEnabled;
    case UICinematicFeature::Clouds:
        return cinematic.cloudsEnabled;
    case UICinematicFeature::GodRays:
        return cinematic.godRaysEnabled;
    case UICinematicFeature::VolumetricLight:
        return cinematic.volumetricLightingEnabled;
    case UICinematicFeature::Bloom:
        return cinematic.bloomEnabled;
    case UICinematicFeature::Fog:
        return cinematic.fogEnabled;
    case UICinematicFeature::TerrainRelief:
        return cinematic.terrainReliefEnabled;
    case UICinematicFeature::Shadows:
        return cinematic.shadowsEnabled;
    default:
        return false;
    }
}


float EditorMiniChipWidth( const char* label )
{
    return Text2d::MeasureText( 10.5f, label ? label : "" ) + 18.0f;
}


float EditorMinimizedWidth( const InGameUIFrameData& data, int screenW )
{
    constexpr float margin = 14.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const char* shapeLabel = EditorTab::ObjectLabel( data.editorObjectType );
    const char* modeLabel = data.editorPlacementMode ? "Place" : "Gizmo";
    const char* bodyLabel = data.editorPlaceStatic ? "Static" : "Dynamic";
    const float desiredW = 140.0f +
                           Text2d::MeasureText( 12.0f, shapeLabel ) +
                           EditorMiniChipWidth( modeLabel ) +
                           EditorMiniChipWidth( bodyLabel );
    return std::clamp( desiredW, 328.0f, maxW );
}


void DrawEditorMiniChip( const UIDrawContext& draw,
                         float x,
                         float y,
                         const char* label,
                         const Style::UIColor& fill,
                         const Style::UIColor& text )
{
    const float w = EditorMiniChipWidth( label );
    draw.RoundedRect( x, y, w, 20.0f, Style::Radii().smallButton, fill.r, fill.g, fill.b, fill.a );
    draw.Text( x + 9.0f, y + 5.0f, 10.5f, text.r, text.g, text.b, label );
}


void DrawEditorMiniGlyph( const UIDrawContext& draw, const UIRect& bounds, int objectType )
{
    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedRect( bounds.x, bounds.y, bounds.w, bounds.h, 6.0f, palette.control.r, palette.control.g, palette.control.b, 0.92f );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, palette.border.r, palette.border.g, palette.border.b, 0.75f );

    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.31f;
    const int type = std::clamp( objectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );
    if ( type == EditorTab::OBJECT_BALL || type == EditorTab::OBJECT_SPHERE )
    {
        draw.RoundedRect( cx - r, cy - r, r * 2.0f, r * 2.0f, 999.0f, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b, 0.92f );
        return;
    }
    if ( type == EditorTab::OBJECT_BOX )
    {
        draw.Rect( cx - r, cy - r, r * 2.0f, r * 2.0f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b, 0.94f );
        return;
    }
    if ( type == EditorTab::OBJECT_HULL_DIAMOND )
    {
        draw.Triangle( cx, cy - r, cx + r, cy, cx, cy + r, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b, 0.92f );
        draw.Triangle( cx, cy - r, cx - r, cy, cx, cy + r, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b, 0.92f );
        return;
    }

    draw.Triangle( cx - r, cy + r, cx + r, cy + r, cx, cy - r, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b, 0.92f );
}


void DrawEditorMinimizedWindow( const UIDrawContext& draw, const UIRect& minimized, const InGameUIFrameData& data )
{
    const Style::UIPalette& palette = Style::Palette();
    const UIRect restoreButton = { minimized.x + minimized.w - 36.0f, minimized.y + 7.0f, 26.0f, 22.0f };
    draw.RoundedRect( minimized.x + 4.0f, minimized.y + 5.0f, minimized.w, minimized.h, Style::Radii().window, 0.0f, 0.0f, 0.0f, 0.26f );
    draw.RoundedPanel( minimized, Style::Radii().window, palette.window, palette.border );

    const UIRect glyph = { minimized.x + 11.0f, minimized.y + 7.0f, 24.0f, 24.0f };
    DrawEditorMiniGlyph( draw, glyph, data.editorObjectType );

    const char* modeLabel = data.editorPlacementMode ? "Place" : "Gizmo";
    const char* bodyLabel = data.editorPlaceStatic ? "Static" : "Dynamic";
    const float bodyW = EditorMiniChipWidth( bodyLabel );
    const float modeW = EditorMiniChipWidth( modeLabel );
    const float chipY = minimized.y + 9.0f;
    const float bodyX = restoreButton.x - 10.0f - bodyW;
    const float modeX = bodyX - 8.0f - modeW;

    char shapeLabel[64] = {};
    snprintf( shapeLabel, sizeof( shapeLabel ), "%s", EditorTab::ObjectLabel( data.editorObjectType ) );
    const float labelX = glyph.x + glyph.w + 10.0f;
    const float labelMaxW = (std::max)( 42.0f, modeX - labelX - 10.0f );
    Chrome::FitTitleText( shapeLabel, sizeof( shapeLabel ), 12.0f, labelMaxW );
    draw.Text( labelX, minimized.y + 13.0f, 12.0f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b, shapeLabel );

    Style::UIColor modeFill = palette.accent;
    modeFill.a = 0.92f;
    Style::UIColor bodyFill = data.editorPlaceStatic ? palette.control : palette.warningAccent;
    bodyFill.a = 0.92f;
    DrawEditorMiniChip( draw, modeX, chipY, modeLabel, modeFill, palette.textPrimary );
    DrawEditorMiniChip( draw, bodyX, chipY, bodyLabel, bodyFill, palette.textPrimary );

    draw.RoundedPanel( restoreButton, Style::Radii().smallButton, palette.control, palette.border );
    const float plusX = restoreButton.x + restoreButton.w * 0.5f;
    const float plusY = restoreButton.y + restoreButton.h * 0.5f;
    draw.Rect( plusX - 5.0f, plusY - 1.0f, 10.0f, 2.0f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, 0.96f );
    draw.Rect( plusX - 1.0f, plusY - 5.0f, 2.0f, 10.0f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, 0.96f );
}


} // namespace

bool InGameUI::IsVisible() const
{
    return m_window.isVisible;
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
    }
    else
    {
        m_window.isMinimized = true;
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        m_interaction.blocksCameraMouse = false;
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        m_cineSceneCombo.Close();
        m_renderTargetCombo.Close();
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
    if ( minimized )
    {
        m_window.isMinimized = true;
        Chrome::BeginWindowAnimation( m_window, currentBounds, minimizedBounds, now, true );
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        m_cineSceneCombo.Close();
        m_renderTargetCombo.Close();
        m_activeSlider = 0;
    }
    else
    {
        m_window.isMinimized = false;
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
    m_cineSceneCombo.Close();
    m_renderTargetCombo.Close();
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
}


bool InGameUI::BlocksCameraMouse() const
{
    return m_interaction.blocksCameraMouse;
}


bool InGameUI::BlocksKeyboard() const
{
    return m_window.isVisible && !m_window.isMinimized && ( m_sceneCombo.IsOpen() || m_cineSceneCombo.IsOpen() || m_editorTab.objectCombo.IsOpen() || m_renderTargetCombo.IsOpen() );
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
        m_cineSceneCombo.Close();
        m_renderTargetCombo.Close();
    }
}


void InGameUI::SetWaterComboOpen( bool open )
{
    m_reflectionCombo.SetOpen( open );
    if ( open )
    {
        m_rendererCombo.Close();
        CloseSceneCombo();
        m_cineSceneCombo.Close();
        m_renderTargetCombo.Close();
    }
}


void InGameUI::SetSceneComboOpen( bool open )
{
    m_sceneCombo.SetOpen( open );
    if ( open )
    {
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        m_cineSceneCombo.Close();
        m_renderTargetCombo.Close();
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


void InGameUI::DrawHitboxOverlay( const UIDrawContext& draw, const InGameUIFrameData& data, const UIRect& windowBounds, const UIRect& contentBounds, const UIRect& footerBounds ) const
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
        DrawHitboxRect( draw, { windowBounds.x + windowBounds.w - 26.0f, windowBounds.y + windowBounds.h - 26.0f, 26.0f, 26.0f }, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    }

    DrawTabHitboxes( draw, m_tabBar, static_cast<int>( InGameUITab::Count ) );
    DrawHitboxRect( draw, contentBounds, contentR, contentG, contentB, 0.018f, 0.48f );

    switch ( m_activeTab )
    {
    case InGameUITab::Scene:
        DrawComboHitboxes( draw, m_sceneCombo, SceneDropdownHitboxOptionCount( m_sceneTab, data ), contentR, contentG, contentB );
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
    {
        const char* labels[UI_CINE_SCENE_MAX_OPTIONS] = {};
        int sceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};
        const int cineSceneOptionCount = BuildCineSceneOptions( data.sceneOptions, data.sceneOptionCount, labels, sceneIndices );
        DrawComboHitboxes( draw, m_cineSceneCombo, cineSceneOptionCount, contentR, contentG, contentB );
        for ( int i = 0; i < static_cast<int>( UICinematicFeature::Count ); ++i )
        {
            DrawHitboxRect( draw, m_cinematicFeatureToggles[i].Bounds(), contentR, contentG, contentB );
        }
        for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
        {
            DrawHitboxRect( draw, m_cinematicSliders[i].Bounds(), contentR, contentG, contentB );
        }
    }
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
        return CinematicContentHeight();
    default:
        return ControlsTab::ContentHeight();
    }
}


void InGameUI::CloseSceneCombo()
{
    SceneTab::CloseCombo( m_sceneTab, m_sceneCombo );
}


InGameUIInputResult InGameUI::UpdateInput( HWND hwnd, int screenW, int screenH, double now, const char* const* sceneOptions, int sceneOptionCount, int selectedSceneOption )
{
    PROFILE_SCOPED( "Frame/UI/Input" );
    InGameUIInputResult result;
    m_interaction.blocksCameraMouse = false;
    const InputControl::UIInputSnapshot input = InputControl::CaptureSnapshot( m_interaction.leftWasDown, m_hasMouseOverride, m_mouseOverrideX, m_mouseOverrideY );
    const int wheelDelta = input.wheelDelta;
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
        if ( input.leftPressed && insideMinimized )
        {
            SetMinimized( false, now );
            result.commands.ui.userInteracted = true;
        }
        m_interaction.leftWasDown = leftNow;
        m_interaction.blocksCameraMouse = insideMinimized;
        return result;
    }

    const UIRect inputBounds = Chrome::CurrentWindowRect( m_window, now );
    const int inputX = static_cast<int>( std::round( inputBounds.x ) );
    const int inputY = static_cast<int>( std::round( inputBounds.y ) );
    const int inputW = static_cast<int>( std::round( inputBounds.w ) );
    const int inputH = static_cast<int>( std::round( inputBounds.h ) );
    const UIRect inputHitBounds = { static_cast<float>( inputX ), static_cast<float>( inputY ), static_cast<float>( inputW ), static_cast<float>( inputH ) };
    const bool inside = m_mouseX >= inputX && m_mouseX <= inputX + inputW &&
                        m_mouseY >= inputY && m_mouseY <= inputY + inputH;
    const bool inTitle = inside && m_mouseY < inputY + titleH;
    const bool inTabs = inside && m_mouseY >= inputY + titleH && m_mouseY < inputY + titleH + tabH;
    const bool inResize = !m_window.isMaximized && inside && Chrome::IsResizeHotspot( inputHitBounds, m_mouseX, m_mouseY );
    const int contentY = inputY + titleH + tabH + 12;
    const int contentH = (std::max)( 24, inputH - titleH - tabH - bottomH - contentPad );
    const int bottomY = inputY + inputH - bottomH;
    const bool inContent = inside && m_mouseY >= contentY && m_mouseY <= contentY + contentH;
    const float maxScroll = static_cast<float>( (std::max)( 0, ContentHeight() - contentH ) );
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( inputHitBounds );

    m_tabBar.SetBounds( static_cast<float>( inputX + 14 ), static_cast<float>( inputY + titleH ), static_cast<float>( inputW - 28 ), static_cast<float>( tabH ) );
    const float footerX = static_cast<float>( inputX );
    const float footerY = static_cast<float>( bottomY );
    const UIRect rendererComboBounds = FooterRendererComboBounds( footerX, footerY );
    const UIRect waterComboBounds = FooterWaterComboBounds( footerX, footerY );
    const UIRect blurBounds = FooterBlurBounds( footerX, footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( footerX, footerY );
    const UIRect hitboxBounds = FooterHitboxBounds( footerX, footerY );
    const UIRect timelineBounds = FooterTimelineBounds( footerX, footerY );
    const UIRect perfBounds = FooterPerfBounds( footerX, footerY );
    m_rendererCombo.SetBounds( rendererComboBounds.x, rendererComboBounds.y, rendererComboBounds.w, rendererComboBounds.h );
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

    const char* cineSceneOptions[UI_CINE_SCENE_MAX_OPTIONS] = {};
    int cineSceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};
    const int cineSceneOptionCount = BuildCineSceneOptions( sceneOptions, sceneOptionCount, cineSceneOptions, cineSceneIndices );

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
        wheelHandled = SceneTab::HandleComboWheel( m_sceneTab, m_sceneCombo, sceneOptions, sceneOptionCount, m_mouseX, m_mouseY, wheelDelta, contentX, rowBase, contentW );
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
            m_cineSceneCombo.Close();
            m_editorTab.objectCombo.Close();
            m_renderTargetCombo.Close();
        }
        else if ( m_cineSceneCombo.IsOpen() )
        {
            const int option = m_cineSceneCombo.HitOption( m_mouseX, m_mouseY, cineSceneOptionCount );
            if ( option >= 0 && option < cineSceneOptionCount )
            {
                result.commands.cinematic.requestedModeSceneIndex = cineSceneIndices[option];
                m_cineSceneCombo.Close();
            }
            else if ( m_cineSceneCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_cineSceneCombo.ToggleOpen();
            }
            else
            {
                m_cineSceneCombo.Close();
            }
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
                const bool optionDisabled = option >= 0 && option < 32 && ( m_lastRenderTargetDisabledMask & ( 1u << option ) ) != 0;
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
            m_cineSceneCombo.Close();
            m_editorTab.objectCombo.Close();
        }
        else if ( m_editorTab.objectCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Editor )
            {
                const float contentX = static_cast<float>( inputX + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
                EditorTab::HandleContentClick( m_editorTab,
                                               result,
                                               m_mouseX,
                                               m_mouseY,
                                               contentX,
                                               rowBase,
                                               contentW );
            }
            else
            {
                m_editorTab.objectCombo.Close();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            m_cineSceneCombo.Close();
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
            m_cineSceneCombo.Close();
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
                m_cineSceneCombo.Close();
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
            m_cineSceneCombo.Close();
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
                m_cineSceneCombo.Close();
                m_editorTab.objectCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Editor )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            EditorTab::HandleContentClick( m_editorTab,
                                           result,
                                           m_mouseX,
                                           m_mouseY,
                                           contentX,
                                           rowBase,
                                           contentW );
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            m_cineSceneCombo.Close();
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
                 m_activeSlider != 0 &&
                 m_activeSlider != previousActiveSlider )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_cineSceneCombo.Close();
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
            m_cineSceneCombo.Close();
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
            m_saveRenderDefaultsButton.SetBounds( contentX + contentW - UI_RENDER_SAVE_BUTTON_W, scrolledY + UI_RENDER_FEATURE_START_Y, UI_RENDER_SAVE_BUTTON_W, 24.0f );
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
            m_cineSceneCombo.Close();
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
                m_cineSceneCombo.Close();
                m_editorTab.objectCombo.Close();
            }
            else
            {
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                m_cineSceneCombo.Close();
                m_editorTab.objectCombo.Close();
                m_renderTargetCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Cinematic )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
            const float colW = (std::max)( 148.0f, contentW * 0.46f );
            bool capturedSlider = false;

            m_cineSceneCombo.SetBounds( contentX, scrolledY + UI_CINEMATIC_SCENE_Y, contentW, 24.0f );
            if ( m_cineSceneCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_cineSceneCombo.ToggleOpen();
                m_rendererCombo.Close();
                m_reflectionCombo.Close();
                CloseSceneCombo();
            }
            else
            {
                const float featureBaseY = scrolledY + UI_CINEMATIC_FEATURE_START_Y + 26.0f;
                for ( int i = 0; i < static_cast<int>( UICinematicFeature::Count ); ++i )
                {
                    const float tx = CinematicFeatureX( i, contentX, colW );
                    const float toggleY = CinematicFeatureY( i, featureBaseY );
                    m_cinematicFeatureToggles[i].SetBounds( tx, toggleY, colW, 24.0f );
                    if ( m_cinematicFeatureToggles[i].HitTest( m_mouseX, m_mouseY ) )
                    {
                        result.commands.cinematic.requestedFeature = kCinematicFeatureSpecs[i].feature;
                        break;
                    }
                }

                if ( result.commands.cinematic.requestedFeature == UICinematicFeature::None )
                {
                    const float rowBase = scrolledY + UI_CINEMATIC_START_Y;
                    for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
                    {
                        m_cinematicSliders[i].SetBounds( contentX, CinematicSliderY( i, rowBase ), contentW, 34.0f );
                        if ( m_cinematicSliders[i].HitTest( m_mouseX, m_mouseY ) )
                        {
                            m_activeSlider = UI_CINEMATIC_SLIDER_BASE + i;
                            SetCinematicSliderResult( result, m_cinematicSliders[i], m_mouseX, kCinematicSliderSpecs[i] );
                            capturedSlider = true;
                            break;
                        }
                    }
                }
            }

            if ( capturedSlider )
            {
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            if ( capturedSlider )
            {
                m_cineSceneCombo.Close();
            }
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
            m_cineSceneCombo.Close();
        }
        else if ( inside && m_mouseY >= inputY + inputH - bottomH )
        {
            if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                m_cineSceneCombo.Close();
                m_editorTab.objectCombo.Close();
                m_renderTargetCombo.Close();
            }
            else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
                m_rendererCombo.Close();
                CloseSceneCombo();
                m_cineSceneCombo.Close();
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
            m_cineSceneCombo.Close();
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
             !ProfilerTab::UpdateActiveSlider( m_profilerTab, m_activeSlider, m_mouseX, m_lastMaxWorkerThreadCount, result ) &&
             !OptionsTab::UpdateActiveSlider( m_optionsTab, m_activeSlider, m_mouseX, m_lastModelCapacity, result ) &&
             !PhysicsTab::UpdateActiveSlider( m_physicsTab, m_activeSlider, m_mouseX, result ) )
        {
            const int renderSlider = RenderSliderIndexFromActiveSlider( m_activeSlider );
            if ( renderSlider >= 0 )
            {
                SetRenderSliderResult( result, m_renderSliders[renderSlider], m_mouseX, kRenderSliderSpecs[renderSlider] );
            }
            else
            {
                const int cinematicSlider = CinematicSliderIndexFromActiveSlider( m_activeSlider );
                if ( cinematicSlider >= 0 )
                {
                    SetCinematicSliderResult( result, m_cinematicSliders[cinematicSlider], m_mouseX, kCinematicSliderSpecs[cinematicSlider] );
                }
                else
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
        m_window.x = std::clamp( m_mouseX - m_interaction.dragOffsetX, margin, (std::max)( margin, screenW - m_window.width - margin ) );
        m_window.y = std::clamp( m_mouseY - m_interaction.dragOffsetY, margin, (std::max)( margin, screenH - m_window.height - margin ) );
        if ( oldX != m_window.x || oldY != m_window.y )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }
    if ( leftNow && m_interaction.isResizing )
    {
        const int oldW = m_window.width;
        const int oldH = m_window.height;
        m_window.width = std::clamp( m_interaction.resizeStartW + m_mouseX - m_interaction.resizeStartMouseX, minW, maxW );
        m_window.height = std::clamp( m_interaction.resizeStartH + m_mouseY - m_interaction.resizeStartMouseY, minH, maxH );
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
                SetRenderSliderResult( result, m_renderSliders[renderSlider], m_mouseX, kRenderSliderSpecs[renderSlider] );
            }
            else
            {
                const int cinematicSlider = CinematicSliderIndexFromActiveSlider( m_activeSlider );
                if ( cinematicSlider >= 0 )
                {
                    SetCinematicSliderResult( result, m_cinematicSliders[cinematicSlider], m_mouseX, kCinematicSliderSpecs[cinematicSlider] );
                }
                else
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
    m_interaction.blocksCameraMouse = inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0;
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
        m_window.minimizedWidth = data.editorModeEnabled ? EditorMinimizedWidth( data, screenW ) : MinimizedWidthForTitle( titleText, screenW );
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        if ( data.editorModeEnabled )
        {
            DrawEditorMinimizedWindow( draw, minimized, data );
        }
        else
        {
            Chrome::FitTitleText( titleText, sizeof( titleText ), 12.5f, minimized.w - 76.0f );
            Chrome::DrawMinimizedWindow( draw, minimized, titleText );
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
                                                                 m_cineSceneCombo.IsOpen(),
                                                                 m_editorTab.objectCombo.IsOpen(),
                                                                 m_renderTargetCombo.IsOpen(),
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
    Chrome::DrawTitleButtons( draw, Chrome::GetTitleButtonRects( windowBounds ), m_window.isMaximized, m_mouseX, m_mouseY );

    static const char* kTabs[] = { "Profile", "Scene", "Editor", "Physics", "Options", "Render", "Targets", "Controls", "Cine" };
    const int tabCount = static_cast<int>( InGameUITab::Count );
    const float tabPad = 14.0f;
    m_tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    m_tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( m_activeTab ) );

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedPanel( { contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f }, Style::Radii().window, palette.windowSubtle, palette.innerBorder );

    if ( m_activeTab == InGameUITab::Profiler )
    {
        ProfilerTab::Draw( m_profilerTab, draw, data, contentX, contentY, contentW, contentH, m_scrollY, m_activeSlider );
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
        m_saveRenderDefaultsButton.SetBounds( contentX + contentW - UI_RENDER_SAVE_BUTTON_W, scrolledY + UI_RENDER_FEATURE_START_Y, UI_RENDER_SAVE_BUTTON_W, 24.0f );
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
                DrawSectionTitle( draw, contentX, contentY, contentH, sliderY - UI_RENDER_SECTION_H + 4.0f, 12.0f, spec.section );
            }
            const float value = std::clamp( RenderValueForParam( data.ordinaryRender, spec.param ), spec.minValue, spec.maxValue );
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
        const UIRenderTargetPreviewResource* selected = hasSelection ? &data.renderTargetPreviews[selectedIndex] : nullptr;
        const bool selectedAvailable = selected && selected->available && selected->textureHandle != 0 && selected->width > 0 && selected->height > 0;
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
            draw.RoundedPanel( previewPanel, Style::Radii().control, targetPalette.windowSubtle, targetPalette.innerBorder );
            const UIRect previewInset = { previewPanel.x + 10.0f, previewPanel.y + 10.0f, (std::max)( 1.0f, previewPanel.w - 20.0f ), (std::max)( 1.0f, previewPanel.h - 20.0f ) };
            previewImage = selected ? FitRectToAspect( previewInset, selected->width, selected->height ) : previewInset;
            draw.RoundedRect( previewImage.x - 1.0f, previewImage.y - 1.0f, previewImage.w + 2.0f, previewImage.h + 2.0f, Style::Radii().control, 0.01f, 0.015f, 0.018f, 0.92f );
        }

        if ( selectedAvailable && IsBlockVisible( contentY, contentH, previewImage.y, previewImage.h ) )
        {
            FlushUIDrawList( drawList, screenW, screenH );
            drawList.Clear();
            DrawRenderTargetPreviewTexture( m_renderTargetPreviewShader, m_renderTargetPreviewVB, draw, *selected, previewImage, previewClip );
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
            draw.Outline( previewImage.x, previewImage.y, previewImage.w, previewImage.h, targetPalette.border.r, targetPalette.border.g, targetPalette.border.b, 0.72f );
        }

        const char* selectedText = selected ? selected->label : "No targets";
        m_renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_COMBO_Y, 24.0f ) )
        {
            m_renderTargetCombo.Draw( draw, "View", selectedText, options, targetCount, selectedIndex, m_mouseX, m_mouseY, m_lastRenderTargetDisabledMask );
        }
    }
    else if ( m_activeTab == InGameUITab::Cinematic )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        const char* cineSceneOptions[UI_CINE_SCENE_MAX_OPTIONS] = {};
        int cineSceneIndices[UI_CINE_SCENE_MAX_OPTIONS] = {};
        const int cineSceneOptionCount = BuildCineSceneOptions( data.sceneOptions, data.sceneOptionCount, cineSceneOptions, cineSceneIndices );
        const int selectedCineSceneOption = SelectedCineSceneOption( cineSceneIndices, cineSceneOptionCount, data.selectedCineModeSceneOption );

        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Cine" );
        m_cineSceneCombo.SetBounds( contentX, scrolledY + UI_CINEMATIC_SCENE_Y, contentW, 24.0f );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_CINEMATIC_SCENE_Y, 24.0f ) )
        {
            m_cineSceneCombo.Draw( draw, "Mode", cineSceneOptions, cineSceneOptionCount, selectedCineSceneOption, m_mouseX, m_mouseY );
        }
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_CINEMATIC_FEATURE_START_Y, 18.0f ) )
        {
            DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + UI_CINEMATIC_FEATURE_START_Y, 12.0f, "Passes" );
        }
        const float featureBaseY = scrolledY + UI_CINEMATIC_FEATURE_START_Y + 26.0f;
        for ( int i = 0; i < static_cast<int>( UICinematicFeature::Count ); ++i )
        {
            const float tx = CinematicFeatureX( i, contentX, colW );
            const float toggleY = CinematicFeatureY( i, featureBaseY );
            DrawContentToggle( draw,
                               contentY,
                               contentH,
                               m_cinematicFeatureToggles[i],
                               tx,
                               toggleY,
                               colW,
                               kCinematicFeatureSpecs[i].label,
                               CinematicFeatureEnabled( data.cinematic, kCinematicFeatureSpecs[i].feature ) );
        }
        const float baseY = scrolledY + UI_CINEMATIC_START_Y;
        for ( int i = 0; i < static_cast<int>( UICinematicParam::Count ); ++i )
        {
            const CinematicSliderSpec& spec = kCinematicSliderSpecs[i];
            const float sliderY = CinematicSliderY( i, baseY );
            if ( spec.section && IsRowVisible( contentY, contentH, sliderY - UI_CINEMATIC_SECTION_H + 4.0f, 18.0f ) )
            {
                DrawSectionTitle( draw, contentX, contentY, contentH, sliderY - UI_CINEMATIC_SECTION_H + 4.0f, 12.0f, spec.section );
            }
            const float value = std::clamp( CinematicValueForParam( data.cinematic, spec.param ), spec.minValue, spec.maxValue );
            snprintf( buf, sizeof( buf ), spec.valueFormat, value );
            m_cinematicSliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );
            if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
            {
                m_cinematicSliders[i].Draw( draw, spec.label, buf, value, spec.minValue, spec.maxValue );
            }
        }
    }
    else
    {
        ControlsTab::Draw( m_controlsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY );
    }

    m_scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    m_scrollBar.Draw( draw, static_cast<float>( ContentHeight() ), contentH, m_scrollY, m_scrollbarVisibleUntil, data.now );

    const float by = y + h - bottomH;
    draw.Rect( x + 16.0f, by, w - 32.0f, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.14f );
    const float footerPad = 18.0f;
    const float footerGap = 16.0f;
    const float footerX = x + footerPad;
    const float footerW = (std::max)( 120.0f, w - footerPad * 2.0f );
    const bool hasSeparateStats = footerW >= 560.0f;
    const float controlsW = hasSeparateStats ? 462.0f : footerW;
    draw.RoundedPanel( { footerX, by + 16.0f, controlsW, 56.0f }, Style::Radii().control, palette.windowSubtle, palette.innerBorder );

    const UIRect rendererComboBounds = FooterRendererComboBounds( x, by );
    const UIRect waterComboBounds = FooterWaterComboBounds( x, by );
    const UIRect blurFooterBounds = FooterBlurBounds( x, by );
    const UIRect vsyncFooterBounds = FooterVsyncBounds( x, by );
    const UIRect hitboxFooterBounds = FooterHitboxBounds( x, by );
    const UIRect timelineFooterBounds = FooterTimelineBounds( x, by );
    const UIRect perfFooterBounds = FooterPerfBounds( x, by );
    m_rendererCombo.SetBounds( rendererComboBounds.x, rendererComboBounds.y, rendererComboBounds.w, rendererComboBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurFooterBounds.x, blurFooterBounds.y, blurFooterBounds.w, blurFooterBounds.h );
    m_vsyncToggle.SetBounds( vsyncFooterBounds.x, vsyncFooterBounds.y, vsyncFooterBounds.w, vsyncFooterBounds.h );
    m_hitboxToggle.SetBounds( hitboxFooterBounds.x, hitboxFooterBounds.y, hitboxFooterBounds.w, hitboxFooterBounds.h );
    m_histogramToggle.SetBounds( perfFooterBounds.x, perfFooterBounds.y, perfFooterBounds.w, perfFooterBounds.h );
    m_timelineToggle.SetBounds( timelineFooterBounds.x, timelineFooterBounds.y, timelineFooterBounds.w, timelineFooterBounds.h );
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
    const int cpuPercent = static_cast<int>( std::clamp( ( data.renderMs + data.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int gpuPercent = static_cast<int>( std::clamp( data.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.drawCallsBeforeUI + data.UIDrawCalls;
    snprintf( status, sizeof( status ), "%.0f", data.fps );
    if ( hasSeparateStats )
    {
        const float statsX = footerX + controlsW + footerGap;
        const float statsW = (std::max)( 120.0f, x + w - footerPad - statsX );
        draw.RoundedPanel( { statsX, by + 16.0f, statsW, 56.0f }, Style::Radii().control, palette.windowSubtle, palette.innerBorder );

        if ( statsW < 350.0f )
        {
            char fpsText[32];
            char frameText[32];
            char drawText[32];
            snprintf( fpsText, sizeof( fpsText ), "%.0f", data.fps );
            snprintf( frameText, sizeof( frameText ), "%.2f ms", frameDisplayMs );
            snprintf( drawText, sizeof( drawText ), "%d/%d", drawCalls, data.UIDrawCalls );
            DrawCompactFooterStat( draw, statsX, by + 23.0f, "FPS", fpsText, palette.accent.r, palette.accent.g, palette.accent.b );
            DrawCompactFooterStat( draw, statsX, by + 41.0f, "Frame", frameText, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
            DrawCompactFooterStat( draw, statsX, by + 59.0f, "Draw/UI", drawText, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
        }
        else
        {
            DrawFooterStatCell( draw, statsX + 18.0f, by, "FPS", status, palette.accent.r, palette.accent.g, palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 78.0f, by );
            snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
            DrawFooterStatCell( draw, statsX + 100.0f, by, "Frame Time", status, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
            DrawFooterStatDivider( draw, statsX + 190.0f, by );
            snprintf( status, sizeof( status ), "%d%%", cpuPercent );
            DrawFooterStatCell( draw, statsX + 212.0f, by, "CPU", status, palette.accent.r, palette.accent.g, palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 266.0f, by );
            snprintf( status, sizeof( status ), "%d%%", gpuPercent );
            DrawFooterStatCell( draw, statsX + 288.0f, by, "GPU", status, palette.accent.r, palette.accent.g, palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 342.0f, by );
            snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.UIDrawCalls );
            DrawFooterStatCell( draw, statsX + statsW - 112.0f, by, "Draws / UI", status, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
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

    draw.Rect( x + w - 24.0f, y + h - 9.0f, 14.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.58f );
    draw.Rect( x + w - 18.0f, y + h - 15.0f, 8.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.46f );
    draw.Rect( x + w - 12.0f, y + h - 21.0f, 2.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.38f );

    DrawHitboxOverlay( draw, data, windowBounds, { contentX, contentY, contentW, contentH }, { footerX, by + 16.0f, controlsW, 56.0f } );

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
