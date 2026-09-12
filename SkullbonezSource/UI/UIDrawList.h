/*
File: SkullbonezSource/UI/UIDrawList.h
Purpose:
  Declares the fixed-capacity ordered draw values authored by GameUI.

Summary:
  UIDrawList stores plain screen-space commands plus copied text. Cached lists
  can be composed with offsets, while Runtime consumes a read-only span and
  resolves preview identities at submission time.

Invariants:
  - Command and text storage never grows in steady runtime.
  - Append preserves source order and copies referenced text.
  - Fingerprints hash semantic values, never padding or unused capacity.

Related:
  - SkullbonezSource/UI/UIDrawList.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

#include <cstdint>
#include <span>
#include <type_traits>

namespace SkullbonezCore
{
namespace UI
{

// Stable presentation groups let independent presenters share one transition clock.
enum class UIPanel : uint8_t
{
    None,
    Left,
    Right,
    Transport,
    Drawer,
    DiagnosticPrimary,
    DiagnosticSecondary,
    QuickTools,
    AuxiliaryPrimary,
    AuxiliarySecondary,
    AuxiliaryGrid,
    Header,
    Popup,
    LowerLeft,
    AttachedRight,
    Count
};

class UIDrawList
{
  public:
    // Concept: widgets record drawing intent; Runtime/Render later consumes a
    // bounded read-only view. The list never calls back into a backend owner.
    //
    // This keeps layout/input code independent from the renderer. Widgets can
    // push rectangles, triangles, and text in UI order; the final draw context
    // translates those records to the active render backend after hit testing
    // has already used the same layout numbers.
    // Why: a dense 64 by 48 value grid needs 3,072 rectangles before its
    // labels, surrounding panels and transport. Storage remains fixed; stats
    // report exhaustion so larger compositions cannot silently lose controls.
    static constexpr int MAX_COMMANDS = 4096;
    static constexpr int MAX_TEXT_BYTES = 16384;
    static constexpr int MAX_CLIP_DEPTH = 32;
    static_assert( MAX_TEXT_BYTES > 0 );

    struct PreviewTargetId
    {
        uint16_t catalogIndex;
        bool valid;
    };

    enum class CommandType : uint8_t
    {
        Rect,
        RoundedRect,
        Triangle,
        Text,
        PushClip,
        PopClip,
        PreviewImage,
        LayerBreak,
        VerticalText
    };

    struct Stats
    {
        int commandCount = 0;
        int textBytes = 0;
        bool commandOverflow = false;
        bool textOverflow = false;
        bool clipOverflow = false;
        int maxClipDepth = 0;
    };

    struct Command
    {
        // Why: PushCommand value-initializes each committed row. Keeping unused
        // fixed-capacity rows trivial avoids touching every reserved page when
        // retained UI scratch owners are constructed.
        CommandType type;
        float x0;
        float y0;
        float x1;
        float y1;
        float x2;
        float y2;
        float w;
        float h;
        float radius;
        float pxSize;
        float r;
        float g;
        float b;
        float a;
        int textOffset;
        PreviewTargetId preview;
        bool foreground;
        UIPanel panel;
    };
    static_assert( std::is_trivially_copyable_v<Command>, "UI draw commands must remain plain inspectable values." );

    void Clear();
    void AddRect( const UIRect& bounds, const Style::UIColor& color );
    void AddRoundedRect( const UIRect& bounds, float radius, const Style::UIColor& color );
    void AddTriangle( const UITriangle& triangle, const Style::UIColor& color );
    void AddText( UIPoint position, float pxSize, const Style::UIColor& color, const char* value, bool vertical = false );
    void PushClip( const UIRect& bounds );
    void BeginLayer();
    // Foreground groups are independently clipped popup/tooltip drawing.
    // App may extract them and submit them after other workspace presenters.
    void BeginForeground();
    void EndForeground();
    void ExtractForeground( UIDrawList& destination );
    void PopClip();

    // Fallback fill and label are part of the recorded value so a missing
    // frame-local renderer target cannot silently produce a blank panel.
    void AddPreviewImage( PreviewTargetId target, const UIRect& bounds, const Style::UIColor& fallbackColor, const char* fallbackLabel );

    // Appends another list in order and applies a screen-space translation to
    // its geometry. Text is copied into this list's bounded storage so neither
    // the source list nor its cache must outlive the composed frame.
    void Append( const UIDrawList& source, float offsetX = 0.0f, float offsetY = 0.0f );

    // Panel metadata affects composition, not the settled visual fingerprint.
    UIPanel SetPanel( UIPanel panel );
    void CopyPanel( const UIDrawList& source, UIPanel panel );
    void ApplyPresentation( UIPoint offset, float opacity );
    bool HasPanel( UIPanel panel ) const;
    bool Empty() const;
    Stats GetStats() const;
    std::span<const Command> Commands() const;
    const char* TextAt( int offset ) const;

    // Returns a semantic fingerprint of command values and referenced text.
    // Object padding and unused buffer capacity never affect the result.
    uint64_t Fingerprint() const;

  private:
    Command* PushCommand();
    int StoreText( const char* value );

    Command m_commands[MAX_COMMANDS];
    char m_text[MAX_TEXT_BYTES];
    int m_commandCount = 0;
    int m_textBytes = 0;
    bool m_commandOverflow = false;
    bool m_textOverflow = false;
    bool m_clipOverflow = false;
    int m_clipDepth = 0;
    int m_suppressedClipDepth = 0;
    int m_maxClipDepth = 0;
    int m_foregroundDepth = 0;
    UIPanel m_panel = UIPanel::None;
};

class UIPanelScope
{
  public:
    UIPanelScope( UIDrawList& draw, UIPanel panel ) : m_draw( draw ), m_previous( draw.SetPanel( panel ) )
    {
    }
    ~UIPanelScope()
    {
        m_draw.SetPanel( m_previous );
    }
    UIPanelScope( const UIPanelScope& ) = delete;
    UIPanelScope& operator=( const UIPanelScope& ) = delete;

  private:
    UIDrawList& m_draw;
    UIPanel m_previous;
};

} // namespace UI
} // namespace SkullbonezCore
