# --- Auto-added: sol2/lua includes ---
include_directories(${CMAKE_BINARY_DIR}/_deps/sol2-src/include)
include_directories(${CMAKE_BINARY_DIR}/_deps/lua-src/src)
# --- End ---

find_package(PNG REQUIRED)

add_executable(GaleshiEditor
    src/main.cpp
    src/EditorApp.cpp
    src/EditorAnimation.cpp
    src/EditorAssets.cpp
    src/EditorAudio.cpp
    src/EditorCamera.cpp
    src/EditorCodeEditor.cpp
    src/EditorDetails.cpp
    src/EditorLandscape.cpp
    src/EditorLoading.cpp
    src/EditorMiscPanels.cpp
    src/EditorOutliner.cpp
    src/EditorPackage.cpp
    src/EditorPanels.cpp
    src/EditorPhysics.cpp
    src/EditorPostProcess.cpp
    src/EditorProject.cpp
    src/EditorSceneIO.cpp
    src/EditorSupport.cpp
    src/EditorUIDesigner.cpp
    src/EditorViewport.cpp
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
