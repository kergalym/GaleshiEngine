# --- Auto-added: sol2/lua includes ---
include_directories(${CMAKE_BINARY_DIR}/_deps/sol2-src/include)
include_directories(${CMAKE_BINARY_DIR}/_deps/lua-src/src)
# --- End ---

find_package(PNG REQUIRED)

add_executable(GaleshiEditor
    src/main.cpp
    src/TextEditor/TextEditor.cpp
)

target_link_libraries(GaleshiEditor PRIVATE GaleshiEngine Jolt PNG::PNG)
galeshi_set_warnings(GaleshiEditor)

set_target_properties(GaleshiEditor PROPERTIES
    FOLDER "GaleshiTools"
    OUTPUT_NAME "GaleshiEditor"
)

target_include_directories(GaleshiEditor PRIVATE
    src/TextEditor
    ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bimg/3rdparty/stb
    ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/include
    ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/3rdparty
    ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/include/compat/linux
)
