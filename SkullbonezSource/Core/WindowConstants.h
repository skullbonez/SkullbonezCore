/*
File: SkullbonezSource/Core/WindowConstants.h
Purpose:
  Owns process-wide window labels and the data-root prefix used to load
  committed engine assets.

Summary:
  The executable launches from the repository root in validation and local
  development. These constants are the lower-layer compatibility surface that
  keeps window creation, title updates, rendering resources, and asset path
  construction on the same stable strings.

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
  - SkullbonezSource/Runtime/Startup/Window.cpp consumes the window labels.
  - SkullbonezSource/Assets/AssetSystem.h consumes the shared data root.
*/
#pragma once

constexpr const char* WINDOW_NAME = "SkullbonezWindow";
constexpr const char* TITLE_TEXT = "::SKULLBONEZ CORE::";
constexpr const char* DATA_ROOT = "SkullbonezData/";
