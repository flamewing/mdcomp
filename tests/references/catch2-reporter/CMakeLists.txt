cmake_minimum_required(VERSION 3.19)
project(mdcomp_reporter_reproducer LANGUAGES CXX)
find_package(Catch2 3 REQUIRED CONFIG)
add_executable(reporter-repro repro.cc ../../asm/main.cc)
target_compile_features(reporter-repro PRIVATE cxx_std_20)
target_link_libraries(reporter-repro PRIVATE Catch2::Catch2)
# Intentionally failing standalone reproducer, never registered with CTest.
