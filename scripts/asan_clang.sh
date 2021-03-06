#!/bin/sh
script_dir="$(dirname "$0")"
cmake -D CMAKE_C_COMPILER=clang-10 -D CMAKE_CXX_COMPILER=clang++-10 -D CMAKE_CXX_FLAGS="-fsanitize=address" -D CMAKE_BUILD_TYPE=Release -D SFIZZ_TESTS=ON -D SFIZZ_BENCHMARKS=ON -D SFIZZ_CLIENTS=ON -S "$script_dir/.." -B .