# --- Auto-added: sol2/lua includes (ScriptEngine public API) ---
include_directories(${CMAKE_BINARY_DIR}/_deps/sol2-src/include)
include_directories(${CMAKE_BINARY_DIR}/_deps/lua-src/src)
# --- End ---

add_executable(GaleshiRuntime src/main.cpp)
target_link_libraries(GaleshiRuntime PRIVATE GaleshiEngine)
galeshi_set_warnings(GaleshiRuntime)
set_target_properties(GaleshiRuntime PROPERTIES
    FOLDER "GaleshiTools" OUTPUT_NAME "GaleshiRuntime")
