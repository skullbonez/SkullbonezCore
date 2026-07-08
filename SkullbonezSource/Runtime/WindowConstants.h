/*
File: SkullbonezSource/Runtime/WindowConstants.h
Purpose:
  Owns process-wide window labels and the runtime data-root prefix used to load
  committed engine assets.

Mental model:
  The executable launches from the repository root in validation and local
  development. These constants are the narrow compatibility surface that keeps
  window creation, title updates, and asset path construction using the same
  stable strings while Common.h is being split.

Glossary:
  Window class name: Win32 registration key used before any window handle can
    be created.
  Data root: Repository-relative directory prefix for scenes, shaders, styles,
    textures, and configuration files.

Invariants:
  - DATA_ROOT is repository-relative and includes the trailing slash expected by
    legacy std::string path joins.
  - WINDOW_NAME must match registration and unregistration paths.

Related:
  - SkullbonezSource/Core/Common.h includes this during the aliasing period.
  - SkullbonezSource/Runtime/Window.cpp consumes the window labels.
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

constexpr const char* WINDOW_NAME = "SkullbonezWindow";
constexpr const char* TITLE_TEXT = "::SKULLBONEZ CORE::";
constexpr const char* DATA_ROOT = "SkullbonezData/";
