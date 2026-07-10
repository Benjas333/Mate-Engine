cmake_minimum_required(VERSION 3.20)
project(ClipCursorHook)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(
  minhook STATIC
  external/minhook/src/buffer.c external/minhook/src/hook.c
  external/minhook/src/trampoline.c external/minhook/src/hde/hde32.c
  external/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC external/minhook/include)

add_library(${PROJECT_NAME} SHARED src/dllmain.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE minhook)

# set_target_properties(PROJECT_NAME PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES RUNTIME_OUTPUT_DIRECTORY
             "${CMAKE_SOURCE_DIR}/../../Assets/Plugins/Windows/${PROJECT_NAME}")
