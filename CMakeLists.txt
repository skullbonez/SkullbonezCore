cmake_minimum_required(VERSION 3.16)
project(SKULLBONEZ_CORE CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------
set(ENGINE_SOURCES
    SkullbonezSource/SkullbonezBoundingSphere.cpp
    SkullbonezSource/SkullbonezCamera.cpp
    SkullbonezSource/SkullbonezCameraCollection.cpp
    SkullbonezSource/SkullbonezCollisionResponse.cpp
    SkullbonezSource/SkullbonezFramebuffer.cpp
    SkullbonezSource/SkullbonezGameModel.cpp
    SkullbonezSource/SkullbonezGameModelCollection.cpp
    SkullbonezSource/SkullbonezGeometricMath.cpp
    SkullbonezSource/SkullbonezHelper.cpp
    SkullbonezSource/SkullbonezInit.cpp
    SkullbonezSource/SkullbonezInput.cpp
    SkullbonezSource/SkullbonezMatrix4.cpp
    SkullbonezSource/SkullbonezMesh.cpp
    SkullbonezSource/SkullbonezQuaternion.cpp
    SkullbonezSource/SkullbonezRigidBody.cpp
    SkullbonezSource/SkullbonezRotationMatrix.cpp
    SkullbonezSource/SkullbonezRun.cpp
    SkullbonezSource/SkullbonezShader.cpp
    SkullbonezSource/SkullbonezSkyBox.cpp
    SkullbonezSource/SkullbonezSpatialGrid.cpp
    SkullbonezSource/SkullbonezTerrain.cpp
    SkullbonezSource/SkullbonezTestScene.cpp
    SkullbonezSource/SkullbonezText.cpp
    SkullbonezSource/SkullbonezTextureCollection.cpp
    SkullbonezSource/SkullbonezTimer.cpp
    SkullbonezSource/SkullbonezVector3.cpp
    SkullbonezSource/SkullbonezWindow.cpp
    SkullbonezSource/SkullbonezWorldEnvironment.cpp
    # GLAD OpenGL loader
    ThirdPtySource/GLAD/src/gl.c
)

add_executable(SKULLBONEZ_CORE ${ENGINE_SOURCES})

# ------------------------------------------------------------------------------
# Include paths
# ------------------------------------------------------------------------------
target_include_directories(SKULLBONEZ_CORE PRIVATE
    SkullbonezSource
    ThirdPtySource/GLAD/include   # glad/gl.h and KHR/khrplatform.h
)

# ------------------------------------------------------------------------------
# Find system libraries
# ------------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(glfw3  REQUIRED)

find_package(JPEG)
if(NOT JPEG_FOUND)
    # Fall back to pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JPEG REQUIRED libjpeg)
endif()

# ------------------------------------------------------------------------------
# Link libraries
# ------------------------------------------------------------------------------
target_link_libraries(SKULLBONEZ_CORE PRIVATE
    OpenGL::GL
    OpenGL::GLU
    glfw
    ${JPEG_LIBRARIES}
    m          # libm (math)
    dl         # dlopen (needed by GLAD)
)

target_include_directories(SKULLBONEZ_CORE PRIVATE ${JPEG_INCLUDE_DIRS})

# ------------------------------------------------------------------------------
# Compiler flags
# ------------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(SKULLBONEZ_CORE PRIVATE
        -Wall -Wextra
        -Wno-unused-parameter          # engine has many unused Win32-style params
        -Wno-missing-field-initializers
        # Pre-existing code quality issues surfaced by GCC (not Linux-porting bugs).
        # These flags are C++ only — use generator expression to skip gl.c.
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-class-memaccess>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-conversion-null>
    )
endif()

# Ensure Debug build has debug info
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
