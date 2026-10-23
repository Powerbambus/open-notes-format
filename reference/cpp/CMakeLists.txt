cmake_minimum_required(VERSION 4.1)
project(cpp)

set(CMAKE_CXX_STANDARD 20)

include_directories(third_party)

find_library(LIBZIP_LIBRARY NAMES zip REQUIRED)
find_path(LIBZIP_INCLUDE_DIR NAMES zip.h REQUIRED)
include_directories(${LIBZIP_INCLUDE_DIR})
add_executable(cpp
        main.cpp
        src/onf_types.h
        src/onf_writer.h
        src/onf_writer.cpp
        src/onf_reader.cpp
        src/onf_reader.h
        src/sha256.h
        src/sha256.cpp
        src/asset_check.h
        src/asset_check.cpp
)

target_link_libraries(cpp PRIVATE ${LIBZIP_LIBRARY})

# Tests: run `ctest` in the build directory.
enable_testing()
add_executable(onf_ref_tests
        tests/test_reference.cpp
        src/onf_reader.cpp
        src/onf_writer.cpp
        src/sha256.cpp
        src/asset_check.cpp
)
target_link_libraries(onf_ref_tests PRIVATE ${LIBZIP_LIBRARY})
add_test(NAME onf_ref_tests COMMAND onf_ref_tests)
