function(galeshi_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /MP)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:/O2 /Oi /GL>
            $<$<CONFIG:Debug>:/Od /RTC1>)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
            -Wcast-align -Wunused -Woverloaded-virtual -Wconversion
            -Wsign-conversion -Wdouble-promotion -Wformat=2
            -Wimplicit-fallthrough)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-O3 -ffast-math>
            $<$<CONFIG:Debug>:-O0 -g>)
    endif()
endfunction()
