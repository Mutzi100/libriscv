#!/bin/bash
set -e
export CC=clang-12
export CXX=clang++-12

mkdir -p build32_clang
pushd build32_clang
cmake .. -DRISCV_ARCH=32 -DLIBC_USE_STDLIB=OFF -DCMAKE_TOOLCHAIN_FILE=micro_toolchain.cmake
make -j4
popd
