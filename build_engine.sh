#!/bin/bash

cmake --build build/debug --target clean
cmake --build build/debug -j"$(nproc)"

cmake --build build/release --target clean
cmake --build build/release -j"$(nproc)"

