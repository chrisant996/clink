cmake_minimum_required(VERSION 2.8.12)
project(wildmatch)

include(GNUInstallDirs)
include(CTest)

enable_testing()
set(CMAKE_SKIP_INSTALL_RPATH true)
set(${PROJECT_NAME}_MAJOR_VERSION 0)
set(${PROJECT_NAME}_MINOR_VERSION 5)
set(${PROJECT_NAME}_PATCH_LEVEL 0)

set(PROJECT_CFLAGS -std=c99 -Wall -Wextra -Wno-unused-parameter)
set(PROJECT_CXXFLAGS -std=c++11 -Wall -Wextra -Wno-unused-parameter)
include_directories(${CMAKE_SOURCE_DIR})

install(FILES README.md DESTINATION share/doc/${PROJECT_NAME})

add_subdirectory(wildmatch)
add_subdirectory(tests)
