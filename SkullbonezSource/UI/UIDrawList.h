/*
File: SkullbonezSource/UI/UIDrawList.h
Purpose:
  Implements UI DrawList widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIDrawList.h implements UI DrawList widgets, layout, drawing, or UI state
  for the in-engine controls. As a public header, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

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
    static constexpr int MAX_COMMANDS = 8192;
    static constexpr int MAX_TEXT_BYTES = 65536;
    static constexpr int MAX_CLIP_DEPTH = 32;

    struct PreviewTargetId
    {
        uint16_t catalogIndex = 0;
        bool valid = false;
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
        CommandType type = CommandType::Rect;
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float radius = 0.0f;
        float pxSize = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
        int textOffset = 0;
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
    void AddPreviewImage(
        PreviewTargetId target,
        float x,
        float y,
        float w,
        float h,
        float fallbackR,
        float fallbackG,
        float fallbackB,
        float fallbackA,
        const char* fallbackLabel
    );

    bool Empty() const;
    Stats GetStats() const;
    std::span<const Command> Commands() const;
    const char* TextAt( int offset ) const;

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
    int m_maxClipDepth = 0;
};

} // namespace UI
} // namespace SkullbonezCore
