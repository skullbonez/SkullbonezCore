/*
File: SkullbonezSource/Runtime/RuntimeCommandQueue.h
Purpose:
  Defines a typed command queue for runtime/UI/tool requests.

Mental model:
  Input and tools should describe intent as commands before mutating runtime
  state. The queue starts as the shared contract; later slices can move direct
  UI and editor mutations onto it without changing command vocabulary.

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
