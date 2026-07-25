/*
File: SkullbonezSource/UI/UIInput.h
Purpose:
  Defines the detached keyboard/pointer value consumed by UI interaction.

Summary:
  Runtime copies its sampled device levels and router-owned pointer edges into
  UIInputSnapshot. UI then derives widget edges from this passive value without
  polling hardware or naming Runtime input authority.

Glossary:
  Virtual key word: One 64-bit group of keyboard levels; four words cover all
    256 Windows virtual-key codes.
  Pointer override: UI-owned automation value that substitutes a deterministic
    client position while preserving sampled button levels and edges.

Invariants:
  - UI input receives copied levels/edges and never samples keyboard, pointer,
    wheel, or cursor state itself.
  - The snapshot owns every input bit it exposes and contains no Runtime type,
    owner pointer, callback, or borrowed lifetime.

Related:
  - SkullbonezSource/UI/UIInput.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{
namespace InputControl
{

struct UIInputSnapshot
{
    static constexpr int VIRTUAL_KEY_COUNT = 256;
    static constexpr std::size_t KEY_WORD_COUNT = VIRTUAL_KEY_COUNT / 64;
    std::array<uint64_t, KEY_WORD_COUNT> keyWords = {};
    int mouseX = 0;
    int mouseY = 0;
    int wheelDelta = 0;
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
};

struct UIPointerOverride
{
    bool enabled = false;
    int x = 0;
    int y = 0;
};

void CaptureKeyStates( bool keyWasDown[UIInputSnapshot::VIRTUAL_KEY_COUNT], const UIInputSnapshot& input );
bool ConsumeKeyPress(
    bool keyWasDown[UIInputSnapshot::VIRTUAL_KEY_COUNT],
    const UIInputSnapshot& input,
    int virtualKey
);
bool IsVirtualKeyDown( const UIInputSnapshot& input, int virtualKey );

} // namespace InputControl
} // namespace UI
} // namespace SkullbonezCore
