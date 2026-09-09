#pragma once

#include "UIDraw.h"

#include <cstdint>

namespace SkullbonezCore::UI
{
struct UITooltipText
{
    // Lifetime: metadata strings are static action descriptions. Dynamic
    // owner snapshots must not supply temporary text to this retained value.
    const char* action = "";
    const char* units = "";
    const char* shortcut = "";
    const char* disabledReason = "";
};

struct UITooltipTarget
{
    uint32_t id = 0;
    UIRect bounds;
    UITooltipText text;
    bool hovered = false;
    bool focused = false;
    bool enabled = true;
};

// Stores presentation timing only; input focus and gesture capture belong to
// the caller's input router. Dismiss at workspace changes and focus loss.
class UITooltip
{
  public:
    void Update( const UITooltipTarget& target, double now, bool gestureActive );
    void Dismiss();
    bool Visible( double now ) const;
    UITooltipTarget VisibleTarget( double now ) const
    {
        return Visible( now ) ? m_target : UITooltipTarget {};
    }
    UIRect Bounds( const UIRect& window ) const;
    void Draw( const UIDrawContext& draw, const UIRect& window, double now ) const;

  private:
    UITooltipTarget m_target;
    double m_hoverStarted = 0.0;
};
} // namespace SkullbonezCore::UI
