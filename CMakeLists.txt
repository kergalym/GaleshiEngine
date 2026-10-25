cmake_minimum_required(VERSION 3.24)

project(GaleshiEngine
    VERSION 0.2.0
    DESCRIPTION "GaleshiEngine — a minimal UE5-like game engine"
    LANGUAGES CXX C
)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
# --- Глобально: тесты не собираем и не запускаем ---
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING      OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS       OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS   OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS   OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES      OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS         OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST      OFF CACHE BOOL "" FORCE)
set(GTEST_FORCE_SHARED_CRT OFF CACHE BOOL "" FORCE)

# CTest регистрируем, но без BUILD_TESTING ничего не собирается
include(CTest)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Dependencies)

add_subdirectory(engine)
add_subdirectory(editor)
add_subdirectory(runtime)

# Копирование ассетов рядом с бинарником
add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/bin/assets
    COMMENT "Copying assets..."
)

# Keep target-specific builds (e.g. --target GaleshiEditor) shipping new assets.
add_dependencies(GaleshiEditor CopyAssets)
add_dependencies(GaleshiRuntime CopyAssets)

# Компиляция шейдеров через скрипт (после сборки shaderc)
add_custom_target(CompileGaleshiShaders
    COMMAND ${CMAKE_COMMAND} -E env
        bash ${CMAKE_SOURCE_DIR}/scripts/compile_shaders.sh
             ${CMAKE_BINARY_DIR}
             ${CMAKE_SOURCE_DIR}/shaders
             ${CMAKE_BINARY_DIR}/bin/shaders
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Compiling Galeshi shaders..."
)

add_dependencies(GaleshiEditor CompileGaleshiShaders)
add_dependencies(GaleshiRuntime CompileGaleshiShaders)

# === GALESHI_SYSTEM_INCLUDES ===
# Помечаем include-пути сторонних библиотек как системные,
# чтобы -Wconversion / -Wsign-conversion не сыпались из _deps.
set(_galeshi_dep_incs
    ${CMAKE_BINARY_DIR}/_deps/glm-src
    ${CMAKE_BINARY_DIR}/_deps/spdlog-src/include
    ${CMAKE_BINARY_DIR}/_deps/entt-src/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/single_include
    ${CMAKE_BINARY_DIR}/_deps/imgui-src
    ${CMAKE_BINARY_DIR}/_deps/imgui-src/backends
    ${CMAKE_BINARY_DIR}/_deps/imguizmo-src/src
    ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bgfx/include
    ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bimg/include
    ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/include
    ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/3rdparty
    ${CMAKE_BINARY_DIR}/_deps/sdl3-build/include-revision
    ${CMAKE_BINARY_DIR}/_deps/sdl3-src/include
    ${CMAKE_BINARY_DIR}/_deps/assimp-src/include
    ${CMAKE_BINARY_DIR}/_deps/recastnavigation-src/Recast/Include
    ${CMAKE_BINARY_DIR}/_deps/recastnavigation-src/Detour/Include
    ${CMAKE_BINARY_DIR}/_deps/recastnavigation-src/DetourCrowd/Include
    ${CMAKE_BINARY_DIR}/_deps/joltphysics-src
    ${CMAKE_BINARY_DIR}/_deps/sol2-src/include
    ${CMAKE_BINARY_DIR}/_deps/lua-src/src
)
foreach(_d ${_galeshi_dep_incs})
    if(EXISTS "${_d}")
        include_directories(SYSTEM "${_d}")
    endif()
endforeach()
# === /GALESHI_SYSTEM_INCLUDES ===
