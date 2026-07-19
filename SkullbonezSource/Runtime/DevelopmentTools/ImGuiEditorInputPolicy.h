/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h
Purpose:
  Defines value-only routing policy between Dear ImGui and engine input.

Summary:
  Win32 messages are always offered to the pinned ImGui backend. This header
  then chooses exactly one application consumer for mouse, keyboard, or text
  intent: the editor tool surface or the existing engine input path. Platform
  lifecycle messages continue through the engine regardless of capture flags.

Glossary:
  Tool surface: ImGui chrome or widget area outside the live game viewport.
  Game viewport: Central editor surface where camera, selection, gizmo, replay,
    and other existing engine input must remain authoritative.
  Capture intent: ImGui's previous completed frame request to retain one input
    class for its widgets.
  Platform message: Focus, resize, DPI, display, device, or OS-navigation event
    that both integrations must observe and that no UI may swallow.

Invariants:
  - Each mouse, keyboard, or text event has exactly one application consumer.
  - Game-viewport hover/focus overrides generic ImGui mouse/keyboard capture.
  - Text intent is never inferred from mouse capture.
  - Platform messages always remain available to Window and the OS.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
  - SkullbonezSource/Runtime/InputRouter.h
  - SkullbonezSource/Runtime/Window.cpp
  - Agentic/Plans/TODO/imgui-tracy-editor-campaign.md (E7)
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include <cstdint>

namespace SkullbonezCore::Runtime::DevelopmentTools
{
enum class ImGuiEditorMessageClass : uint8_t
{
    Mouse,
    Keyboard,
    Text,
    Platform
};

struct ImGuiEditorInputIntent
{
    bool editorVisible = false;
    bool wantCaptureMouse = false;
    bool wantCaptureKeyboard = false;
    bool wantTextInput = false;
    bool gameViewportHovered = false;
    bool gameViewportFocused = false;
};

struct ImGuiEditorInputCapture
{
    bool mouse = false;
    bool keyboard = false;
    bool text = false;
};

struct ImGuiEditorMessageDecision
{
    bool editorConsumes = false;
    bool engineConsumes = true;
};

constexpr ImGuiEditorMessageClass ClassifyImGuiEditorNativeMessage( UINT message, WPARAM wParam ) noexcept
{
    switch ( message )
    {
    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_INPUT:
    case WM_SETCURSOR:
        return ImGuiEditorMessageClass::Mouse;

    case WM_KEYDOWN:
    case WM_KEYUP:
        return ImGuiEditorMessageClass::Keyboard;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        // Why: Alt+Tab and Alt+F4 remain OS navigation even while an editor
        // widget wants keyboard input. Other system keys follow ordinary
        // keyboard capture policy.
        return wParam == VK_TAB || wParam == VK_F4 ? ImGuiEditorMessageClass::Platform
                                                   : ImGuiEditorMessageClass::Keyboard;

    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_CHAR:
        return ImGuiEditorMessageClass::Text;

    default:
        // Focus, resize, DPI, display, device, and clipboard ownership remain
        // platform/core responsibilities rather than captured input classes.
        return ImGuiEditorMessageClass::Platform;
    }
}

constexpr ImGuiEditorInputCapture EvaluateImGuiEditorInputCapture( const ImGuiEditorInputIntent& intent ) noexcept
{
    if ( !intent.editorVisible )
    {
        return {};
    }

    ImGuiEditorInputCapture capture;
    // Invariant: viewport mouse/keyboard activity stays on the established
    // camera/selection/gizmo path even though the containing dock host may make
    // ImGui publish a broad WantCapture flag.
    capture.mouse = intent.wantCaptureMouse && !intent.gameViewportHovered;
    capture.keyboard = intent.wantCaptureKeyboard && !intent.gameViewportFocused;
    capture.text = intent.wantTextInput;
    return capture;
}

constexpr ImGuiEditorMessageDecision DecideImGuiEditorMessageRoute( ImGuiEditorMessageClass messageClass,
                                                                    const ImGuiEditorInputCapture& capture ) noexcept
{
    bool editorConsumes = false;
    switch ( messageClass )
    {
    case ImGuiEditorMessageClass::Mouse:
        editorConsumes = capture.mouse;
        break;
    case ImGuiEditorMessageClass::Keyboard:
        editorConsumes = capture.keyboard;
        break;
    case ImGuiEditorMessageClass::Text:
        editorConsumes = capture.text || capture.keyboard;
        break;
    case ImGuiEditorMessageClass::Platform:
    default:
        // Why: focus loss, alt-tab, resize, DPI, IME configuration, and device
        // changes must still reach Window/DefWindowProc even while a tool edits.
        editorConsumes = false;
        break;
    }
    return ImGuiEditorMessageDecision{ editorConsumes, !editorConsumes };
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
