/*
File: PlatformWin32.h
Purpose:
  Owns the narrow Windows SDK prelude for engine code that exposes or calls
  Win32 APIs.

Summary:
  Platform-facing headers and translation units include this file explicitly.
  Domain headers remain independent of Windows macros and handle types.

Glossary:
  Win32 handle: Opaque operating-system token such as HWND or HANDLE.
  Lean SDK surface: Windows declarations with rarely used APIs excluded before
    the SDK headers are parsed.

Invariants:
  - WIN32_LEAN_AND_MEAN and NOMINMAX are defined before windows.h.
  - Common.h must not include this file; platform dependence is explicit.
  - Physics, maths, scene-schema, and UI-layout headers must not include it.

Related:
  - SkullbonezSource/Core/Common.h owns the platform-free legacy prelude.
  - SkullbonezSource/Runtime/App/Window.h owns the application window handle.
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h owns the native render startup boundary.
*/
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
