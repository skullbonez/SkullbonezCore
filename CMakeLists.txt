# File: CMakeLists.txt
# Purpose:
#   Defines the portable SkullbonezCore CPU library used by cross-toolchain tests.
#
# Summary:
#   The Windows application remains owned by the Visual Studio solution. This
#   build compiles the renderer-free value, parsing, terrain, gameplay, maths,
#   and physics closure plus the matching portable doctest source set.
#
# Invariants:
#   - Maths, Physics, and UI compile from their complete production source sets.
#   - Portable test sources are the same files compiled by SKULLBONEZ_TESTS.
#   - A portable test source must not directly include Runtime or Rendering;
#     platform-neutral value/schema headers may remain in its include closure.
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
    SkullbonezSource/Gameplay/TornadoField.cpp
    SkullbonezSource/Gameplay/TornadoGameplay.cpp
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
    SkullbonezSource/Physics/PhysicsPoseIntegration.cpp
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
    SkullbonezSource/UI/OperatorEditorExchange.cpp
    SkullbonezSource/UI/UI.cpp
    SkullbonezSource/UI/UIBackdropBlur.cpp
    SkullbonezSource/UI/UIButton.cpp
    SkullbonezSource/UI/UICache.cpp
    SkullbonezSource/UI/UICheckBox.cpp
    SkullbonezSource/UI/UIComboBox.cpp
    SkullbonezSource/UI/UIDraw.cpp
    SkullbonezSource/UI/UIDrawList.cpp
    SkullbonezSource/UI/UIDrawWidgets.cpp
    SkullbonezSource/UI/UIEditorMiniPalette.cpp
    SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp
    SkullbonezSource/UI/UIFontMetrics.cpp
    SkullbonezSource/UI/UIFrameComposition.cpp
    SkullbonezSource/UI/UIIconButton.cpp
    SkullbonezSource/UI/UIInput.cpp
    SkullbonezSource/UI/UILayout.cpp
    SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp
    SkullbonezSource/UI/UIScrollBar.cpp
    SkullbonezSource/UI/UISlider.cpp
    SkullbonezSource/UI/UIStyle.cpp
    SkullbonezSource/UI/UITabBar.cpp
    SkullbonezSource/UI/UITabCinematic.cpp
    SkullbonezSource/UI/UITabControls.cpp
    SkullbonezSource/UI/UITabEditor.cpp
    SkullbonezSource/UI/UITabMemory.cpp
    SkullbonezSource/UI/UITabOptions.cpp
    SkullbonezSource/UI/UITabPhysics.cpp
    SkullbonezSource/UI/UITabProfiler.cpp
    SkullbonezSource/UI/UITabProfilerHistogram.cpp
    SkullbonezSource/UI/UITabScene.cpp
    SkullbonezSource/UI/UITabSky.cpp
    SkullbonezSource/UI/UIWindowChrome.cpp
    SkullbonezSource/UI/UIWindowInteractionOwner.cpp
    SkullbonezSource/World/Terrain.cpp
)

