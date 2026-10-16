add_executable(GaleshiRuntime src/main.cpp)
target_link_libraries(GaleshiRuntime PRIVATE GaleshiEngine)
galeshi_set_warnings(GaleshiRuntime)
set_target_properties(GaleshiRuntime PROPERTIES
    FOLDER "GaleshiTools" OUTPUT_NAME "GaleshiRuntime")
