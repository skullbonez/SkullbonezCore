/*
File: SkullbonezSource/GameObjects/GameModel.cpp
Purpose:
  Implements the presentation row for one renderable scene object.

Mental model:
  GameModel is no longer a physics-authoring cache. Scene and editor creation
  pass PhysicsBodyCreateDesc and PhysicsColliderCreateDesc beside the model;
  this file keeps only render metadata and short presentation highlight timers.

Glossary:
  Presentation row: Per-object display and diagnostic data kept in model order.
  Contact flash: Render-only feedback that marks fixed-body or audio contacts.
  Legacy tint: Older scene color fields converted into RenderMaterial payloads.

Invariants:
  - Physics pose, mass, terrain support, fixed flags, and collider shapes must
    not be stored or mirrored here.
  - Highlight timers are presentation-only and allocation-free.

Related:
  - SkullbonezSource/GameObjects/GameModel.h
  - SkullbonezSource/GameObjects/GameModelCollection.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModel.h"
#include "../Core/Common.h"

#include <algorithm>


using namespace SkullbonezCore::GameObjects;
namespace Rendering = SkullbonezCore::Rendering;

namespace
{
float HighlightAlpha( float seconds, float fadeSeconds )
{
    if ( fadeSeconds <= 0.0f )
    {
        return 0.0f;
    }
    return std::clamp( seconds / fadeSeconds, 0.0f, 1.0f );
}

void TickHighlightSeconds( float& seconds, float dt )
{
    if ( seconds <= 0.0f || dt <= 0.0f )
    {
        return;
    }

    seconds = (std::max)( 0.0f, seconds - dt );
}
} // namespace

GameModel::GameModel()
{
    m_fixedContactHighlightSeconds = 0.0f;
    m_audioContactHighlightSeconds = 0.0f;
    m_renderTintR = 1.0f;
    m_renderTintG = 1.0f;
    m_renderTintB = 1.0f;
    m_renderColorOverride = 0.0f;
    m_renderMaterial = Rendering::MakeRenderMaterialFromLegacyTint( m_renderTintR,
                                                                    m_renderTintG,
                                                                    m_renderTintB,
                                                                    m_renderColorOverride );
    m_name[0] = '\0';
}

void GameModel::SetName( const char* name )
{
    strncpy_s( m_name, sizeof( m_name ), name, _TRUNCATE );
}

const char* GameModel::GetName() const
{
    return m_name;
}

void GameModel::SetRenderTint( float tintR, float tintG, float tintB, float colorOverride )
{
    m_renderTintR = tintR;
    m_renderTintG = tintG;
    m_renderTintB = tintB;
    m_renderColorOverride = colorOverride;
    m_renderMaterial = Rendering::MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride );
}

void GameModel::GetRenderTint( float& tintR, float& tintG, float& tintB, float& colorOverride ) const
{
    tintR = m_renderTintR;
    tintG = m_renderTintG;
    tintB = m_renderTintB;
    colorOverride = m_renderColorOverride;
}

void GameModel::SetRenderMaterial( const Rendering::RenderMaterial& material )
{
    m_renderMaterial = material;
    m_renderTintR = material.baseColor[0];
    m_renderTintG = material.baseColor[1];
    m_renderTintB = material.baseColor[2];
    m_renderColorOverride = Rendering::RenderMaterialLegacyInstanceMode( material );
}

const Rendering::RenderMaterial& GameModel::GetRenderMaterial() const
{
    return m_renderMaterial;
}

void GameModel::NotifyFixedContact( float highlightSeconds )
{
    if ( highlightSeconds > m_fixedContactHighlightSeconds )
    {
        m_fixedContactHighlightSeconds = highlightSeconds;
    }
}

void GameModel::NotifyAudioContact( float highlightSeconds )
{
    if ( highlightSeconds > m_audioContactHighlightSeconds )
    {
        m_audioContactHighlightSeconds = highlightSeconds;
    }
}

void GameModel::TickFixedContactHighlight( float dt )
{
    TickHighlightSeconds( m_fixedContactHighlightSeconds, dt );
    TickHighlightSeconds( m_audioContactHighlightSeconds, dt );
}

float GameModel::GetFixedContactHighlightAlpha() const
{
    static constexpr float FADE_SECONDS = 0.5f;
    return HighlightAlpha( m_fixedContactHighlightSeconds, FADE_SECONDS );
}

float GameModel::GetAudioContactHighlightAlpha() const
{
    static constexpr float FADE_SECONDS = 0.1f;
    return HighlightAlpha( m_audioContactHighlightSeconds, FADE_SECONDS );
}

float GameModel::GetFixedContactHighlightSeconds() const
{
    return m_fixedContactHighlightSeconds;
}

void GameModel::SetFixedContactHighlightSeconds( float seconds )
{
    m_fixedContactHighlightSeconds = (std::max)( 0.0f, seconds );
}
