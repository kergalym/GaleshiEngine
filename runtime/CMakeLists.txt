# --- Auto-added: sol2/lua includes (ScriptEngine public API) ---
include_directories(${CMAKE_BINARY_DIR}/_deps/sol2-src/include)
include_directories(${CMAKE_BINARY_DIR}/_deps/lua-src/src)
# --- End ---

add_executable(GaleshiRuntime src/main.cpp)
target_link_libraries(GaleshiRuntime PRIVATE GaleshiEngine)
galeshi_set_warnings(GaleshiRuntime)
set_target_properties(GaleshiRuntime PROPERTIES
    FOLDER "GaleshiTools" OUTPUT_NAME "GaleshiRuntime")

add_executable(GaleshiScriptWorker src/cpp_script_worker.cpp)
target_include_directories(GaleshiScriptWorker PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(GaleshiScriptWorker PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(GaleshiScriptWorker PROPERTIES
    FOLDER "GaleshiTools" OUTPUT_NAME "GaleshiScriptWorker")

# The worker must be present beside the editor/runtime when scripts run.
add_dependencies(GaleshiEditor GaleshiScriptWorker)
add_dependencies(GaleshiRuntime GaleshiScriptWorker)
