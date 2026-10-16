include(FetchContent)

# =====================================================================
#  ГЛОБАЛЬНО ОТКЛЮЧАЕМ ТЕСТЫ И ПРИМЕРЫ
#  Должно быть ДО FetchContent_Declare/MakeAvailable
# =====================================================================
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES         OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING         OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH     OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE   OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES         OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES          OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS       OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING         OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
# чтобы случайно не тянулся gtest/catch2
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
set(GTEST_FORCE_SHARED_CRT OFF CACHE BOOL "" FORCE)

set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# --- SDL3 ---
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
    GIT_SHALLOW    TRUE)
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI_LIBUSB_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
set(SDL_TESTS  OFF CACHE BOOL "" FORCE)
set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3)

# --- Dear ImGui (docking) ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# --- ImGuizmo ---
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imguizmo)
#add_library(imguizmo STATIC ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
# --- ImGuizmo ---
# ВАЖНО: репозиторий ImGuizmo уже содержит CMakeLists.txt,
# который создаёт таргет "imguizmo" и линкует его с imgui.
# Свой add_library делать НЕЛЬЗЯ — иначе конфликт имён.
# Апстримный CMakeLists ищет imgui через find_package, поэтому
# подсовываем ему путь через IMGUI_DIR.
set(IMGUI_DIR "${imgui_SOURCE_DIR}" CACHE PATH "" FORCE)

FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imguizmo)
target_include_directories(imguizmo PUBLIC ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(imguizmo PUBLIC imgui)

# --- GLM ---
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)
set(GLM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# --- Assimp ---
FetchContent_Declare(assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        v6.0.5
    GIT_SHALLOW    TRUE)
set(ASSIMP_BUILD_TESTS                OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS         OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES              OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL                    OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL_PDB                OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT                  OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS         OFF CACHE BOOL "" FORCE)
set(ASSIMP_ASAN_FLAG                  OFF CACHE BOOL "" FORCE)
set(ASSIMP_UBSAN_FLAG                 OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB                 OFF CACHE BOOL "" FORCE)
set(ASSIMP_USE_HUNTER                 OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(assimp)

# --- Jolt Physics (ВРЕМЕННО ОТКЛЮЧЁН) ---
# Разберёмся после того, как редактор запустится.
# FetchContent_Declare(JoltPhysics
#     GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
#     GIT_TAG        v5.2.0
#     GIT_SHALLOW    TRUE)
# set(TARGET_UNIT_TESTS       OFF CACHE BOOL "" FORCE)
# set(TARGET_HELLO_WORLD      OFF CACHE BOOL "" FORCE)
# set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
# set(TARGET_SAMPLES          OFF CACHE BOOL "" FORCE)
# set(TARGET_VIEWER           OFF CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(JoltPhysics)

# --- RecastNavigation ---
FetchContent_Declare(recastnavigation
    GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
    GIT_TAG        v1.6.0
    GIT_SHALLOW    TRUE)
set(RECASTNAVIGATION_DEMO     OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS    OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(recastnavigation)

# --- bgfx + bimg + bx (нужен только shaderc, БЕЗ примеров) ---
set(BGFX_BUILD_TOOLS        ON  CACHE BOOL "" FORCE)   # shaderc нужен
set(BGFX_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_RENDERER_VULKAN ON CACHE BOOL "" FORCE)
set(BGFX_CONFIG_DEBUG       OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS     OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL            OFF CACHE BOOL "" FORCE)
set(BIMG_BUILD_TOOLS        OFF CACHE BOOL "" FORCE)
set(BIMG_DECODE_ETC2        ON  CACHE BOOL "" FORCE)
set(BIMG_DECODE_BC7         ON  CACHE BOOL "" FORCE)
set(BX_BUILD_TESTS          OFF CACHE BOOL "" FORCE)
set(BX_BUILD_TOOLS          OFF CACHE BOOL "" FORCE)

# --- bgfx + bimg + bx (Vulkan backend) ---
set(BGFX_BUILD_TOOLS    ON  CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_RENDERER_VULKAN ON CACHE BOOL "" FORCE)
set(BGFX_CONFIG_DEBUG   OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        v1.136.9114-510   # <-- актуальный тег из релизов
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(bgfx)

# --- spdlog ---
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0
    GIT_SHALLOW    TRUE)
set(SPDLOG_BUILD_EXAMPLE   OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO  OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH     OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL         OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# --- EnTT ---
FetchContent_Declare(entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.14.0
    GIT_SHALLOW    TRUE)
set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_INSTALL       OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(entt)
