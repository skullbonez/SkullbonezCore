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
    Quit,
    SaveSkyDefaults
};

constexpr int RUNTIME_COMMAND_TEXT_CAPACITY = 256;
constexpr int RUNTIME_COMMAND_QUEUE_CAPACITY = 64;

struct RuntimeCommandText
{
    RuntimeCommandText() = default;
    RuntimeCommandText( const char* value )
    {
        Assign( value );
    }
    RuntimeCommandText( const std::string& value )
    {
        Assign( value.c_str() );
    }

    RuntimeCommandText& operator=( const char* value )
    {
        Assign( value );
        return *this;
    }
    RuntimeCommandText& operator=( const std::string& value )
    {
        Assign( value.c_str() );
        return *this;
    }

    void Assign( const char* value );
    const char* c_str() const;
    bool empty() const;
    void clear();

    // Runtime command text is a bounded payload; long tool/UI labels truncate
    // instead of allocating so the queue stays frame-stable.
    char text[RUNTIME_COMMAND_TEXT_CAPACITY] = {};
};

struct RuntimeCommand
{
    RuntimeCommandType type = RuntimeCommandType::None;        // Command intent
    int index = -1;                                            // Optional scene/model index payload
    RuntimeCommandText text;                                   // Optional fixed-size path/name payload
    bool preserveUIState = true;                               // Reset/load policy for scene commands
    bool suppressExitOnComplete = true;                        // Reset/load policy for scene commands
    bool preserveRuntimeState = true;                          // Reset/load policy for scene commands
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
    RuntimeCommand m_commands[RUNTIME_COMMAND_QUEUE_CAPACITY]; // Fixed FIFO command ring
    int m_head = 0;                                            // Oldest queued command
    int m_count = 0;                                           // Number of valid ring entries
};
} // namespace Basics
} // namespace SkullbonezCore
