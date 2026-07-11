/*
File: SkullbonezSource/GameObjects/GameModel.cpp
Purpose:
  Implements transient per-object contact-highlight state.

Mental model:
  Durable scene metadata lives in SceneEntityStore. This file updates only the
  short timers sampled into render-instance contact feedback.

Glossary:
  Presentation row: Per-object transient display feedback kept in model order.
  Contact flash: Render-only feedback that marks fixed-body or audio contacts.

Invariants:
  - Identity, names, materials, asset provenance, and physics state must not be
    stored or mirrored here.
  - Highlight timers are presentation-only and allocation-free.

Related:
  - SkullbonezSource/GameObjects/GameModel.h
  - SkullbonezSource/GameObjects/GameModelCollection.cpp
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModel.h"
#include "../Core/Common.h"

#include <algorithm>


using namespace SkullbonezCore::GameObjects;

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
