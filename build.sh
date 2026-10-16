#!/bin/bash
# Удаляем кэш — обязательно, иначе старые флаги останутся
rm -rf build/

# Конфигурируем
cmake --preset debug \
    -DBUILD_TESTING=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_SAMPLES=OFF \
    -DBUILD_BENCHMARKS=OFF \
    -DENABLE_TESTING=OFF \
    -DINSTALL_GTEST=OFF \
    -DSDL_TEST=OFF \
    -DSDL_TESTS=OFF \
    -DGLM_BUILD_TESTS=OFF \
    -DASSIMP_BUILD_TESTS=OFF \
    -DASSIMP_BUILD_SAMPLES=OFF \
    -DTARGET_UNIT_TESTS=OFF \
    -DTARGET_SAMPLES=OFF \
    -DRECASTNAVIGATION_TESTS=OFF \
    -DRECASTNAVIGATION_EXAMPLES=OFF \
    -DBGFX_BUILD_EXAMPLES=OFF \
    -DBGFX_BUILD_TESTS=OFF \
    -DBX_BUILD_TESTS=OFF \
    -DSPDLOG_BUILD_TESTS=OFF \
    -DSPDLOG_BUILD_BENCH=OFF \
    -DSPDLOG_BUILD_EXAMPLE=OFF \
    -DENTT_BUILD_TESTING=OFF

# Собираем ТОЛЬКО нужные цели (без тестовых таргетов)
cmake --build --preset debug --target GaleshiEditor

# Или, если хотите собрать всё, но без CTest:
#cmake --build --preset debug
#./build/debug/bin/GaleshiEditor