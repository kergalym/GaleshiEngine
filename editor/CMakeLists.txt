add_executable(GaleshiEditor src/main.cpp)
target_link_libraries(GaleshiEditor PRIVATE GaleshiEngine)
galeshi_set_warnings(GaleshiEditor)
set_target_properties(GaleshiEditor PROPERTIES
    FOLDER "GaleshiTools" OUTPUT_NAME "GaleshiEditor")
