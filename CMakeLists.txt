cmake_minimum_required(VERSION 3.20)
project(posmv_sweep VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(POSMV_BUILD_GUI "Build GTK4 operator interface" ON)
include(CTest)
add_library(posmv_core src/protocol/tss1.cpp)
target_include_directories(posmv_core PUBLIC src)
add_executable(posmv-sweep src/main.cpp)
target_link_libraries(posmv-sweep PRIVATE posmv_core)
add_library(posmv_serial src/transport/serial_transport.cpp)
target_include_directories(posmv_serial PUBLIC src)
target_link_libraries(posmv-sweep PRIVATE posmv_serial)
add_executable(posmv-simulator src/simulator/main.cpp)
target_link_libraries(posmv-simulator PRIVATE posmv_core posmv_serial)
if(BUILD_TESTING)
  add_executable(test_proxy tests/test_proxy.cpp)
  target_link_libraries(test_proxy PRIVATE posmv_core posmv_serial)
  add_test(NAME proxy COMMAND test_proxy)
  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE posmv_core)
  add_test(NAME core COMMAND test_core)
  add_test(NAME replay COMMAND ${CMAKE_COMMAND}
    -DAPP=$<TARGET_FILE:posmv-sweep> -DFIXTURE=$<TARGET_FILE:test_core> -DBINARY=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/replay.cmake)
  add_test(NAME simulator COMMAND ${CMAKE_COMMAND}
    -DSIM=$<TARGET_FILE:posmv-simulator> -DAPP=$<TARGET_FILE:posmv-sweep>
    -DBINARY=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/simulator.cmake)
endif()
if(POSMV_BUILD_GUI)
  if(WIN32)
    set_target_properties(posmv-sweep posmv-simulator PROPERTIES WIN32_EXECUTABLE TRUE)
  endif()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
  target_sources(posmv-sweep PRIVATE src/gui/main_window.cpp)
  target_link_libraries(posmv-sweep PRIVATE PkgConfig::GTK4)
  target_compile_definitions(posmv-sweep PRIVATE POSMV_HAS_GUI)
  target_sources(posmv-simulator PRIVATE src/simulator/window.cpp)
  target_link_libraries(posmv-simulator PRIVATE PkgConfig::GTK4)
  target_compile_definitions(posmv-simulator PRIVATE POSMV_HAS_GUI)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(posmv-sweep PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32 AND MINGW)
  get_filename_component(POSMV_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
  foreach(app IN ITEMS posmv-sweep posmv-simulator)
    add_custom_command(TARGET ${app} POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        "-DAPP=$<TARGET_FILE:${app}>"
        "-DDESTINATION=$<TARGET_FILE_DIR:${app}>"
        "-DRUNTIME_DIR=${POSMV_RUNTIME_DIR}"
        "-DOBJDUMP=${CMAKE_OBJDUMP}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployRuntime.cmake"
      COMMENT "Deploying ${app} runtime DLL dependencies" VERBATIM)
  endforeach()
endif()