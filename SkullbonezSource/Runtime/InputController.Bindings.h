/*
File: InputController.Bindings.h
Purpose:
  Publishes the keyboard binding table consumed by RunInput.

Mental model:
  Keyboard shortcuts are data first and side effects second. This module names
  the key/action/context rows so tests and runtime dispatch read the same table,
  while RunInput still owns what each action does.

Glossary:
  Virtual key: Win32 integer key code used by the existing input poller.
  Runtime input action: Engine command name produced from a key, UI, mouse, or
    scripted source.
  Context mask: Bit set describing which dispatch pass or runtime state owns a
    binding row.
  Binding view: Borrowed pointer/count pair over static rows; callers must not
    store it past process lifetime assumptions.

Invariants:
  - A key/context row maps to one action in the shared table.
  - This module does not poll hardware or apply gameplay side effects.

Related:
  - InputController.h defines the action and context vocabulary.
  - RunInput.cpp executes the actions during the frame.
*/
#pragma once

#include "InputController.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Basics
{
struct RuntimeInputKeyBindingView
{
    const RuntimeInputKeyBinding* bindings = nullptr;
    std::size_t count = 0;
};

// Returns the static TakeInput keyboard table. The rows are immutable process
// data and may be inspected by tests without constructing Run.
RuntimeInputKeyBindingView TakeInputKeyboardBindings();
} // namespace Basics
} // namespace SkullbonezCore
