cmake_minimum_required(VERSION 3.26)
project(ryzen_socket_breach LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Arch Linux only -- AM5/Zen5 native tuning
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=znver4 -mtune=znver4 -ffast-math -funroll-loops -flto")
set(CMAKE_CXX_FLAGS_DEBUG   "-Og -g3 -fsanitize=address,undefined")

find_package(Threads REQUIRED)
find_package(Curses  REQUIRED)

find_library(RYZENADJ_LIB ryzenadj HINTS /usr/lib /usr/local/lib)
if(NOT RYZENADJ_LIB)
  message(WARNING "libryzenadj not found -- power-limit features disabled")
  set(HAVE_RYZENADJ 0)
else()
  set(HAVE_RYZENADJ 1)
endif()

add_executable(rsb
  src/main.cpp
  src/stress_engine.cpp
  src/thermal.cpp
  src/perf_counter.cpp
  src/tui.cpp
)

target_include_directories(rsb PRIVATE src)
target_link_libraries(rsb PRIVATE Threads::Threads ${CURSES_LIBRARIES})
target_compile_definitions(rsb PRIVATE HAVE_RYZENADJ=${HAVE_RYZENADJ})

if(HAVE_RYZENADJ EQUAL 1)
  target_link_libraries(rsb PRIVATE ${RYZENADJ_LIB})
  target_include_directories(rsb PRIVATE /usr/include/ryzenadj)
endif()

install(TARGETS rsb DESTINATION /usr/local/bin)
