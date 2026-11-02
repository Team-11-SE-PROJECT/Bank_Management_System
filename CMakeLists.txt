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

# 1. Main executable
add_executable(bms src/main.cpp)

# 2. Automatically find all .cpp files in src/ (except main.cpp)
file(GLOB_RECURSE BMS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER BMS_SOURCES EXCLUDE REGEX "/main\\.cpp$")
list(FILTER BMS_SOURCES EXCLUDE REGEX "/web_main\\.cpp$")

# 3. Create the core library
if(BMS_SOURCES)
  add_library(bms_core STATIC ${BMS_SOURCES})
  target_include_directories(bms_core PUBLIC "${CMAKE_SOURCE_DIR}/include")
  target_compile_definitions(
    bms_core PRIVATE BMS_WEB_ROOT="${CMAKE_SOURCE_DIR}/web"
  )
  target_link_libraries(bms PRIVATE bms_core)

  add_executable(bms_web src/web_main.cpp)
  target_link_libraries(bms_web PRIVATE bms_core)
  if(WIN32)
    target_link_libraries(bms_core PUBLIC ws2_32)
  endif()
endif()

# 4. Enable testing and load the tests folder
enable_testing()
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()