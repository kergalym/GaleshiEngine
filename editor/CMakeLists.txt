# --- Auto-added: sol2/lua includes ---
include_directories(${CMAKE_BINARY_DIR}/_deps/sol2-src/include)
include_directories(${CMAKE_BINARY_DIR}/_deps/lua-src/src)
# --- End ---

add_executable(GaleshiEditor
    src/main.cpp
)

target_link_libraries(GaleshiEditor PRIVATE GaleshiEngine Jolt)
galeshi_set_warnings(GaleshiEditor)

set_target_properties(GaleshiEditor PROPERTIES
    FOLDER "GaleshiTools"
    OUTPUT_NAME "GaleshiEditor"
)

target_include_directories(GaleshiEditor PRIVATE
)
