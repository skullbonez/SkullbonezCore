/*
File: SkullbonezSource/Runtime/UI/RuntimeUiSurface.h
Purpose:
  Defines the fixed-capacity control and surface values shared by runtime UI
  overlays, editor tools, and in-game panels.

Summary:
  A domain owner rebuilds a small ordered table of controls each frame. Input
  resolves one hot row from that table, domain code handles the row's semantic
  action, and rendering reads the same row geometry and state. The table owns
  no replay, editor, gameplay, renderer, or pointer-capture state.

Glossary:
  Control id: Stable identifier for one interactive element within a surface.
  Action id: Domain-defined operation requested by a control; the shared UI
    layer stores the value but never interprets or executes it.
  Pointer control: First visible row whose hit rectangle contains the current
    pointer, including a disabled row that blocks controls behind it.

Invariants:
  - Storage is inline and cannot grow after construction.
  - Control ids are nonzero and unique within a surface.
  - Rows are authored front-to-back; the first visible hit wins and disabled
    rows prevent click-through without becoming hot.
  - Persistent domain state and pointer-capture authority stay with their
    existing owners.

Related:
  - Agentic/Reports/2026-07-11/runtime-ui-control-u6-review.md
  - SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h
  - SkullbonezSource/UI/UIDraw.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../UI/UIDraw.h"

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
enum class RuntimeUiControlKind
{
    Panel,
    HotZone,
    Button,
    Toggle,
    Slider,
    Track,
    Tab,
    ToolHandle
};

struct RuntimeUiControlId
{
    uint32_t value = 0;

    explicit operator bool() const
    {
        return value != 0;
    }
};

inline bool operator==( RuntimeUiControlId left, RuntimeUiControlId right )
{
    return left.value == right.value;
}

inline bool operator!=( RuntimeUiControlId left, RuntimeUiControlId right )
{
    return !( left == right );
}

struct RuntimeUiActionId
{
    uint32_t value = 0;

    explicit operator bool() const
    {
        return value != 0;
    }
};

struct RuntimeUiControl
{
    RuntimeUiControlId id;
    RuntimeUiControlKind kind = RuntimeUiControlKind::Panel;
    RuntimeUiActionId action;
    UI::UIRect drawRect;
    UI::UIRect hitRect;
    bool visible = true;
    bool enabled = true;
    bool hovered = false;
    bool focused = false;
    bool active = false;
    bool requestsReveal = false;
};

// Concept: array order is the z-order contract. Builders add the most specific
// or visually front-most controls first and broad panels/hot zones last. Hit
// testing therefore never needs sorting, allocation, or a callback.
template <std::size_t Capacity> struct RuntimeUiSurface
{
    static_assert( Capacity > 0, "A runtime UI surface must hold at least one control." );

    RuntimeUiControl controls[Capacity] = {};
    std::size_t controlCount = 0;
    RuntimeUiControlId hotControl;
    RuntimeUiControlId pointerControl;
    RuntimeUiControlId activeControl;
    bool hasHotControl = false;
    bool hasPointerControl = false;
    bool hasActiveControl = false;
    bool consumesPointer = false;

    void Reset()
    {
        controlCount = 0;
        hotControl = {};
        pointerControl = {};
        activeControl = {};
        hasHotControl = false;
        hasPointerControl = false;
        hasActiveControl = false;
        consumesPointer = false;
    }

    // Returns false instead of growing storage or accepting an ambiguous id.
    bool TryAdd( const RuntimeUiControl& control )
    {

        if ( !control.id || controlCount >= Capacity || Find( control.id ) != nullptr )
        {
            return false;
        }

        controls[controlCount] = control;
        ++controlCount;
        return true;
    }

    RuntimeUiControl* Find( RuntimeUiControlId id )
    {

        for ( std::size_t index = 0; index < controlCount; ++index )
        {

            if ( controls[index].id == id )
            {
                return &controls[index];
            }
        }

        return nullptr;
    }

    const RuntimeUiControl* Find( RuntimeUiControlId id ) const
    {

        for ( std::size_t index = 0; index < controlCount; ++index )
        {

            if ( controls[index].id == id )
            {
                return &controls[index];
            }
        }

        return nullptr;
    }

    void ResolvePointer( int pointerX, int pointerY )
    {
        ResolvePointer( pointerX, pointerY, false );
    }

    // Publishes one disposable hit result, or clears it when another surface
    // already owns the pointer for this frame.
    void ResolvePointer( int pointerX, int pointerY, bool pointerBlocked )
    {
        hotControl = {};
        pointerControl = {};
        hasHotControl = false;
        hasPointerControl = false;
        consumesPointer = false;

        for ( std::size_t index = 0; index < controlCount; ++index )
        {
            controls[index].hovered = false;
        }

        // Invariant: a higher-priority surface blocks both actions and visual
        // hover; clearing first prevents the previous pointer result leaking.

        if ( pointerBlocked )
        {
            return;
        }

        for ( std::size_t index = 0; index < controlCount; ++index )
        {
            RuntimeUiControl& control = controls[index];

            if ( control.visible && Contains( control.hitRect, pointerX, pointerY ) )
            {
                pointerControl = control.id;
                hasPointerControl = true;
                consumesPointer = true;

                if ( control.enabled )
                {
                    control.hovered = true;
                    hotControl = control.id;
                    hasHotControl = true;
                }

                return;
            }
        }
    }

  private:
    static bool Contains( const UI::UIRect& bounds, int x, int y )
    {
        const float pointerX = static_cast<float>( x );
        const float pointerY = static_cast<float>( y );
        return pointerX >= bounds.x && pointerX <= bounds.x + bounds.w && pointerY >= bounds.y &&
               pointerY <= bounds.y + bounds.h;
    }
};
} // namespace Runtime
} // namespace SkullbonezCore