set(SKULLBONEZ_PORTABLE_TEST_SOURCES
    SkullbonezTests/TestAssetSystem.cpp
    SkullbonezTests/TestBounds.cpp
    SkullbonezTests/TestConfig.cpp
    SkullbonezTests/TestConvexHull.cpp
    SkullbonezTests/TestDeterminism.cpp
    SkullbonezTests/TestDeterministicMath.cpp
    SkullbonezTests/TestFrustum.cpp
    SkullbonezTests/TestGeometricMath.cpp
    SkullbonezTests/TestMain.cpp
    SkullbonezTests/TestMatrix4.cpp
    SkullbonezTests/TestObjectContactManifold.cpp
    SkullbonezTests/TestOrbitalMechanics.cpp
    SkullbonezTests/TestPersistentContactSolver.cpp
    SkullbonezTests/TestPhysicsApi.cpp
    SkullbonezTests/TestPhysicsPoseIntegration.cpp
    SkullbonezTests/TestPhysicsStageState.cpp
    SkullbonezTests/TestQuaternion.cpp
    SkullbonezTests/TestReserveAllocator.cpp
    SkullbonezTests/TestSbResult.cpp
    SkullbonezTests/TestSceneParserUnit.cpp
    SkullbonezTests/TestSleepController.cpp
    SkullbonezTests/TestSmoke.cpp
    SkullbonezTests/TestSolverBroadphaseStage.cpp
    SkullbonezTests/TestSpatialGrid.cpp
    SkullbonezTests/TestTerrain.cpp
    SkullbonezTests/TestUIDrawValues.cpp
    SkullbonezTests/TestVector3.cpp
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
skullbonez_require_complete_domain(UI)
skullbonez_reject_platform_includes(Maths)
skullbonez_reject_platform_includes(Physics)

foreach(test_source IN LISTS SKULLBONEZ_PORTABLE_TEST_SOURCES)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}" test_text)
    if(test_text MATCHES "#[ \t]*include[ \t]*[<\"][^>\"]*SkullbonezSource/(Runtime|Rendering)/")
        message(FATAL_ERROR "Portable test source crosses Runtime or Rendering: ${test_source}")
    endif()
endforeach()

file(
    GLOB all_test_sources
    CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/SkullbonezTests/*.cpp")
foreach(test_source IN LISTS all_test_sources)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}" test_text)
    if(NOT test_text MATCHES "#[ \t]*include[ \t]*[<\"][^>\"]*SkullbonezSource/(Runtime|Rendering)/" AND
       NOT test_source IN_LIST SKULLBONEZ_PORTABLE_TEST_SOURCES)
        message(FATAL_ERROR "Renderer-free test source is missing from the portable target: ${test_source}")
    endif()
endforeach()

set(SKULLBONEZ_FLOATING_POINT_CONTRACT
    "${CMAKE_CURRENT_SOURCE_DIR}/SkullbonezSource/Core/FloatingPointContract.h")

function(skullbonez_apply_portable_compile_contract target)
    target_compile_definitions(
        ${target}
        PRIVATE JSON_NOEXCEPTION NDEBUG SKULLBONEZ_PORTABLE_CPU)

    if(MSVC)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0 _HAS_STD_BYTE=0)
        target_compile_options(
            ${target}
            PRIVATE /fp:precise "/FI${SKULLBONEZ_FLOATING_POINT_CONTRACT}")
    else()
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-ffp-contract=off" SKULLBONEZ_HAS_FFP_CONTRACT_OFF)
        if(NOT SKULLBONEZ_HAS_FFP_CONTRACT_OFF)
            message(FATAL_ERROR "The portable CPU compiler must support -ffp-contract=off.")
        endif()
        target_compile_definitions(${target} PRIVATE SKULLBONEZ_FFP_CONTRACT_OFF=1)
        target_compile_options(
            ${target}
            PRIVATE -ffp-contract=off -fno-exceptions -fno-rtti
                    -include "${SKULLBONEZ_FLOATING_POINT_CONTRACT}")
    endif()
endfunction()

add_library(skullbonez_portable_cpu STATIC ${SKULLBONEZ_PORTABLE_CPU_SOURCES})
target_include_directories(skullbonez_portable_cpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(
    skullbonez_portable_cpu
    PUBLIC SKULLBONEZ_RENDER_FREE_TESTS)
skullbonez_apply_portable_compile_contract(skullbonez_portable_cpu)

add_executable(skullbonez_portable_tests ${SKULLBONEZ_PORTABLE_TEST_SOURCES})
target_include_directories(skullbonez_portable_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdPtySource)
target_compile_definitions(
    skullbonez_portable_tests
    PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS DOCTEST_CONFIG_NO_RTTI
            DOCTEST_CONFIG_USE_STD_HEADERS)
target_link_libraries(skullbonez_portable_tests PRIVATE skullbonez_portable_cpu)
skullbonez_apply_portable_compile_contract(skullbonez_portable_tests)

include(CTest)
add_test(NAME skullbonez_portable_cpu_tests COMMAND skullbonez_portable_tests)
set_tests_properties(skullbonez_portable_cpu_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
