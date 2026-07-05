/*
File: SkullbonezSource/GameObjects/GameModel.h
Purpose:
  Defines the presentation row for one renderable scene object.

Mental model:
  Physics body and collider authority lives in PhysicsBodyStore and
  ColliderStore. GameModel remains as collection-order presentation metadata:
  display name, render material intent, and short contact flash timers used by
  rendering and replay restore.

Glossary:
  Presentation row: Per-object data that affects display or diagnostics but
    does not define simulation state.
  Contact flash: Short render-only highlight after fixed-body or audio contact.

Invariants:
  - GameModel does not store pose, velocity, mass, fixed-body state, terrain, or
    collision geometry. Those values belong to physics stores and descriptors.
  - Contact flash timers are presentation state; changing them must not alter
    deterministic physics rows.

Related:
  - SkullbonezSource/GameObjects/GameModel.cpp
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Physics/ColliderStore.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>
#include "../Rendering/RenderMaterial.h"


namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel
{
  private:
    float m_fixedContactHighlightSeconds;       // Seconds remaining for fixed-body red contact feedback.
    float m_audioContactHighlightSeconds;       // Seconds remaining for contact-audio white feedback.
    float m_renderTintR;                        // Per-instance render tint red channel.
    float m_renderTintG;                        // Per-instance render tint green channel.
    float m_renderTintB;                        // Per-instance render tint blue channel.
    float m_renderColorOverride;                // 1 = tint as material color, 0 = material tint multiplier.
    Rendering::RenderMaterial m_renderMaterial; // Render-only material intent mirrored from legacy tint fields.
    char m_name[64];                            // Optional display/logging name (empty = unnamed).

  public:
    GameModel();
    ~GameModel() = default;
    GameModel( GameModel&& ) noexcept = default;
    GameModel& operator=( GameModel&& ) noexcept = default;

    void SetName( const char* name );           // Diagnostic name is capped at 63 bytes for deterministic logs.
    const char* GetName() const;
    void SetRenderTint( float tintR,
                        float tintG,
                        float tintB,
                        float colorOverride );  // Render-only color override; physics state is unaffected.
    void GetRenderTint( float& tintR, float& tintG, float& tintB, float& colorOverride ) const;
    void SetRenderMaterial( const Rendering::RenderMaterial& material );
    const Rendering::RenderMaterial& GetRenderMaterial() const;
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
