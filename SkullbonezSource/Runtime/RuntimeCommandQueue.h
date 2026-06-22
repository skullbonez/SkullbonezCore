/*
File: SkullbonezSource/Runtime/RuntimeCommandQueue.h
Purpose:
  Defines a typed command queue for runtime/UI/tool requests.

Mental model:
  Input and tools should describe intent as commands before mutating runtime
  state. The queue starts as the shared contract; later slices can move direct
  UI and editor mutations onto it without changing command vocabulary.

Glossary:
  FIFO (First In, First Out): Queue order where the oldest command is consumed
    first.
  Runtime command: Typed intent record emitted by input, UI, or tools.
  Command consumer: Runtime code that applies a queued command to live state.
  Deferred intent: User/tool request stored until the frame boundary.

Invariants:
  - Command order is part of same-frame runtime behavior.
  - Command payload fields stay simple value types for predictable ownership.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace SkullbonezCore
{
namespace Basics
{
enum class RuntimeCommandType
{
    None,
    LoadSceneIndex,
    LoadDemoScene,
    ResetCurrentScene,
    CreateScene,
    SaveScreenshot,
    SaveSceneDefaults,
    SaveRenderDefaults,
    AdvanceScene,
    Quit
};

struct RuntimeCommand
{
    RuntimeCommandType type = RuntimeCommandType::None; // Command intent
    int index = -1;                                     // Optional scene/model index payload
    std::string text;                                   // Optional path/name payload
    bool preserveUIState = true;                        // Reset/load policy for scene commands
    bool suppressExitOnComplete = true;                 // Reset/load policy for scene commands
    bool preserveRuntimeState = true;                   // Reset/load policy for scene commands
};

class RuntimeCommandQueue
{
  public:
    void Push( RuntimeCommand command );
    bool TryPop( RuntimeCommand& outCommand );
    void Clear();
    bool Empty() const;
    std::size_t Size() const;

  private:
    std::deque<RuntimeCommand> m_commands;              // FIFO command list
};
} // namespace Basics
} // namespace SkullbonezCore
