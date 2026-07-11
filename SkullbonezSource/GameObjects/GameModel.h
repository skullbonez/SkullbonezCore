/*
File: SkullbonezSource/GameObjects/GameModel.h
Purpose:
  Defines transient per-object contact-highlight presentation state.

Mental model:
  SceneEntityStore owns durable identity, names, and material intent. GameModel
  is the same-row ephemeral contact feedback that render preparation samples.

Glossary:
  Presentation row: Per-object transient data that affects display but does not
    define simulation or durable scene state.
  Contact flash: Short render-only highlight after fixed-body or audio contact.

Invariants:
  - GameModel stores no identity, name, material, physics, or asset metadata.
  - Contact flash timers are presentation state; changing them must not alter
    deterministic physics rows.

Related:
  - SkullbonezSource/GameObjects/GameModel.cpp
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Physics/ColliderStore.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>


namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel
{
  private:
    float m_fixedContactHighlightSeconds;       // Seconds remaining for fixed-body red contact feedback.
    float m_audioContactHighlightSeconds;       // Seconds remaining for contact-audio white feedback.

  public:
    GameModel();
    ~GameModel() = default;
    GameModel( GameModel&& ) noexcept = default;
    GameModel& operator=( GameModel&& ) noexcept = default;

    void NotifyFixedContact( float highlightSeconds );
    void NotifyAudioContact( float highlightSeconds );
    void TickFixedContactHighlight( float dt ); // dt is seconds; saturates both contact highlight timers.
    float GetFixedContactHighlightAlpha() const;
    float GetAudioContactHighlightAlpha() const;
    float GetFixedContactHighlightSeconds() const;
    void SetFixedContactHighlightSeconds( float seconds );
};
} // namespace GameObjects
} // namespace SkullbonezCore
