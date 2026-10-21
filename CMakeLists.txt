cmake_minimum_required(VERSION 3.16)
project(BankManagementSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

file(GLOB_RECURSE BMS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER BMS_SOURCES EXCLUDE REGEX "/main\\.cpp$")

add_library(bms_core STATIC ${BMS_SOURCES})
target_include_directories(bms_core PUBLIC "${CMAKE_SOURCE_DIR}/include")

add_executable(bms src/main.cpp)
target_link_libraries(bms PRIVATE bms_core)

enable_testing()
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
