/*
File: UIFontMetrics.h
Purpose:
  Owns the renderer-independent glyph advances used by Legacy UI layout.

Summary:
  Publishes one fixed immutable metric table so every UI measurement uses the
  same baked facts as renderer glyph placement without importing its owners.

Mental model:
  The baked font atlas contains pixels and one advance per printable ASCII
  glyph. Runtime installs those advances once during cold font setup; UI then
  measures text without borrowing the renderer's atlas, shader, or text batch.

Glossary:
  Glyph advance: Horizontal distance added after laying out one character.
  Baked font: The committed SDF atlas whose header is the single metric source.

Invariants:
  - Printable ASCII 32-127 maps to exactly 96 immutable advance values.
  - Reinstallation may confirm identical values but cannot change live layout.
  - Unsupported bytes retain the legacy half-size fallback advance.

Related:
  - Rendering/Text.cpp loads the same advances from font_atlas.sdf.
  - UIDrawList.h records text without renderer ownership.
*/
#pragma once

#include <array>

namespace SkullbonezCore::UI
{
class UIFontMetrics
{
  public:
    static constexpr int GLYPH_COUNT = 96;

    // Installs the baked advances during cold setup. Returns false if a later
    // install attempts to change the already-published layout contract.
    static bool Install( const float* advances, int count );
    static bool Ready();
    static float MeasureText( float size, const char* text );
    static const std::array<float, GLYPH_COUNT>& Advances();

  private:
    inline static std::array<float, GLYPH_COUNT> s_advances {};
    inline static bool s_ready = false;
};
} // namespace SkullbonezCore::UI
