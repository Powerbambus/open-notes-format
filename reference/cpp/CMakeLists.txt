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
)

target_link_libraries(cpp PRIVATE ${LIBZIP_LIBRARY})