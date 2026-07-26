/*
File: SkullbonezSource/UI/UIDrawList.h
Purpose:
  Declares the fixed-capacity ordered draw values authored by Legacy UI.

Summary:
  UIDrawList stores plain screen-space commands plus copied text. Cached lists
  can be composed with offsets, while Runtime consumes a read-only span and
  resolves preview identities at submission time.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Command and text storage never grows in steady runtime.
  - Append preserves source order and copies referenced text.
  - Fingerprints hash semantic values, never padding or unused capacity.

Related:
  - SkullbonezSource/UI/UIDrawList.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>
#include <span>
#include <type_traits>

namespace SkullbonezCore
{
namespace UI
{

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
    // Invariant: closure capture measured 289 commands and 1,369 text bytes
    // across the heaviest editor/render/targets/memory/replay surfaces. These
    // limits retain at least 7x command and 11x text headroom without making
    // every retained scratch list carry the obsolete pre-stream 8K/64K budget.
    static constexpr int MAX_COMMANDS = 2048;
    static constexpr int MAX_TEXT_BYTES = 16384;
    static constexpr int MAX_CLIP_DEPTH = 32;

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
        PreviewImage
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
    };
    static_assert( std::is_trivially_copyable_v<Command>, "UI draw commands must remain plain inspectable values." );

    void Clear();
    void AddRect( float x, float y, float w, float h, float r, float g, float b, float a );
    void AddRoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a );
    void AddTriangle( float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a );
    void AddText( float x, float y, float pxSize, float r, float g, float b, const char* value );
    void PushClip( float x, float y, float w, float h );
    void PopClip();

    // Fallback fill and label are part of the recorded value so a missing
    // frame-local renderer target cannot silently produce a blank panel.
    void AddPreviewImage( PreviewTargetId target, float x, float y, float w, float h, float fallbackR, float fallbackG,
                          float fallbackB, float fallbackA, const char* fallbackLabel );

    // Appends another list in order and applies a screen-space translation to
    // its geometry. Text is copied into this list's bounded storage so neither
    // the source list nor its cache must outlive the composed frame.
    void Append( const UIDrawList& source, float offsetX = 0.0f, float offsetY = 0.0f );

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
};

} // namespace UI
} // namespace SkullbonezCore
