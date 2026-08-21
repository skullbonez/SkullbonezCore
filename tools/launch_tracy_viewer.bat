@rem
@rem File: tools/launch_tracy_viewer.bat
@rem Purpose:
@rem   Builds the repository-pinned Tracy profiler on first use and opens it.
@rem
@rem Summary:
@rem   The ImGui button starts this cold developer action asynchronously. A
@rem   ready viewer connects to the local engine immediately; a fresh machine
@rem   configures and builds it under ignored validation output first.
@rem
@rem Glossary:
@rem   Pinned viewer: Tracy profiler source committed under ThirdPtySource so
@rem   the client and viewer stay on the same protocol version.
@rem
@rem Invariants:
@rem   - The viewer remains an external development tool, never an engine input.
@rem   - Generated dependencies and binaries stay under ignored output paths.
@rem   - --build-only performs the same build without starting a GUI process.
@rem
@rem Related:
@rem   - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
@rem   - ThirdPtySource/tracy/profiler/CMakeLists.txt
@rem
@echo off
setlocal

set "SKORE_TRACY_BUILD_ONLY=0"
if /I "%~1"=="--build-only" (
    set "SKORE_TRACY_BUILD_ONLY=1"
) else if not "%~1"=="" (
    echo ERROR: Unknown option "%~1". Expected --build-only or no option.
    exit /b 2
)

for %%I in ("%~dp0..") do set "SKORE_TRACY_REPO=%%~fI"
set "SKORE_TRACY_SOURCE=%SKORE_TRACY_REPO%\ThirdPtySource\tracy\profiler"
set "SKORE_TRACY_BUILD=%SKORE_TRACY_REPO%\TestOutput\validation\tracy_profiler"
set "SKORE_TRACY_VIEWER=%SKORE_TRACY_BUILD%\Release\tracy-profiler.exe"

if exist "%SKORE_TRACY_VIEWER%" goto :viewer_ready

set "SKORE_TRACY_CMAKE="
for %%I in (cmake.exe) do if not "%%~$PATH:I"=="" set "SKORE_TRACY_CMAKE=%%~$PATH:I"
if defined SKORE_TRACY_CMAKE goto :cmake_ready

set "SKORE_TRACY_VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%SKORE_TRACY_VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%SKORE_TRACY_VSWHERE%" -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do (
        set "SKORE_TRACY_CMAKE=%%I"
        goto :cmake_ready
    )
)

echo ERROR: CMake was not found. Install Visual Studio with Desktop development with C++.
exit /b 99

:cmake_ready
if not exist "%SKORE_TRACY_BUILD%\CMakeCache.txt" (
    echo Configuring pinned Tracy viewer...
    "%SKORE_TRACY_CMAKE%" -S "%SKORE_TRACY_SOURCE%" -B "%SKORE_TRACY_BUILD%" -A x64
    if errorlevel 1 exit /b 1
)

echo Building pinned Tracy viewer...
"%SKORE_TRACY_CMAKE%" --build "%SKORE_TRACY_BUILD%" --config Release --target tracy-profiler --parallel
if errorlevel 1 exit /b %errorlevel%

if not exist "%SKORE_TRACY_VIEWER%" (
    echo ERROR: Tracy build succeeded but the viewer executable was not produced.
    exit /b 3
)

:viewer_ready
echo Tracy viewer ready: "%SKORE_TRACY_VIEWER%"
if "%SKORE_TRACY_BUILD_ONLY%"=="1" exit /b 0

start "" /D "%SKORE_TRACY_REPO%" "%SKORE_TRACY_VIEWER%" -a 127.0.0.1
exit /b %errorlevel%
