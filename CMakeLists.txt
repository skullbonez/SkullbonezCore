# File: CMakeLists.txt
# Purpose:
#   Defines the portable SkullbonezCore CPU library used by cross-toolchain tests.
#
# Summary:
#   The Windows application remains owned by the Visual Studio solution. This
#   build compiles the renderer-free value, parsing, terrain, maths, and physics
#   closure so a second compiler can exercise the same production sources.
#
# Invariants:
#   - Maths and Physics compile from their complete production source sets.
#   - Renderer-free macros may remove GPU construction, never CPU behavior.
#   - Non-MSVC compilers must accept -ffp-contract=off and receive the matching
#     force-include marker consumed by FloatingPointContract.h.
#
# Related:
#   - SkullbonezSource/Core/FloatingPointContract.h
#   - README.md documents the configure and build commands.

cmake_minimum_required(VERSION 3.25)
project(SkullbonezPortableCpu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SKULLBONEZ_PORTABLE_CPU_SOURCES
    SkullbonezSource/Assets/AssetSystem.cpp
    SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp
    SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp
    SkullbonezSource/Core/AmortizedTask.cpp
    SkullbonezSource/Core/AtomicTextFileWriter.cpp
    SkullbonezSource/Core/Config.cpp
    SkullbonezSource/Core/FatalError.cpp
    SkullbonezSource/Core/LockOrderValidator.cpp
    SkullbonezSource/Core/Log.cpp
    SkullbonezSource/Core/PlatformProfiler.cpp
    SkullbonezSource/Core/Profiler.cpp
    SkullbonezSource/Core/SbResult.cpp
    SkullbonezSource/Core/WorkerPool.cpp
    SkullbonezSource/Maths/DeterministicMath.cpp
    SkullbonezSource/Maths/Frustum.cpp
    SkullbonezSource/Maths/GeometricMath.cpp
    SkullbonezSource/Maths/Matrix4.cpp
    SkullbonezSource/Maths/OrbitalMechanics.cpp
    SkullbonezSource/Maths/Quaternion.cpp
    SkullbonezSource/Maths/RotationMatrix.cpp
    SkullbonezSource/Physics/BoundingBox.cpp
    SkullbonezSource/Physics/BoundingSphere.cpp
    SkullbonezSource/Physics/BuoyancySystem.cpp
    SkullbonezSource/Physics/ColliderStore.cpp
    SkullbonezSource/Physics/ConvexHullShape.cpp
    SkullbonezSource/Physics/Diagnostics/SkullScope.cpp
    SkullbonezSource/Physics/ObjectContactManifold.cpp
    SkullbonezSource/Physics/PersistentContactSolver.cpp
    SkullbonezSource/Physics/PhysicsBodyStore.cpp
    SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp
    SkullbonezSource/Physics/PhysicsEngine.cpp
    SkullbonezSource/Physics/PhysicsObjectPolicy.cpp
    SkullbonezSource/Physics/PhysicsTerrainView.cpp
    SkullbonezSource/Physics/PhysicsWorld.cpp
    SkullbonezSource/Physics/Ragdoll.cpp
    SkullbonezSource/Physics/SleepIslandSystem.cpp
    SkullbonezSource/Physics/SpatialGrid.cpp
    SkullbonezSource/Physics/Stages/ExternalForceStage.cpp
    SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
    SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp
    SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
    SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp
    SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp
    SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
    SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp
    SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp
    SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp
    SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp
    SkullbonezSource/Physics/TerrainContactManifold.cpp
    SkullbonezSource/Scene/AuthoredScene.cpp
    SkullbonezSource/Scene/AuthoredSceneParser.cpp
    SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp
    SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp
    SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp
    SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp
    SkullbonezSource/Scene/StandaloneStyleWriter.cpp
    SkullbonezSource/World/Terrain.cpp
)

function(skullbonez_require_complete_domain domain)
    file(
        GLOB_RECURSE domain_sources
        CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/SkullbonezSource/${domain}/*.cpp")
    foreach(domain_source IN LISTS domain_sources)
        if(NOT domain_source IN_LIST SKULLBONEZ_PORTABLE_CPU_SOURCES)
            message(FATAL_ERROR "Portable CPU target omits ${domain_source} from complete ${domain} ownership.")
        endif()
    endforeach()
endfunction()

function(skullbonez_reject_platform_includes domain)
    file(
        GLOB_RECURSE domain_sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/SkullbonezSource/${domain}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SkullbonezSource/${domain}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/SkullbonezSource/${domain}/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SkullbonezSource/${domain}/*.inl")
    foreach(domain_source IN LISTS domain_sources)
        file(READ "${domain_source}" domain_text)
        if(domain_text MATCHES "#[ \t]*include[ \t]*[<\"][^>\"]*Platform(Win32|Posix)\\.h[>\"]")
            message(FATAL_ERROR "${domain} must not include a platform contract: ${domain_source}")
        endif()
    endforeach()
endfunction()

skullbonez_require_complete_domain(Maths)
skullbonez_require_complete_domain(Physics)
skullbonez_reject_platform_includes(Maths)
skullbonez_reject_platform_includes(Physics)

add_library(skullbonez_portable_cpu STATIC ${SKULLBONEZ_PORTABLE_CPU_SOURCES})
target_include_directories(skullbonez_portable_cpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(
    skullbonez_portable_cpu
    PUBLIC SKULLBONEZ_RENDER_FREE_TESTS
    PRIVATE JSON_NOEXCEPTION NDEBUG SKULLBONEZ_PORTABLE_CPU)

set(SKULLBONEZ_FLOATING_POINT_CONTRACT
    "${CMAKE_CURRENT_SOURCE_DIR}/SkullbonezSource/Core/FloatingPointContract.h")

if(MSVC)
    target_compile_definitions(skullbonez_portable_cpu PRIVATE _HAS_EXCEPTIONS=0 _HAS_STD_BYTE=0)
    target_compile_options(
        skullbonez_portable_cpu
        PRIVATE /fp:precise "/FI${SKULLBONEZ_FLOATING_POINT_CONTRACT}")
else()
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-ffp-contract=off" SKULLBONEZ_HAS_FFP_CONTRACT_OFF)
    if(NOT SKULLBONEZ_HAS_FFP_CONTRACT_OFF)
        message(FATAL_ERROR "The portable CPU compiler must support -ffp-contract=off.")
    endif()
    target_compile_definitions(skullbonez_portable_cpu PRIVATE SKULLBONEZ_FFP_CONTRACT_OFF=1)
    target_compile_options(
        skullbonez_portable_cpu
        PRIVATE -ffp-contract=off -fno-exceptions -fno-rtti
                -include "${SKULLBONEZ_FLOATING_POINT_CONTRACT}")
endif()
